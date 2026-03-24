//! Raw Win32 window creation and message loop.

use tracing::{info, warn};
use windows::Win32::Foundation::*;
use windows::Win32::Graphics::Dwm::*;
use windows::Win32::Graphics::Gdi::*;
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::UI::Accessibility::*;
use windows::Win32::UI::HiDpi::*;
use windows::Win32::UI::Input::Ime::*;
use windows::Win32::UI::Input::KeyboardAndMouse::*;
use windows::Win32::UI::Shell::{DragAcceptFiles, DragFinish, DragQueryFileW, HDROP};
use windows::Win32::UI::WindowsAndMessaging::*;
use windows::core::*;
use wixen_config::{BackgroundStyle, WindowConfig};

/// Mouse button identifier.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MouseButton {
    Left,
    Right,
    Middle,
}

/// Messages sent from the window thread to the main event loop.
#[derive(Debug)]
pub enum WindowEvent {
    /// Window was resized to (width, height) in physical pixels
    Resized(u32, u32),
    /// Keyboard input (virtual key code, scan code, is_down, modifiers)
    KeyInput {
        vk: u16,
        scan: u16,
        down: bool,
        shift: bool,
        ctrl: bool,
        alt: bool,
    },
    /// Character input (from WM_CHAR)
    CharInput(char),
    /// Mouse wheel scroll (delta in lines, positive = up)
    MouseWheel {
        delta: i16,
        x: i32,
        y: i32,
        mods: u8,
    },
    /// Mouse button pressed at (x, y) in client pixels
    MouseDown {
        button: MouseButton,
        x: i32,
        y: i32,
        mods: u8,
    },
    /// Mouse button released at (x, y) in client pixels
    MouseUp {
        button: MouseButton,
        x: i32,
        y: i32,
        mods: u8,
    },
    /// Mouse moved to (x, y) in client pixels
    MouseMove { x: i32, y: i32, mods: u8 },
    /// Left mouse button double-clicked at (x, y) in client pixels
    DoubleClick { x: i32, y: i32 },
    /// Window close requested
    CloseRequested,
    /// Window gained focus
    FocusGained,
    /// Window lost focus
    FocusLost,
    /// DPI changed
    DpiChanged(u32),
    /// Quake-mode global hotkey was pressed — toggle visibility.
    QuakeToggle,
    /// Files were drag-and-dropped onto the window.
    FilesDropped(Vec<String>),
    /// User selected a context menu action.
    ContextMenu(ContextMenuAction),
}

/// Actions available in the right-click context menu.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ContextMenuAction {
    Copy,
    Paste,
    SelectAll,
    Search,
    SplitHorizontal,
    SplitVertical,
    NewTab,
    CloseTab,
    Settings,
}

/// Opaque handle to the Win32 window.
pub struct Window {
    hwnd: HWND,
    event_rx: crossbeam_channel::Receiver<WindowEvent>,
    fullscreen: bool,
    saved_placement: WINDOWPLACEMENT,
    visible: bool,
    /// Whether the window is always-on-top (topmost).
    always_on_top: bool,
    /// Registered global hotkey ID (0 = none).
    quake_hotkey_id: i32,
}

struct WindowState {
    event_tx: crossbeam_channel::Sender<WindowEvent>,
    /// UIA provider set by the application after window creation.
    uia_provider: parking_lot::RwLock<Option<IRawElementProviderSimple>>,
}

impl Window {
    /// Create a new Win32 window.
    pub fn new(title: &str, width: u32, height: u32) -> Result<Self> {
        // Enable per-monitor DPI awareness
        unsafe {
            let _ = SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }

        let (event_tx, event_rx) = crossbeam_channel::unbounded();

        let hmodule: HMODULE = unsafe { GetModuleHandleW(None)? };
        let hinstance = HINSTANCE(hmodule.0);

        let class_name = w!("WixenTerminalWindow");

        let wc = WNDCLASSEXW {
            cbSize: std::mem::size_of::<WNDCLASSEXW>() as u32,
            style: CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
            lpfnWndProc: Some(wnd_proc),
            hInstance: hinstance,
            hCursor: unsafe { LoadCursorW(None, IDC_IBEAM)? },
            lpszClassName: class_name,
            hbrBackground: HBRUSH(std::ptr::null_mut()),
            ..Default::default()
        };

        let atom = unsafe { RegisterClassExW(&wc) };
        if atom == 0 {
            return Err(Error::from_win32());
        }

        // Adjust window size to account for non-client area
        let mut rect = RECT {
            left: 0,
            top: 0,
            right: width as i32,
            bottom: height as i32,
        };
        unsafe {
            AdjustWindowRectEx(&mut rect, WS_OVERLAPPEDWINDOW, false, WINDOW_EX_STYLE(0))?;
        }

        let adjusted_width = rect.right - rect.left;
        let adjusted_height = rect.bottom - rect.top;

        let title_wide: Vec<u16> = title.encode_utf16().chain(std::iter::once(0)).collect();

        // Allocate state on heap, pass as LPARAM
        let state = Box::new(WindowState {
            event_tx,
            uia_provider: parking_lot::RwLock::new(None),
        });
        let state_ptr = Box::into_raw(state);

        let hwnd = unsafe {
            CreateWindowExW(
                WINDOW_EX_STYLE(0),
                class_name,
                PCWSTR(title_wide.as_ptr()),
                WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                adjusted_width,
                adjusted_height,
                None,
                None,
                Some(hinstance),
                Some(state_ptr as *const std::ffi::c_void),
            )?
        };

        info!(?hwnd, width, height, "Win32 window created");

        unsafe {
            let _ = ShowWindow(hwnd, SW_SHOW);
            let _ = UpdateWindow(hwnd);
            // Enable drag-and-drop file support
            DragAcceptFiles(hwnd, true);
        }

        Ok(Self {
            hwnd,
            event_rx,
            fullscreen: false,
            saved_placement: WINDOWPLACEMENT {
                length: std::mem::size_of::<WINDOWPLACEMENT>() as u32,
                ..Default::default()
            },
            visible: true,
            always_on_top: false,
            quake_hotkey_id: 0,
        })
    }

