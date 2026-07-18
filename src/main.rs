//! Wixen Terminal — accessible, GPU-accelerated terminal emulator for Windows.

mod crash;

use std::collections::HashMap;
use std::sync::Arc;
use std::time::{Duration, Instant, SystemTime};

use crossbeam_channel::Receiver;
use parking_lot::RwLock;
use tracing::{debug, error, info, trace, warn};

use windows::Win32::UI::Accessibility::{
    IRawElementProviderFragmentRoot, IRawElementProviderSimple,
};
use windows::core::Interface;
use wixen_a11y::{
    EventThrottler, FieldSnapshot, FieldType, HistoryEntrySnapshot, HistorySnapshot,
    PaletteEntrySnapshot, PaletteSnapshot, SettingsSnapshot, SubFieldSnapshot, TerminalA11yState,
    TerminalProvider,
};
use wixen_config::plugin::{PluginEvent, PluginRegistry};
use wixen_config::{
    BellStyle, Config, ConfigWatcher, ScreenReaderVerbosity, SessionState, config_path,
    is_portable, vk_to_chord,
};
use wixen_core::cursor::CursorShape;
use wixen_core::error_detect::{OutputLineClass, classify_output_line, detect_progress};
use wixen_core::modes::MouseMode;
use wixen_core::mouse::{self, MouseModifiers};
use wixen_core::selection::{SelectionType, selection_description};
use wixen_core::{ProgressState, Terminal};
use wixen_ipc::messages::{IpcRequest, IpcResponse};
use wixen_ipc::{IpcClient, IpcServer, PipeName};
use wixen_pty::{PtyEvent, PtyHandle};
use wixen_render::{
    CellHighlight, ColorScheme, OverlayLayer, OverlayLine, TabBarItem, TerminalRenderer,
};
use wixen_search::{SearchDirection, SearchEngine, SearchOptions};
use wixen_ui::audio::{AudioConfig, AudioEvent, play_event};
use wixen_ui::history::{CommandHistory, HistoryBrowser, HistoryEntry};
use wixen_ui::panes::ToggleMode;
use wixen_ui::window::{
    Window, WindowEvent, WindowEventMux, WindowId, WindowRegistry, pump_thread_messages,
};
use wixen_ui::{CommandPalette, PaneId, SettingsField, SettingsUI, TabId, TabManager};
use wixen_vt::{Action, Parser};

// ---------------------------------------------------------------------------
// Per-pane state
// ---------------------------------------------------------------------------

/// Whether a pane's PTY is still running or has exited.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum PaneStatus {
    /// PTY child process is running.
    Running,
    /// PTY child process has exited; exit code stored.
    Exited(Option<u32>),
}

/// Which arrow key was last pressed — consumed by the frame loop for boundary detection.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum NavKey {
    Up,
    Down,
    Left,
    Right,
}

/// Each pane is an independent terminal session with its own PTY.
struct PaneState {
    terminal: Terminal,
    parser: Parser,
    pty: PtyHandle,
    pty_rx: Receiver<PtyEvent>,
    search_engine: SearchEngine,
    event_throttler: EventThrottler,
    last_shell_generation: u64,
    search_mode: bool,
    search_buffer: String,
    status: PaneStatus,
    /// When the current command started executing (for long-running notifications)
    command_started_at: Option<Instant>,
    /// Browsable history of completed commands for this pane.
    command_history: CommandHistory,
    /// Previous line text at cursor row — used to detect edits (backspace, history recall, etc.)
    prev_cursor_line: String,
    /// Last arrow key pressed — consumed by frame loop for boundary detection.
    last_nav_key: Option<NavKey>,
    /// Previous cursor column — used to detect edit boundaries.
    prev_cursor_col: usize,
    /// True when up arrow left line unchanged (at oldest history entry).
    at_history_start: bool,
    /// True when down arrow cleared the line (past newest history entry).
    at_history_end: bool,
}

/// Spawn a new pane — creates a terminal, parser, and PTY session.
/// Uses the given profile (or default) for shell configuration.
fn spawn_pane(
    cols: u16,
    rows: u16,
    config: &Config,
    profile: Option<&wixen_config::Profile>,
) -> PaneState {
    let default_profile = config.default_profile();
    let p = profile.unwrap_or(&default_profile);

    let (pty, pty_rx) = match PtyHandle::spawn_with_shell(
        cols,
        rows,
        &p.program,
        &p.args,
        &p.working_directory,
    ) {
        Ok(pair) => pair,
        Err(e) => {
            error!("Failed to spawn PTY: {e}");
            fatal_error(&format!(
                "Could not start shell: {e}\n\nShell: {}\nCheck that the shell exists and is in your PATH.",
                if p.program.is_empty() {
                    "(default)"
                } else {
                    &p.program
                }
            ));
        }
    };

    let mut terminal = Terminal::new(cols as usize, rows as usize);
    terminal.grid.cursor.shape = parse_cursor_style(&config.terminal.cursor_style);
    terminal.grid.cursor.blinking = config.terminal.cursor_blink;

    PaneState {
        terminal,
        parser: Parser::new(),
        pty,
        pty_rx,
        search_engine: SearchEngine::new(),
        event_throttler: {
            let mut t = EventThrottler::new();
            t.set_debounce_ms(config.accessibility.output_debounce_ms);
            t
        },
        last_shell_generation: 0,
        search_mode: false,
        search_buffer: String::new(),
        status: PaneStatus::Running,
        command_started_at: None,
        command_history: CommandHistory::new(),
        prev_cursor_line: String::new(),
        last_nav_key: None,
        prev_cursor_col: 0,
        at_history_start: false,
        at_history_end: false,
    }
}

/// Spawn a pane with a specific command and arguments (e.g., SSH).
fn spawn_pane_with_command(
    cols: u16,
    rows: u16,
    config: &Config,
    program: &str,
    args: &[String],
) -> PaneState {
    let (pty, pty_rx) = match PtyHandle::spawn_with_shell(cols, rows, program, args, "") {
        Ok(pair) => pair,
        Err(e) => {
            error!("Failed to spawn PTY for command: {e}");
            fatal_error(&format!(
                "Could not start process: {e}\n\nCommand: {program}"
            ));
        }
    };

    let mut terminal = Terminal::new(cols as usize, rows as usize);
    terminal.grid.cursor.shape = parse_cursor_style(&config.terminal.cursor_style);
    terminal.grid.cursor.blinking = config.terminal.cursor_blink;

    PaneState {
        terminal,
        parser: Parser::new(),
        pty,
        pty_rx,
        search_engine: SearchEngine::new(),
        event_throttler: {
            let mut t = EventThrottler::new();
            t.set_debounce_ms(config.accessibility.output_debounce_ms);
            t
        },
        last_shell_generation: 0,
        search_mode: false,
        search_buffer: String::new(),
        status: PaneStatus::Running,
        command_started_at: None,
        command_history: CommandHistory::new(),
        prev_cursor_line: String::new(),
        last_nav_key: None,
        prev_cursor_col: 0,
        at_history_start: false,
        at_history_end: false,
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

/// Show a fatal error message box and exit.
fn fatal_error(msg: &str) -> ! {
    eprintln!("FATAL: {msg}");
    unsafe {
        use windows::Win32::UI::WindowsAndMessaging::{MB_ICONERROR, MB_OK, MessageBoxW};
        use windows::core::HSTRING;
        let title = HSTRING::from("Wixen Terminal — Fatal Error");
        let text = HSTRING::from(msg);
        MessageBoxW(None, &text, &title, MB_ICONERROR | MB_OK);
    }
    std::process::exit(1);
}

/// SEH handler for access violations. Logs the crash details to wixen-crash.log
/// before the process terminates.
unsafe extern "system" fn crash_handler(
    info: *mut windows::Win32::System::Diagnostics::Debug::EXCEPTION_POINTERS,
) -> i32 {
    use std::io::Write;
    const EXCEPTION_CONTINUE_SEARCH: i32 = 0;
    const EXCEPTION_ACCESS_VIOLATION: u32 = 0xC0000005;

    if info.is_null() {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    let info = unsafe { &*info };
    let record = unsafe { &*info.ExceptionRecord };

    if record.ExceptionCode.0 != EXCEPTION_ACCESS_VIOLATION as i32 {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Get the instruction pointer and the faulting address
    let rip = record.ExceptionAddress as u64;
    let access_addr = if record.NumberParameters >= 2 {
        record.ExceptionInformation[1] as u64
    } else {
        0
    };
    let access_type = if record.NumberParameters >= 1 {
        match record.ExceptionInformation[0] {
            0 => "READ",
            1 => "WRITE",
            8 => "DEP (execute)",
            _ => "UNKNOWN",
        }
    } else {
        "UNKNOWN"
    };

    // Get module base for RVA calculation
    let exe_base = unsafe {
        windows::Win32::System::LibraryLoader::GetModuleHandleW(None)
            .map(|h| h.0 as u64)
            .unwrap_or(0)
    };
    let rva = if exe_base > 0 && rip >= exe_base {
        rip - exe_base
    } else {
        rip
    };

    // Capture stack frames
    let mut stack_frames = [std::ptr::null_mut::<std::ffi::c_void>(); 32];
    let frame_count = unsafe {
        windows::Win32::System::Diagnostics::Debug::RtlCaptureStackBackTrace(
            0,
            &mut stack_frames,
            None,
        )
    };

    let msg = format!(
        "ACCESS VIOLATION at RIP=0x{rip:X} (RVA=0x{rva:X})\n\
         Access type: {access_type} at address 0x{access_addr:X}\n\
         Module base: 0x{exe_base:X}\n\
         Thread: {:?}\n\
         Stack frames ({frame_count}):\n{}",
        std::thread::current().id(),
        (0..frame_count as usize)
            .map(|i| {
                let addr = stack_frames[i] as u64;
                let frame_rva = if exe_base > 0 && addr >= exe_base {
                    addr - exe_base
                } else {
                    addr
                };
                format!("  [{i}] 0x{addr:X} (RVA 0x{frame_rva:X})\n")
            })
            .collect::<String>()
    );

    // Also capture a symbolicated backtrace via Rust's std::backtrace
    let bt = std::backtrace::Backtrace::force_capture();

    let full_msg = format!("{msg}\nSymbolicated backtrace:\n{bt}\n");
    eprintln!("{full_msg}");
    if let Ok(exe) = std::env::current_exe() {
        let log = exe.with_file_name("wixen-crash.log");
        let _ = std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(&log)
            .and_then(|mut f| writeln!(f, "{full_msg}"));
    }

    EXCEPTION_CONTINUE_SEARCH
}

fn main() {
    // Install a panic hook that logs to stderr before the default handler runs.
    // This catches panics in COM threads (UIA provider) that would otherwise
    // silently become RPC_E_SERVERFAULT.
    std::panic::set_hook(Box::new(|info| {
        let location = info
            .location()
            .map(|l| format!("{}:{}:{}", l.file(), l.line(), l.column()));
        let payload = if let Some(s) = info.payload().downcast_ref::<&str>() {
            s.to_string()
        } else if let Some(s) = info.payload().downcast_ref::<String>() {
            s.clone()
        } else {
            "unknown panic".to_string()
        };
        let timestamp = SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| format!("epoch+{}s", d.as_secs()))
            .unwrap_or_else(|_| "unknown time".to_string());
        let entry = crash::format_panic_entry(
            &timestamp,
            env!("CARGO_PKG_VERSION"),
            location.as_deref(),
            &payload,
        );
        eprintln!("{entry}");
        // Append to a user-writable log so installed users (whose exe lives in a
        // read-only Program Files directory) still get a crash record.
        let _ = std::fs::create_dir_all(crash::crash_log_dir());
        let _ = std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(crash::crash_log_path())
            .and_then(|mut f| {
                use std::io::Write;
                f.write_all(entry.as_bytes())
            });
    }));

    // Install a vectored exception handler to capture access violations.
    // Without UseComThreading, UIA calls arrive on MTA threads. If a vtable
    // dispatch goes wrong, we get an AV (0xc0000005) with no Rust stack trace.
    // This handler logs the faulting address and module offset before the crash.
    unsafe {
        windows::Win32::System::Diagnostics::Debug::AddVectoredExceptionHandler(
            1, // first handler
            Some(crash_handler),
        );
    }

    // Initialize tracing (infallible — use ok() to avoid panic)
    let directive = "wixen=info"
        .parse()
        .unwrap_or_else(|_| tracing_subscriber::filter::Directive::from(tracing::Level::INFO));
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env().add_directive(directive))
        .init();

    info!("Wixen Terminal starting");

    // Initialize COM (required for ITaskbarList3 and other Shell APIs).
    unsafe {
        let _ = windows::Win32::System::Com::CoInitializeEx(
            None,
            windows::Win32::System::Com::COINIT_APARTMENTTHREADED,
        );
    }

    // Single-instance check: if another wixen process owns the IPC pipe,
    // send a NewWindow request and exit. Otherwise, we become the server.
    // --standalone flag skips this check (used by IPC-spawned child windows).
    let standalone = std::env::args().any(|a| a == "--standalone");
    let pipe_name = PipeName::default_name();
    let is_server = if standalone {
        info!("Standalone mode — skipping IPC single-instance check");
        false
    } else if !IpcServer::try_claim(&pipe_name) {
        info!("Another instance detected — sending NewWindow request");
        let client = IpcClient::new(pipe_name.clone());
        match client.send(IpcRequest::NewWindow) {
            Ok(IpcResponse::Ok) => {
                info!("NewWindow acknowledged — exiting");
                return;
            }
            Ok(other) => {
                warn!("Unexpected IPC response: {other:?}");
                false
            }
            Err(e) => {
                warn!(error = %e, "IPC connection failed — launching standalone");
                false
            }
        }
    } else {
        info!("First instance — claiming IPC server pipe");
        true
    };

    // Detect portable mode: --portable flag or wixen.portable marker file
    let portable = std::env::args().any(|a| a == "--portable") || is_portable();
    if portable {
        info!("Portable mode active");
    }

    // Load configuration (portable → exe-relative, normal → %APPDATA%/wixen/)
    let cfg_path = config_path(portable);
    let mut config = match Config::load(&cfg_path) {
        Ok(cfg) => cfg,
        Err(e) => {
            warn!(error = %e, "Failed to load config, using defaults");
            Config::default()
        }
    };

    // Detect WSL distributions and add them as profiles (if not already present by name)
    let wsl_distros = wixen_config::wsl::detect_wsl_distros();
    if !wsl_distros.is_empty() {
        let existing_names: std::collections::HashSet<String> =
            config.profiles.iter().map(|p| p.name.clone()).collect();
        for distro in &wsl_distros {
            let profile = wixen_config::wsl::distro_to_profile(distro);
            if !existing_names.contains(&profile.name) {
                config.profiles.push(profile);
            }
        }
        info!(
            count = wsl_distros.len(),
            names = %wsl_distros.iter().map(|d| d.name.as_str()).collect::<Vec<_>>().join(", "),
            "WSL distros detected"
        );
    }

    // Load user macros from config; invalid ones are reported but skipped.
    let (macro_store, macro_errors) = wixen_config::MacroStore::load_from_config(&config.macros);
    for err in &macro_errors {
        warn!(error = %err, "Skipped invalid macro");
    }
    info!(count = macro_store.list().len(), "Macros loaded");

    // Turn configured serial ports into launchable profiles.
    for sc in &config.serial {
        match wixen_config::serial_to_profile_command(sc) {
            Ok((program, args)) => {
                config.profiles.push(wixen_config::Profile {
                    name: format!("Serial: {}", sc.port),
                    program,
                    args,
                    ..Default::default()
                });
                info!(port = %sc.port, "Serial port registered as profile");
            }
            Err(errors) => {
                for e in errors {
                    warn!(port = %sc.port, error = %e, "Invalid serial config");
                }
            }
        }
    }

    // Start watching the config file for hot-reload
    let config_watcher = ConfigWatcher::new(cfg_path.clone(), config.clone());

    info!(
        font = %config.font.family,
        font_size = config.font.size,
        window_size = %format!("{}x{}", config.window.width, config.window.height),
        "Configuration loaded"
    );

    // Create the Win32 window
    let mut window = match Window::new(
        &config.window.title,
        config.window.width,
        config.window.height,
    ) {
        Ok(w) => w,
        Err(e) => fatal_error(&format!("Could not create window: {e}")),
    };

    // Apply window chrome (dark title bar, opacity, backdrop material)
    wixen_ui::window::apply_window_chrome(window.hwnd(), &config.window);

    // Restore saved window placement (position, size, maximized state)
    let window_state_file = wixen_ui::window::window_state_path(wixen_config::is_portable());
    if let Some(placement) = wixen_ui::window::load_placement(&window_state_file) {
        window.apply_placement(&placement);
        info!("Window placement restored");
    }

    // Initialize ITaskbarList3 for progress bar support (OSC 9;4)
    let taskbar: Option<windows::Win32::UI::Shell::ITaskbarList3> = unsafe {
        windows::Win32::System::Com::CoCreateInstance(
            &windows::Win32::UI::Shell::TaskbarList,
            None,
            windows::Win32::System::Com::CLSCTX_INPROC_SERVER,
        )
        .ok()
    };
    if taskbar.is_some() {
        info!("ITaskbarList3 initialized for taskbar progress");
    }
    let last_progress = ProgressState::Hidden;

    // Create and register the UIA accessibility provider
    let a11y_state = Arc::new(RwLock::new(TerminalA11yState::new()));
    {
        let mut state = a11y_state.write();
        state.title = config.window.title.clone();
    }
    let cursor_offset = Arc::new(std::sync::atomic::AtomicI32::new(0));
    let provider = TerminalProvider::new(
        window.hwnd(),
        Arc::clone(&a11y_state),
        Arc::clone(&cursor_offset),
    );
    let uia_provider = provider.into_interface();
    window.set_uia_provider(uia_provider.clone());
    info!("UIA accessibility provider registered");

    let (width, height) = window.client_size();
    let dpi = window.dpi() as f32;
    info!(width, height, dpi, "Window ready");

    // Pump messages during initialization so Windows doesn't mark us as "not responding".
    // Renderer and font initialization can take 200-500ms on first launch.
    window.pump_messages();

    // Build color scheme from config (resolving theme if set)
    let colors = build_color_scheme(&config.colors);

    // Initialize renderer (unsafe: HWND must outlive Renderer — guaranteed by scope)
    // Mode is "auto" (default), "gpu", or "software" from config
    let mut renderer = unsafe {
        TerminalRenderer::new(
            &config.window.renderer,
            window.hwnd(),
            width,
            height,
            dpi,
            &config.font.family,
            config.font.size,
            config.font.line_height,
            colors,
            &config.font.fallback_fonts,
            config.font.ligatures,
            &config.font.font_path,
        )
    }
    .unwrap_or_else(|e| {
        fatal_error(&format!(
            "Could not initialize renderer: {e}\n\n\
             Try setting renderer = \"software\" in your config\n\
             or check that your GPU drivers are up to date."
        ))
    });

    // Load background image if configured
    if !config.window.background_image.is_empty() {
        if let Ok(data) = std::fs::read(&config.window.background_image) {
            renderer.set_background_image(&data, config.window.background_image_opacity);
        } else {
            tracing::warn!(
                path = %config.window.background_image,
                "Failed to read background image file"
            );
        }
    }

    // Apply visual effects (background blur, custom shader) from config.
    apply_effects(&mut renderer, &config.effects);

    // Calculate terminal dimensions from font metrics
    let metrics = renderer.font_metrics();
    let mut cell_width = metrics.cell_width;
    let mut cell_height = metrics.cell_height;
    let pad_h = config.window.padding.horizontal() as f32;
    let pad_v = config.window.padding.vertical() as f32;
    // Guard against zero cell dimensions (DirectWrite may return 0 if font loading failed)
    if cell_width < 1.0 {
        warn!(cell_width, "Cell width too small, using fallback");
        cell_width = 8.0;
    }
    if cell_height < 1.0 {
        warn!(cell_height, "Cell height too small, using fallback");
        cell_height = 16.0;
    }
    let current_cols = ((width as f32 - pad_h) / cell_width)
        .floor()
        .clamp(1.0, 500.0) as u16;
    let current_rows = ((height as f32 - pad_v) / cell_height)
        .floor()
        .clamp(1.0, 500.0) as u16;

    // --- Tab and pane management (with session restore) ---
    let session_path = SessionState::default_path(&cfg_path);
    let mut panes: HashMap<PaneId, PaneState> = HashMap::new();

    let session_mode = config.terminal.session_restore.as_str();
    let tab_mgr = if SessionState::should_restore(session_mode)
        && let Some(session) = SessionState::load(&session_path)
    {
        let (mgr, pane_ids) = TabManager::from_session_state(&session);
        for pane_id in &pane_ids {
            panes.insert(
                *pane_id,
                spawn_pane(current_cols, current_rows, &config, None),
            );
        }
        info!(tabs = mgr.tab_count(), "Session restored");
        mgr
    } else {
        let mgr = TabManager::new(&config.window.title);
        let initial_pane_id = mgr.active_tab().active_pane;
        panes.insert(
            initial_pane_id,
            spawn_pane(current_cols, current_rows, &config, None),
        );
        mgr
    };

    info!(
        cols = current_cols,
        rows = current_rows,
        cell_width,
        cell_height,
        "Terminal session started"
    );

    // Keybinding dispatch table (chord → action ID)
    let keybinding_map = build_keybinding_map(&config);

    // Command palette — populate with profile and SSH entries from config
    let mut palette = CommandPalette::new();
    palette.set_profiles(&build_profile_entries(&config));
    palette.load_ssh_targets(&config.ssh);
    palette.set_macros(&macro_palette_entries(&macro_store));

    // Command history browser overlay (Ctrl+Shift+H).
    let history_browser = HistoryBrowser::new();

    // Populate Windows Jump List with profile tasks
    let jump_profiles: Vec<wixen_ui::jumplist::JumpListProfile> = config
        .resolved_profiles()
        .iter()
        .map(|p| wixen_ui::jumplist::JumpListProfile {
            name: p.name.clone(),
            profile_arg: p.name.clone(),
        })
        .collect();
    wixen_ui::jumplist::update_jump_list(&jump_profiles);

    // Settings UI
    let settings_ui = SettingsUI::from_config(&config);

    // Cursor blink state
    let blink_interval_ms = config.terminal.cursor_blink_ms as u128;
    let blink_timer = Instant::now();
    let cursor_blink_on = true;
    let mouse_dragging = false;
    let base_title = config.window.title.clone();

    // Triple-click detection: track last double-click time and position
    let last_dblclick: Option<(Instant, i32, i32)> = None;

    // Tab drag reorder: (source tab index, start x position)
    let tab_drag: Option<(usize, i32)> = None;

    // Start IPC server thread — listen for commands from other instances.
    // Only the first instance (server) starts the listener.
    let ipc_rx = if is_server {
        let ipc_pipe = pipe_name.clone();
        let (tx, rx) = crossbeam_channel::unbounded::<IpcRequest>();
        std::thread::Builder::new()
            .name("ipc-server".into())
            .spawn(move || {
                loop {
                    let pipe = ipc_pipe.clone();
                    let mut server = IpcServer::new(pipe);
                    let tx = tx.clone();
                    if let Err(e) = server.serve_once(|req| {
                        match &req {
                            IpcRequest::Ping => {
                                return IpcResponse::Pong {
                                    pid: std::process::id(),
                                    version: env!("CARGO_PKG_VERSION").into(),
                                };
                            }
                            IpcRequest::Shutdown => {
                                let _ = tx.send(req);
                                return IpcResponse::Ok;
                            }
                            _ => {
                                let _ = tx.send(req);
                            }
                        }
                        IpcResponse::Ok
                    }) {
                        tracing::debug!(error = %e, "IPC serve_once error — retrying");
                        std::thread::sleep(std::time::Duration::from_millis(100));
                    }
                }
            })
            .map_err(|e| warn!("Failed to spawn IPC server thread: {e}"))
            .ok();
        Some(rx)
    } else {
        None
    };

    // Quake mode: register global hotkey if enabled
    if config.window.quake_mode {
        if let Some((mods, vk)) = wixen_config::parse_quake_hotkey(&config.window.quake_hotkey) {
            if !window.register_quake_hotkey(mods, vk) {
                warn!(
                    hotkey = %config.window.quake_hotkey,
                    "Quake hotkey registration failed — another program may have claimed it"
                );
            }
        } else {
            warn!(
                hotkey = %config.window.quake_hotkey,
                "Invalid quake hotkey string"
            );
        }
    }

    // System tray icon — shown at startup so left/double-click can toggle the
    // window and the minimize_to_tray action has a target.
    let mut tray_icon = wixen_ui::tray::TrayIcon::new(window.hwnd(), "Wixen Terminal");
    tray_icon.show();
    let _tray_menu = wixen_ui::tray::TrayMenu::build_default_menu();

    // Plugin registry — plugins register for events and get dispatched at key points
    let (plugin_registry, rejected_plugins) = PluginRegistry::from_manifests(&config.plugins);
    for name in &rejected_plugins {
        warn!(plugin = %name, "Duplicate plugin manifest rejected");
    }
    info!(count = plugin_registry.list().len(), "Plugins loaded");

    // A sandboxed Lua VM used to run plugin event handlers. The main loop is
    // single-threaded, so a non-Send Lua is fine to hold here.
    let plugin_lua = mlua::Lua::new();
    if let Err(e) = wixen_config::lua::sandbox_lua(&plugin_lua) {
        warn!(error = %e, "Failed to sandbox plugin Lua VM");
    }

    // Track window focus for long-running command notifications
    let window_focused = true;
    let broadcast_input = false;

    // --- Accessibility state ---
    let audio_config = AudioConfig::from_accessibility(&config.accessibility);
    let last_announced_progress: Option<u8> = None;

    // Apply contrast enforcement from config
    renderer.set_min_contrast_ratio(config.accessibility.min_contrast_ratio as f64);

    // Apply reduced motion from config
    if wixen_config::should_reduce_motion(
        &config.accessibility.reduced_motion,
        wixen_ui::window::system_reduced_motion(),
    ) {
        for pane in panes.values_mut() {
            pane.terminal.grid.cursor.blinking = false;
        }
    }

    // Ensure the window is in the foreground and has keyboard focus.
    // Without this, the window may be visible but not receive input events.
    unsafe {
        use windows::Win32::UI::WindowsAndMessaging::SetForegroundWindow;
        let _ = SetForegroundWindow(window.hwnd());
    }

    // Main event loop
    // Final message pump before entering the loop. This ensures all initialization
    // messages (WM_SIZE, WM_PAINT, WM_ACTIVATE) are processed, and Windows doesn't
    // show a "not responding" dialog during startup.
    window.pump_messages();

    // -----------------------------------------------------------------------
    // Bundle per-window state; hold shared globals once.
    // -----------------------------------------------------------------------
    let primary_id = window.id();
    let prev_active_pane = tab_mgr.active_tab().active_pane;
    let prev_active_tab_id = tab_mgr.active_tab_id();
    let primary = WindowState {
        window,
        renderer,
        tab_mgr,
        panes,
        a11y_state,
        cursor_offset,
        uia_provider,
        palette,
        history_browser,
        settings_ui,
        taskbar,
        current_cols,
        current_rows,
        cell_width,
        cell_height,
        last_progress,
        last_announced_progress,
        blink_interval_ms,
        blink_timer,
        cursor_blink_on,
        mouse_dragging,
        base_title,
        last_dblclick,
        tab_drag,
        window_focused,
        broadcast_input,
        prev_active_pane,
        prev_active_tab_id,
        prev_selection_active: false,
        close_requested: false,
    };
    let mut globals = AppGlobals {
        config,
        config_watcher,
        keybinding_map,
        macro_store,
        plugin_registry,
        plugin_lua,
        audio_config,
        tray_icon,
        cfg_path,
    };

    let mut windows: HashMap<WindowId, WindowState> = HashMap::new();
    windows.insert(primary_id, primary);
    let mut registry = WindowRegistry::new();
    registry.register(primary_id);
    let mut mux = WindowEventMux::new();
    if let Some(ws) = windows.get(&primary_id) {
        mux.attach_window(&ws.window);
    }
    let mut shutdown_all = false;

    info!("Entering main event loop");
    loop {
        // One message pump serves every window on this thread.
        if !pump_thread_messages() {
            break;
        }

        // Drain window events; dispatch each to the window it addresses.
        while let Some(tagged) = mux.try_next() {
            let post = windows
                .get_mut(&tagged.window_id)
                .map(|ws| handle_window_event(ws, tagged.event, &mut globals))
                .unwrap_or(PostAction::None);
            match post {
                PostAction::TearOff => tear_off_active_tab(
                    tagged.window_id,
                    &mut windows,
                    &mut registry,
                    &mut mux,
                    &globals,
                ),
                PostAction::None => {}
            }
        }

        // Config hot-reload (applies to all windows + shared globals).
        apply_config_reload(&mut windows, &mut globals);

        // IPC requests from other instances (targeted at the primary window).
        if let Some(rx) = ipc_rx.as_ref() {
            while let Ok(ipc_req) = rx.try_recv() {
                if handle_ipc_request(ipc_req, primary_id, &mut windows, &globals) {
                    shutdown_all = true;
                }
            }
        }

        // Per-window frame: PTY output, announcements, rendering.
        let frame_ids: Vec<WindowId> = registry.open_ids().to_vec();
        for id in frame_ids {
            if let Some(ws) = windows.get_mut(&id) {
                run_window_frame(ws, &mut globals);
            }
        }

        // Close any windows flagged during event/frame handling.
        let to_close: Vec<WindowId> = windows
            .iter()
            .filter(|(_, ws)| ws.close_requested)
            .map(|(id, _)| *id)
            .collect();
        for id in to_close {
            close_window(id, &mut windows, &mut registry, &mut mux);
        }

        if shutdown_all {
            let ids: Vec<WindowId> = registry.open_ids().to_vec();
            for id in ids {
                close_window(id, &mut windows, &mut registry, &mut mux);
            }
        }

        if registry.should_quit() {
            break;
        }

        // Yield CPU until a message or PTY data arrives, without blocking the pump.
        unsafe {
            use ::windows::Win32::UI::WindowsAndMessaging::{
                MsgWaitForMultipleObjects, QS_ALLINPUT,
            };
            MsgWaitForMultipleObjects(None, false, 1, QS_ALLINPUT);
        }
    }

    // -----------------------------------------------------------------------
    // Shutdown
    // -----------------------------------------------------------------------
    globals.tray_icon.hide();
    if let Some(ws) = windows.get_mut(&primary_id) {
        ws.window.unregister_quake_hotkey();
        let placement = ws.window.get_placement();
        if let Err(e) = wixen_ui::window::save_placement(&placement, &window_state_file) {
            warn!(error = %e, "Failed to save window placement");
        }
        if SessionState::should_save(&globals.config.terminal.session_restore) {
            let session = ws.tab_mgr.to_session_state();
            if !session.is_empty()
                && let Err(e) = session.save(&session_path)
            {
                warn!(error = %e, "Failed to save session state");
            }
        }
    }

    info!("Wixen Terminal exiting");
}

// ---------------------------------------------------------------------------
// Multi-window state
// ---------------------------------------------------------------------------

/// Pixels a torn-off window is offset from its source window's top-left.
const TEAR_OFF_OFFSET_PX: i32 = 40;

/// A follow-up action a window's event handling asks the main loop to perform
/// once the per-window borrow has ended. Closing and shutdown are signalled via
/// the `WindowState::close_requested` flag and the IPC path respectively; only
/// tear-off (which moves state between windows) needs an out-of-band action.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum PostAction {
    None,
    TearOff,
}

/// All state that belongs to a single OS window: its Win32 window, GPU
/// renderer, tabs, live panes (keyed by process-unique `PaneId`), the UIA
/// provider bound to this window's HWND, and the per-window overlay UIs.
struct WindowState {
    window: Window,
    renderer: TerminalRenderer,
    tab_mgr: TabManager,
    panes: HashMap<PaneId, PaneState>,
    a11y_state: Arc<RwLock<TerminalA11yState>>,
    cursor_offset: Arc<std::sync::atomic::AtomicI32>,
    uia_provider: IRawElementProviderSimple,
    palette: CommandPalette,
    history_browser: HistoryBrowser,
    settings_ui: SettingsUI,
    taskbar: Option<windows::Win32::UI::Shell::ITaskbarList3>,
    current_cols: u16,
    current_rows: u16,
    cell_width: f32,
    cell_height: f32,
    last_progress: ProgressState,
    last_announced_progress: Option<u8>,
    blink_interval_ms: u128,
    blink_timer: Instant,
    cursor_blink_on: bool,
    mouse_dragging: bool,
    base_title: String,
    last_dblclick: Option<(Instant, i32, i32)>,
    tab_drag: Option<(usize, i32)>,
    window_focused: bool,
    broadcast_input: bool,
    prev_active_pane: PaneId,
    prev_active_tab_id: TabId,
    prev_selection_active: bool,
    close_requested: bool,
}

/// Process-wide state owned once and shared across every window.
struct AppGlobals {
    config: Config,
    config_watcher: ConfigWatcher,
    keybinding_map: HashMap<String, ResolvedAction>,
    macro_store: wixen_config::MacroStore,
    plugin_registry: PluginRegistry,
    plugin_lua: mlua::Lua,
    audio_config: AudioConfig,
    tray_icon: wixen_ui::tray::TrayIcon,
    cfg_path: std::path::PathBuf,
}

/// Destroy one window and remove it from the registry and event mux. The last
/// window destroyed makes [`WindowRegistry::should_quit`] return `true`.
fn close_window(
    id: WindowId,
    windows: &mut HashMap<WindowId, WindowState>,
    registry: &mut WindowRegistry,
    mux: &mut WindowEventMux,
) {
    if let Some(mut ws) = windows.remove(&id) {
        // Dropping the WindowState tears down its PTYs (ConPTY handles) and GPU
        // surface; closing the window destroys the HWND without ending the process.
        ws.window.close();
    }
    registry.close(id);
    mux.detach(id);
    info!(id = id.raw(), "Window closed");
}

/// Tear the active tab off `source_id` into a brand-new OS window, moving its
/// live PTY panes across without respawning them.
fn tear_off_active_tab(
    source_id: WindowId,
    windows: &mut HashMap<WindowId, WindowState>,
    registry: &mut WindowRegistry,
    mux: &mut WindowEventMux,
    g: &AppGlobals,
) {
    // Detach the tab and pull its live panes out of the source window.
    let (tab, moved_panes, seed_w, seed_h, origin_x, origin_y) = {
        let Some(src) = windows.get_mut(&source_id) else {
            return;
        };
        let active_tab_id = src.tab_mgr.active_tab_id();
        let Some(tab) = src.tab_mgr.detach_tab(active_tab_id) else {
            info!("Tear-off ignored: window has only one tab");
            return;
        };
        let pane_ids = tab.pane_tree.pane_ids();
        let mut moved: Vec<(PaneId, PaneState)> = Vec::with_capacity(pane_ids.len());
        for pid in pane_ids {
            if let Some(state) = src.panes.remove(&pid) {
                moved.push((pid, state));
            }
        }
        let new_active = src.tab_mgr.active_tab().active_pane;
        if let Some(p) = src.panes.get_mut(&new_active) {
            p.terminal.dirty = true;
        }
        let (sw, sh) = src.window.client_size();
        let (ox, oy) = src.window.client_to_screen(0, 0);
        (tab, moved, sw, sh, ox, oy)
    };

    // Create the new window near the source window's origin.
    let title = tab.display_title();
    let width = seed_w.max(200);
    let height = seed_h.max(150);
    let new_window = match Window::new_at(
        &title,
        width,
        height,
        origin_x + TEAR_OFF_OFFSET_PX,
        origin_y + TEAR_OFF_OFFSET_PX,
    ) {
        Ok(win) => win,
        Err(e) => {
            error!(error = %e, "Tear-off failed: could not create window");
            return;
        }
    };
    wixen_ui::window::apply_window_chrome(new_window.hwnd(), &g.config.window);

    // UIA provider bound to the new window's HWND.
    let a11y_state = Arc::new(RwLock::new(TerminalA11yState::new()));
    a11y_state.write().title = title.clone();
    let cursor_offset = Arc::new(std::sync::atomic::AtomicI32::new(0));
    let provider = TerminalProvider::new(
        new_window.hwnd(),
        Arc::clone(&a11y_state),
        Arc::clone(&cursor_offset),
    );
    let uia_provider = provider.into_interface();
    new_window.set_uia_provider(uia_provider.clone());

    // Build a renderer/surface for the new window.
    let (client_w, client_h) = new_window.client_size();
    let dpi = new_window.dpi() as f32;
    let colors = build_color_scheme(&g.config.colors);
    let mut renderer = match unsafe {
        TerminalRenderer::new(
            &g.config.window.renderer,
            new_window.hwnd(),
            client_w,
            client_h,
            dpi,
            &g.config.font.family,
            g.config.font.size,
            g.config.font.line_height,
            colors,
            &g.config.font.fallback_fonts,
            g.config.font.ligatures,
            &g.config.font.font_path,
        )
    } {
        Ok(r) => r,
        Err(e) => {
            error!(error = %e, "Tear-off failed: renderer init");
            return;
        }
    };
    if !g.config.window.background_image.is_empty()
        && let Ok(data) = std::fs::read(&g.config.window.background_image)
    {
        renderer.set_background_image(&data, g.config.window.background_image_opacity);
    }
    apply_effects(&mut renderer, &g.config.effects);
    renderer.set_min_contrast_ratio(g.config.accessibility.min_contrast_ratio as f64);

    let metrics = renderer.font_metrics();
    let cell_width = if metrics.cell_width < 1.0 {
        8.0
    } else {
        metrics.cell_width
    };
    let cell_height = if metrics.cell_height < 1.0 {
        16.0
    } else {
        metrics.cell_height
    };
    let pad_h = g.config.window.padding.horizontal() as f32;
    let pad_v = g.config.window.padding.vertical() as f32;
    let current_cols = ((client_w as f32 - pad_h) / cell_width)
        .floor()
        .clamp(1.0, 500.0) as u16;
    let current_rows = ((client_h as f32 - pad_v) / cell_height)
        .floor()
        .clamp(1.0, 500.0) as u16;

    // Move the live panes into the new window, resizing them to the new grid.
    let mut new_panes: HashMap<PaneId, PaneState> = HashMap::new();
    for (pid, mut state) in moved_panes {
        state
            .terminal
            .resize(current_cols as usize, current_rows as usize);
        let _ = state.pty.resize(current_cols, current_rows);
        state.terminal.dirty = true;
        new_panes.insert(pid, state);
    }

    let tab_mgr = TabManager::from_detached_tab(tab);
    let prev_active_pane = tab_mgr.active_tab().active_pane;
    let prev_active_tab_id = tab_mgr.active_tab_id();

    let mut palette = CommandPalette::new();
    palette.set_profiles(&build_profile_entries(&g.config));
    palette.load_ssh_targets(&g.config.ssh);
    palette.set_macros(&macro_palette_entries(&g.macro_store));

    let taskbar: Option<windows::Win32::UI::Shell::ITaskbarList3> = unsafe {
        windows::Win32::System::Com::CoCreateInstance(
            &windows::Win32::UI::Shell::TaskbarList,
            None,
            windows::Win32::System::Com::CLSCTX_INPROC_SERVER,
        )
        .ok()
    };

    let new_id = new_window.id();
    let state = WindowState {
        window: new_window,
        renderer,
        tab_mgr,
        panes: new_panes,
        a11y_state,
        cursor_offset,
        uia_provider,
        palette,
        history_browser: HistoryBrowser::new(),
        settings_ui: SettingsUI::from_config(&g.config),
        taskbar,
        current_cols,
        current_rows,
        cell_width,
        cell_height,
        last_progress: ProgressState::Hidden,
        last_announced_progress: None,
        blink_interval_ms: g.config.terminal.cursor_blink_ms as u128,
        blink_timer: Instant::now(),
        cursor_blink_on: true,
        mouse_dragging: false,
        base_title: title,
        last_dblclick: None,
        tab_drag: None,
        window_focused: true,
        broadcast_input: false,
        prev_active_pane,
        prev_active_tab_id,
        prev_selection_active: false,
        close_requested: false,
    };
    windows.insert(new_id, state);
    registry.register(new_id);
    if let Some(ws) = windows.get(&new_id) {
        mux.attach_window(&ws.window);
    }
    info!(
        source = source_id.raw(),
        new = new_id.raw(),
        "Tab torn off into new window"
    );
}

/// Handle one IPC request. Returns `true` if the process should shut down.
fn handle_ipc_request(
    req: IpcRequest,
    primary_id: WindowId,
    windows: &mut HashMap<WindowId, WindowState>,
    g: &AppGlobals,
) -> bool {
    match req {
        IpcRequest::NewWindow => {
            if let Ok(exe) = std::env::current_exe() {
                let _ = std::process::Command::new(exe).arg("--standalone").spawn();
                info!("IPC: spawned new window process");
            }
            false
        }
        IpcRequest::NewTab { profile } => {
            if let Some(ws) = windows.get_mut(&primary_id) {
                let p = profile
                    .as_ref()
                    .and_then(|name| g.config.profiles.iter().find(|p| p.name == *name));
                let (new_tab_id, new_pane_id) = ws.tab_mgr.add_tab("New Tab");
                ws.panes.insert(
                    new_pane_id,
                    spawn_pane(ws.current_cols, ws.current_rows, &g.config, p),
                );
                info!("IPC: opened new tab");
                dispatch_plugin_event(
                    &g.plugin_lua,
                    &g.plugin_registry,
                    &PluginEvent::TabCreated {
                        tab_id: new_tab_id.0,
                    },
                );
            }
            false
        }
        IpcRequest::Shutdown => {
            info!("IPC: shutdown requested");
            true
        }
        IpcRequest::Ping | IpcRequest::ListSessions | IpcRequest::FocusWindow { .. } => false,
    }
}

/// Apply a hot-reloaded config to every window and the shared globals.
fn apply_config_reload(windows: &mut HashMap<WindowId, WindowState>, g: &mut AppGlobals) {
    let Some(delta) = g.config_watcher.poll() else {
        return;
    };
    for ws in windows.values_mut() {
        apply_config_delta_to_window(ws, &delta, &g.config, &g.macro_store);
    }
    if delta.keybindings_changed {
        g.keybinding_map = build_keybinding_map(&delta.config);
        info!(
            "Keybinding map rebuilt ({} bindings)",
            g.keybinding_map.len()
        );
    }
    g.config = delta.config;
    g.audio_config = AudioConfig::from_accessibility(&g.config.accessibility);
    dispatch_plugin_event(
        &g.plugin_lua,
        &g.plugin_registry,
        &PluginEvent::ConfigReloaded,
    );
}

/// Apply the per-window portion of a config delta (colors, font, cursor,
/// chrome, palette entries) to one window. `old_config` is the still-current
/// config, used to detect background-image changes.
fn apply_config_delta_to_window(
    ws: &mut WindowState,
    delta: &wixen_config::ConfigDelta,
    old_config: &Config,
    macro_store: &wixen_config::MacroStore,
) {
    if delta.colors_changed {
        let new_colors = build_color_scheme(&delta.config.colors);
        ws.renderer.update_colors(new_colors);
        ws.renderer
            .set_min_contrast_ratio(delta.config.accessibility.min_contrast_ratio as f64);
        for pane in ws.panes.values_mut() {
            pane.terminal.dirty = true;
        }
    }

    if delta.font_changed {
        let dpi = ws.window.dpi() as f32;
        if let Err(e) = ws.renderer.rebuild_font(
            &delta.config.font.family,
            delta.config.font.size,
            dpi,
            delta.config.font.line_height,
            &delta.config.font.fallback_fonts,
            delta.config.font.ligatures,
            &delta.config.font.font_path,
        ) {
            error!("Font rebuild failed on config reload: {e}");
        } else {
            let metrics = ws.renderer.font_metrics();
            let (w, h) = ws.window.client_size();
            ws.cell_width = metrics.cell_width;
            ws.cell_height = metrics.cell_height;
            let pad_h = delta.config.window.padding.horizontal() as f32;
            let pad_v = delta.config.window.padding.vertical() as f32;
            ws.current_cols = ((w as f32 - pad_h) / ws.cell_width)
                .floor()
                .clamp(1.0, 500.0) as u16;
            ws.current_rows = ((h as f32 - pad_v) / ws.cell_height)
                .floor()
                .clamp(1.0, 500.0) as u16;
            for pane in ws.panes.values_mut() {
                pane.terminal
                    .resize(ws.current_cols as usize, ws.current_rows as usize);
                if let Err(e) = pane.pty.resize(ws.current_cols, ws.current_rows) {
                    error!("PTY resize error on font change: {}", e);
                }
                pane.terminal.dirty = true;
            }
        }
    }

    if delta.terminal_changed {
        let new_shape = parse_cursor_style(&delta.config.terminal.cursor_style);
        let reduce_motion = wixen_config::should_reduce_motion(
            &delta.config.accessibility.reduced_motion,
            wixen_ui::window::system_reduced_motion(),
        );
        for pane in ws.panes.values_mut() {
            pane.terminal.grid.cursor.shape = new_shape;
            pane.terminal.grid.cursor.blinking =
                delta.config.terminal.cursor_blink && !reduce_motion;
            pane.terminal.dirty = true;
        }
        ws.blink_interval_ms = delta.config.terminal.cursor_blink_ms as u128;
    }

    if delta.window_changed {
        set_window_title(ws.window.hwnd(), &delta.config.window.title);
        ws.base_title = delta.config.window.title.clone();
        wixen_ui::window::apply_window_chrome(ws.window.hwnd(), &delta.config.window);

        if delta.config.window.background_image != old_config.window.background_image
            || (delta.config.window.background_image_opacity
                - old_config.window.background_image_opacity)
                .abs()
                > f32::EPSILON
        {
            if delta.config.window.background_image.is_empty() {
                ws.renderer.clear_background_image();
            } else if let Ok(data) = std::fs::read(&delta.config.window.background_image) {
                ws.renderer
                    .set_background_image(&data, delta.config.window.background_image_opacity);
            }
            for pane in ws.panes.values_mut() {
                pane.terminal.dirty = true;
            }
        }
    }

    ws.palette
        .set_profiles(&build_profile_entries(&delta.config));
    ws.palette.load_ssh_targets(&delta.config.ssh);
    ws.palette.set_macros(&macro_palette_entries(macro_store));

    if delta.config.effects != old_config.effects {
        apply_effects(&mut ws.renderer, &delta.config.effects);
    }

    for pane in ws.panes.values_mut() {
        pane.event_throttler
            .set_debounce_ms(delta.config.accessibility.output_debounce_ms);
    }
}

/// Handle a single window event for one window. Returns a follow-up action for
/// the main loop (tear-off / close / shutdown) once the borrow has ended.
fn handle_window_event(ws: &mut WindowState, event: WindowEvent, g: &mut AppGlobals) -> PostAction {
    let pad_left = g.config.window.padding.left as f32;
    let pad_top = g.config.window.padding.top as f32;
    let _ = (pad_left, pad_top);
    match event {
        WindowEvent::CloseRequested => {
            info!("Close requested");
            ws.close_requested = true;
        }
        WindowEvent::Resized(w, h) => {
            ws.renderer.resize(w, h);
            let pad_h = g.config.window.padding.horizontal() as f32;
            let pad_v = g.config.window.padding.vertical() as f32;
            ws.current_cols = ((w as f32 - pad_h) / ws.cell_width)
                .floor()
                .clamp(1.0, 500.0) as u16;
            ws.current_rows = ((h as f32 - pad_v) / ws.cell_height)
                .floor()
                .clamp(1.0, 500.0) as u16;
            // Resize ALL panes — they all share the window dimensions
            for pane in ws.panes.values_mut() {
                pane.terminal
                    .resize(ws.current_cols as usize, ws.current_rows as usize);
                if let Err(e) = pane.pty.resize(ws.current_cols, ws.current_rows) {
                    error!("PTY resize error: {}", e);
                }
            }
        }
        WindowEvent::KeyInput {
            vk,
            down,
            ctrl,
            shift,
            alt,
            ..
        } => {
            if down {
                // ---- Profile shortcuts: Ctrl+Shift+1..9 (positional, not configurable) ----
                if ctrl && shift && !alt && (0x31..=0x39).contains(&vk) {
                    let profile_idx = (vk - 0x31) as usize;
                    if let Some(profile) = g.config.profile_at(profile_idx) {
                        let (tab_id, pane_id) = ws.tab_mgr.add_tab(&profile.name);
                        ws.panes.insert(
                            pane_id,
                            spawn_pane(ws.current_cols, ws.current_rows, &g.config, Some(&profile)),
                        );
                        if let Some(p) = ws.panes.get_mut(&pane_id) {
                            p.terminal.dirty = true;
                        }
                        info!(
                            profile = %profile.name,
                            tabs = ws.tab_mgr.tab_count(),
                            "New tab opened with profile"
                        );
                        dispatch_plugin_event(
                            &g.plugin_lua,
                            &g.plugin_registry,
                            &PluginEvent::TabCreated { tab_id: tab_id.0 },
                        );
                    }
                    return PostAction::None;
                }

                // ---- Command palette keys (modal) ----
                if ws.palette.visible {
                    match vk {
                        0x1B => ws.palette.close(),       // Escape
                        0x26 => ws.palette.select_prev(), // Up
                        0x28 => ws.palette.select_next(), // Down
                        0x0D => {
                            // Enter — confirm selection, dispatch action
                            if let Some(result) = ws.palette.confirm() {
                                match result {
                                    wixen_ui::PaletteResult::Action(ref action_id)
                                        if action_id == "tear_off_tab" =>
                                    {
                                        ws.palette.close();
                                        return PostAction::TearOff;
                                    }
                                    wixen_ui::PaletteResult::Action(ref action_id)
                                        if action_id == "rename_tab" =>
                                    {
                                        ws.palette.open_input("Enter new tab name:");
                                    }
                                    wixen_ui::PaletteResult::Action(ref action_id)
                                        if action_id == "toggle_broadcast_input" =>
                                    {
                                        ws.broadcast_input = !ws.broadcast_input;
                                        info!(
                                            broadcast = ws.broadcast_input,
                                            "Broadcast input toggled via palette"
                                        );
                                        announce_mode_toggle(
                                            ToggleMode::BroadcastInput,
                                            ws.broadcast_input,
                                            &ws.uia_provider,
                                            g.config.accessibility.screen_reader_output,
                                            &g.audio_config,
                                        );
                                    }
                                    wixen_ui::PaletteResult::Action(action_id)
                                        if handle_local_action(
                                            &action_id,
                                            None,
                                            &g.macro_store,
                                            &mut ws.history_browser,
                                            &ws.tab_mgr,
                                            &mut ws.panes,
                                            &mut ws.window,
                                            &mut g.tray_icon,
                                            &ws.uia_provider,
                                            g.config.accessibility.screen_reader_output,
                                        ) =>
                                    {
                                        let active = ws.tab_mgr.active_tab().active_pane;
                                        if let Some(p) = ws.panes.get_mut(&active) {
                                            p.terminal.dirty = true;
                                        }
                                    }
                                    wixen_ui::PaletteResult::Action(action_id) => {
                                        let result = dispatch_keybinding_action(
                                            &action_id,
                                            &mut ws.tab_mgr,
                                            &mut ws.panes,
                                            &g.config,
                                            &mut ws.close_requested,
                                            ws.current_cols,
                                            ws.current_rows,
                                            &mut ws.window,
                                            &ws.base_title,
                                            &g.plugin_lua,
                                            &g.plugin_registry,
                                        );
                                        if let DispatchResult::ModeToggled { mode, active } = result
                                        {
                                            announce_mode_toggle(
                                                mode,
                                                active,
                                                &ws.uia_provider,
                                                g.config.accessibility.screen_reader_output,
                                                &g.audio_config,
                                            );
                                        }
                                        handle_deferred_action(
                                            result,
                                            &mut g.config,
                                            &mut ws.renderer,
                                            &ws.window,
                                            &mut ws.cell_width,
                                            &mut ws.cell_height,
                                            &mut ws.current_cols,
                                            &mut ws.current_rows,
                                            &mut ws.panes,
                                            &mut g.config_watcher,
                                            &g.cfg_path,
                                        );
                                    }
                                    wixen_ui::PaletteResult::Input(text) => {
                                        ws.tab_mgr.rename_active_tab(&text);
                                        let active = ws.tab_mgr.active_tab().active_pane;
                                        if let Some(p) = ws.panes.get_mut(&active) {
                                            p.terminal.dirty = true;
                                        }
                                    }
                                }
                            }
                        }
                        0x08 => ws.palette.pop_char(), // Backspace
                        _ => {}                        // Other keys ignored in palette mode
                    }
                    // Update a11y snapshot + focus
                    update_overlay_a11y(&ws.palette, &ws.history_browser, &ws.a11y_state);
                    unsafe {
                        wixen_a11y::events::raise_structure_changed(&ws.uia_provider, &mut [0i32]);
                        if ws.palette.visible {
                            let root: IRawElementProviderFragmentRoot =
                                ws.uia_provider.cast().unwrap();
                            let panel = wixen_a11y::palette_provider::PalettePanelProvider::new(
                                ws.window.hwnd(),
                                Arc::clone(&ws.a11y_state),
                                root,
                            );
                            let frag: IRawElementProviderSimple = panel.into();
                            wixen_a11y::events::raise_focus_changed(&frag);
                        } else {
                            wixen_a11y::events::raise_focus_changed(&ws.uia_provider);
                        }
                    }
                    // Mark dirty to re-render overlay
                    let active = ws.tab_mgr.active_tab().active_pane;
                    if let Some(p) = ws.panes.get_mut(&active) {
                        p.terminal.dirty = true;
                    }
                    return PostAction::None;
                }

                // ---- Command history browser keys (modal) ----
                if ws.history_browser.visible {
                    let active = ws.tab_mgr.active_tab().active_pane;
                    match vk {
                        0x1B => ws.history_browser.close(),       // Escape
                        0x26 => ws.history_browser.select_prev(), // Up
                        0x28 => ws.history_browser.select_next(), // Down
                        0x0D => {
                            // Enter — write the selected command to the PTY
                            if let Some(cmd) = ws.history_browser.confirm()
                                && let Some(pane) = ws.panes.get_mut(&active)
                            {
                                let _ = pane.pty.write(cmd.as_bytes());
                            }
                        }
                        0x08 => {
                            // Backspace — refine the query
                            if let Some(pane) = ws.panes.get(&active) {
                                ws.history_browser.pop_char(&pane.command_history);
                            }
                        }
                        _ => {}
                    }
                    // Refresh the UIA snapshot so screen readers observe the
                    // new selection / filtered results (and the closed state).
                    update_overlay_a11y(&ws.palette, &ws.history_browser, &ws.a11y_state);
                    if let Some(p) = ws.panes.get_mut(&active) {
                        p.terminal.dirty = true;
                    }
                    return PostAction::None;
                }

                // ---- Settings UI keys (modal) ----
                if ws.settings_ui.visible {
                    match vk {
                        0x1B => ws.settings_ui.close(), // Escape
                        0x09 => {
                            // Tab/Shift+Tab — field or sub-field navigation
                            if ws.settings_ui.is_keybinding_editing() {
                                if shift {
                                    ws.settings_ui.keybinding_prev_sub();
                                } else {
                                    ws.settings_ui.keybinding_next_sub();
                                }
                            } else if shift {
                                ws.settings_ui.prev_field();
                            } else {
                                ws.settings_ui.next_field();
                            }
                        }
                        0x0D => ws.settings_ui.start_edit(), // Enter — activate field
                        0x26 => {
                            // Up — cycle dropdown backward or prev field
                            ws.settings_ui.prev_field();
                        }
                        0x28 => {
                            // Down — cycle dropdown forward or next field
                            ws.settings_ui.next_field();
                        }
                        0x25 => ws.settings_ui.prev_tab(), // Left — prev tab
                        0x27 => ws.settings_ui.next_tab(), // Right — next tab
                        0x20 => {
                            // Space — toggle or modifier checkbox
                            if ws.settings_ui.is_keybinding_editing() {
                                ws.settings_ui.keybinding_toggle_modifier();
                            } else {
                                ws.settings_ui.start_edit();
                            }
                        }
                        0x08 => ws.settings_ui.pop_char(), // Backspace
                        // Ctrl+S — save settings
                        _ if ctrl && !shift && !alt && vk == 0x53 => {
                            let new_config = ws.settings_ui.save();
                            if let Err(e) = new_config.save(&g.cfg_path) {
                                error!("Failed to save config: {}", e);
                            } else {
                                info!("Settings saved");
                            }
                            ws.settings_ui.close();
                        }
                        _ => {}
                    }
                    // Update a11y snapshot + focus management
                    update_settings_a11y(&ws.settings_ui, &ws.a11y_state);
                    if !ws.settings_ui.visible {
                        // Settings was closed (Escape or Ctrl+S) — return focus
                        unsafe {
                            wixen_a11y::events::raise_structure_changed(
                                &ws.uia_provider,
                                &mut [0i32],
                            );
                            wixen_a11y::events::raise_focus_changed(&ws.uia_provider);
                        }
                    }
                    // Mark dirty to re-render overlay
                    let active = ws.tab_mgr.active_tab().active_pane;
                    if let Some(p) = ws.panes.get_mut(&active) {
                        p.terminal.dirty = true;
                    }
                    return PostAction::None;
                }

                // ---- Search-mode keys (modal, not user-configurable) ----
                let active = ws.tab_mgr.active_tab().active_pane;
                let in_search = ws.panes.get(&active).is_some_and(|p| p.search_mode);

                if in_search {
                    let Some(pane) = ws.panes.get_mut(&active) else {
                        return PostAction::None;
                    };

                    // Escape — close search
                    if vk == 0x1B {
                        pane.search_mode = false;
                        pane.search_engine.clear();
                        pane.search_buffer.clear();
                        set_window_title(ws.window.hwnd(), &ws.base_title);
                        pane.terminal.dirty = true;
                        return PostAction::None;
                    }

                    // F3 / Shift+F3 — next/prev match
                    if vk == 0x72 {
                        let dir = if shift {
                            SearchDirection::Backward
                        } else {
                            SearchDirection::Forward
                        };
                        pane.search_engine.next_match(dir);
                        update_search_title(
                            ws.window.hwnd(),
                            &ws.base_title,
                            &pane.search_buffer,
                            &pane.search_engine,
                        );
                        pane.terminal.dirty = true;
                        return PostAction::None;
                    }

                    // Backspace — delete last search char
                    if vk == 0x08 {
                        pane.search_buffer.pop();
                        pane.search_engine.search(
                            &pane.terminal,
                            &pane.search_buffer,
                            SearchOptions::default(),
                        );
                        update_search_title(
                            ws.window.hwnd(),
                            &ws.base_title,
                            &pane.search_buffer,
                            &pane.search_engine,
                        );
                        pane.terminal.dirty = true;
                        return PostAction::None;
                    }
                }

                // ---- Configurable keybinding dispatch ----
                let chord = vk_to_chord(vk, ctrl, shift, alt);
                if vk == 0xBC {
                    info!(vk, ctrl, shift, alt, %chord, "Comma key pressed");
                }
                debug!(vk, ctrl, shift, alt, %chord, "Key event");
                if let Some(resolved) = g.keybinding_map.get(&chord) {
                    info!(%chord, action = %resolved.action, "Keybinding matched");
                    let action = resolved.action.clone();
                    let args = resolved.args.clone();

                    // Tear-off moves live PTYs between windows, so it is
                    // handled by the main loop, not the per-window dispatch.
                    if action == "tear_off_tab" {
                        return PostAction::TearOff;
                    }

                    // Handle broadcast toggle directly
                    if action == "toggle_broadcast_input" {
                        ws.broadcast_input = !ws.broadcast_input;
                        info!(broadcast = ws.broadcast_input, "Broadcast input toggled");
                        announce_mode_toggle(
                            ToggleMode::BroadcastInput,
                            ws.broadcast_input,
                            &ws.uia_provider,
                            g.config.accessibility.screen_reader_output,
                            &g.audio_config,
                        );
                        return PostAction::None;
                    }

                    // Handle send_text directly (needs PTY access + args)
                    if action == "send_text" {
                        if let Some(text) = &args {
                            let active = ws.tab_mgr.active_tab().active_pane;
                            if let Some(pane) = ws.panes.get_mut(&active) {
                                let bytes = unescape_send_text(text);
                                let _ = pane.pty.write(&bytes);
                            }
                        }
                        return PostAction::None;
                    }

                    // Handle palette/settings toggles directly (need local access)
                    if action == "command_palette" {
                        ws.palette.toggle();
                        update_overlay_a11y(&ws.palette, &ws.history_browser, &ws.a11y_state);
                        unsafe {
                            wixen_a11y::events::raise_structure_changed(
                                &ws.uia_provider,
                                &mut [0i32],
                            );
                            if ws.palette.visible {
                                let root: IRawElementProviderFragmentRoot =
                                    ws.uia_provider.cast().unwrap();
                                let panel = wixen_a11y::palette_provider::PalettePanelProvider::new(
                                    ws.window.hwnd(),
                                    Arc::clone(&ws.a11y_state),
                                    root,
                                );
                                let frag: IRawElementProviderSimple = panel.into();
                                wixen_a11y::events::raise_focus_changed(&frag);
                            } else {
                                wixen_a11y::events::raise_focus_changed(&ws.uia_provider);
                            }
                        }
                        let active = ws.tab_mgr.active_tab().active_pane;
                        if let Some(p) = ws.panes.get_mut(&active) {
                            p.terminal.dirty = true;
                        }
                        return PostAction::None;
                    }
                    if action == "settings" || action == "open_settings" {
                        ws.settings_ui.toggle();
                        update_settings_a11y(&ws.settings_ui, &ws.a11y_state);
                        // Fire structure changed so NVDA re-queries the tree,
                        // then fire focus on the settings panel (or terminal
                        // root when closing).
                        unsafe {
                            wixen_a11y::events::raise_structure_changed(
                                &ws.uia_provider,
                                &mut [0i32],
                            );
                            if ws.settings_ui.visible {
                                // Create a settings panel provider and fire focus
                                let root: IRawElementProviderFragmentRoot =
                                    ws.uia_provider.cast().unwrap();
                                let panel =
                                    wixen_a11y::settings_provider::SettingsPanelProvider::new(
                                        ws.window.hwnd(),
                                        Arc::clone(&ws.a11y_state),
                                        root,
                                    );
                                let frag: IRawElementProviderSimple = panel.into();
                                wixen_a11y::events::raise_focus_changed(&frag);
                            } else {
                                // Return focus to terminal
                                wixen_a11y::events::raise_focus_changed(&ws.uia_provider);
                            }
                        }
                        let active = ws.tab_mgr.active_tab().active_pane;
                        if let Some(p) = ws.panes.get_mut(&active) {
                            p.terminal.dirty = true;
                        }
                        return PostAction::None;
                    }

                    // Actions needing loop-local state (macros, history
                    // overlay, tray) are handled here before the generic
                    // dispatcher.
                    if handle_local_action(
                        &action,
                        args.as_deref(),
                        &g.macro_store,
                        &mut ws.history_browser,
                        &ws.tab_mgr,
                        &mut ws.panes,
                        &mut ws.window,
                        &mut g.tray_icon,
                        &ws.uia_provider,
                        g.config.accessibility.screen_reader_output,
                    ) {
                        // A local action may have toggled the history
                        // browser (show_history) — refresh the UIA snapshot.
                        update_overlay_a11y(&ws.palette, &ws.history_browser, &ws.a11y_state);
                        let active = ws.tab_mgr.active_tab().active_pane;
                        if let Some(p) = ws.panes.get_mut(&active) {
                            p.terminal.dirty = true;
                        }
                        return PostAction::None;
                    }

                    let result = dispatch_keybinding_action(
                        &action,
                        &mut ws.tab_mgr,
                        &mut ws.panes,
                        &g.config,
                        &mut ws.close_requested,
                        ws.current_cols,
                        ws.current_rows,
                        &mut ws.window,
                        &ws.base_title,
                        &g.plugin_lua,
                        &g.plugin_registry,
                    );
                    if let DispatchResult::ModeToggled { mode, active } = result {
                        announce_mode_toggle(
                            mode,
                            active,
                            &ws.uia_provider,
                            g.config.accessibility.screen_reader_output,
                            &g.audio_config,
                        );
                    }
                    let deferred_handled = handle_deferred_action(
                        result,
                        &mut g.config,
                        &mut ws.renderer,
                        &ws.window,
                        &mut ws.cell_width,
                        &mut ws.cell_height,
                        &mut ws.current_cols,
                        &mut ws.current_rows,
                        &mut ws.panes,
                        &mut g.config_watcher,
                        &g.cfg_path,
                    );
                    if deferred_handled {
                        return PostAction::None;
                    }
                }

                // ---- Normal key sequences → active pane's PTY ----
                let active = ws.tab_mgr.active_tab().active_pane;
                if let Some(pane) = ws.panes.get_mut(&active)
                    && !pane.search_mode
                    && !ws.tab_mgr.active_tab().pane_tree.is_read_only(active)
                {
                    // Track arrow keys for boundary detection.
                    // Suppress if we already know we're at a boundary.
                    let nav = match vk {
                        0x25 if !shift && !ctrl && !alt => Some(NavKey::Left),
                        0x27 if !shift && !ctrl && !alt => Some(NavKey::Right),
                        0x26 if !shift && !ctrl && !alt => Some(NavKey::Up),
                        0x28 if !shift && !ctrl && !alt => Some(NavKey::Down),
                        _ => None,
                    };

                    // Suppress at known boundaries
                    let suppress = match nav {
                        Some(NavKey::Up) if pane.at_history_start => true,
                        Some(NavKey::Down) if pane.at_history_end => true,
                        _ => false,
                    };

                    if suppress {
                        play_event(&g.audio_config, AudioEvent::HistoryBoundary);
                    } else {
                        // Record which nav key for frame loop boundary detection
                        if nav.is_some() {
                            pane.last_nav_key = nav;
                            pane.prev_cursor_col = pane.terminal.grid.cursor.col;
                        }

                        // Clear boundary flags on movement in opposite direction
                        // or on any non-arrow key (typing resets history state)
                        match nav {
                            Some(NavKey::Down) => pane.at_history_start = false,
                            Some(NavKey::Up) => pane.at_history_end = false,
                            None => {
                                pane.at_history_start = false;
                                pane.at_history_end = false;
                            }
                            _ => {}
                        }

                        if let Some(seq) = wixen_core::keyboard::encode_key_with_kitty(
                            vk,
                            shift,
                            ctrl,
                            alt,
                            pane.terminal.modes.cursor_keys_application,
                            pane.terminal.kitty_keyboard_flags(),
                        ) && let Err(e) = pane.pty.write(&seq)
                        {
                            error!("PTY write error: {}", e);
                        }
                    }
                }
            }
        }
        WindowEvent::CharInput(ch) => {
            // Palette mode: printable chars go to palette query
            if ws.palette.visible {
                if ch >= ' ' && !ch.is_control() {
                    ws.palette.push_char(ch);
                    update_overlay_a11y(&ws.palette, &ws.history_browser, &ws.a11y_state);
                    let active = ws.tab_mgr.active_tab().active_pane;
                    if let Some(p) = ws.panes.get_mut(&active) {
                        p.terminal.dirty = true;
                    }
                }
                return PostAction::None;
            }

            // History browser mode: printable chars refine the query
            if ws.history_browser.visible {
                if ch >= ' ' && !ch.is_control() {
                    let active = ws.tab_mgr.active_tab().active_pane;
                    if let Some(pane) = ws.panes.get(&active) {
                        ws.history_browser.push_char(&pane.command_history, ch);
                    }
                    if let Some(p) = ws.panes.get_mut(&active) {
                        p.terminal.dirty = true;
                    }
                }
                return PostAction::None;
            }

            // Settings mode: printable chars go to settings field editing
            if ws.settings_ui.visible && ws.settings_ui.editing {
                if ch >= ' ' && !ch.is_control() {
                    ws.settings_ui.push_char(ch);
                    update_settings_a11y(&ws.settings_ui, &ws.a11y_state);
                    let active = ws.tab_mgr.active_tab().active_pane;
                    if let Some(p) = ws.panes.get_mut(&active) {
                        p.terminal.dirty = true;
                    }
                }
                return PostAction::None;
            }

            let active = ws.tab_mgr.active_tab().active_pane;
            let pane = match ws.panes.get_mut(&active) {
                Some(p) => p,
                None => return PostAction::None,
            };

            // Search mode: printable chars go to the search buffer
            if pane.search_mode {
                if ch >= ' ' && !ch.is_control() {
                    pane.search_buffer.push(ch);
                    pane.search_engine.search(
                        &pane.terminal,
                        &pane.search_buffer,
                        SearchOptions::default(),
                    );
                    update_search_title(
                        ws.window.hwnd(),
                        &ws.base_title,
                        &pane.search_buffer,
                        &pane.search_engine,
                    );
                    pane.terminal.dirty = true;
                }
                return PostAction::None;
            }

            // Ctrl+C copies when there's a selection, otherwise sends to PTY
            if ch == '\x03' && pane.terminal.selection.is_some() {
                if let Some(text) = pane.terminal.selected_text() {
                    clipboard_set(&text);
                }
                pane.terminal.clear_selection();
                return PostAction::None;
            }

            // Ctrl+V pastes from clipboard
            if ch == '\x16' {
                if let Some(text) = clipboard_get() {
                    if pane.terminal.modes.bracketed_paste {
                        let _ = pane.pty.write(b"\x1b[200~");
                        let _ = pane.pty.write(text.as_bytes());
                        let _ = pane.pty.write(b"\x1b[201~");
                    } else {
                        let _ = pane.pty.write(text.as_bytes());
                    }
                }
                return PostAction::None;
            }

            // Read-only panes block character input to PTY
            if ws.tab_mgr.active_tab().pane_tree.is_read_only(active) {
                trace!("Read-only pane — char input blocked");
                return PostAction::None;
            }

            let mut buf = [0u8; 4];
            let s = ch.encode_utf8(&mut buf);
            if let Err(e) = pane.pty.write(s.as_bytes()) {
                error!("PTY write error: {}", e);
            }
            // Broadcast: send to all other panes too
            if ws.broadcast_input {
                let bytes = s.as_bytes().to_vec();
                let skip = active;
                // End the borrow on `pane` by shadowing it
                #[allow(unused_variables)]
                let pane = ();
                for (&pid, p) in ws.panes.iter_mut() {
                    if pid != skip {
                        let _ = p.pty.write(&bytes);
                    }
                }
            }
        }
        WindowEvent::DoubleClick { x, y } => {
            let tbh = ws.renderer.tab_bar_height(ws.tab_mgr.tab_count());
            if (y as f32) < tbh && tbh > 0.0 {
                // Double-click in tab bar → rename the clicked tab
                ws.palette.open_input("Enter new tab name:");
                update_overlay_a11y(&ws.palette, &ws.history_browser, &ws.a11y_state);
                let active = ws.tab_mgr.active_tab().active_pane;
                if let Some(p) = ws.panes.get_mut(&active) {
                    p.terminal.dirty = true;
                }
            } else {
                // Double-click in terminal → word selection
                let col = ((x as f32 - pad_left) / ws.cell_width) as usize;
                let row = ((y as f32 - tbh - pad_top) / ws.cell_height) as usize;
                let active = ws.tab_mgr.active_tab().active_pane;
                if let Some(pane) = ws.panes.get_mut(&active) {
                    pane.terminal.start_selection(col, row, SelectionType::Word);
                }
                // Record for triple-click detection
                ws.last_dblclick = Some((Instant::now(), x, y));
            }
        }
        WindowEvent::MouseDown { button, x, y, mods } => {
            let tbh = ws.renderer.tab_bar_height(ws.tab_mgr.tab_count());

            // Triple-click detection: left-click shortly after double-click
            // at roughly the same position → line selection
            if matches!(button, wixen_ui::window::MouseButton::Left) {
                if let Some((t, dx, dy)) = ws.last_dblclick {
                    let elapsed = t.elapsed().as_millis();
                    let near = (x - dx).abs() < 10 && (y - dy).abs() < 10;
                    if elapsed < 500 && near && (y as f32) >= tbh {
                        ws.last_dblclick = None;
                        let col = ((x as f32 - pad_left) / ws.cell_width) as usize;
                        let row = ((y as f32 - tbh - pad_top) / ws.cell_height) as usize;
                        let active = ws.tab_mgr.active_tab().active_pane;
                        if let Some(pane) = ws.panes.get_mut(&active) {
                            pane.terminal.start_selection(col, row, SelectionType::Line);
                        }
                        return PostAction::None;
                    }
                }
                ws.last_dblclick = None;
            }
            if (y as f32) < tbh && tbh > 0.0 {
                // Middle-click in tab bar → close the clicked tab
                if matches!(button, wixen_ui::window::MouseButton::Middle) {
                    let tab_items = ws.tab_mgr.tab_bar_items();
                    let tab_width = ws.renderer.font_metrics().cell_width * ws.current_cols as f32
                        / tab_items.len().max(1) as f32;
                    let clicked_idx = (x as f32 / tab_width).floor() as usize;
                    if let Some((tab_id, _, _, _, _)) = tab_items.get(clicked_idx) {
                        let tab_id = *tab_id;
                        if ws.tab_mgr.close_tab(tab_id) {
                            let new_active = ws.tab_mgr.active_tab().active_pane;
                            if let Some(p) = ws.panes.get_mut(&new_active) {
                                p.terminal.dirty = true;
                            }
                            dispatch_plugin_event(
                                &g.plugin_lua,
                                &g.plugin_registry,
                                &PluginEvent::TabClosed { tab_id: tab_id.0 },
                            );
                        }
                    }
                } else {
                    // Left-click in tab bar — select the clicked tab + start drag
                    let tab_items = ws.tab_mgr.tab_bar_items();
                    let tab_width = ws.renderer.font_metrics().cell_width * ws.current_cols as f32
                        / tab_items.len().max(1) as f32;
                    let clicked_idx = (x as f32 / tab_width).floor() as usize;
                    if let Some((tab_id, _, _, _, _)) = tab_items.get(clicked_idx) {
                        let tab_id = *tab_id;
                        ws.tab_mgr.select_tab(tab_id);
                        ws.tab_drag = Some((clicked_idx, x));
                        let new_active = ws.tab_mgr.active_tab().active_pane;
                        if let Some(p) = ws.panes.get_mut(&new_active) {
                            p.terminal.dirty = true;
                        }
                    }
                }
            } else {
                // Hit-test against pane layout to find the target pane
                let content_w = {
                    let (ww, _) = ws.window.client_size();
                    ww as f32 - pad_left - g.config.window.padding.right as f32
                };
                let content_h = {
                    let (_, wh) = ws.window.client_size();
                    wh as f32 - tbh - pad_top - g.config.window.padding.bottom as f32
                };
                let norm_x = (x as f32 - pad_left) / content_w.max(1.0);
                let norm_y = (y as f32 - tbh - pad_top) / content_h.max(1.0);
                let hit_pane = ws
                    .tab_mgr
                    .active_tab()
                    .pane_tree
                    .pane_at_point(norm_x, norm_y);
                let target_pane_id = hit_pane.unwrap_or(ws.tab_mgr.active_tab().active_pane);

                // Left-click in a non-active pane → switch focus to it
                if matches!(button, wixen_ui::window::MouseButton::Left)
                    && target_pane_id != ws.tab_mgr.active_tab().active_pane
                {
                    ws.tab_mgr.set_active_pane(target_pane_id);
                }

                // Compute grid coordinates relative to the hit pane's rect
                let pane_rects = ws.tab_mgr.active_tab().pane_tree.layout();
                let pane_rect = pane_rects.iter().find(|r| r.pane_id == target_pane_id);
                let (pane_px, pane_py) = if let Some(pr) = pane_rect {
                    (pad_left + pr.x * content_w, pad_top + pr.y * content_h)
                } else {
                    (pad_left, pad_top)
                };
                let col = ((x as f32 - pane_px) / ws.cell_width).max(0.0) as usize;
                let row = ((y as f32 - tbh - pane_py) / ws.cell_height).max(0.0) as usize;

                if let Some(pane) = ws.panes.get_mut(&target_pane_id) {
                    let ctrl = mods & 16 != 0;
                    let shift = mods & 4 != 0;

                    // Ctrl+click: open hyperlink if present.
                    if ctrl && matches!(button, wixen_ui::window::MouseButton::Left) {
                        if let Some(url) = pane.terminal.hyperlink_at(col, row) {
                            open_url(&url);
                            // Don't start selection or report to PTY.
                        } else {
                            // No URL — fall through to normal handling.
                        }
                    } else {
                        let mouse_mode = pane.terminal.modes.mouse_tracking;
                        if mouse_mode != MouseMode::None && !shift {
                            // Report to PTY
                            let mb = ui_to_mouse_button(button);
                            let mm = mods_to_mouse_mods(mods);
                            send_mouse_event(pane, mb, mm, col, row, true);
                        } else if matches!(button, wixen_ui::window::MouseButton::Right) {
                            // Right-click → show context menu
                            let has_sel = pane.terminal.selection.is_some();
                            ws.window.show_context_menu(x, y, has_sel);
                        } else {
                            // UI selection (Alt+click = block/rectangular selection)
                            let alt = mods & 8 != 0;
                            let sel_type = if alt {
                                SelectionType::Block
                            } else {
                                SelectionType::Normal
                            };
                            ws.mouse_dragging = true;
                            pane.terminal.start_selection(col, row, sel_type);
                        }
                    }
                }
            }
        }
        WindowEvent::MouseMove { x, y, mods } => {
            // Tab drag reorder
            if let Some((from_idx, start_x)) = ws.tab_drag {
                let tbh = ws.renderer.tab_bar_height(ws.tab_mgr.tab_count());
                if (x - start_x).abs() > 5 && (y as f32) < tbh {
                    let tab_count = ws.tab_mgr.tab_count();
                    let tab_width = ws.renderer.font_metrics().cell_width * ws.current_cols as f32
                        / tab_count.max(1) as f32;
                    let to_idx = (x as f32 / tab_width)
                        .floor()
                        .clamp(0.0, (tab_count - 1) as f32)
                        as usize;
                    if to_idx != from_idx {
                        ws.tab_mgr.move_tab(from_idx, to_idx);
                        ws.tab_drag = Some((to_idx, x));
                        let active = ws.tab_mgr.active_tab().active_pane;
                        if let Some(p) = ws.panes.get_mut(&active) {
                            p.terminal.dirty = true;
                        }
                    }
                }
            }

            let tbh = ws.renderer.tab_bar_height(ws.tab_mgr.tab_count());
            let col = ((x as f32 - pad_left) / ws.cell_width).max(0.0) as usize;
            let row = ((y as f32 - tbh - pad_top) / ws.cell_height).max(0.0) as usize;
            let active = ws.tab_mgr.active_tab().active_pane;
            if let Some(pane) = ws.panes.get_mut(&active) {
                let mouse_mode = pane.terminal.modes.mouse_tracking;
                let shift = mods & 4 != 0;
                if mouse_mode != MouseMode::None && !shift {
                    // Report motion when Button or Any mode
                    let report_motion = matches!(mouse_mode, MouseMode::Button | MouseMode::Any);
                    if report_motion {
                        let mut mm = mods_to_mouse_mods(mods);
                        mm.motion = true;
                        send_mouse_event(pane, mouse::MouseButton::Left, mm, col, row, true);
                    }
                } else if ws.mouse_dragging {
                    pane.terminal.update_selection(col, row);
                }
            }
        }
        WindowEvent::MouseUp { button, x, y, mods } => {
            let tbh = ws.renderer.tab_bar_height(ws.tab_mgr.tab_count());
            let col = ((x as f32 - pad_left) / ws.cell_width).max(0.0) as usize;
            let row = ((y as f32 - tbh - pad_top) / ws.cell_height).max(0.0) as usize;
            let active = ws.tab_mgr.active_tab().active_pane;
            if let Some(pane) = ws.panes.get_mut(&active) {
                let shift = mods & 4 != 0;
                let mouse_mode = pane.terminal.modes.mouse_tracking;
                if mouse_mode != MouseMode::None && !shift {
                    let mb = ui_to_mouse_button(button);
                    let mm = mods_to_mouse_mods(mods);
                    send_mouse_event(pane, mb, mm, col, row, false);
                }
            }
            ws.mouse_dragging = false;
            ws.tab_drag = None;
        }
        WindowEvent::MouseWheel { delta, x, y, mods } => {
            let active = ws.tab_mgr.active_tab().active_pane;
            if let Some(pane) = ws.panes.get_mut(&active) {
                let shift = mods & 4 != 0;
                let mouse_mode = pane.terminal.modes.mouse_tracking;
                if mouse_mode != MouseMode::None && !shift {
                    let tbh = ws.renderer.tab_bar_height(ws.tab_mgr.tab_count());
                    let col = ((x as f32 - pad_left) / ws.cell_width).max(0.0) as usize;
                    let row = ((y as f32 - tbh - pad_top) / ws.cell_height).max(0.0) as usize;
                    let mb = if delta > 0 {
                        mouse::MouseButton::WheelUp
                    } else {
                        mouse::MouseButton::WheelDown
                    };
                    let mm = mods_to_mouse_mods(mods);
                    send_mouse_event(pane, mb, mm, col, row, true);
                } else if pane.terminal.modes.alternate_screen {
                    // In alternate screen (vim, less, etc.), translate
                    // scroll to arrow key presses so the app handles it
                    let count = delta.unsigned_abs() * 3;
                    let key = if delta > 0 { b"\x1b[A" } else { b"\x1b[B" };
                    for _ in 0..count {
                        let _ = pane.pty.write(key);
                    }
                } else {
                    let lines = delta as i32 * 3;
                    pane.terminal.scroll_viewport(lines);
                }
            }
        }
        WindowEvent::FocusGained => {
            ws.window_focused = true;
            if let Some(mut s) = ws.a11y_state.try_write() {
                s.has_focus = true;
            }
        }
        WindowEvent::FocusLost => {
            ws.window_focused = false;
            if let Some(mut s) = ws.a11y_state.try_write() {
                s.has_focus = false;
            }
        }
        WindowEvent::QuakeToggle => {
            ws.window.toggle_visibility();
        }
        WindowEvent::TrayClick(click) => {
            use wixen_ui::tray::TrayClickEvent;
            match click {
                TrayClickEvent::LeftClick | TrayClickEvent::DoubleClick => {
                    ws.window.toggle_visibility();
                }
                TrayClickEvent::RightClick => {
                    // Context menu is defined but not yet displayed.
                }
            }
        }
        WindowEvent::FilesDropped(paths) => {
            // Paste dropped file paths into the active pane.
            let active = ws.tab_mgr.active_tab().active_pane;
            if let Some(pane) = ws.panes.get_mut(&active) {
                let text = wixen_ui::window::format_dropped_paths(&paths);
                if pane.terminal.modes.bracketed_paste {
                    let _ = pane.pty.write(b"\x1b[200~");
                    let _ = pane.pty.write(text.as_bytes());
                    let _ = pane.pty.write(b"\x1b[201~");
                } else {
                    let _ = pane.pty.write(text.as_bytes());
                }
            }
        }
        WindowEvent::ContextMenu(action) => {
            use wixen_ui::window::ContextMenuAction;
            let active = ws.tab_mgr.active_tab().active_pane;
            match action {
                ContextMenuAction::Copy => {
                    if let Some(pane) = ws.panes.get_mut(&active) {
                        if let Some(text) = pane.terminal.selected_text() {
                            clipboard_set(&text);
                        }
                        pane.terminal.clear_selection();
                        pane.terminal.dirty = true;
                    }
                }
                ContextMenuAction::Paste => {
                    if let Some(pane) = ws.panes.get_mut(&active)
                        && let Some(text) = clipboard_get()
                    {
                        if pane.terminal.modes.bracketed_paste {
                            let _ = pane.pty.write(b"\x1b[200~");
                            let _ = pane.pty.write(text.as_bytes());
                            let _ = pane.pty.write(b"\x1b[201~");
                        } else {
                            let _ = pane.pty.write(text.as_bytes());
                        }
                    }
                }
                ContextMenuAction::SelectAll => {
                    if let Some(pane) = ws.panes.get_mut(&active) {
                        let rows = pane.terminal.grid.rows;
                        let cols = pane.terminal.grid.cols;
                        pane.terminal.start_selection(0, 0, SelectionType::Normal);
                        pane.terminal.update_selection(cols - 1, rows - 1);
                        pane.terminal.dirty = true;
                    }
                }
                ContextMenuAction::Search => {
                    if let Some(pane) = ws.panes.get_mut(&active) {
                        pane.search_mode = !pane.search_mode;
                        if !pane.search_mode {
                            pane.search_engine.clear();
                            pane.search_buffer.clear();
                            set_window_title(ws.window.hwnd(), &ws.base_title);
                        } else {
                            set_window_title(
                                ws.window.hwnd(),
                                &format!("{} — Search: _", ws.base_title),
                            );
                        }
                        pane.terminal.dirty = true;
                    }
                }
                ContextMenuAction::SplitHorizontal | ContextMenuAction::SplitVertical => {
                    // Pane splitting is tracked in the data model.
                    // Full rendering support for split panes is deferred.
                    info!(?action, "Pane split requested (render not yet wired)");
                }
                ContextMenuAction::NewTab | ContextMenuAction::CloseTab => {
                    let name = match action {
                        ContextMenuAction::NewTab => "new_tab",
                        _ => "close_tab",
                    };
                    let result = dispatch_keybinding_action(
                        name,
                        &mut ws.tab_mgr,
                        &mut ws.panes,
                        &g.config,
                        &mut ws.close_requested,
                        ws.current_cols,
                        ws.current_rows,
                        &mut ws.window,
                        &ws.base_title,
                        &g.plugin_lua,
                        &g.plugin_registry,
                    );
                    handle_deferred_action(
                        result,
                        &mut g.config,
                        &mut ws.renderer,
                        &ws.window,
                        &mut ws.cell_width,
                        &mut ws.cell_height,
                        &mut ws.current_cols,
                        &mut ws.current_rows,
                        &mut ws.panes,
                        &mut g.config_watcher,
                        &g.cfg_path,
                    );
                }
                ContextMenuAction::Settings => {
                    ws.settings_ui.toggle();
                    update_settings_a11y(&ws.settings_ui, &ws.a11y_state);
                    unsafe {
                        wixen_a11y::events::raise_structure_changed(&ws.uia_provider, &mut [0i32]);
                        if ws.settings_ui.visible {
                            let root: IRawElementProviderFragmentRoot =
                                ws.uia_provider.cast().unwrap();
                            let panel = wixen_a11y::settings_provider::SettingsPanelProvider::new(
                                ws.window.hwnd(),
                                Arc::clone(&ws.a11y_state),
                                root,
                            );
                            let frag: IRawElementProviderSimple = panel.into();
                            wixen_a11y::events::raise_focus_changed(&frag);
                        } else {
                            wixen_a11y::events::raise_focus_changed(&ws.uia_provider);
                        }
                    }
                    if let Some(p) = ws.panes.get_mut(&active) {
                        p.terminal.dirty = true;
                    }
                }
            }
        }
        WindowEvent::DpiChanged(new_dpi) => {
            info!(new_dpi, "DPI changed — rebuilding font");
            let dpi_f32 = new_dpi as f32;
            if let Err(e) = ws.renderer.rebuild_font(
                &g.config.font.family,
                g.config.font.size,
                dpi_f32,
                g.config.font.line_height,
                &g.config.font.fallback_fonts,
                g.config.font.ligatures,
                &g.config.font.font_path,
            ) {
                error!("Font rebuild failed on DPI change: {e}");
                return PostAction::None;
            }
            let metrics = ws.renderer.font_metrics();
            ws.cell_width = metrics.cell_width;
            ws.cell_height = metrics.cell_height;
            let (w, h) = ws.window.client_size();
            let pad_h = g.config.window.padding.horizontal() as f32;
            let pad_v = g.config.window.padding.vertical() as f32;
            ws.current_cols = ((w as f32 - pad_h) / ws.cell_width)
                .floor()
                .clamp(1.0, 500.0) as u16;
            ws.current_rows = ((h as f32 - pad_v) / ws.cell_height)
                .floor()
                .clamp(1.0, 500.0) as u16;
            for pane in ws.panes.values_mut() {
                pane.terminal
                    .resize(ws.current_cols as usize, ws.current_rows as usize);
                if let Err(e) = pane.pty.resize(ws.current_cols, ws.current_rows) {
                    error!("PTY resize error on DPI change: {}", e);
                }
                pane.terminal.dirty = true;
            }
        }
    }

    PostAction::None
}

/// Run one frame for a window: announcements, PTY output processing, and
/// rendering.
fn run_window_frame(ws: &mut WindowState, g: &mut AppGlobals) {
    let pad_left = g.config.window.padding.left as f32;
    let pad_top = g.config.window.padding.top as f32;
    let _ = (pad_left, pad_top);
    // Resolve the active pane after event processing (tab switches may have changed it)
    let active_pane_id = ws.tab_mgr.active_tab().active_pane;
    let current_tab_id = ws.tab_mgr.active_tab_id();

    // Feature 5: Announce pane position on switch
    if active_pane_id != ws.prev_active_pane {
        let label = wixen_ui::panes::pane_position_label(
            &ws.tab_mgr.active_tab().pane_tree,
            active_pane_id,
        );
        unsafe {
            raise_if_allowed(
                &ws.uia_provider,
                &label,
                "pane-switch",
                g.config.accessibility.screen_reader_output,
                "output",
            );
            // Re-anchor screen reader focus on the terminal root so the
            // newly active pane's content is what gets read next.
            wixen_a11y::events::raise_focus_changed(&ws.uia_provider);
        }
        play_event(&g.audio_config, AudioEvent::Navigation);
        ws.prev_active_pane = active_pane_id;
    }

    // Feature 6: Announce tab on switch
    if current_tab_id != ws.prev_active_tab_id {
        let announcement = wixen_ui::tabs::tab_announcement(&ws.tab_mgr, current_tab_id);
        if !announcement.is_empty() {
            unsafe {
                raise_if_allowed(
                    &ws.uia_provider,
                    &announcement,
                    "tab-switch",
                    g.config.accessibility.screen_reader_output,
                    "output",
                );
            }
            play_event(&g.audio_config, AudioEvent::Navigation);
        }
        ws.prev_active_tab_id = current_tab_id;
    }

    // Feature 9: Announce selection changes
    {
        let has_sel = ws
            .panes
            .get(&active_pane_id)
            .is_some_and(|p| p.terminal.selection.is_some());
        if has_sel != ws.prev_selection_active {
            ws.prev_selection_active = has_sel;
            if let Some(pane) = ws.panes.get(&active_pane_id) {
                let desc = selection_description(
                    pane.terminal.selection.as_ref(),
                    pane.terminal.grid.cols,
                );
                unsafe {
                    raise_if_allowed(
                        &ws.uia_provider,
                        &desc,
                        "selection",
                        g.config.accessibility.screen_reader_output,
                        "output",
                    );
                }
                play_event(&g.audio_config, AudioEvent::Selection);
            }
        }
    }

    // --- Process PTY output for ALL panes ---
    for (&pane_id, pane) in ws.panes.iter_mut() {
        while let Ok(event) = pane.pty_rx.try_recv() {
            match event {
                PtyEvent::Output(bytes) => {
                    // Feed a11y event throttler only for the active pane
                    if pane_id == active_pane_id
                        && let Ok(text) = std::str::from_utf8(&bytes)
                    {
                        pane.event_throttler.on_output(text);
                    }
                    let actions = pane.parser.process(&bytes);
                    for action in actions {
                        dispatch_action(&mut pane.terminal, action);
                    }
                    // Feature 2 & 3: Scan new output for errors and progress
                    if pane_id == active_pane_id
                        && let Ok(text) = std::str::from_utf8(&bytes)
                    {
                        for line in text.lines() {
                            // Feature 2: Error detection
                            match classify_output_line(line) {
                                OutputLineClass::Error => {
                                    play_event(&g.audio_config, AudioEvent::CommandError);
                                    unsafe {
                                        raise_if_allowed(
                                            &ws.uia_provider,
                                            line,
                                            "error-output",
                                            g.config.accessibility.screen_reader_output,
                                            "error",
                                        );
                                    }
                                }
                                OutputLineClass::Warning => {
                                    play_event(&g.audio_config, AudioEvent::OutputWarning);
                                }
                                _ => {}
                            }
                            // Feature 3: Progress detection
                            if let Some(pct) = detect_progress(line)
                                && ws.last_announced_progress != Some(pct)
                            {
                                ws.last_announced_progress = Some(pct);
                                let msg = format!("{pct}%");
                                unsafe {
                                    raise_if_allowed(
                                        &ws.uia_provider,
                                        &msg,
                                        "progress",
                                        g.config.accessibility.screen_reader_output,
                                        "output",
                                    );
                                }
                                if pct >= 100 {
                                    play_event(&g.audio_config, AudioEvent::ProgressComplete);
                                    ws.last_announced_progress = None;
                                } else {
                                    play_event(&g.audio_config, AudioEvent::Progress);
                                }
                            }
                        }
                    }
                    // Track command execution for long-running notifications
                    if let Some(block) = pane.terminal.shell_integ.current_block() {
                        match block.state {
                            wixen_shell_integ::BlockState::Executing => {
                                if pane.command_started_at.is_none() {
                                    pane.command_started_at = Some(Instant::now());
                                }
                            }
                            wixen_shell_integ::BlockState::Completed => {
                                if let Some(started) = pane.command_started_at.take() {
                                    let threshold = g.config.terminal.command_notify_threshold;
                                    if threshold > 0
                                        && started.elapsed().as_secs() >= threshold
                                        && !ws.window_focused
                                    {
                                        let cmd_name =
                                            block.command_text.as_deref().unwrap_or("command");
                                        info!(
                                            cmd = cmd_name,
                                            elapsed = started.elapsed().as_secs(),
                                            "Long-running command finished"
                                        );
                                        fire_bell(ws.window.hwnd(), wixen_config::BellStyle::Both);
                                    }
                                }
                                // Record completed command in pane history
                                if let Some(cmd_text) = block.command_text.as_deref() {
                                    pane.command_history.push(HistoryEntry {
                                        command: cmd_text.to_string(),
                                        exit_code: block.exit_code,
                                        cwd: block.cwd.clone(),
                                        timestamp: Some(SystemTime::now()),
                                        block_id: block.id,
                                    });
                                }
                            }
                            _ => {}
                        }
                    }
                    // Send any pending responses (DSR, DECRQSS) back to the PTY
                    for response in pane.terminal.drain_responses() {
                        let _ = pane.pty.write(&response);
                    }
                    // OSC 52: clipboard writes → system clipboard
                    for text in pane.terminal.drain_clipboard_writes() {
                        clipboard_set(&text);
                    }
                    // OSC 52: clipboard read query → inject response
                    if pane.terminal.clipboard_read_pending {
                        if let Some(text) = clipboard_get() {
                            pane.terminal.inject_clipboard_response(&text);
                            for response in pane.terminal.drain_responses() {
                                let _ = pane.pty.write(&response);
                            }
                        } else {
                            pane.terminal.clipboard_read_pending = false;
                        }
                    }
                    // Image placement announcements → a11y events
                    for ann in pane.terminal.drain_image_announcements() {
                        let message = ann.message();
                        info!(pane = pane_id.0, msg = %message, "Image placed");
                        unsafe {
                            raise_if_allowed(
                                &ws.uia_provider,
                                &message,
                                "image-placed",
                                g.config.accessibility.screen_reader_output,
                                "output",
                            );
                        }
                    }
                    // Command completion notifications for long-running commands
                    let notify_threshold = g.config.terminal.command_notify_threshold;
                    if notify_threshold > 0 {
                        let threshold = Duration::from_secs(notify_threshold);
                        for completion in
                            pane.terminal.shell_integ.take_completion_if_long(threshold)
                        {
                            let cmd_name = completion.command.as_deref().unwrap_or("command");
                            info!(
                                pane = pane_id.0,
                                cmd = cmd_name,
                                exit_code = ?completion.exit_code,
                                duration_secs = completion.duration.as_secs(),
                                "Command completion notification"
                            );
                            // Dispatch to plugins listening for command completion
                            let plugin_event = PluginEvent::CommandComplete {
                                command: cmd_name.to_string(),
                                exit_code: completion.exit_code.unwrap_or(0),
                                duration_ms: completion.duration.as_millis() as u64,
                            };
                            dispatch_plugin_event(&g.plugin_lua, &g.plugin_registry, &plugin_event);
                            // Feature 1: Announce command summary via UIA
                            if let Some(block) =
                                pane.terminal.shell_integ.block_by_id(completion.block_id)
                            {
                                let output_text = block.output.map(|range| {
                                    pane.terminal.extract_row_text(range.start, range.end)
                                });
                                let output_lines: Vec<&str> = output_text
                                    .as_deref()
                                    .map_or_else(Vec::new, |text| text.lines().collect());
                                let mut summary =
                                    wixen_core::structured_detect::completion_announcement(
                                        block,
                                        &output_lines,
                                    );
                                // Append an error/warning count summary so a
                                // screen-reader user hears "N errors, M warnings".
                                if let Some(errs) =
                                    wixen_core::error_detect::error_summary(&output_lines)
                                {
                                    summary.push_str(". ");
                                    summary.push_str(&errs);
                                }
                                unsafe {
                                    raise_if_allowed(
                                        &ws.uia_provider,
                                        &summary,
                                        "command-complete",
                                        g.config.accessibility.screen_reader_output,
                                        "command",
                                    );
                                }
                            }
                            // Feature 1: Play audio cue for command result
                            let audio_event = if completion.exit_code == Some(0)
                                || completion.exit_code.is_none()
                            {
                                AudioEvent::CommandSuccess
                            } else {
                                AudioEvent::CommandError
                            };
                            play_event(&g.audio_config, audio_event);
                        }
                    } else {
                        // Discard completions when notifications are disabled
                        let _ = pane.terminal.shell_integ.drain_completions();
                    }
                }
                PtyEvent::Exited(code) => {
                    info!(pane = pane_id.0, exit_code = ?code, "PTY exited");
                    pane.status = PaneStatus::Exited(code);
                    pane.terminal.write_exit_banner(code);

                    // Update the tab's exit status indicator
                    if let Some(tab) = ws.tab_mgr.tabs().iter().find(|t| t.active_pane == pane_id) {
                        let tab_id = tab.id;
                        ws.tab_mgr.set_exit_status(tab_id, code);
                    }

                    // Flash taskbar for background tab exits
                    if pane_id != active_pane_id {
                        use windows::Win32::UI::WindowsAndMessaging::*;
                        let info = FLASHWINFO {
                            cbSize: std::mem::size_of::<FLASHWINFO>() as u32,
                            hwnd: ws.window.hwnd(),
                            dwFlags: FLASHW_ALL,
                            uCount: 3,
                            dwTimeout: 0,
                        };
                        unsafe {
                            let _ = FlashWindowEx(&info);
                        }
                    }
                }
            }
        }

        // Sync terminal title → tab title (OSC 0/2 or OSC 7 CWD)
        if pane.terminal.title_dirty {
            pane.terminal.title_dirty = false;
            let new_title = pane.terminal.effective_title().to_string();
            if let Some(tab) = ws.tab_mgr.tabs().iter().find(|t| t.active_pane == pane_id) {
                let tab_id = tab.id;
                ws.tab_mgr.set_title(tab_id, &new_title);
            }
            // Also update the Win32 window title for the active tab
            if pane_id == active_pane_id {
                set_window_title(ws.window.hwnd(), &new_title);
            }
        }

        // Pump messages after PTY processing — COM calls from NVDA
        // (GetSelection, GetCaretRange) arrive as Win32 messages via UseComThreading.
        // Without this pump, they sit queued during the entire PTY/terminal processing
        // phase and COM can return RPC_E_SERVER_FAULT to the screen reader.
        ws.window.pump_messages();

        // Shell integration → a11y tree (active pane only)
        if pane_id == active_pane_id {
            let current_gen = pane.terminal.shell_integ.generation();
            // Collect pending output text before taking the write lock
            let pending_output = if pane.event_throttler.has_pending() {
                pane.event_throttler.take_pending()
            } else {
                None
            };

            let tree_changed = current_gen != pane.last_shell_generation;
            if tree_changed {
                pane.last_shell_generation = current_gen;
            }

            // Whether the a11y text (state.full_text) is about to be updated —
            // used to raise UIA TextChanged / LiveRegionChanged below.
            let a11y_text_changed = tree_changed || pending_output.is_some();

            // Single write lock: update tree, full_text, and cursor atomically
            let (structure_changed, cursor_moved, _row_changed) = {
                let new_row = pane.terminal.grid.cursor.row;
                let new_col = pane.terminal.grid.cursor.col;
                let needs_text_update = tree_changed || pending_output.is_some();

                if let Some(mut state) = ws.a11y_state.try_write() {
                    if tree_changed {
                        state
                            .tree
                            .rebuild(pane.terminal.shell_integ.blocks(), |start, end| {
                                pane.terminal.extract_row_text(start, end)
                            });
                    }
                    if needs_text_update {
                        state.full_text = pane.terminal.visible_text();
                    }
                    let moved = state.cursor_row != new_row || state.cursor_col != new_col;
                    let row_diff = state.cursor_row != new_row;
                    if moved {
                        state.cursor_row = new_row;
                        state.cursor_col = new_col;
                    }
                    if moved || needs_text_update {
                        // Update lock-free atomic so GetSelection/GetCaretRange
                        // always return the correct offset, even during frame processing
                        ws.cursor_offset.store(
                            state.cursor_offset_utf16(),
                            std::sync::atomic::Ordering::Release,
                        );
                    }
                    (tree_changed, moved, row_diff)
                } else {
                    (false, false, false)
                }
            };

            // Pump messages so pending COM calls (GetSelection, GetCaretRange)
            // can be dispatched against the now-consistent state.
            ws.window.pump_messages();

            // Raise events outside the lock
            if structure_changed {
                unsafe {
                    wixen_a11y::events::raise_structure_changed(&ws.uia_provider, &mut [0i32]);
                }
                if let Some(block) = pane.terminal.shell_integ.current_block()
                    && block.state == wixen_shell_integ::BlockState::Completed
                    && let Some(exit_code) = block.exit_code
                {
                    let cmd = block.command_text.as_deref().unwrap_or("command");
                    unsafe {
                        wixen_a11y::events::raise_command_complete(
                            &ws.uia_provider,
                            cmd,
                            exit_code,
                        );
                    }
                }
                trace!(generation = current_gen, "A11y tree rebuilt");
            }

            if cursor_moved {
                unsafe {
                    wixen_a11y::events::raise_text_selection_changed(&ws.uia_provider);
                }
            }

            // New terminal output changed the accessible text — notify UIA so
            // screen readers re-read the text and observe the live region.
            if a11y_text_changed {
                unsafe {
                    wixen_a11y::events::raise_text_changed(&ws.uia_provider);
                    wixen_a11y::events::raise_live_region_changed(&ws.uia_provider);
                }
            }

            // UIA live region events for accumulated output.
            // Feature 10: Verbosity filter — only raise for permitted categories.
            // Skip short output without newlines — that's keyboard echo.
            // NVDA's own character/word echo settings should control that,
            // not our notification events.
            let has_real_output = if let Some(raw_text) = pending_output {
                let text = wixen_a11y::strip_vt_escapes(&raw_text);
                let trimmed = text.trim();
                if !trimmed.is_empty() && trimmed.contains('\n') {
                    // Multi-line output — real terminal content
                    unsafe {
                        raise_if_allowed(
                            &ws.uia_provider,
                            &text,
                            "terminal-output",
                            g.config.accessibility.screen_reader_output,
                            "output",
                        );
                    }
                    true
                } else {
                    false
                }
            } else {
                false
            };

            // Row-change line reading is handled by NVDA's native caret
            // tracking via GetSelection + ExpandToEnclosingUnit(Line).

            // NVDA's native caret tracking handles character/line reading
            // via GetSelection + UIATextInfo. We only need to supplement for
            // history recall (line replacement) which NVDA can't detect as a
            // distinct operation.
            let scrollback_len = pane.terminal.scrollback.len();
            let abs_row = scrollback_len + pane.terminal.grid.cursor.row;
            let current_line = pane.terminal.extract_row_text(abs_row, abs_row);
            if current_line != pane.prev_cursor_line && !has_real_output && !structure_changed {
                let prev = &pane.prev_cursor_line;
                let cur_trimmed = current_line.trim_end();
                let prev_trimmed = prev.trim_end();

                if !prev.is_empty() && cur_trimmed != prev_trimmed {
                    let is_append = cur_trimmed.len() > prev_trimmed.len()
                        && cur_trimmed.starts_with(prev_trimmed);
                    let is_deletion = cur_trimmed.len() < prev_trimmed.len()
                        && prev_trimmed.starts_with(cur_trimmed);

                    if !is_append && !is_deletion {
                        // Line content replaced (up arrow history recall)
                        // Strip the shell prompt — announce only the command
                        let text = strip_prompt(cur_trimmed);
                        if text.is_empty() {
                            unsafe {
                                wixen_a11y::events::raise_notification(
                                    &ws.uia_provider,
                                    "line cleared",
                                    "cursor-line",
                                    false,
                                );
                            }
                        } else {
                            unsafe {
                                wixen_a11y::events::raise_notification(
                                    &ws.uia_provider,
                                    text,
                                    "cursor-line",
                                    false,
                                );
                            }
                        }
                    }
                }
            }
            // Boundary detection: consume last_nav_key and check if
            // the cursor/line didn't change (meaning we're at an edge).
            if let Some(nav) = pane.last_nav_key.take() {
                let new_col = pane.terminal.grid.cursor.col;
                match nav {
                    NavKey::Left if new_col == pane.prev_cursor_col && !cursor_moved => {
                        play_event(&g.audio_config, AudioEvent::EditBoundary);
                    }
                    NavKey::Right if new_col == pane.prev_cursor_col && !cursor_moved => {
                        play_event(&g.audio_config, AudioEvent::EditBoundary);
                    }
                    NavKey::Up if current_line.trim_end() == pane.prev_cursor_line.trim_end() => {
                        pane.at_history_start = true;
                        play_event(&g.audio_config, AudioEvent::HistoryBoundary);
                    }
                    NavKey::Down
                        if current_line.trim_end().is_empty()
                            && !pane.prev_cursor_line.trim_end().is_empty() =>
                    {
                        pane.at_history_end = true;
                        play_event(&g.audio_config, AudioEvent::HistoryBoundary);
                    }
                    _ => {}
                }
            }

            pane.prev_cursor_line = current_line;
        }
    }

    // --- Handle bell notifications ---
    for (&pane_id, pane) in ws.panes.iter_mut() {
        if pane.terminal.bell_pending {
            pane.terminal.bell_pending = false;
            // Feature 8: Suppress visual bell flash when reduced motion is active
            let bell_style = if wixen_config::should_reduce_motion(
                &g.config.accessibility.reduced_motion,
                wixen_ui::window::system_reduced_motion(),
            ) {
                match g.config.terminal.bell {
                    BellStyle::Visual => BellStyle::None,
                    BellStyle::Both => BellStyle::Audible,
                    other => other,
                }
            } else {
                g.config.terminal.bell
            };
            fire_bell(ws.window.hwnd(), bell_style);
            // Set bell badge on the tab if this is a background tab
            if pane_id != active_pane_id
                && let Some(tab) = ws.tab_mgr.tabs().iter().find(|t| t.active_pane == pane_id)
            {
                let tab_id = tab.id;
                ws.tab_mgr.set_bell(tab_id);
            }
        }
    }

    // Feature 4: Echo suppression — check for password prompts
    {
        let active = ws.tab_mgr.active_tab().active_pane;
        if let Some(pane) = ws.panes.get_mut(&active)
            && pane.terminal.check_echo_timeout()
        {
            play_event(&g.audio_config, AudioEvent::PasswordPrompt);
            unsafe {
                raise_if_allowed(
                    &ws.uia_provider,
                    "Text not echoed",
                    "password-prompt",
                    g.config.accessibility.screen_reader_output,
                    "output",
                );
            }
        }
    }

    // (Panes with exited PTY stay around with an exit banner;
    // the user can restart via "restart_shell" or close the tab.)

    // --- Taskbar progress (OSC 9;4) ---
    if let Some(ref tb) = ws.taskbar {
        let active = ws.tab_mgr.active_tab().active_pane;
        if let Some(pane) = ws.panes.get(&active) {
            let progress = pane.terminal.progress;
            if progress != ws.last_progress {
                ws.last_progress = progress;
                update_taskbar_progress(tb, ws.window.hwnd(), progress);
            }
        }
    }

    // --- Cursor blink ---
    let blink_elapsed = ws.blink_timer.elapsed().as_millis();
    if blink_elapsed >= ws.blink_interval_ms {
        ws.cursor_blink_on = !ws.cursor_blink_on;
        ws.blink_timer = Instant::now();
        let active = ws.tab_mgr.active_tab().active_pane;
        if let Some(pane) = ws.panes.get_mut(&active)
            && pane.terminal.grid.cursor.blinking
        {
            pane.terminal.dirty = true;
        }
    }

    // --- Render all visible panes ---
    let active = ws.tab_mgr.active_tab().active_pane;
    let any_dirty = {
        let pane_ids = ws.tab_mgr.active_tab().pane_tree.pane_ids();
        pane_ids
            .iter()
            .any(|id| ws.panes.get(id).is_some_and(|p| p.terminal.dirty))
    };

    if any_dirty {
        // Build tab bar items for the renderer
        let raw_tab_items = ws.tab_mgr.tab_bar_items();
        let tab_items: Vec<TabBarItem> = raw_tab_items
            .iter()
            .map(|(_, title, is_active, has_bell, tab_color)| TabBarItem {
                title,
                is_active: *is_active,
                has_bell: *has_bell,
                tab_color: tab_color.map(|c| (c.r, c.g, c.b)),
            })
            .collect();
        let tab_bar_slice: Option<&[TabBarItem]> = if ws
            .tab_mgr
            .should_show_tab_bar_with_mode(g.config.window.tab_bar)
        {
            Some(&tab_items)
        } else {
            None
        };

        let (win_w, win_h) = ws.window.client_size();
        let wf = win_w as f32;
        let hf = win_h as f32;

        // Update a11y layout metrics for BoundingRectangle calculations
        if let Some(mut state) = ws.a11y_state.try_write() {
            state.cell_width = ws.cell_width as f64;
            state.cell_height = ws.cell_height as f64;
            state.tab_bar_height = ws.renderer.tab_bar_height(ws.tab_mgr.tab_count()) as f64;
            state.window_width = wf as f64;
            state.window_height = hf as f64;
        }

        // Update IME position for the active pane
        if let Some(active_pane) = ws.panes.get(&active) {
            let tbh = ws.renderer.tab_bar_height(ws.tab_mgr.tab_count());
            let ime_x =
                (active_pane.terminal.grid.cursor.col as f32 * ws.cell_width + pad_left) as i32;
            let ime_y = (active_pane.terminal.grid.cursor.row as f32 * ws.cell_height
                + tbh
                + pad_top) as i32;
            ws.window
                .set_ime_position(ime_x, ime_y, ws.cell_height as i32);
        }

        // Materialize history overlay lines so the overlay can borrow them.
        let history_lines: Vec<String> = if ws.history_browser.visible {
            let header = if ws.history_browser.query.is_empty() {
                "> Type to search history...".to_string()
            } else {
                ws.history_browser.query.clone()
            };
            let mut lines = Vec::with_capacity(ws.history_browser.entries().len() + 1);
            lines.push(header);
            lines.extend(ws.history_browser.overlay_lines());
            lines
        } else {
            Vec::new()
        };

        let mut overlays: Vec<OverlayLayer> = Vec::new();
        if ws.settings_ui.visible {
            trace!("Building settings overlay");
        }
        if let Some(ol) = build_settings_overlay(&ws.settings_ui, wf, hf, ws.cell_height) {
            trace!(lines = ol.lines.len(), "Settings overlay built");
            overlays.push(ol);
        }
        if let Some(ol) = build_palette_overlay(&ws.palette, wf, hf, ws.cell_height) {
            overlays.push(ol);
        }
        if let Some(ol) =
            build_history_overlay(&ws.history_browser, &history_lines, wf, hf, ws.cell_height)
        {
            overlays.push(ol);
        }

        // Compute pane layout and build render infos
        let pane_rects = ws.tab_mgr.active_tab().pane_tree.layout();
        let tbh = ws.renderer.tab_bar_height(ws.tab_mgr.tab_count());
        let content_w = wf - pad_left - g.config.window.padding.right as f32;
        let content_h = hf - tbh - pad_top - g.config.window.padding.bottom as f32;

        let pane_render_infos: Vec<wixen_render::PaneRenderInfo<'_>> = pane_rects
            .iter()
            .filter_map(|pr| {
                let pane = ws.panes.get(&pr.pane_id)?;
                let show_scrollbar = match g.config.window.scrollbar {
                    wixen_config::ScrollbarMode::Always => !pane.terminal.scrollback.is_empty(),
                    wixen_config::ScrollbarMode::Auto => pane.terminal.viewport_offset > 0,
                    wixen_config::ScrollbarMode::Never => false,
                };
                Some(wixen_render::PaneRenderInfo {
                    terminal: &pane.terminal,
                    cursor_visible: !pane.terminal.grid.cursor.blinking || ws.cursor_blink_on,
                    is_active: pr.pane_id == active,
                    show_scrollbar,
                    rect: (
                        pad_left + pr.x * content_w,
                        pad_top + pr.y * content_h,
                        pr.width * content_w,
                        pr.height * content_h,
                    ),
                })
            })
            .collect();

        if pane_render_infos.len() > 1 {
            // Multi-pane render
            let border_color = [0.4, 0.4, 0.4, 1.0]; // Gray border
            if let Err(e) =
                ws.renderer
                    .render_panes(&pane_render_infos, tab_bar_slice, &overlays, border_color)
            {
                error!("Multi-pane render error: {}", e);
            }
        } else if let Some(active_pane) = ws.panes.get(&active) {
            // Single pane — use the original render path (with search highlighting)
            let scrollback_len = active_pane.terminal.scrollback.len();
            let viewport_offset = active_pane.terminal.viewport_offset;
            let search_engine = &active_pane.search_engine;
            let hl_closure;
            let highlight: Option<&dyn Fn(usize, usize) -> CellHighlight> =
                if search_engine.is_active() {
                    hl_closure = |viewport_row: usize, col: usize| -> CellHighlight {
                        let abs_row = scrollback_len.saturating_sub(viewport_offset) + viewport_row;
                        match search_engine.cell_match_state(abs_row, col) {
                            wixen_search::CellMatchState::Active => CellHighlight::ActiveMatch,
                            wixen_search::CellMatchState::Highlighted => CellHighlight::Match,
                            wixen_search::CellMatchState::None => CellHighlight::None,
                        }
                    };
                    Some(&hl_closure)
                } else {
                    None
                };
            let show_scrollbar = match g.config.window.scrollbar {
                wixen_config::ScrollbarMode::Always => !active_pane.terminal.scrollback.is_empty(),
                wixen_config::ScrollbarMode::Auto => active_pane.terminal.viewport_offset > 0,
                wixen_config::ScrollbarMode::Never => false,
            };
            let pad = (pad_left, pad_top);
            if let Err(e) = ws.renderer.render(
                &active_pane.terminal,
                !active_pane.terminal.grid.cursor.blinking || ws.cursor_blink_on,
                highlight,
                tab_bar_slice,
                &overlays,
                show_scrollbar,
                pad,
            ) {
                error!("Render error: {}", e);
            }
        }

        // Clear dirty flags on all visible panes
        let pane_ids = ws.tab_mgr.active_tab().pane_tree.pane_ids();
        for id in &pane_ids {
            if let Some(p) = ws.panes.get_mut(id) {
                p.terminal.dirty = false;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Route VT parser actions to the terminal emulator.
fn dispatch_action(terminal: &mut Terminal, action: Action) {
    match action {
        Action::Print(ch) => terminal.print(ch),
        Action::Execute(byte) => terminal.execute(byte),
        Action::CsiDispatch {
            params,
            subparams,
            intermediates,
            action,
        } => terminal.csi_dispatch(&params, &intermediates, action, &subparams),
        Action::EscDispatch {
            intermediates,
            action,
        } => terminal.esc_dispatch(&intermediates, action),
        Action::OscDispatch(params) => terminal.osc_dispatch(&params),
        Action::DcsHook {
            params,
            intermediates,
            action,
        } => terminal.dcs_hook(&params, &intermediates, action),
        Action::DcsPut(byte) => terminal.dcs_put(byte),
        Action::DcsUnhook => terminal.dcs_unhook(),
        Action::ApcDispatch(data) => terminal.apc_dispatch(&data),
    }
}

/// Set the Windows clipboard to the given text.
fn clipboard_set(text: &str) {
    use windows::Win32::Foundation::HANDLE;
    use windows::Win32::System::DataExchange::*;
    use windows::Win32::System::Memory::*;
    use windows::Win32::System::Ole::CF_UNICODETEXT;

    let wide: Vec<u16> = text.encode_utf16().chain(std::iter::once(0)).collect();
    let size = wide.len() * std::mem::size_of::<u16>();

    unsafe {
        if OpenClipboard(None).is_ok() {
            let _ = EmptyClipboard();
            if let Ok(hmem) = GlobalAlloc(GMEM_MOVEABLE, size) {
                let ptr = GlobalLock(hmem);
                if !ptr.is_null() {
                    std::ptr::copy_nonoverlapping(wide.as_ptr(), ptr as *mut u16, wide.len());
                    let _ = GlobalUnlock(hmem);
                    let _ = SetClipboardData(CF_UNICODETEXT.0 as u32, Some(HANDLE(hmem.0)));
                }
            }
            let _ = CloseClipboard();
        }
    }
}

/// Get text from the Windows clipboard.
fn clipboard_get() -> Option<String> {
    use windows::Win32::Foundation::HGLOBAL;
    use windows::Win32::System::DataExchange::*;
    use windows::Win32::System::Memory::*;
    use windows::Win32::System::Ole::CF_UNICODETEXT;

    unsafe {
        if OpenClipboard(None).is_err() {
            return None;
        }
        let result = (|| {
            let handle = GetClipboardData(CF_UNICODETEXT.0 as u32).ok()?;
            let hmem = HGLOBAL(handle.0);
            let ptr = GlobalLock(hmem) as *const u16;
            if ptr.is_null() {
                return None;
            }
            let mut len = 0;
            while *ptr.add(len) != 0 {
                len += 1;
            }
            let slice = std::slice::from_raw_parts(ptr, len);
            let text = String::from_utf16_lossy(slice);
            let _ = GlobalUnlock(hmem);
            Some(text)
        })();
        let _ = CloseClipboard();
        result
    }
}

/// Parse a cursor style string from config into a CursorShape.
fn parse_cursor_style(style: &str) -> CursorShape {
    match style.to_lowercase().as_str() {
        "underline" => CursorShape::Underline,
        "bar" | "ibeam" => CursorShape::Bar,
        _ => CursorShape::Block,
    }
}

/// Set the Win32 window title.
fn set_window_title(hwnd: windows::Win32::Foundation::HWND, title: &str) {
    let wide: Vec<u16> = title.encode_utf16().chain(std::iter::once(0)).collect();
    unsafe {
        let _ = windows::Win32::UI::WindowsAndMessaging::SetWindowTextW(
            hwnd,
            windows::core::PCWSTR(wide.as_ptr()),
        );
    }
}

/// Update the window title with search status.
fn update_search_title(
    hwnd: windows::Win32::Foundation::HWND,
    base_title: &str,
    query: &str,
    engine: &SearchEngine,
) {
    let title = if query.is_empty() {
        format!("{} — Search: _", base_title)
    } else if let Some((current, total)) = engine.match_status() {
        format!("{} — Search: {} ({}/{})", base_title, query, current, total)
    } else {
        format!("{} — Search: {} (no matches)", base_title, query)
    };
    set_window_title(hwnd, &title);
}

/// Convert a UI MouseButton to a core mouse::MouseButton for encoding.
fn ui_to_mouse_button(button: wixen_ui::window::MouseButton) -> mouse::MouseButton {
    match button {
        wixen_ui::window::MouseButton::Left => mouse::MouseButton::Left,
        wixen_ui::window::MouseButton::Right => mouse::MouseButton::Right,
        wixen_ui::window::MouseButton::Middle => mouse::MouseButton::Middle,
    }
}

/// Convert window modifier bits to core MouseModifiers.
fn mods_to_mouse_mods(mods: u8) -> MouseModifiers {
    MouseModifiers {
        shift: mods & 4 != 0,
        alt: mods & 8 != 0,
        ctrl: mods & 16 != 0,
        motion: false,
    }
}

/// Encode and send a mouse event to the PTY.
fn send_mouse_event(
    pane: &mut PaneState,
    button: mouse::MouseButton,
    mods: MouseModifiers,
    col: usize,
    row: usize,
    pressed: bool,
) {
    if pane.terminal.modes.mouse_sgr {
        let seq = mouse::encode_mouse_sgr(button, mods, col, row, pressed);
        let _ = pane.pty.write(seq.as_bytes());
    } else {
        let btn = if !pressed {
            mouse::MouseButton::Release
        } else {
            button
        };
        if let Some(seq) = mouse::encode_mouse_normal(btn, mods, col, row) {
            let _ = pane.pty.write(&seq);
        }
    }
}

/// Open a web URL in the user's default browser via `ShellExecuteW`.
///
/// Defense in depth: only `http`, `https`, and `ftp` URLs are launched. One
/// caller is the Ctrl+click OSC 8 handler, whose URI comes straight from
/// terminal output and is therefore attacker-influenced. A hostile program
/// could otherwise smuggle a `file://`, `ms-settings:`, `javascript:`, or UNC
/// (`\\host\share`) target that `ShellExecuteW` would happily execute. The
/// scheme is filtered at the source in `Terminal::hyperlink_at`; this guard
/// closes the same hole for every other caller. Program-controlled local files
/// must go through [`open_local_file`], never this function.
fn open_url(url: &str) {
    use windows::Win32::UI::Shell::ShellExecuteW;
    use windows::core::PCWSTR;

    if !wixen_core::url::is_safe_url_scheme(url) {
        warn!(url, "Refusing to open URL with unsafe scheme");
        return;
    }

    let wide: Vec<u16> = url.encode_utf16().chain(std::iter::once(0)).collect();
    let verb: Vec<u16> = "open".encode_utf16().chain(std::iter::once(0)).collect();
    unsafe {
        ShellExecuteW(
            None,
            PCWSTR(verb.as_ptr()),
            PCWSTR(wide.as_ptr()),
            PCWSTR::null(),
            PCWSTR::null(),
            windows::Win32::UI::WindowsAndMessaging::SW_SHOWNORMAL,
        );
    }
    info!(url, "Opened URL via ShellExecuteW");
}

/// Open a program-controlled local file (e.g. the user's own config file) via
/// `ShellExecuteW`.
///
/// This deliberately bypasses the web-scheme guard in [`open_url`] because a
/// local path like `C:\Users\...\wixen.toml` has no `http`/`https`/`ftp` scheme
/// and would be rejected there. It must ONLY be called with paths the program
/// itself constructs — never with any string derived from terminal output,
/// OSC sequences, or other attacker input.
fn open_local_file(path: &std::path::Path) {
    use std::os::windows::ffi::OsStrExt;
    use windows::Win32::UI::Shell::ShellExecuteW;
    use windows::core::PCWSTR;

    let wide: Vec<u16> = path
        .as_os_str()
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    let verb: Vec<u16> = "open".encode_utf16().chain(std::iter::once(0)).collect();
    unsafe {
        ShellExecuteW(
            None,
            PCWSTR(verb.as_ptr()),
            PCWSTR(wide.as_ptr()),
            PCWSTR::null(),
            PCWSTR::null(),
            windows::Win32::UI::WindowsAndMessaging::SW_SHOWNORMAL,
        );
    }
    info!(path = %path.display(), "Opened local file via ShellExecuteW");
}

/// Apply a font size change: rebuild the font, recalculate grid, resize all panes.
#[allow(clippy::too_many_arguments)]
fn apply_zoom(
    font_size: f32,
    config: &Config,
    renderer: &mut TerminalRenderer,
    window: &Window,
    cell_width: &mut f32,
    cell_height: &mut f32,
    current_cols: &mut u16,
    current_rows: &mut u16,
    panes: &mut HashMap<PaneId, PaneState>,
) {
    let dpi = window.dpi() as f32;
    if let Err(e) = renderer.rebuild_font(
        &config.font.family,
        font_size,
        dpi,
        config.font.line_height,
        &config.font.fallback_fonts,
        config.font.ligatures,
        &config.font.font_path,
    ) {
        error!("Font rebuild failed during zoom: {e}");
        return;
    }
    let metrics = renderer.font_metrics();
    *cell_width = metrics.cell_width;
    *cell_height = metrics.cell_height;
    let (w, h) = window.client_size();
    let pad_h = config.window.padding.horizontal() as f32;
    let pad_v = config.window.padding.vertical() as f32;
    *current_cols = ((w as f32 - pad_h) / *cell_width).floor().max(1.0) as u16;
    *current_rows = ((h as f32 - pad_v) / *cell_height).floor().max(1.0) as u16;
    for pane in panes.values_mut() {
        pane.terminal
            .resize(*current_cols as usize, *current_rows as usize);
        let _ = pane.pty.resize(*current_cols, *current_rows);
        pane.terminal.dirty = true;
    }
}

/// Handle deferred actions that need access to renderer, config_watcher, etc.
///
/// Returns `true` if the action was handled (including deferred), `false` if unhandled.
#[allow(clippy::too_many_arguments)]
fn handle_deferred_action(
    result: DispatchResult,
    config: &mut Config,
    renderer: &mut TerminalRenderer,
    window: &Window,
    cell_width: &mut f32,
    cell_height: &mut f32,
    current_cols: &mut u16,
    current_rows: &mut u16,
    panes: &mut HashMap<PaneId, PaneState>,
    config_watcher: &mut ConfigWatcher,
    cfg_path: &std::path::Path,
) -> bool {
    match result {
        DispatchResult::Handled => true,
        // Announced at the dispatch call site, which owns the UIA provider.
        DispatchResult::ModeToggled { .. } => true,
        DispatchResult::Unhandled => false,
        DispatchResult::ZoomIn => {
            let new_size = (config.font.size + 2.0).min(72.0);
            apply_zoom(
                new_size,
                config,
                renderer,
                window,
                cell_width,
                cell_height,
                current_cols,
                current_rows,
                panes,
            );
            info!(font_size = new_size, "Zoom in");
            true
        }
        DispatchResult::ZoomOut => {
            let new_size = (config.font.size - 2.0).max(6.0);
            apply_zoom(
                new_size,
                config,
                renderer,
                window,
                cell_width,
                cell_height,
                current_cols,
                current_rows,
                panes,
            );
            info!(font_size = new_size, "Zoom out");
            true
        }
        DispatchResult::ZoomReset => {
            apply_zoom(
                config.font.size,
                config,
                renderer,
                window,
                cell_width,
                cell_height,
                current_cols,
                current_rows,
                panes,
            );
            info!(font_size = config.font.size, "Zoom reset");
            true
        }
        DispatchResult::OpenConfigFile => {
            open_local_file(cfg_path);
            info!(path = %cfg_path.display(), "Opened config file");
            true
        }
        DispatchResult::ReloadConfig => {
            config_watcher.force_reload();
            info!("Config reload forced");
            true
        }
        DispatchResult::ToggleTabBar => {
            use wixen_config::TabBarMode;
            config.window.tab_bar = match config.window.tab_bar {
                TabBarMode::AutoHide => TabBarMode::Always,
                TabBarMode::Always => TabBarMode::Never,
                TabBarMode::Never => TabBarMode::AutoHide,
            };
            info!(mode = ?config.window.tab_bar, "Tab bar mode toggled");
            true
        }
        DispatchResult::ToggleBell => {
            use wixen_config::BellStyle;
            config.terminal.bell = match config.terminal.bell {
                BellStyle::Both => BellStyle::Visual,
                BellStyle::Visual => BellStyle::Audible,
                BellStyle::Audible => BellStyle::None,
                BellStyle::None => BellStyle::Both,
            };
            info!(bell = ?config.terminal.bell, "Bell style toggled");
            true
        }
        DispatchResult::ToggleCursorBlink => {
            config.terminal.cursor_blink = !config.terminal.cursor_blink;
            info!(blink = config.terminal.cursor_blink, "Cursor blink toggled");
            true
        }
        DispatchResult::CycleCursorStyle => {
            config.terminal.cursor_style = match config.terminal.cursor_style.as_str() {
                "block" => "underline".to_string(),
                "underline" => "bar".to_string(),
                _ => "block".to_string(),
            };
            info!(style = %config.terminal.cursor_style, "Cursor style cycled");
            true
        }
        DispatchResult::IncreaseOpacity => {
            config.window.opacity = (config.window.opacity + 0.1).min(1.0);
            wixen_ui::window::apply_window_chrome(window.hwnd(), &config.window);
            info!(opacity = config.window.opacity, "Opacity increased");
            true
        }
        DispatchResult::DecreaseOpacity => {
            config.window.opacity = (config.window.opacity - 0.1).max(0.1);
            wixen_ui::window::apply_window_chrome(window.hwnd(), &config.window);
            info!(opacity = config.window.opacity, "Opacity decreased");
            true
        }
        DispatchResult::ResetOpacity => {
            config.window.opacity = 1.0;
            wixen_ui::window::apply_window_chrome(window.hwnd(), &config.window);
            info!("Opacity reset to 1.0");
            true
        }
        DispatchResult::MinimizeToTray => {
            // Tray icon show/hide will be wired when Shell_NotifyIconW is implemented
            info!("Minimize to tray requested");
            true
        }
    }
}

/// Result of dispatching a keybinding action.
#[derive(Clone, Copy)]
enum DispatchResult {
    /// Action was fully handled.
    Handled,
    /// A pane mode (zoom, broadcast input, read-only) was toggled; the caller
    /// announces the new state via UIA and plays the mode-toggle audio cue.
    ModeToggled {
        mode: ToggleMode,
        active: bool,
    },
    /// Action not recognized.
    Unhandled,
    /// Zoom: rebuild font at new size. `f32` is the delta (+2, -2) or 0 for reset.
    ZoomIn,
    ZoomOut,
    ZoomReset,
    /// Open the config file in an external editor.
    OpenConfigFile,
    /// Force a config reload.
    ReloadConfig,
    /// Cycle the tab bar visibility mode (AutoHide → Always → Never → AutoHide).
    ToggleTabBar,
    /// Cycle bell style (Both → Visual → Audible → None → Both).
    ToggleBell,
    /// Toggle cursor blinking on/off.
    ToggleCursorBlink,
    /// Cycle cursor style (block → underline → bar → block).
    CycleCursorStyle,
    /// Increase window opacity by 10%.
    IncreaseOpacity,
    /// Decrease window opacity by 10%.
    DecreaseOpacity,
    /// Reset window opacity to 1.0.
    ResetOpacity,
    /// Minimize to system tray.
    MinimizeToTray,
}

/// Dispatch a keybinding action by its string identifier.
#[allow(clippy::too_many_arguments)]
fn dispatch_keybinding_action(
    action: &str,
    tab_mgr: &mut TabManager,
    panes: &mut HashMap<PaneId, PaneState>,
    config: &Config,
    close_window: &mut bool,
    cols: u16,
    rows: u16,
    window: &mut Window,
    base_title: &str,
    plugin_lua: &mlua::Lua,
    plugin_registry: &PluginRegistry,
) -> DispatchResult {
    match action {
        "new_tab" => {
            let (tab_id, pane_id) = tab_mgr.add_tab("Terminal");
            panes.insert(pane_id, spawn_pane(cols, rows, config, None));
            if let Some(p) = panes.get_mut(&pane_id) {
                p.terminal.dirty = true;
            }
            info!(tabs = tab_mgr.tab_count(), "New tab opened");
            dispatch_plugin_event(
                plugin_lua,
                plugin_registry,
                &PluginEvent::TabCreated { tab_id: tab_id.0 },
            );
            DispatchResult::Handled
        }
        "close_tab" => {
            let tab_id = tab_mgr.active_tab_id();
            let pane_id = tab_mgr.active_tab().active_pane;
            if tab_mgr.close_tab(tab_id) {
                panes.remove(&pane_id);
                let new_active = tab_mgr.active_tab().active_pane;
                if let Some(p) = panes.get_mut(&new_active) {
                    p.terminal.dirty = true;
                }
                info!(tabs = tab_mgr.tab_count(), "Tab closed");
                dispatch_plugin_event(
                    plugin_lua,
                    plugin_registry,
                    &PluginEvent::TabClosed { tab_id: tab_id.0 },
                );
            } else {
                info!("Last tab closed — exiting");
                *close_window = true;
            }
            DispatchResult::Handled
        }
        "next_tab" => {
            tab_mgr.next_tab();
            let new_active = tab_mgr.active_tab().active_pane;
            if let Some(p) = panes.get_mut(&new_active) {
                p.terminal.dirty = true;
            }
            DispatchResult::Handled
        }
        "prev_tab" => {
            tab_mgr.prev_tab();
            let new_active = tab_mgr.active_tab().active_pane;
            if let Some(p) = panes.get_mut(&new_active) {
                p.terminal.dirty = true;
            }
            DispatchResult::Handled
        }
        "copy" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active) {
                if let Some(text) = pane.terminal.selected_text() {
                    clipboard_set(&text);
                }
                pane.terminal.clear_selection();
            }
            DispatchResult::Handled
        }
        "paste" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active)
                && let Some(text) = clipboard_get()
            {
                if pane.terminal.modes.bracketed_paste {
                    let _ = pane.pty.write(b"\x1b[200~");
                    let _ = pane.pty.write(text.as_bytes());
                    let _ = pane.pty.write(b"\x1b[201~");
                } else {
                    let _ = pane.pty.write(text.as_bytes());
                }
            }
            DispatchResult::Handled
        }
        "copy_last_command" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get(&active)
                && let Some(text) = pane.terminal.last_command_text()
            {
                clipboard_set(&text);
                info!("Copied last command to clipboard");
            }
            DispatchResult::Handled
        }
        "copy_last_output" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get(&active)
                && let Some(text) = pane.terminal.last_command_output()
            {
                clipboard_set(&text);
                info!("Copied last output to clipboard");
            }
            DispatchResult::Handled
        }
        "find" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active) {
                pane.search_mode = !pane.search_mode;
                if !pane.search_mode {
                    pane.search_engine.clear();
                    pane.search_buffer.clear();
                    set_window_title(window.hwnd(), base_title);
                    pane.terminal.dirty = true;
                } else {
                    set_window_title(window.hwnd(), &format!("{} — Search: _", base_title));
                }
            }
            DispatchResult::Handled
        }
        "toggle_fullscreen" => {
            window.toggle_fullscreen();
            DispatchResult::Handled
        }
        "new_window" => {
            if let Ok(exe) = std::env::current_exe() {
                let _ = std::process::Command::new(exe).spawn();
                info!("Spawned new window process");
            }
            DispatchResult::Handled
        }
        "split_horizontal" => {
            if let Some(new_pane_id) =
                tab_mgr.split_active_pane(wixen_ui::panes::SplitDirection::Horizontal)
            {
                panes.insert(new_pane_id, spawn_pane(cols, rows, config, None));
                if let Some(p) = panes.get_mut(&new_pane_id) {
                    p.terminal.dirty = true;
                }
                info!("Split horizontal — new pane");
            }
            DispatchResult::Handled
        }
        "split_vertical" => {
            if let Some(new_pane_id) =
                tab_mgr.split_active_pane(wixen_ui::panes::SplitDirection::Vertical)
            {
                panes.insert(new_pane_id, spawn_pane(cols, rows, config, None));
                if let Some(p) = panes.get_mut(&new_pane_id) {
                    p.terminal.dirty = true;
                }
                info!("Split vertical — new pane");
            }
            DispatchResult::Handled
        }
        "close_pane" => {
            let old_pane = tab_mgr.active_tab().active_pane;
            if let Some(new_active) = tab_mgr.close_active_pane() {
                panes.remove(&old_pane);
                if let Some(p) = panes.get_mut(&new_active) {
                    p.terminal.dirty = true;
                }
                info!("Pane closed");
            } else {
                // Last pane in tab — close the entire tab
                let tab_id = tab_mgr.active_tab_id();
                if tab_mgr.close_tab(tab_id) {
                    panes.remove(&old_pane);
                    let new_active = tab_mgr.active_tab().active_pane;
                    if let Some(p) = panes.get_mut(&new_active) {
                        p.terminal.dirty = true;
                    }
                    info!("Last pane → tab closed");
                    dispatch_plugin_event(
                        plugin_lua,
                        plugin_registry,
                        &PluginEvent::TabClosed { tab_id: tab_id.0 },
                    );
                } else {
                    *close_window = true;
                }
            }
            DispatchResult::Handled
        }
        "focus_next_pane" => {
            tab_mgr.focus_next_pane();
            let new_active = tab_mgr.active_tab().active_pane;
            if let Some(p) = panes.get_mut(&new_active) {
                p.terminal.dirty = true;
            }
            DispatchResult::Handled
        }
        "focus_prev_pane" => {
            tab_mgr.focus_prev_pane();
            let new_active = tab_mgr.active_tab().active_pane;
            if let Some(p) = panes.get_mut(&new_active) {
                p.terminal.dirty = true;
            }
            DispatchResult::Handled
        }
        "zoom_in" => DispatchResult::ZoomIn,
        "zoom_out" => DispatchResult::ZoomOut,
        "zoom_reset" => DispatchResult::ZoomReset,
        "open_config_file" => DispatchResult::OpenConfigFile,
        "open_help" => {
            // Open the user guide HTML in the default browser
            let local_guide = std::env::current_exe()
                .ok()
                .and_then(|p| p.parent().map(|d| d.to_path_buf()))
                .and_then(|d| wixen_ui::help::resolve_help_file(&d));
            if let Some(path) = local_guide {
                wixen_ui::help::open_help(&path);
            } else {
                // Local HTML not found: open the online version
                open_url("https://github.com/PratikP1/Wixen-Terminal/blob/main/docs/user-guide.md");
                info!("Opened online user guide (local HTML not found)");
            }
            DispatchResult::Handled
        }
        "reload_config" => DispatchResult::ReloadConfig,
        "toggle_tab_bar" => DispatchResult::ToggleTabBar,
        "toggle_always_on_top" => {
            window.toggle_always_on_top();
            DispatchResult::Handled
        }
        "minimize_to_tray" => DispatchResult::MinimizeToTray,
        "toggle_broadcast_input" => {
            // This is handled at the call site — we just return Handled.
            // The actual toggle happens in the event loop.
            DispatchResult::Handled
        }
        "focus_last_tab" => {
            tab_mgr.focus_last_tab();
            let active = tab_mgr.active_tab().active_pane;
            if let Some(p) = panes.get_mut(&active) {
                p.terminal.dirty = true;
            }
            DispatchResult::Handled
        }
        "clear_terminal" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active) {
                let _ = pane.pty.write(b"\x0c");
            }
            DispatchResult::Handled
        }
        "focus_pane_left" => {
            let tab = tab_mgr.active_tab();
            if let Some(target) = tab.pane_tree.find_adjacent(
                tab.active_pane,
                wixen_ui::panes::SplitDirection::Horizontal,
                false,
            ) {
                tab_mgr.set_active_pane(target);
                if let Some(p) = panes.get_mut(&target) {
                    p.terminal.dirty = true;
                }
            }
            DispatchResult::Handled
        }
        "focus_pane_right" => {
            let tab = tab_mgr.active_tab();
            if let Some(target) = tab.pane_tree.find_adjacent(
                tab.active_pane,
                wixen_ui::panes::SplitDirection::Horizontal,
                true,
            ) {
                tab_mgr.set_active_pane(target);
                if let Some(p) = panes.get_mut(&target) {
                    p.terminal.dirty = true;
                }
            }
            DispatchResult::Handled
        }
        "focus_pane_up" => {
            let tab = tab_mgr.active_tab();
            if let Some(target) = tab.pane_tree.find_adjacent(
                tab.active_pane,
                wixen_ui::panes::SplitDirection::Vertical,
                false,
            ) {
                tab_mgr.set_active_pane(target);
                if let Some(p) = panes.get_mut(&target) {
                    p.terminal.dirty = true;
                }
            }
            DispatchResult::Handled
        }
        "focus_pane_down" => {
            let tab = tab_mgr.active_tab();
            if let Some(target) = tab.pane_tree.find_adjacent(
                tab.active_pane,
                wixen_ui::panes::SplitDirection::Vertical,
                true,
            ) {
                tab_mgr.set_active_pane(target);
                if let Some(p) = panes.get_mut(&target) {
                    p.terminal.dirty = true;
                }
            }
            DispatchResult::Handled
        }
        "resize_pane_grow" => {
            tab_mgr.resize_active_pane(0.05);
            let active = tab_mgr.active_tab().active_pane;
            if let Some(p) = panes.get_mut(&active) {
                p.terminal.dirty = true;
            }
            DispatchResult::Handled
        }
        "resize_pane_shrink" => {
            tab_mgr.resize_active_pane(-0.05);
            let active = tab_mgr.active_tab().active_pane;
            if let Some(p) = panes.get_mut(&active) {
                p.terminal.dirty = true;
            }
            DispatchResult::Handled
        }
        "reset_pane_sizes" => {
            // Reset is done by setting ratio to 0.5 — but our resize_pane adjusts
            // by delta, not sets absolute. For now, we can adjust toward 0.5:
            // We'll just mark dirty — full reset would need a tree walk.
            let active = tab_mgr.active_tab().active_pane;
            if let Some(p) = panes.get_mut(&active) {
                p.terminal.dirty = true;
            }
            DispatchResult::Handled
        }
        "zoom_pane" | "toggle_zoom" => {
            let tab = tab_mgr.active_tab_mut();
            let zoomed = tab.pane_tree.toggle_zoom(tab.active_pane);
            let active = tab.active_pane;
            if let Some(p) = panes.get_mut(&active) {
                p.terminal.dirty = true;
            }
            info!(zoomed, "Pane zoom toggled");
            DispatchResult::ModeToggled {
                mode: ToggleMode::Zoom,
                active: zoomed,
            }
        }
        "toggle_read_only" => {
            let tab = tab_mgr.active_tab_mut();
            let read_only = tab.pane_tree.toggle_read_only(tab.active_pane);
            info!(read_only, "Read-only mode toggled");
            DispatchResult::ModeToggled {
                mode: ToggleMode::ReadOnly,
                active: read_only,
            }
        }
        "copy_cwd" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get(&active)
                && let Some(cwd) = pane.terminal.shell_integ.cwd()
            {
                clipboard_set(cwd);
                info!(cwd, "Copied CWD to clipboard");
            }
            DispatchResult::Handled
        }
        "open_cwd_in_explorer" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get(&active)
                && let Some(cwd) = pane.terminal.shell_integ.cwd()
            {
                let _ = std::process::Command::new("explorer.exe").arg(cwd).spawn();
                info!(cwd, "Opened CWD in Explorer");
            }
            DispatchResult::Handled
        }
        "toggle_bell" => DispatchResult::ToggleBell,
        "toggle_cursor_blink" => DispatchResult::ToggleCursorBlink,
        "cycle_cursor_style" => DispatchResult::CycleCursorStyle,
        "increase_opacity" => DispatchResult::IncreaseOpacity,
        "decrease_opacity" => DispatchResult::DecreaseOpacity,
        "reset_opacity" => DispatchResult::ResetOpacity,
        "toggle_word_wrap" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active) {
                pane.terminal.modes.auto_wrap = !pane.terminal.modes.auto_wrap;
                pane.terminal.dirty = true;
                info!(wrap = pane.terminal.modes.auto_wrap, "Word wrap toggled");
            }
            DispatchResult::Handled
        }
        "close_all_tabs" => {
            *close_window = true;
            DispatchResult::Handled
        }
        "export_buffer_text" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get(&active) {
                let text = pane.terminal.export_buffer_text();
                clipboard_set(&text);
                info!(len = text.len(), "Exported buffer text to clipboard");
            }
            DispatchResult::Handled
        }
        "scroll_to_selection" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active) {
                pane.terminal.scroll_to_selection();
            }
            DispatchResult::Handled
        }
        "scroll_to_cursor" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active) {
                pane.terminal.scroll_to_cursor();
            }
            DispatchResult::Handled
        }
        "find_next" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active) {
                pane.search_engine
                    .next_match(wixen_search::SearchDirection::Forward);
                pane.terminal.dirty = true;
            }
            DispatchResult::Handled
        }
        "find_previous" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active) {
                pane.search_engine
                    .next_match(wixen_search::SearchDirection::Backward);
                pane.terminal.dirty = true;
            }
            DispatchResult::Handled
        }
        "scroll_up_page" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active) {
                let page = pane.terminal.grid.rows as i32;
                pane.terminal.scroll_viewport(page);
            }
            DispatchResult::Handled
        }
        "scroll_down_page" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active) {
                let page = -(pane.terminal.grid.rows as i32);
                pane.terminal.scroll_viewport(page);
            }
            DispatchResult::Handled
        }
        "scroll_to_top" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active) {
                let total = pane.terminal.scrollback.len() as i32;
                pane.terminal.scroll_viewport(total);
            }
            DispatchResult::Handled
        }
        "scroll_to_bottom" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active) {
                pane.terminal.viewport_offset = 0;
                pane.terminal.dirty = true;
            }
            DispatchResult::Handled
        }
        "jump_to_previous_prompt" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active) {
                pane.terminal.jump_to_previous_prompt();
            }
            DispatchResult::Handled
        }
        "jump_to_next_prompt" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active) {
                pane.terminal.jump_to_next_prompt();
            }
            DispatchResult::Handled
        }
        "select_all" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active) {
                pane.terminal.select_all();
            }
            DispatchResult::Handled
        }
        "clear_scrollback" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active) {
                pane.terminal.clear_scrollback();
            }
            DispatchResult::Handled
        }
        "reset_terminal" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active) {
                pane.terminal.reset();
            }
            DispatchResult::Handled
        }
        "duplicate_tab" => {
            let active = tab_mgr.active_tab().active_pane;
            let cwd = panes
                .get(&active)
                .and_then(|p| p.terminal.shell_integ.cwd().map(|s| s.to_string()));
            let (tab_id, pane_id) = tab_mgr.add_tab("Terminal");
            let mut new_pane = spawn_pane(cols, rows, config, None);
            // If the current pane has a CWD, send a `cd` to the new shell
            if let Some(dir) = cwd {
                let cd_cmd = format!("cd {}\r", dir.replace('/', "\\"));
                let _ = new_pane.pty.write(cd_cmd.as_bytes());
            }
            panes.insert(pane_id, new_pane);
            info!(tabs = tab_mgr.tab_count(), "Tab duplicated");
            dispatch_plugin_event(
                plugin_lua,
                plugin_registry,
                &PluginEvent::TabCreated { tab_id: tab_id.0 },
            );
            DispatchResult::Handled
        }
        "move_tab_left" => {
            let idx = tab_mgr.active_index();
            if idx > 0 {
                tab_mgr.move_tab(idx, idx - 1);
                let active = tab_mgr.active_tab().active_pane;
                if let Some(p) = panes.get_mut(&active) {
                    p.terminal.dirty = true;
                }
            }
            DispatchResult::Handled
        }
        "move_tab_right" => {
            let idx = tab_mgr.active_index();
            if idx + 1 < tab_mgr.tab_count() {
                tab_mgr.move_tab(idx, idx + 1);
                let active = tab_mgr.active_tab().active_pane;
                if let Some(p) = panes.get_mut(&active) {
                    p.terminal.dirty = true;
                }
            }
            DispatchResult::Handled
        }
        "close_other_tabs" => {
            let keep_id = tab_mgr.active_tab_id();
            let removed = tab_mgr.close_other_tabs(keep_id);
            for pane_id in removed {
                panes.remove(&pane_id);
            }
            if let Some(p) = panes.get_mut(&tab_mgr.active_tab().active_pane) {
                p.terminal.dirty = true;
            }
            DispatchResult::Handled
        }
        "close_tabs_to_right" => {
            let removed = tab_mgr.close_tabs_to_right();
            for pane_id in removed {
                panes.remove(&pane_id);
            }
            if let Some(p) = panes.get_mut(&tab_mgr.active_tab().active_pane) {
                p.terminal.dirty = true;
            }
            DispatchResult::Handled
        }
        "close_tabs_to_left" => {
            let removed = tab_mgr.close_tabs_to_left();
            for pane_id in removed {
                panes.remove(&pane_id);
            }
            if let Some(p) = panes.get_mut(&tab_mgr.active_tab().active_pane) {
                p.terminal.dirty = true;
            }
            DispatchResult::Handled
        }
        // command_palette and settings are handled inline (need local state access)
        "command_palette" | "settings" | "open_settings" => DispatchResult::Handled,
        "rerun_last_command" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get_mut(&active)
                && let Some(cmd) = pane.terminal.last_command_text()
            {
                let _ = pane.pty.write(cmd.trim().as_bytes());
                let _ = pane.pty.write(b"\r");
                info!(cmd = %cmd.trim(), "Rerunning last command");
            }
            DispatchResult::Handled
        }
        "restart_shell" => {
            let active = tab_mgr.active_tab().active_pane;
            if let Some(pane) = panes.get(&active)
                && pane.status != PaneStatus::Running
            {
                // Replace the exited pane with a fresh one
                let new_pane = spawn_pane(cols, rows, config, None);
                panes.insert(active, new_pane);
                // Clear the exit status indicator on the tab
                let tab_id = tab_mgr.active_tab_id();
                tab_mgr.clear_exit_status(tab_id);
                info!("Shell restarted in active pane");
            }
            DispatchResult::Handled
        }
        "send_text" => {
            // send_text requires the keybinding's `args` field.
            // This is handled at the call site — see the keybinding dispatch loop.
            // If we get here without args, it's a no-op.
            DispatchResult::Handled
        }
        _ if action.starts_with("select_tab_") && !action.starts_with("select_tab_profile") => {
            if let Some(n_str) = action.strip_prefix("select_tab_")
                && let Ok(n) = n_str.parse::<usize>()
                && (1..=9).contains(&n)
            {
                // select_tab_9 means "last tab", others are 0-indexed
                let idx = if n == 9 {
                    tab_mgr.tab_count().saturating_sub(1)
                } else {
                    n - 1
                };
                tab_mgr.select_tab_by_index(idx);
                let active = tab_mgr.active_tab().active_pane;
                if let Some(p) = panes.get_mut(&active) {
                    p.terminal.dirty = true;
                }
            }
            DispatchResult::Handled
        }
        _ if action.starts_with("new_tab_profile_") => {
            if let Some(idx_str) = action.strip_prefix("new_tab_profile_")
                && let Ok(idx) = idx_str.parse::<usize>()
                && let Some(profile) = config.profile_at(idx)
            {
                let (tab_id, pane_id) = tab_mgr.add_tab(&profile.name);
                panes.insert(pane_id, spawn_pane(cols, rows, config, Some(&profile)));
                if let Some(p) = panes.get_mut(&pane_id) {
                    p.terminal.dirty = true;
                }
                info!(profile = %profile.name, "New tab opened with profile");
                dispatch_plugin_event(
                    plugin_lua,
                    plugin_registry,
                    &PluginEvent::TabCreated { tab_id: tab_id.0 },
                );
            }
            DispatchResult::Handled
        }
        _ if action.starts_with("ssh_") => {
            if let Some(target) = wixen_config::ssh::resolve_ssh_action(action, &config.ssh) {
                let (program, args) = target.to_command();
                let label = if target.name.is_empty() {
                    target.host.clone()
                } else {
                    target.name.clone()
                };
                let (tab_id, pane_id) = tab_mgr.add_tab(&label);
                panes.insert(
                    pane_id,
                    spawn_pane_with_command(cols, rows, config, &program, &args),
                );
                if let Some(p) = panes.get_mut(&pane_id) {
                    p.terminal.dirty = true;
                }
                info!(ssh_target = %label, "New SSH tab opened");
                dispatch_plugin_event(
                    plugin_lua,
                    plugin_registry,
                    &PluginEvent::TabCreated { tab_id: tab_id.0 },
                );
            }
            DispatchResult::Handled
        }
        "clear_tab_color" => {
            let tab_id = tab_mgr.active_tab_id();
            tab_mgr.clear_tab_color(tab_id);
            info!("Tab color cleared");
            DispatchResult::Handled
        }
        _ if action.starts_with("set_tab_color_") => {
            if let Some(idx_str) = action.strip_prefix("set_tab_color_")
                && let Ok(idx) = idx_str.parse::<usize>()
                && let Some((name, color)) = wixen_ui::tabs::tab_color_presets().get(idx).copied()
            {
                let tab_id = tab_mgr.active_tab_id();
                tab_mgr.set_tab_color(tab_id, color);
                info!(color = name, "Tab color set");
            }
            DispatchResult::Handled
        }
        "install_context_menu" => {
            let exe = std::env::current_exe().unwrap_or_default();
            let cfg = wixen_ui::explorer_menu::ExplorerMenuConfig {
                enabled: true,
                ..Default::default()
            };
            let entries = wixen_ui::explorer_menu::registry_commands(&exe.to_string_lossy(), &cfg);
            match wixen_ui::explorer_menu::write_registry_entries(&entries) {
                Ok(()) => {
                    info!(count = entries.len(), "Explorer context menu installed");
                }
                Err(e) => {
                    error!(error = %e, "Failed to write context menu registry entries");
                }
            }
            DispatchResult::Handled
        }
        "uninstall_context_menu" => {
            match wixen_ui::explorer_menu::delete_registry_entries() {
                Ok(()) => {
                    info!("Explorer context menu uninstalled");
                }
                Err(e) => {
                    error!(error = %e, "Failed to delete context menu registry entries");
                }
            }
            DispatchResult::Handled
        }
        _ => {
            trace!(action, "Unhandled keybinding action");
            DispatchResult::Unhandled
        }
    }
}