    /// Get the raw HWND handle.
    pub fn hwnd(&self) -> HWND {
        self.hwnd
    }

    /// Get the event receiver for window events.
    pub fn events(&self) -> &crossbeam_channel::Receiver<WindowEvent> {
        &self.event_rx
    }

    /// Get the current client area size in physical pixels.
    pub fn client_size(&self) -> (u32, u32) {
        let mut rect = RECT::default();
        unsafe {
            let _ = GetClientRect(self.hwnd, &mut rect);
        }
        (
            (rect.right - rect.left) as u32,
            (rect.bottom - rect.top) as u32,
        )
    }

    /// Get the current DPI for this window.
    pub fn dpi(&self) -> u32 {
        unsafe { GetDpiForWindow(self.hwnd) }
    }

    /// Set the UIA accessibility provider for this window.
    ///
    /// Call this after creating the `TerminalProvider` so that `WM_GETOBJECT`
    /// can return it to screen readers.
    pub fn set_uia_provider(&self, provider: IRawElementProviderSimple) {
        let ptr = unsafe { GetWindowLongPtrW(self.hwnd, GWLP_USERDATA) };
        if ptr != 0 {
            let state = unsafe { &*(ptr as *const WindowState) };
            *state.uia_provider.write() = Some(provider);
        }
    }

    /// Whether the window is currently in borderless fullscreen mode.
    pub fn is_fullscreen(&self) -> bool {
        self.fullscreen
    }

    /// Toggle borderless fullscreen mode.
    ///
    /// Entering fullscreen: saves the current window placement, removes the title bar
    /// style, and stretches the window to cover the entire monitor.
    /// Exiting fullscreen: restores the saved placement and title bar style.
    pub fn toggle_fullscreen(&mut self) {
        if self.fullscreen {
            // Restore windowed mode
            unsafe {
                let style = GetWindowLongW(self.hwnd, GWL_STYLE) as u32;
                let _ =
                    SetWindowLongW(self.hwnd, GWL_STYLE, (style | WS_OVERLAPPEDWINDOW.0) as i32);
                let _ = SetWindowPlacement(self.hwnd, &self.saved_placement);
                let _ = SetWindowPos(
                    self.hwnd,
                    None,
                    0,
                    0,
                    0,
                    0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED,
                );
            }
            self.fullscreen = false;
            info!("Exited fullscreen");
        } else {
            // Save placement and enter fullscreen
            self.saved_placement.length = std::mem::size_of::<WINDOWPLACEMENT>() as u32;
            unsafe {
                let _ = GetWindowPlacement(self.hwnd, &mut self.saved_placement);
                let style = GetWindowLongW(self.hwnd, GWL_STYLE) as u32;
                let _ = SetWindowLongW(
                    self.hwnd,
                    GWL_STYLE,
                    (style & !WS_OVERLAPPEDWINDOW.0) as i32,
                );

                let monitor = MonitorFromWindow(self.hwnd, MONITOR_DEFAULTTONEAREST);
                let mut mi = MONITORINFO {
                    cbSize: std::mem::size_of::<MONITORINFO>() as u32,
                    ..Default::default()
                };
                if GetMonitorInfoW(monitor, &mut mi).as_bool() {
                    let rc = mi.rcMonitor;
                    let _ = SetWindowPos(
                        self.hwnd,
                        Some(HWND_TOP),
                        rc.left,
                        rc.top,
                        rc.right - rc.left,
                        rc.bottom - rc.top,
                        SWP_NOOWNERZORDER | SWP_FRAMECHANGED,
                    );
                }
            }
            self.fullscreen = true;
            info!("Entered fullscreen");
        }
    }

    /// Whether the window is currently always-on-top.
    pub fn is_always_on_top(&self) -> bool {
        self.always_on_top
    }