/// A resolved keybinding action with optional args.
struct ResolvedAction {
    action: String,
    args: Option<String>,
}

/// Build a chord→action lookup table from config keybindings.
fn build_keybinding_map(config: &Config) -> HashMap<String, ResolvedAction> {
    config
        .keybindings
        .bindings
        .iter()
        .map(|kb| {
            (
                kb.chord.clone(),
                ResolvedAction {
                    action: kb.action.clone(),
                    args: kb.args.clone(),
                },
            )
        })
        .collect()
}

/// Unescape a keybinding args string: `\n` → LF, `\r` → CR, `\t` → tab,
/// `\x1b` → ESC, `\\` → backslash.
fn unescape_send_text(s: &str) -> Vec<u8> {
    let mut out = Vec::with_capacity(s.len());
    let mut chars = s.chars();
    while let Some(c) = chars.next() {
        if c == '\\' {
            match chars.next() {
                Some('n') => out.push(b'\n'),
                Some('r') => out.push(b'\r'),
                Some('t') => out.push(b'\t'),
                Some('\\') => out.push(b'\\'),
                Some('x') => {
                    // Parse \xNN hex escape
                    let h: String = chars.by_ref().take(2).collect();
                    if let Ok(byte) = u8::from_str_radix(&h, 16) {
                        out.push(byte);
                    }
                }
                Some(other) => {
                    out.push(b'\\');
                    let mut buf = [0u8; 4];
                    out.extend_from_slice(other.encode_utf8(&mut buf).as_bytes());
                }
                None => out.push(b'\\'),
            }
        } else {
            let mut buf = [0u8; 4];
            out.extend_from_slice(c.encode_utf8(&mut buf).as_bytes());
        }
    }
    out
}

/// Execute a macro's steps against a pane's PTY.
///
/// `SendText` writes the literal text, `RunCommand` appends a carriage return,
/// `SendKeys` encodes a key chord (e.g. `"Enter"`, `"Ctrl+C"`) into a terminal
/// sequence, and `Wait` sleeps briefly. Long waits block the single-threaded
/// event loop, so macros should keep them short.
fn run_macro(store: &wixen_config::MacroStore, name: &str, pane: &mut PaneState) {
    use wixen_config::MacroStep;
    let Some(m) = store.get(name) else {
        warn!(macro_name = name, "run_macro: unknown macro");
        return;
    };
    let app_cursor = pane.terminal.modes.cursor_keys_application;
    let flags = pane.terminal.kitty_keyboard_flags();
    for step in &m.steps {
        match step {
            MacroStep::SendText(text) => {
                let _ = pane.pty.write(text.as_bytes());
            }
            MacroStep::RunCommand(cmd) => {
                let _ = pane.pty.write(cmd.as_bytes());
                let _ = pane.pty.write(b"\r");
            }
            MacroStep::SendKeys(keys) => {
                let (ctrl, shift, alt, _win, key) = wixen_config::parse_chord(keys);
                if let Some(vk) = wixen_config::key_name_to_vk(&key) {
                    if let Some(seq) = wixen_core::keyboard::encode_key_with_kitty(
                        vk, shift, ctrl, alt, app_cursor, flags,
                    ) {
                        let _ = pane.pty.write(&seq);
                    } else if key.chars().count() == 1 {
                        // Plain printable key with no encodable sequence — send literally.
                        let _ = pane.pty.write(key.as_bytes());
                    }
                } else {
                    warn!(macro_name = name, keys = %keys, "run_macro: unrecognized key");
                }
            }
            MacroStep::Wait(dur) => {
                std::thread::sleep(*dur);
            }
        }
    }
    info!(macro_name = name, steps = m.steps.len(), "Macro executed");
}