    /// Toggle the window's always-on-top (topmost) state.
    pub fn toggle_always_on_top(&mut self) {
        use windows::Win32::UI::WindowsAndMessaging::{HWND_NOTOPMOST, HWND_TOPMOST};
        let z = if self.always_on_top {
            HWND_NOTOPMOST
        } else {
            HWND_TOPMOST
        };
        unsafe {
            let _ = SetWindowPos(self.hwnd, Some(z), 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        }
        self.always_on_top = !self.always_on_top;
        info!(topmost = self.always_on_top, "Always-on-top toggled");
    }

    /// Whether the window is currently visible.
    pub fn is_visible(&self) -> bool {
        self.visible
    }

    /// Toggle window visibility (for quake mode).
    ///
    /// When hiding: minimizes or hides the window.
    /// When showing: restores and brings to foreground.
    pub fn toggle_visibility(&mut self) {
        if self.visible {
            unsafe {
                let _ = ShowWindow(self.hwnd, SW_HIDE);
            }
            self.visible = false;
            info!("Quake: window hidden");
        } else {
            unsafe {
                let _ = ShowWindow(self.hwnd, SW_SHOW);
                let _ = SetForegroundWindow(self.hwnd);
            }
            self.visible = true;
            info!("Quake: window shown");
        }
    }

    /// Register a global hotkey for quake-mode toggle.
    ///
    /// `modifiers` uses Win32 `MOD_*` flags, `vk` is the virtual key code.
    /// Returns `true` on success.
    pub fn register_quake_hotkey(&mut self, modifiers: u32, vk: u32) -> bool {
        use windows::Win32::UI::Input::KeyboardAndMouse::{HOT_KEY_MODIFIERS, RegisterHotKey};
        const QUAKE_HOTKEY_ID: i32 = 0x0042;
        let ok = unsafe {
            RegisterHotKey(
                Some(self.hwnd),
                QUAKE_HOTKEY_ID,
                HOT_KEY_MODIFIERS(modifiers),
                vk,
            )
            .is_ok()
        };
        if ok {
            self.quake_hotkey_id = QUAKE_HOTKEY_ID;
            info!(modifiers, vk, "Quake hotkey registered");
        } else {
            warn!(modifiers, vk, "Failed to register quake hotkey");
        }
        ok
    }

    /// Unregister the quake-mode global hotkey (if registered).
    pub fn unregister_quake_hotkey(&mut self) {
        if self.quake_hotkey_id != 0 {
            unsafe {
                let _ = windows::Win32::UI::Input::KeyboardAndMouse::UnregisterHotKey(
                    Some(self.hwnd),
                    self.quake_hotkey_id,
                );
            }
            self.quake_hotkey_id = 0;
            info!("Quake hotkey unregistered");
        }
    }

    /// Show the right-click context menu at the given client-area coordinates.
    ///
    /// `has_selection` controls whether the Copy item is enabled.
    pub fn show_context_menu(&self, x: i32, y: i32, has_selection: bool) {
        const ID_COPY: u32 = 100;
        const ID_PASTE: u32 = 101;
        const ID_SELECT_ALL: u32 = 102;
        const ID_SEARCH: u32 = 103;
        const ID_SPLIT_H: u32 = 104;
        const ID_SPLIT_V: u32 = 105;
        const ID_NEW_TAB: u32 = 106;
        const ID_CLOSE_TAB: u32 = 107;
        const ID_SETTINGS: u32 = 108;

        unsafe {
            let menu = CreatePopupMenu().unwrap();

            let copy_flags = if has_selection {
                MF_STRING
            } else {
                MF_STRING | MF_GRAYED
            };
            let _ = AppendMenuW(menu, copy_flags, ID_COPY as usize, w!("Copy\tCtrl+C"));
            let _ = AppendMenuW(menu, MF_STRING, ID_PASTE as usize, w!("Paste\tCtrl+V"));
            let _ = AppendMenuW(
                menu,
                MF_STRING,
                ID_SELECT_ALL as usize,
                w!("Select All\tCtrl+Shift+A"),
            );
            let _ = AppendMenuW(
                menu,
                MF_STRING,
                ID_SEARCH as usize,
                w!("Find\tCtrl+Shift+F"),
            );
            let _ = AppendMenuW(menu, MF_SEPARATOR, 0, PCWSTR::null());
            let _ = AppendMenuW(menu, MF_STRING, ID_SPLIT_H as usize, w!("Split Horizontal"));
            let _ = AppendMenuW(menu, MF_STRING, ID_SPLIT_V as usize, w!("Split Vertical"));
            let _ = AppendMenuW(menu, MF_SEPARATOR, 0, PCWSTR::null());
            let _ = AppendMenuW(menu, MF_STRING, ID_NEW_TAB as usize, w!("New Tab\tCtrl+T"));
            let _ = AppendMenuW(
                menu,
                MF_STRING,
                ID_CLOSE_TAB as usize,
                w!("Close Tab\tCtrl+W"),
            );
            let _ = AppendMenuW(menu, MF_SEPARATOR, 0, PCWSTR::null());
            let _ = AppendMenuW(
                menu,
                MF_STRING,
                ID_SETTINGS as usize,
                w!("Settings\tCtrl+,"),
            );

            // Convert client coords to screen coords for TrackPopupMenu
            let mut pt = POINT { x, y };
            let _ = ClientToScreen(self.hwnd, &mut pt);

            let cmd = TrackPopupMenu(
                menu,
                TPM_RETURNCMD | TPM_RIGHTBUTTON,
                pt.x,
                pt.y,
                Some(0),
                self.hwnd,
                None,
            );

            let _ = DestroyMenu(menu);

            if cmd.0 != 0 {
                let action = match cmd.0 as u32 {
                    ID_COPY => Some(ContextMenuAction::Copy),
                    ID_PASTE => Some(ContextMenuAction::Paste),
                    ID_SELECT_ALL => Some(ContextMenuAction::SelectAll),
                    ID_SEARCH => Some(ContextMenuAction::Search),
                    ID_SPLIT_H => Some(ContextMenuAction::SplitHorizontal),
                    ID_SPLIT_V => Some(ContextMenuAction::SplitVertical),
                    ID_NEW_TAB => Some(ContextMenuAction::NewTab),
                    ID_CLOSE_TAB => Some(ContextMenuAction::CloseTab),
                    ID_SETTINGS => Some(ContextMenuAction::Settings),
                    _ => None,
                };
                if let Some(action) = action {
                    let ptr = GetWindowLongPtrW(self.hwnd, GWLP_USERDATA);
                    if ptr != 0 {
                        let state = &*(ptr as *const WindowState);
                        let _ = state.event_tx.send(WindowEvent::ContextMenu(action));
                    }
                }
            }
        }
    }

    /// Position the IME composition window at the given client-area pixel coordinates.
    ///
    /// Call this whenever the cursor position changes so that the IME candidate
    /// window appears at the correct location (e.g., near the terminal cursor).
    pub fn set_ime_position(&self, x: i32, y: i32, font_height: i32) {
        unsafe {
            let himc = ImmGetContext(self.hwnd);
            if !himc.is_invalid() {
                let comp = COMPOSITIONFORM {
                    dwStyle: CFS_POINT,
                    ptCurrentPos: POINT { x, y },
                    rcArea: RECT::default(),
                };
                let _ = ImmSetCompositionWindow(himc, &comp);

                // Set the font size so the IME candidate window scales appropriately
                let mut lf: windows::Win32::Graphics::Gdi::LOGFONTW = std::mem::zeroed();
                lf.lfHeight = font_height;
                let _ = ImmSetCompositionFontW(himc, &lf);

                let _ = ImmReleaseContext(self.hwnd, himc);
            }
        }
    }

    /// Convert client-area coordinates to screen coordinates.
    pub fn client_to_screen(&self, x: i32, y: i32) -> (i32, i32) {
        let mut pt = POINT { x, y };
        unsafe {
            let _ = ClientToScreen(self.hwnd, &mut pt);
        }
        (pt.x, pt.y)
    }

    /// Pump the Win32 message queue. Returns false if WM_QUIT was received.
    pub fn pump_messages(&self) -> bool {
        unsafe {
            let mut msg = MSG::default();
            while PeekMessageW(&mut msg as *mut _, None, 0, 0, PM_REMOVE).as_bool() {
                if msg.message == WM_QUIT {
                    return false;
                }
                let _ = TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            true
        }
    }
}

/// Apply window chrome settings: opacity, dark title bar, and backdrop material.
///
/// Safe to call at any time — after initial creation and on config hot-reload.
pub fn apply_window_chrome(hwnd: HWND, config: &WindowConfig) {
    // --- Dark title bar (DWMWA_USE_IMMERSIVE_DARK_MODE = 20) ---
    let dark: BOOL = if config.dark_title_bar { TRUE } else { FALSE };
    unsafe {
        let _ = DwmSetWindowAttribute(
            hwnd,
            DWMWA_USE_IMMERSIVE_DARK_MODE,
            &dark as *const BOOL as *const std::ffi::c_void,
            std::mem::size_of::<BOOL>() as u32,
        );
    }

    // --- Opacity (layered window) ---
    let opacity = config.opacity.clamp(0.0, 1.0);
    if opacity < 1.0 {
        // Add WS_EX_LAYERED if not already set
        let ex_style = unsafe { GetWindowLongW(hwnd, GWL_EXSTYLE) };
        if ex_style & WS_EX_LAYERED.0 as i32 == 0 {
            unsafe {
                SetWindowLongW(hwnd, GWL_EXSTYLE, ex_style | WS_EX_LAYERED.0 as i32);
            }
        }
        let alpha_byte = (opacity * 255.0) as u8;
        unsafe {
            let _ = SetLayeredWindowAttributes(hwnd, COLORREF(0), alpha_byte, LWA_ALPHA);
        }
    } else {
        // Remove WS_EX_LAYERED if opacity is 1.0 (fully opaque)
        let ex_style = unsafe { GetWindowLongW(hwnd, GWL_EXSTYLE) };
        if ex_style & WS_EX_LAYERED.0 as i32 != 0 {
            unsafe {
                SetWindowLongW(hwnd, GWL_EXSTYLE, ex_style & !(WS_EX_LAYERED.0 as i32));
            }
        }
    }

    // --- Backdrop material (DWMWA_SYSTEMBACKDROP_TYPE = 38, Windows 11 22H2+) ---
    // Values: 0 = Auto, 1 = None, 2 = Mica, 3 = Acrylic, 4 = Tabbed Mica
    let backdrop_type: i32 = match config.background {
        BackgroundStyle::Opaque => 1,  // DWM_SYSTEMBACKDROP_TYPE::DWMSBT_NONE
        BackgroundStyle::Mica => 2,    // DWM_SYSTEMBACKDROP_TYPE::DWMSBT_MAINWINDOW
        BackgroundStyle::Acrylic => 3, // DWM_SYSTEMBACKDROP_TYPE::DWMSBT_TRANSIENTWINDOW
    };

    // DWMWA_SYSTEMBACKDROP_TYPE (38) requires Windows 11 Build 22621+
    // On older systems the call silently fails, which is fine.
    let result = unsafe {
        DwmSetWindowAttribute(
            hwnd,
            DWMWINDOWATTRIBUTE(38), // DWMWA_SYSTEMBACKDROP_TYPE
            &backdrop_type as *const i32 as *const std::ffi::c_void,
            std::mem::size_of::<i32>() as u32,
        )
    };

    if result.is_err() && config.background != BackgroundStyle::Opaque {
        warn!(
            background = ?config.background,
            "DWM backdrop not supported on this Windows version — falling back to opaque"
        );
    }

    info!(
        opacity,
        dark_title_bar = config.dark_title_bar,
        background = ?config.background,
        "Window chrome applied"
    );
}

// Implement raw-window-handle traits for wgpu surface creation.

impl raw_window_handle::HasWindowHandle for Window {
    fn window_handle(
        &self,
    ) -> std::result::Result<raw_window_handle::WindowHandle<'_>, raw_window_handle::HandleError>
    {
        let mut handle = raw_window_handle::Win32WindowHandle::new(
            std::num::NonZero::new(self.hwnd.0 as isize).expect("HWND is null"),
        );
        let hmodule = unsafe { GetModuleHandleW(None).unwrap_or_default() };
        handle.hinstance = std::num::NonZero::new(hmodule.0 as isize);
        Ok(unsafe { raw_window_handle::WindowHandle::borrow_raw(handle.into()) })
    }
}

impl raw_window_handle::HasDisplayHandle for Window {
    fn display_handle(
        &self,
    ) -> std::result::Result<raw_window_handle::DisplayHandle<'_>, raw_window_handle::HandleError>
    {
        Ok(unsafe {
            raw_window_handle::DisplayHandle::borrow_raw(
                raw_window_handle::WindowsDisplayHandle::new().into(),
            )
        })
    }
}

/// Win32 window procedure.
unsafe extern "system" fn wnd_proc(
    hwnd: HWND,
    msg: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    match msg {
        WM_CREATE => {
            let cs = unsafe { &*(lparam.0 as *const CREATESTRUCTW) };
            if !cs.lpCreateParams.is_null() {
                unsafe {
                    SetWindowLongPtrW(hwnd, GWLP_USERDATA, cs.lpCreateParams as isize);
                }
                tracing::info!("WM_CREATE: state pointer stored in GWLP_USERDATA");
            } else {
                tracing::warn!("WM_CREATE: lpCreateParams is NULL — no state stored");
            }
            LRESULT(0)
        }
        WM_DESTROY => {
            let ptr = unsafe { GetWindowLongPtrW(hwnd, GWLP_USERDATA) };
            if ptr != 0 {
                unsafe {
                    let _ = Box::from_raw(ptr as *mut WindowState);
                    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                }
            }
            unsafe { PostQuitMessage(0) };
            LRESULT(0)
        }
        _ => {
            let ptr = unsafe { GetWindowLongPtrW(hwnd, GWLP_USERDATA) };
            if ptr == 0 {
            } else {
                let state = unsafe { &*(ptr as *const WindowState) };
                if let Some(result) = handle_message(state, hwnd, msg, wparam, lparam) {
                    return result;
                }
            }
            unsafe { DefWindowProcW(hwnd, msg, wparam, lparam) }
        }
    }
}

/// Check if a key is currently pressed.
fn key_pressed(vk: VIRTUAL_KEY) -> bool {
    (unsafe { GetKeyState(vk.0 as i32) } & (1 << 15)) != 0
}

/// Mouse modifier bits from WM_*BUTTON*/WM_MOUSEMOVE wparam.
///
/// Bit layout (matches `wixen_core::mouse::MouseModifiers`):
///   bit 2 (4) = Shift, bit 3 (8) = Alt, bit 4 (16) = Ctrl.
pub fn mouse_mods_from_wparam(wparam: WPARAM) -> u8 {
    let w = wparam.0 as u32;
    let mut mods = 0u8;
    if w & 0x0004 != 0 {
        mods |= 4; // MK_SHIFT
    }
    if w & 0x0008 != 0 {
        mods |= 16; // MK_CONTROL
    }
    // MK_ALT is not in wparam; check via GetKeyState
    if key_pressed(VK_MENU) {
        mods |= 8;
    }
    mods
}

/// Handle specific window messages. Returns Some(LRESULT) if handled.
fn handle_message(
    state: &WindowState,
    hwnd: HWND,
    msg: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> Option<LRESULT> {
    match msg {
        WM_SIZE => {
            let width = (lparam.0 & 0xFFFF) as u32;
            let height = ((lparam.0 >> 16) & 0xFFFF) as u32;
            tracing::debug!(width, height, "WM_SIZE received in WndProc");
            let _ = state.event_tx.send(WindowEvent::Resized(width, height));
            Some(LRESULT(0))
        }
        WM_KEYDOWN | WM_SYSKEYDOWN => {
            tracing::debug!("WM_KEYDOWN received in WndProc");
            let vk = (wparam.0 & 0xFFFF) as u16;
            let scan = ((lparam.0 >> 16) & 0xFF) as u16;
            let _ = state.event_tx.send(WindowEvent::KeyInput {
                vk,
                scan,
                down: true,
                shift: key_pressed(VK_SHIFT),
                ctrl: key_pressed(VK_CONTROL),
                alt: key_pressed(VK_MENU),
            });
            None // Let DefWindowProc generate WM_CHAR
        }
        WM_KEYUP | WM_SYSKEYUP => {
            let vk = (wparam.0 & 0xFFFF) as u16;
            let scan = ((lparam.0 >> 16) & 0xFF) as u16;
            let _ = state.event_tx.send(WindowEvent::KeyInput {
                vk,
                scan,
                down: false,
                shift: key_pressed(VK_SHIFT),
                ctrl: key_pressed(VK_CONTROL),
                alt: key_pressed(VK_MENU),
            });
            None
        }
        WM_CHAR => {
            if let Some(ch) = char::from_u32(wparam.0 as u32) {
                let _ = state.event_tx.send(WindowEvent::CharInput(ch));
            }
            Some(LRESULT(0))
        }
        WM_SETFOCUS => {
            let _ = state.event_tx.send(WindowEvent::FocusGained);
            Some(LRESULT(0))
        }
        WM_KILLFOCUS => {
            let _ = state.event_tx.send(WindowEvent::FocusLost);
            Some(LRESULT(0))
        }
        WM_DPICHANGED => {
            let new_dpi = (wparam.0 & 0xFFFF) as u32;
            let suggested = unsafe { &*(lparam.0 as *const RECT) };
            unsafe {
                let _ = SetWindowPos(
                    hwnd,
                    None,
                    suggested.left,
                    suggested.top,
                    suggested.right - suggested.left,
                    suggested.bottom - suggested.top,
                    SWP_NOZORDER | SWP_NOACTIVATE,
                );
            }
            let _ = state.event_tx.send(WindowEvent::DpiChanged(new_dpi));
            Some(LRESULT(0))
        }
        WM_MOUSEWHEEL => {
            let delta = ((wparam.0 >> 16) as i16) / 120; // WHEEL_DELTA = 120
            // Wheel coords are screen-relative — convert to client
            let mut pt = POINT {
                x: (lparam.0 & 0xFFFF) as i16 as i32,
                y: ((lparam.0 >> 16) & 0xFFFF) as i16 as i32,
            };
            let _ = unsafe { ScreenToClient(hwnd, &mut pt) };
            let mods = mouse_mods_from_wparam(wparam);
            let _ = state.event_tx.send(WindowEvent::MouseWheel {
                delta,
                x: pt.x,
                y: pt.y,
                mods,
            });
            Some(LRESULT(0))
        }
        WM_LBUTTONDBLCLK => {
            let x = (lparam.0 & 0xFFFF) as i16 as i32;
            let y = ((lparam.0 >> 16) & 0xFFFF) as i16 as i32;
            let _ = state.event_tx.send(WindowEvent::DoubleClick { x, y });
            Some(LRESULT(0))
        }
        WM_LBUTTONDOWN => {
            let x = (lparam.0 & 0xFFFF) as i16 as i32;
            let y = ((lparam.0 >> 16) & 0xFFFF) as i16 as i32;
            let mods = mouse_mods_from_wparam(wparam);
            let _ = state.event_tx.send(WindowEvent::MouseDown {
                button: MouseButton::Left,
                x,
                y,
                mods,
            });
            Some(LRESULT(0))
        }
        WM_LBUTTONUP => {
            let x = (lparam.0 & 0xFFFF) as i16 as i32;
            let y = ((lparam.0 >> 16) & 0xFFFF) as i16 as i32;
            let mods = mouse_mods_from_wparam(wparam);
            let _ = state.event_tx.send(WindowEvent::MouseUp {
                button: MouseButton::Left,
                x,
                y,
                mods,
            });
            Some(LRESULT(0))
        }
        WM_RBUTTONDOWN => {
            let x = (lparam.0 & 0xFFFF) as i16 as i32;
            let y = ((lparam.0 >> 16) & 0xFFFF) as i16 as i32;
            let mods = mouse_mods_from_wparam(wparam);
            let _ = state.event_tx.send(WindowEvent::MouseDown {
                button: MouseButton::Right,
                x,
                y,
                mods,
            });
            Some(LRESULT(0))
        }
        WM_RBUTTONUP => {
            let x = (lparam.0 & 0xFFFF) as i16 as i32;
            let y = ((lparam.0 >> 16) & 0xFFFF) as i16 as i32;
            let mods = mouse_mods_from_wparam(wparam);
            let _ = state.event_tx.send(WindowEvent::MouseUp {
                button: MouseButton::Right,
                x,
                y,
                mods,
            });
            Some(LRESULT(0))
        }
        WM_MBUTTONDOWN => {
            let x = (lparam.0 & 0xFFFF) as i16 as i32;
            let y = ((lparam.0 >> 16) & 0xFFFF) as i16 as i32;
            let mods = mouse_mods_from_wparam(wparam);
            let _ = state.event_tx.send(WindowEvent::MouseDown {
                button: MouseButton::Middle,
                x,
                y,
                mods,
            });
            Some(LRESULT(0))
        }
        WM_MBUTTONUP => {
            let x = (lparam.0 & 0xFFFF) as i16 as i32;
            let y = ((lparam.0 >> 16) & 0xFFFF) as i16 as i32;
            let mods = mouse_mods_from_wparam(wparam);
            let _ = state.event_tx.send(WindowEvent::MouseUp {
                button: MouseButton::Middle,
                x,
                y,
                mods,
            });
            Some(LRESULT(0))
        }
        WM_MOUSEMOVE => {
            let x = (lparam.0 & 0xFFFF) as i16 as i32;
            let y = ((lparam.0 >> 16) & 0xFFFF) as i16 as i32;
            let mods = mouse_mods_from_wparam(wparam);
            let _ = state.event_tx.send(WindowEvent::MouseMove { x, y, mods });
            Some(LRESULT(0))
        }
        WM_CLOSE => {
            let _ = state.event_tx.send(WindowEvent::CloseRequested);
            Some(LRESULT(0))
        }
        WM_GETOBJECT => {
            // UIA queries the window for its accessibility provider
            if lparam.0 == UiaRootObjectId as isize {
                let provider = state.uia_provider.read();
                if let Some(ref p) = *provider {
                    return Some(unsafe { UiaReturnRawElementProvider(hwnd, wparam, lparam, p) });
                }
            }
            None // Let DefWindowProc handle non-UIA WM_GETOBJECT
        }
        WM_GETMINMAXINFO => {
            // Set minimum window size to prevent unusable tiny windows
            let mmi = unsafe { &mut *(lparam.0 as *mut MINMAXINFO) };
            mmi.ptMinTrackSize = POINT { x: 400, y: 200 };
            Some(LRESULT(0))
        }
        WM_HOTKEY => {
            // Global hotkey (quake mode toggle)
            let _ = state.event_tx.send(WindowEvent::QuakeToggle);
            Some(LRESULT(0))
        }
        WM_DROPFILES => {
            let hdrop = HDROP(wparam.0 as *mut std::ffi::c_void);
            let count = unsafe { DragQueryFileW(hdrop, 0xFFFFFFFF, None) };
            let mut paths = Vec::with_capacity(count as usize);
            for i in 0..count {
                // Query required buffer length (includes null terminator)
                let len = unsafe { DragQueryFileW(hdrop, i, None) } as usize;
                let mut buf = vec![0u16; len + 1];
                unsafe {
                    DragQueryFileW(hdrop, i, Some(&mut buf));
                }
                if let Ok(s) = String::from_utf16(&buf[..len]) {
                    paths.push(s);
                }
            }
            unsafe {
                DragFinish(hdrop);
            }
            if !paths.is_empty() {
                let _ = state.event_tx.send(WindowEvent::FilesDropped(paths));
            }
            Some(LRESULT(0))
        }
        WM_IME_STARTCOMPOSITION => {
            // Let DefWindowProc handle this, but we've already positioned the
            // composition window via set_ime_position() in the render loop.
            // Returning None lets the default handler use our COMPOSITIONFORM.
            None
        }
        WM_ERASEBKGND => {
            // Prevent GDI from erasing background (we render with wgpu)
            Some(LRESULT(1))
        }
        _ => None,
    }
}

// ---------------------------------------------------------------------------
// Window placement persistence
// ---------------------------------------------------------------------------

/// Serializable window placement state (position, size, maximized).
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct WindowPlacementState {
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
    pub maximized: bool,
}

impl Window {
    /// Get the current window placement as a serializable state.
    pub fn get_placement(&self) -> WindowPlacementState {
        unsafe {
            let mut wp = WINDOWPLACEMENT {
                length: std::mem::size_of::<WINDOWPLACEMENT>() as u32,
                ..Default::default()
            };
            let _ = GetWindowPlacement(self.hwnd, &mut wp);
            WindowPlacementState {
                x: wp.rcNormalPosition.left,
                y: wp.rcNormalPosition.top,
                width: wp.rcNormalPosition.right - wp.rcNormalPosition.left,
                height: wp.rcNormalPosition.bottom - wp.rcNormalPosition.top,
                maximized: wp.showCmd == SW_SHOWMAXIMIZED.0 as u32,
            }
        }
    }

    /// Apply a saved placement state to the window.
    pub fn apply_placement(&self, state: &WindowPlacementState) {
        unsafe {
            let wp = WINDOWPLACEMENT {
                length: std::mem::size_of::<WINDOWPLACEMENT>() as u32,
                showCmd: if state.maximized {
                    SW_SHOWMAXIMIZED.0 as u32
                } else {
                    SW_SHOWNORMAL.0 as u32
                },
                rcNormalPosition: windows::Win32::Foundation::RECT {
                    left: state.x,
                    top: state.y,
                    right: state.x + state.width,
                    bottom: state.y + state.height,
                },
                ..Default::default()
            };
            let _ = SetWindowPlacement(self.hwnd, &wp);
        }
    }
}

/// Save window placement state to a JSON file.
pub fn save_placement(state: &WindowPlacementState, path: &std::path::Path) -> std::io::Result<()> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let json = serde_json::to_string_pretty(state).map_err(std::io::Error::other)?;
    std::fs::write(path, json)
}

/// Load window placement state from a JSON file.
pub fn load_placement(path: &std::path::Path) -> Option<WindowPlacementState> {
    let data = std::fs::read_to_string(path).ok()?;
    serde_json::from_str(&data).ok()
}

/// Get the default window state file path.
///
/// - **Portable**: `<exe_dir>/window_state.json`
/// - **Normal**: `%APPDATA%/wixen/window_state.json`
pub fn window_state_path(portable: bool) -> std::path::PathBuf {
    if portable {
        std::env::current_exe()
            .ok()
            .and_then(|p| p.parent().map(|d| d.join("window_state.json")))
            .unwrap_or_else(|| std::path::PathBuf::from("window_state.json"))
    } else {
        let appdata = std::env::var("APPDATA").unwrap_or_else(|_| ".".to_string());
        std::path::PathBuf::from(appdata)
            .join("wixen")
            .join("window_state.json")
    }
}

/// Format dropped file paths for pasting into a terminal.
///
/// Paths containing spaces are quoted with double quotes.
/// Multiple paths are separated by a single space.
pub fn format_dropped_paths(paths: &[String]) -> String {
    let formatted: Vec<String> = paths
        .iter()
        .map(|p| {
            if p.contains(' ') {
                format!("\"{}\"", p)
            } else {
                p.clone()
            }
        })
        .collect();
    formatted.join(" ")
}

/// Query the Windows system preference for reduced motion.
///
/// Returns `true` when the user has disabled client-area animations
/// via Settings -> Accessibility -> Visual effects -> Animation effects.
/// The caller should re-check this on `WM_SETTINGCHANGE`.
#[cfg(windows)]
pub fn system_reduced_motion() -> bool {
    let mut animations_enabled: BOOL = TRUE;
    let ok = unsafe {
        SystemParametersInfoW(
            SPI_GETCLIENTAREAANIMATION,
            0,
            Some(&mut animations_enabled as *mut BOOL as *mut std::ffi::c_void),
            SYSTEM_PARAMETERS_INFO_UPDATE_FLAGS(0),
        )
    };
    match ok {
        Ok(()) => animations_enabled == FALSE,
        Err(_) => false,
    }
}

/// Non-Windows stub — always returns `false`.
#[cfg(not(windows))]
pub fn system_reduced_motion() -> bool {
    false
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_format_dropped_single_no_spaces() {
        let paths = vec!["C:\\dev\\project\\file.txt".to_string()];
        assert_eq!(format_dropped_paths(&paths), "C:\\dev\\project\\file.txt");
    }

    #[test]
    fn test_format_dropped_single_with_spaces() {
        let paths = vec!["C:\\Program Files\\app.exe".to_string()];
        assert_eq!(
            format_dropped_paths(&paths),
            "\"C:\\Program Files\\app.exe\""
        );
    }

    #[test]
    fn test_format_dropped_multiple_mixed() {
        let paths = vec![
            "C:\\dev\\a.txt".to_string(),
            "C:\\My Folder\\b.txt".to_string(),
            "D:\\no_spaces.rs".to_string(),
        ];
        assert_eq!(
            format_dropped_paths(&paths),
            "C:\\dev\\a.txt \"C:\\My Folder\\b.txt\" D:\\no_spaces.rs"
        );
    }

    #[test]
    fn test_format_dropped_empty() {
        let paths: Vec<String> = vec![];
        assert_eq!(format_dropped_paths(&paths), "");
    }

    #[test]
    fn test_placement_serialize_roundtrip() {
        let state = WindowPlacementState {
            x: 100,
            y: 200,
            width: 1280,
            height: 720,
            maximized: false,
        };
        let json = serde_json::to_string(&state).unwrap();
        let deserialized: WindowPlacementState = serde_json::from_str(&json).unwrap();
        assert_eq!(state, deserialized);
    }

    #[test]
    fn test_placement_serialize_maximized() {
        let state = WindowPlacementState {
            x: 0,
            y: 0,
            width: 1920,
            height: 1080,
            maximized: true,
        };
        let json = serde_json::to_string(&state).unwrap();
        let deserialized: WindowPlacementState = serde_json::from_str(&json).unwrap();
        assert!(deserialized.maximized);
    }

    #[test]
    fn test_placement_file_roundtrip() {
        let dir = std::env::temp_dir().join("wixen_test_placement_rt");
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("window_state.json");

        let state = WindowPlacementState {
            x: 50,
            y: 75,
            width: 800,
            height: 600,
            maximized: false,
        };
        save_placement(&state, &path).unwrap();
        let loaded = load_placement(&path).unwrap();
        assert_eq!(state, loaded);

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_placement_file_missing_returns_none() {
        let path = std::env::temp_dir().join("wixen_nonexistent_state.json");
        assert!(load_placement(&path).is_none());
    }

    #[test]
    fn test_placement_creates_parent_dirs() {
        let dir = std::env::temp_dir().join("wixen_test_placement_dirs/a/b");
        let _ = std::fs::remove_dir_all(std::env::temp_dir().join("wixen_test_placement_dirs"));
        let path = dir.join("window_state.json");

        let state = WindowPlacementState {
            x: 0,
            y: 0,
            width: 1024,
            height: 768,
            maximized: false,
        };
        assert!(save_placement(&state, &path).is_ok());
        assert!(path.exists());

        let _ = std::fs::remove_dir_all(std::env::temp_dir().join("wixen_test_placement_dirs"));
    }

    #[test]
    fn test_window_state_path_portable() {
        let path = window_state_path(true);
        assert_eq!(path.file_name().unwrap(), "window_state.json");
    }

    #[test]
    fn test_context_menu_action_variants() {
        // Ensure all context menu actions are distinct
        let actions = [
            ContextMenuAction::Copy,
            ContextMenuAction::Paste,
            ContextMenuAction::SelectAll,
            ContextMenuAction::Search,
            ContextMenuAction::SplitHorizontal,
            ContextMenuAction::SplitVertical,
            ContextMenuAction::NewTab,
            ContextMenuAction::CloseTab,
            ContextMenuAction::Settings,
        ];
        for (i, a) in actions.iter().enumerate() {
            for (j, b) in actions.iter().enumerate() {
                assert_eq!(a == b, i == j);
            }
        }
    }
}