/// Build profile entries for the command palette from config.
///
/// Returns `(name, optional_shortcut)` tuples for `CommandPalette::set_profiles()`.
fn build_profile_entries(config: &Config) -> Vec<(String, Option<String>)> {
    config
        .resolved_profiles()
        .iter()
        .enumerate()
        .map(|(i, p)| {
            let shortcut = if i < 9 {
                Some(format!("Ctrl+Shift+{}", i + 1))
            } else {
                None
            };
            (p.name.clone(), shortcut)
        })
        .collect()
}

/// Apply renderer visual effects (background blur, custom post-process shader).
///
/// Called at startup and on config hot-reload.
fn apply_effects(renderer: &mut TerminalRenderer, effects: &wixen_config::EffectsConfig) {
    renderer.set_blur_radius(effects.blur_radius);
    if let Some(path) = effects.shader_path.as_deref()
        && let Err(e) = renderer.load_custom_shader(path)
    {
        warn!(?e, "custom shader failed to load");
    }
}

/// Build `(name, description)` pairs for the command palette's macro entries.
fn macro_palette_entries(store: &wixen_config::MacroStore) -> Vec<(String, String)> {
    store
        .list()
        .iter()
        .map(|m| (m.name.clone(), m.description.clone()))
        .collect()
}

/// Run every plugin subscribed to `event`, logging handler errors.
///
/// The main loop is single-threaded, so a non-`Send` Lua VM is held directly.
fn dispatch_plugin_event(lua: &mlua::Lua, registry: &PluginRegistry, event: &PluginEvent) {
    for plugin in registry.plugins_for_event(wixen_config::plugin::event_name(event)) {
        if let Err(e) = wixen_config::plugin::execute_plugin_event(lua, &plugin.entry_point, event)
        {
            warn!(plugin = %plugin.name, error = %e, "Plugin event handler failed");
        }
    }
}

/// Build a ColorScheme from config, resolving a built-in theme if specified.
/// Individual color fields in the config override the theme's colors.
fn build_color_scheme(colors: &wixen_config::ColorConfig) -> ColorScheme {
    use wixen_core::themes::{BuiltinTheme, ThemeColors, builtin_scheme};

    if let Some(ref theme_name) = colors.theme {
        if let Some(theme) = BuiltinTheme::from_name(theme_name) {
            let tc = builtin_scheme(theme);
            // Start from theme colors, then apply any explicit overrides from config.
            // An empty string means "use theme default" since that's what serde default gives.
            let fg = if colors.foreground.is_empty() || colors.foreground == "#d9d9d9" {
                ThemeColors::to_hex(tc.fg)
            } else {
                colors.foreground.clone()
            };
            let bg = if colors.background.is_empty() || colors.background == "#0d0d14" {
                ThemeColors::to_hex(tc.bg)
            } else {
                colors.background.clone()
            };
            let cursor = if colors.cursor.is_empty() || colors.cursor == "#cccccc" {
                ThemeColors::to_hex(tc.cursor)
            } else {
                colors.cursor.clone()
            };
            let selection = if colors.selection_bg.is_empty() || colors.selection_bg == "#264f78" {
                ThemeColors::to_hex(tc.selection_bg)
            } else {
                colors.selection_bg.clone()
            };
            let palette: Vec<String> = if colors.palette.is_empty() {
                tc.palette.iter().map(|c| ThemeColors::to_hex(*c)).collect()
            } else {
                colors.palette.clone()
            };
            return ColorScheme::from_hex(&fg, &bg, &cursor, &selection, &palette);
        }
        warn!(theme = %theme_name, "Unknown theme name, using individual color settings");
    }

    ColorScheme::from_hex(
        &colors.foreground,
        &colors.background,
        &colors.cursor,
        &colors.selection_bg,
        &colors.palette,
    )
}

/// Fire bell notification (audible, visual, or both).
fn fire_bell(hwnd: windows::Win32::Foundation::HWND, style: BellStyle) {
    use windows::Win32::System::Diagnostics::Debug::MessageBeep;
    use windows::Win32::UI::WindowsAndMessaging::*;

    let audible = matches!(style, BellStyle::Audible | BellStyle::Both);
    let visual = matches!(style, BellStyle::Visual | BellStyle::Both);

    if audible {
        unsafe {
            let _ = MessageBeep(MB_OK);
        }
    }

    if visual {
        unsafe {
            let info = FLASHWINFO {
                cbSize: std::mem::size_of::<FLASHWINFO>() as u32,
                hwnd,
                dwFlags: FLASHW_ALL,
                uCount: 1,
                dwTimeout: 200,
            };
            let _ = FlashWindowEx(&info);
        }
    }
}

/// Update the Windows taskbar progress indicator via ITaskbarList3.
fn update_taskbar_progress(
    taskbar: &windows::Win32::UI::Shell::ITaskbarList3,
    hwnd: windows::Win32::Foundation::HWND,
    state: ProgressState,
) {
    use windows::Win32::UI::Shell::{
        TBPF_ERROR, TBPF_INDETERMINATE, TBPF_NOPROGRESS, TBPF_NORMAL, TBPF_PAUSED,
    };

    unsafe {
        match state {
            ProgressState::Hidden => {
                let _ = taskbar.SetProgressState(hwnd, TBPF_NOPROGRESS);
            }
            ProgressState::Normal(v) => {
                let _ = taskbar.SetProgressState(hwnd, TBPF_NORMAL);
                let _ = taskbar.SetProgressValue(hwnd, v as u64, 100);
            }
            ProgressState::Error(v) => {
                let _ = taskbar.SetProgressState(hwnd, TBPF_ERROR);
                let _ = taskbar.SetProgressValue(hwnd, v as u64, 100);
            }
            ProgressState::Indeterminate => {
                let _ = taskbar.SetProgressState(hwnd, TBPF_INDETERMINATE);
            }
            ProgressState::Paused(v) => {
                let _ = taskbar.SetProgressState(hwnd, TBPF_PAUSED);
                let _ = taskbar.SetProgressValue(hwnd, v as u64, 100);
            }
        }
    }
}

/// Update the a11y state with the current modal-overlay snapshots.
///
/// The command palette and the history browser are mutually exclusive modals,
/// but both are refreshed together under a single write lock so the UIA state
/// never exposes a stale snapshot for either overlay.
fn update_overlay_a11y(
    palette: &CommandPalette,
    history_browser: &HistoryBrowser,
    state: &Arc<RwLock<TerminalA11yState>>,
) {
    let palette_snapshot = if palette.visible {
        let entries: Vec<PaletteEntrySnapshot> = palette
            .entries()
            .iter()
            .enumerate()
            .map(|(i, e)| PaletteEntrySnapshot {
                label: e.label.clone(),
                shortcut: e.shortcut.clone(),
                category: e.category.clone(),
                is_selected: i == palette.selected,
                index: i,
            })
            .collect();
        Some(PaletteSnapshot {
            visible: true,
            query: palette.query.clone(),
            selected_index: palette.selected,
            entries,
        })
    } else {
        None
    };
    let history_snapshot = if history_browser.visible {
        let entries: Vec<HistoryEntrySnapshot> = history_browser
            .entries()
            .iter()
            .enumerate()
            .map(|(i, e)| HistoryEntrySnapshot {
                label: wixen_ui::history::history_entry_label(e),
                index: i,
            })
            .collect();
        Some(HistorySnapshot {
            visible: true,
            entries,
            selected_index: history_browser.selected,
        })
    } else {
        None
    };
    if let Some(mut guard) = state.try_write() {
        guard.palette_snapshot = palette_snapshot;
        guard.history_snapshot = history_snapshot;
    }
}

/// Build an overlay for the command palette (centered, 60% width, up to 12 entries).
fn build_palette_overlay<'a>(
    palette: &'a CommandPalette,
    window_width: f32,
    window_height: f32,
    line_height: f32,
) -> Option<OverlayLayer<'a>> {
    if !palette.visible {
        return None;
    }

    let overlay_width = window_width * 0.6;
    let x = (window_width - overlay_width) / 2.0;
    let y = window_height * 0.15; // 15% from top

    // Palette background — dark semi-transparent
    let bg = [0.1, 0.1, 0.15, 0.95];
    let fg_normal = [0.85, 0.85, 0.85, 1.0];
    let fg_dim = [0.5, 0.5, 0.55, 1.0];
    let selected_bg = [0.25, 0.25, 0.35, 1.0];

    let mut lines;
    let overlay_height;

    if palette.mode == wixen_ui::PaletteMode::Input {
        // Input mode: show prompt + text input line
        lines = Vec::with_capacity(2);
        lines.push(OverlayLine {
            text: &palette.input_prompt,
            fg: fg_dim,
            bg: None,
            bold: false,
        });
        lines.push(OverlayLine {
            text: if palette.query.is_empty() {
                "_"
            } else {
                &palette.query
            },
            fg: fg_normal,
            bg: Some(selected_bg),
            bold: true,
        });
        overlay_height = 4.0 * line_height;
    } else {
        // Command mode: search + entry list
        let max_visible = 12usize;
        let entries = palette.entries();
        let visible_count = entries.len().min(max_visible);
        let total_lines = 1 + visible_count;
        overlay_height = (total_lines as f32 + 2.0) * line_height;

        lines = Vec::with_capacity(total_lines + 1);

        // Search box line
        lines.push(OverlayLine {
            text: if palette.query.is_empty() {
                "> Type to search..."
            } else {
                &palette.query
            },
            fg: fg_dim,
            bg: None,
            bold: false,
        });

        // Entry lines
        for (i, entry) in entries.iter().take(max_visible).enumerate() {
            let is_sel = i == palette.selected;
            lines.push(OverlayLine {
                text: &entry.label,
                fg: if is_sel {
                    [1.0, 1.0, 1.0, 1.0]
                } else {
                    fg_normal
                },
                bg: if is_sel { Some(selected_bg) } else { None },
                bold: is_sel,
            });
        }
    }

    Some(OverlayLayer {
        x,
        y,
        width: overlay_width,
        height: overlay_height,
        bg,
        lines,
    })
}

/// Build the command-history browser overlay.
///
/// `lines[0]` is the search box; the remaining lines are history entries in the
/// same order as `browser.entries()`, so `browser.selected` indexes them.
fn build_history_overlay<'a>(
    browser: &HistoryBrowser,
    lines: &'a [String],
    window_width: f32,
    window_height: f32,
    line_height: f32,
) -> Option<OverlayLayer<'a>> {
    if !browser.visible || lines.is_empty() {
        return None;
    }

    let overlay_width = window_width * 0.6;
    let x = (window_width - overlay_width) / 2.0;
    let y = window_height * 0.15;

    let bg = [0.1, 0.1, 0.15, 0.95];
    let fg_normal = [0.85, 0.85, 0.85, 1.0];
    let fg_dim = [0.5, 0.5, 0.55, 1.0];
    let selected_bg = [0.25, 0.25, 0.35, 1.0];

    let max_visible = 12usize;
    let entry_lines = &lines[1..];
    let visible_count = entry_lines.len().min(max_visible);
    let overlay_height = (visible_count as f32 + 3.0) * line_height;

    let mut overlay_lines = Vec::with_capacity(visible_count + 1);
    // Search box line
    overlay_lines.push(OverlayLine {
        text: &lines[0],
        fg: fg_dim,
        bg: None,
        bold: false,
    });
    for (i, text) in entry_lines.iter().take(max_visible).enumerate() {
        let is_sel = i == browser.selected;
        overlay_lines.push(OverlayLine {
            text,
            fg: if is_sel {
                [1.0, 1.0, 1.0, 1.0]
            } else {
                fg_normal
            },
            bg: if is_sel { Some(selected_bg) } else { None },
            bold: is_sel,
        });
    }

    Some(OverlayLayer {
        x,
        y,
        width: overlay_width,
        height: overlay_height,
        bg,
        lines: overlay_lines,
    })
}

/// Map a SettingsField to an a11y FieldType.
fn map_field_type(field: &SettingsField) -> FieldType {
    match field {
        SettingsField::Text { .. } => FieldType::Text,
        SettingsField::Number { .. } => FieldType::Number,
        SettingsField::Toggle { .. } => FieldType::Toggle,
        SettingsField::Dropdown { .. } => FieldType::Dropdown,
        SettingsField::Keybinding { .. } => FieldType::Keybinding,
    }
}

/// Strip the shell prompt prefix from a terminal line.
///
/// Detects common prompt patterns:
/// - `C:\path>` (cmd.exe)
/// - `PS C:\path>` (PowerShell)
/// - `user@host:path$` / `user@host:path#` (bash/zsh)
/// - `>>>` (Python REPL)
///
/// Returns the text after the prompt, or the original text if no prompt is detected.
fn strip_prompt(line: &str) -> &str {
    // cmd.exe: "C:\Users\prati>" or "C:\Users\prati>command"
    if let Some(pos) = line.find('>') {
        let before = &line[..pos];
        // Validate it looks like a path (contains : or starts with PS)
        if before.contains(':') || before.starts_with("PS ") {
            return line[pos + 1..].trim_start();
        }
    }
    // bash/zsh: "user@host:~$" or "user@host:/path$ "
    if let Some(pos) = line.rfind("$ ")
        && line[..pos].contains('@')
    {
        return line[pos + 2..].trim_start();
    }
    if line.ends_with('$') && line.contains('@') {
        return "";
    }
    // Python REPL
    if let Some(rest) = line.strip_prefix(">>> ") {
        return rest;
    }
    line
}

/// Build sub-field snapshots for a keybinding field.
fn build_sub_field_snapshots(field: &SettingsField) -> Option<Vec<SubFieldSnapshot>> {
    let SettingsField::Keybinding {
        ctrl,
        shift,
        alt,
        win,
        key,
        editing,
        sub_focus,
        ..
    } = field
    else {
        return None;
    };
    if !*editing {
        return None;
    }
    Some(vec![
        SubFieldSnapshot {
            label: "Ctrl".to_string(),
            is_checkbox: true,
            checked: Some(*ctrl),
            value: None,
            is_focused: *sub_focus == 1,
        },
        SubFieldSnapshot {
            label: "Shift".to_string(),
            is_checkbox: true,
            checked: Some(*shift),
            value: None,
            is_focused: *sub_focus == 2,
        },
        SubFieldSnapshot {
            label: "Alt".to_string(),
            is_checkbox: true,
            checked: Some(*alt),
            value: None,
            is_focused: *sub_focus == 3,
        },
        SubFieldSnapshot {
            label: "Win".to_string(),
            is_checkbox: true,
            checked: Some(*win),
            value: None,
            is_focused: *sub_focus == 4,
        },
        SubFieldSnapshot {
            label: "Key".to_string(),
            is_checkbox: false,
            checked: None,
            value: Some(key.clone()),
            is_focused: *sub_focus == 5,
        },
    ])
}

/// Update the a11y state with the current settings UI snapshot.
fn update_settings_a11y(settings: &SettingsUI, state: &Arc<RwLock<TerminalA11yState>>) {
    let snapshot = if settings.visible {
        let tab_labels: Vec<String> = settings
            .tab_bar()
            .iter()
            .map(|(label, _)| label.to_string())
            .collect();

        let active_tab_index = settings
            .tab_bar()
            .iter()
            .position(|(_, active)| *active)
            .unwrap_or(0);

        let fields: Vec<FieldSnapshot> = settings
            .active_fields()
            .iter()
            .enumerate()
            .map(|(i, f)| {
                let (num_min, num_max, num_step, num_val) = match f {
                    SettingsField::Number {
                        min,
                        max,
                        step,
                        value,
                        ..
                    } => (
                        Some(*min as f64),
                        Some(*max as f64),
                        Some(*step as f64),
                        Some(*value as f64),
                    ),
                    _ => (None, None, None, None),
                };
                let toggle_on = match f {
                    SettingsField::Toggle { value, .. } => Some(*value),
                    _ => None,
                };
                let (dd_opts, dd_sel) = match f {
                    SettingsField::Dropdown {
                        options, selected, ..
                    } => (Some(options.clone()), Some(*selected)),
                    _ => (None, None),
                };
                FieldSnapshot {
                    label: f.label().to_string(),
                    field_type: map_field_type(f),
                    value_text: f.display_value(),
                    is_focused: i == settings.focused_field,
                    index: i,
                    number_min: num_min,
                    number_max: num_max,
                    number_step: num_step,
                    number_value: num_val,
                    toggle_on,
                    dropdown_options: dd_opts,
                    dropdown_selected: dd_sel,
                    sub_fields: build_sub_field_snapshots(f),
                }
            })
            .collect();

        Some(SettingsSnapshot {
            visible: true,
            active_tab_label: settings.active_tab.label().to_string(),
            active_tab_index,
            tab_labels,
            fields,
            focused_field: settings.focused_field,
        })
    } else {
        None
    };
    // Use try_write to avoid deadlocking with UIA provider reads.
    // If NVDA is reading the state via COM, we skip this update rather than blocking.
    if let Some(mut guard) = state.try_write() {
        if let Some(ref snap) = snapshot {
            info!(
                tab = %snap.active_tab_label,
                fields = snap.fields.len(),
                focused = snap.focused_field,
                "A11y settings snapshot updated (visible)"
            );
        } else {
            info!("A11y settings snapshot cleared (hidden)");
        }
        guard.settings_snapshot = snapshot;
    } else {
        warn!("A11y state write lock contention — settings snapshot NOT updated");
    }
}

/// Build an overlay for the settings UI (centered, 80% width × 70% height).
fn build_settings_overlay<'a>(
    settings: &'a SettingsUI,
    window_width: f32,
    window_height: f32,
    line_height: f32,
) -> Option<OverlayLayer<'a>> {
    if !settings.visible {
        return None;
    }

    let overlay_width = window_width * 0.8;
    let overlay_height = window_height * 0.7;
    let x = (window_width - overlay_width) / 2.0;
    let y = window_height * 0.15;

    let bg = [0.08, 0.08, 0.12, 0.95];
    let fg_normal = [0.85, 0.85, 0.85, 1.0];
    let fg_dim = [0.5, 0.5, 0.55, 1.0];
    let fg_accent = [0.4, 0.6, 1.0, 1.0];
    let selected_bg = [0.2, 0.2, 0.3, 1.0];

    let max_lines = ((overlay_height / line_height) as usize).saturating_sub(3);
    let mut lines = Vec::with_capacity(max_lines + 2);

    // Tab bar line
    let tab_bar = settings.tab_bar();
    let active_tab_label: &str = tab_bar
        .iter()
        .find(|(_, active)| *active)
        .map(|(label, _)| *label)
        .unwrap_or("Settings");
    lines.push(OverlayLine {
        text: active_tab_label,
        fg: fg_accent,
        bg: None,
        bold: true,
    });

    // Separator
    lines.push(OverlayLine {
        text: "─",
        fg: fg_dim,
        bg: None,
        bold: false,
    });

    // Field lines
    for (i, field) in settings.active_fields().iter().enumerate() {
        if lines.len() >= max_lines {
            break;
        }
        let is_focused = i == settings.focused_field;
        lines.push(OverlayLine {
            text: field.label(),
            fg: if is_focused {
                [1.0, 1.0, 1.0, 1.0]
            } else {
                fg_normal
            },
            bg: if is_focused { Some(selected_bg) } else { None },
            bold: is_focused,
        });
    }

    Some(OverlayLayer {
        x,
        y,
        width: overlay_width,
        height: overlay_height,
        bg,
        lines,
    })
}

/// Verbosity-aware notification: checks `screen_reader_output` before raising.
///
/// `kind` should be one of `"command"`, `"error"`, or `"output"`.
/// Returns `true` if the notification was actually raised.
unsafe fn raise_if_allowed(
    provider: &windows::Win32::UI::Accessibility::IRawElementProviderSimple,
    text: &str,
    activity_id: &str,
    verbosity: ScreenReaderVerbosity,
    kind: &str,
) -> bool {
    let allowed = match verbosity {
        ScreenReaderVerbosity::Silent => false,
        ScreenReaderVerbosity::ErrorsOnly => kind == "error",
        ScreenReaderVerbosity::CommandsOnly => kind == "command",
        ScreenReaderVerbosity::All | ScreenReaderVerbosity::Auto => true,
    };
    if !allowed {
        return false;
    }
    unsafe {
        wixen_a11y::events::raise_notification(provider, text, activity_id, false);
    }
    true
}

/// Announce a pane mode toggle (zoom, broadcast input, read-only) via UIA
/// notification and the mode-toggle audio cue.
fn announce_mode_toggle(
    mode: ToggleMode,
    active: bool,
    provider: &IRawElementProviderSimple,
    verbosity: ScreenReaderVerbosity,
    audio_config: &AudioConfig,
) {
    let message = wixen_ui::panes::mode_toggle_announcement(mode, active);
    unsafe {
        raise_if_allowed(provider, &message, "mode-toggle", verbosity, "output");
    }
    wixen_ui::audio::play_mode_toggle(audio_config, active);
}

/// Handle actions that need main-loop-local state the generic dispatcher lacks:
/// running macros, toggling the command-history overlay, and minimizing to tray.
///
/// Returns `true` if the action was recognized and handled.
#[allow(clippy::too_many_arguments)]
fn handle_local_action(
    action: &str,
    args: Option<&str>,
    macro_store: &wixen_config::MacroStore,
    history_browser: &mut HistoryBrowser,
    tab_mgr: &TabManager,
    panes: &mut HashMap<PaneId, PaneState>,
    window: &mut Window,
    tray_icon: &mut wixen_ui::tray::TrayIcon,
    provider: &IRawElementProviderSimple,
    verbosity: ScreenReaderVerbosity,
) -> bool {
    // Macro name: from keybinding args, or embedded in a palette id ("run_macro:{name}").
    let macro_name = if action == "run_macro" {
        args.map(str::to_string)
    } else {
        action.strip_prefix("run_macro:").map(str::to_string)
    };
    if let Some(name) = macro_name {
        let active = tab_mgr.active_tab().active_pane;
        if let Some(pane) = panes.get_mut(&active) {
            run_macro(macro_store, &name, pane);
        }
        return true;
    }

    match action {
        "show_history" => {
            let active = tab_mgr.active_tab().active_pane;
            if history_browser.visible {
                history_browser.close();
            } else if let Some(pane) = panes.get(&active) {
                history_browser.open(&pane.command_history);
                let msg = format!(
                    "Command history: {} commands",
                    history_browser.entries().len()
                );
                unsafe {
                    raise_if_allowed(provider, &msg, "history-open", verbosity, "output");
                }
            }
            true
        }
        "minimize_to_tray" => {
            tray_icon.show();
            window.hide();
            info!("Minimized to tray");
            true
        }
        "default_terminal_status" => {
            // SAFETY GATE: we intentionally never call
            // `wixen_ui::set_as_default_terminal()` here (or anywhere). Writing the
            // `DelegationConsole`/`DelegationTerminal` CLSIDs would tell Windows to
            // hand console launches to Wixen's CLSID — but the `ITerminalHandoff`
            // COM server that CLSID must resolve to is not built yet, so console
            // apps would fail to launch. Only status-read and restore are wired;
            // "set as default" stays unexposed until the handoff server ships.
            let status = wixen_ui::check_default_terminal_status();
            let msg = if status.is_default {
                "Wixen is the default terminal".to_string()
            } else {
                match status.current_handler {
                    Some(handler) => {
                        format!("Wixen is not the default terminal (current handler {handler})")
                    }
                    None => "Wixen is not the default terminal (no handler registered)".to_string(),
                }
            };
            info!(
                is_default = status.is_default,
                "Default terminal status: {msg}"
            );
            unsafe {
                raise_if_allowed(provider, &msg, "default-terminal", verbosity, "output");
            }
            true
        }
        "restore_default_terminal" => {
            match wixen_ui::restore_default_terminal() {
                Ok(()) => {
                    let msg = "Default terminal restored to the Windows default";
                    info!("{msg}");
                    unsafe {
                        raise_if_allowed(provider, msg, "default-terminal", verbosity, "output");
                    }
                }
                Err(e) => {
                    error!(error = %e, "Failed to restore default terminal");
                    let msg = "Failed to restore the Windows default terminal";
                    unsafe {
                        raise_if_allowed(provider, msg, "default-terminal", verbosity, "output");
                    }
                }
            }
            true
        }
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_unescape_send_text_basic() {
        assert_eq!(unescape_send_text("hello"), b"hello");
    }

    #[test]
    fn test_unescape_send_text_escapes() {
        assert_eq!(unescape_send_text(r"\r"), b"\r");
        assert_eq!(unescape_send_text(r"\n"), b"\n");
        assert_eq!(unescape_send_text(r"\t"), b"\t");
        assert_eq!(unescape_send_text(r"\\"), b"\\");
        assert_eq!(unescape_send_text(r"\x1b"), b"\x1b");
    }

    #[test]
    fn test_unescape_send_text_mixed() {
        let result = unescape_send_text(r"cargo test\r");
        assert_eq!(result, b"cargo test\r");
    }

    #[test]
    fn test_unescape_send_text_hex() {
        let result = unescape_send_text(r"\x1b[A");
        assert_eq!(result, b"\x1b[A");
    }

    // --- open_url scheme-guard decision logic ---
    //
    // `open_url` gates ShellExecuteW on `wixen_core::url::is_safe_url_scheme`.
    // These tests lock the classification the guard depends on: attacker-
    // influenced URIs (OSC 8, OSC 7, regex-detected text) with dangerous
    // schemes must never be treated as openable web URLs.

    #[test]
    fn test_open_url_guard_allows_web_schemes() {
        for url in [
            "http://example.com",
            "https://example.com/path?q=1#frag",
            "ftp://files.example.com/pub",
        ] {
            assert!(
                wixen_core::url::is_safe_url_scheme(url),
                "expected {url} to be openable"
            );
        }
    }

    #[test]
    fn test_open_url_guard_rejects_dangerous_schemes() {
        for url in [
            "file:///C:/Windows/System32/calc.exe",
            "ms-settings:privacy",
            "javascript:alert(1)",
            "data:text/html,<h1>hi</h1>",
            r"\\attacker-host\share\payload.exe",
            "",
        ] {
            assert!(
                !wixen_core::url::is_safe_url_scheme(url),
                "expected {url:?} to be refused"
            );
        }
    }

    #[test]
    fn test_open_url_guard_rejects_local_config_path() {
        // A bare local path (what `open_local_file` handles) has no web scheme
        // and must NOT be openable through the web-URL path.
        assert!(!wixen_core::url::is_safe_url_scheme(
            r"C:\Users\pratik\AppData\Roaming\wixen\wixen.toml"
        ));
    }

    // --- macro_palette_entries ---

    #[test]
    fn test_macro_palette_entries_maps_name_and_description() {
        use wixen_config::{Macro, MacroStep, MacroStore};
        let mut store = MacroStore::default();
        store.add(Macro {
            name: "deploy".into(),
            description: "Deploy to prod".into(),
            steps: vec![MacroStep::RunCommand("./deploy.sh".into())],
            keybinding: None,
        });
        store.add(Macro {
            name: "status".into(),
            description: String::new(),
            steps: vec![],
            keybinding: None,
        });

        let entries = macro_palette_entries(&store);
        assert_eq!(
            entries,
            vec![
                ("deploy".to_string(), "Deploy to prod".to_string()),
                ("status".to_string(), String::new()),
            ]
        );
    }

    #[test]
    fn test_macro_palette_entries_empty_store() {
        let store = wixen_config::MacroStore::default();
        assert!(macro_palette_entries(&store).is_empty());
    }

    // --- build_history_overlay ---

    #[test]
    fn test_build_history_overlay_hidden_returns_none() {
        let browser = HistoryBrowser::new();
        let lines: Vec<String> = Vec::new();
        assert!(build_history_overlay(&browser, &lines, 800.0, 600.0, 16.0).is_none());
    }

    #[test]
    fn test_build_history_overlay_visible_builds_lines() {
        let mut history = CommandHistory::new();
        history.push(HistoryEntry {
            command: "cargo build".into(),
            exit_code: Some(0),
            cwd: None,
            timestamp: None,
            block_id: 1,
        });
        let mut browser = HistoryBrowser::new();
        browser.open(&history);

        // Search box line + one entry line.
        let lines = vec![
            "> Type to search history...".to_string(),
            "cargo build".to_string(),
        ];
        let overlay = build_history_overlay(&browser, &lines, 800.0, 600.0, 16.0)
            .expect("overlay should be built when visible");
        assert_eq!(overlay.lines.len(), 2);
        assert_eq!(overlay.lines[0].text, "> Type to search history...");
        assert_eq!(overlay.lines[1].text, "cargo build");
        // The selected entry (index 0) is bold.
        assert!(overlay.lines[1].bold);
    }
}
