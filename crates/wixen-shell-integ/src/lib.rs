//! wixen-shell-integ: Shell integration — OSC 133, OSC 7, semantic zones.
//!
//! Tracks command boundaries from shell integration sequences and exposes
//! them as `CommandBlock` structures for the accessibility tree.

pub mod heuristic;
pub mod tmux;

use std::time::{Duration, Instant};
use tracing::{debug, trace};

/// A range of rows in the terminal grid.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RowRange {
    /// First row (inclusive, 0-based absolute row including scrollback)
    pub start: usize,
    /// Last row (inclusive)
    pub end: usize,
}

impl RowRange {
    pub fn new(start: usize, end: usize) -> Self {
        Self { start, end }
    }

    pub fn single(row: usize) -> Self {
        Self {
            start: row,
            end: row,
        }
    }

    pub fn len(&self) -> usize {
        self.end.saturating_sub(self.start) + 1
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

/// A single command block — the fundamental semantic unit.
///
/// Represents one prompt → input → output → exit cycle.
#[derive(Debug, Clone)]
pub struct CommandBlock {
    /// Unique sequential ID for this block
    pub id: u64,
    /// Row range of the prompt text (e.g., "C:\Users\foo>")
    pub prompt: Option<RowRange>,
    /// Row range of the user's command input
    pub input: Option<RowRange>,
    /// Row range of the command's output
    pub output: Option<RowRange>,
    /// Exit code (None if command is still running)
    pub exit_code: Option<i32>,
    /// Current working directory at command start (from OSC 7)
    pub cwd: Option<String>,
    /// The command text itself (extracted from input rows)
    pub command_text: Option<String>,
    /// Current state of this block
    pub state: BlockState,
    /// When the command began executing (set on OSC 133;C)
    pub started_at: Option<Instant>,
    /// Number of output lines produced so far (incremented as rows are added)
    pub output_line_count: usize,
}

/// Lifecycle state of a command block.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BlockState {
    /// Prompt is being displayed (after OSC 133;A)
    PromptActive,
    /// User is typing a command (after OSC 133;B)
    InputActive,
    /// Command is executing, output streaming (after OSC 133;C)
    Executing,
    /// Command completed (after OSC 133;D)
    Completed,
}

/// Information about a completed command, used for long-running command notifications.
#[derive(Debug, Clone)]
pub struct CommandCompletion {
    /// The command text, if known
    pub command: Option<String>,
    /// Exit code of the command
    pub exit_code: Option<i32>,
    /// Wall-clock duration of the command execution
    pub duration: Duration,
    /// ID of the command block
    pub block_id: u64,
}

/// Maximum length of sanitized command text.
const MAX_COMMAND_TEXT_LEN: usize = 1024;

/// Regex matching ANSI escape sequences (CSI, OSC, and simple ESC codes).
static ANSI_RE: std::sync::LazyLock<regex::Regex> = std::sync::LazyLock::new(|| {
    // Matches: ESC [ ... final byte | ESC ] ... ST/BEL | ESC followed by one char
    regex::Regex::new(r"\x1b\[[0-9;]*[A-Za-z]|\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)|\x1b[^\[\]]")
        .unwrap()
});

/// Sanitize command text extracted from OSC 133 shell integration markers.
///
/// This defends against malicious programs injecting fake command markers:
/// - Strips ANSI escape sequences
/// - Replaces control characters (0x00-0x1F except `\t`, `\n`, `\r`) with spaces
/// - Truncates to [`MAX_COMMAND_TEXT_LEN`] characters
pub fn sanitize_command_text(text: &str) -> String {
    // 1. Strip ANSI escape sequences
    let stripped = ANSI_RE.replace_all(text, "");

    // 2. Replace dangerous control characters with spaces
    let cleaned: String = stripped
        .chars()
        .map(|c| {
            if c == '\t' || c == '\n' || c == '\r' {
                c
            } else if c.is_control() {
                ' '
            } else {
                c
            }
        })
        .collect();

    // 3. Truncate to max length
    if cleaned.len() > MAX_COMMAND_TEXT_LEN {
        cleaned[..MAX_COMMAND_TEXT_LEN].to_string()
    } else {
        cleaned
    }
}

/// Produce a screen-reader-friendly summary for a completed command block.
///
/// - Success with command text: `"{command} completed: {N} lines of output"`
/// - Failure with command text: `"{command} failed (exit {code}): {N} lines of output"`
/// - No command text: `"Command completed: {N} lines"`
pub fn command_summary(block: &CommandBlock) -> String {
    let n = block.output_line_count;

    match (&block.command_text, block.exit_code) {
        (Some(cmd), Some(code)) if code != 0 => {
            format!("{cmd} failed (exit {code}): {n} lines of output")
        }
        (Some(cmd), _) => {
            format!("{cmd} completed: {n} lines of output")
        }
        (None, _) => {
            format!("Command completed: {n} lines")
        }
    }
}

/// Shell integration tracker.
///
/// Receives OSC 133 events and maintains a list of `CommandBlock`s.
pub struct ShellIntegration {
    /// All tracked command blocks (oldest first)
    blocks: Vec<CommandBlock>,
    /// Next block ID
    next_id: u64,
    /// Current working directory (from OSC 7)
    cwd: Option<String>,
    /// Whether any OSC 133 sequences have been received (if false, fall back to heuristics)
    pub osc133_active: bool,
    /// Completed commands awaiting notification dispatch
    pub pending_completions: Vec<CommandCompletion>,
    /// Monotonically increasing generation counter — incremented on every structural change.
    /// The main loop compares this to a cached value to know when to rebuild the a11y tree.
    generation: u64,
}

impl ShellIntegration {
    pub fn new() -> Self {
        Self {
            blocks: Vec::new(),
            next_id: 1,
            cwd: None,
            osc133_active: false,
            pending_completions: Vec::new(),
            generation: 0,
        }
    }

    /// Get all command blocks.
    pub fn blocks(&self) -> &[CommandBlock] {
        &self.blocks
    }

    /// Get the current (most recent) command block.
    pub fn current_block(&self) -> Option<&CommandBlock> {
        self.blocks.last()
    }

    /// Get a mutable reference to the current (most recent) command block.
    pub fn current_block_mut(&mut self) -> Option<&mut CommandBlock> {
        self.blocks.last_mut()
    }

    /// Get a block by ID.
    pub fn block_by_id(&self, id: u64) -> Option<&CommandBlock> {
        self.blocks.iter().find(|b| b.id == id)
    }

    /// Monotonically increasing generation counter.
    ///
    /// Incremented on every structural change (new block, state transition).
    /// Compare with a cached value to detect when the a11y tree needs rebuilding.
    pub fn generation(&self) -> u64 {
        self.generation
    }

    /// Current working directory.
    pub fn cwd(&self) -> Option<&str> {
        self.cwd.as_deref()
    }

    /// Handle an OSC 133 shell integration sequence.
    ///
    /// `marker` is the sub-command (A, B, C, D, etc.).
    /// `params` are optional key=value parameters after the marker.
    /// `cursor_row` is the current absolute row in the terminal (viewport row + scrollback offset).
    pub fn handle_osc133(&mut self, marker: &str, params: &str, cursor_row: usize) {
        self.osc133_active = true;

        match marker {
            "A" => {
                // Prompt start — begin a new command block
                let block = CommandBlock {
                    id: self.next_id,
                    prompt: Some(RowRange::single(cursor_row)),
                    input: None,
                    output: None,
                    exit_code: None,
                    cwd: self.cwd.clone(),
                    command_text: None,
                    state: BlockState::PromptActive,
                    started_at: None,
                    output_line_count: 0,
                };
                self.next_id += 1;
                self.generation += 1;
                debug!(id = block.id, row = cursor_row, "OSC 133;A — prompt start");
                self.blocks.push(block);
            }
            "B" => {
                // Command start (end of prompt, beginning of user input)
                if let Some(block) = self.blocks.last_mut() {
                    // Extend prompt range to cover all prompt rows
                    if let Some(ref mut prompt) = block.prompt {
                        prompt.end = cursor_row.saturating_sub(1).max(prompt.start);
                    }
                    block.input = Some(RowRange::single(cursor_row));
                    block.state = BlockState::InputActive;
                    self.generation += 1;
                    debug!(id = block.id, row = cursor_row, "OSC 133;B — input start");
                }
            }
            "C" => {
                // Command executed — output begins
                if let Some(block) = self.blocks.last_mut() {
                    // Extend input range
                    if let Some(ref mut input) = block.input {
                        input.end = cursor_row.saturating_sub(1).max(input.start);
                    }
                    block.output = Some(RowRange::single(cursor_row));
                    block.state = BlockState::Executing;
                    block.started_at = Some(Instant::now());
                    self.generation += 1;
                    debug!(id = block.id, row = cursor_row, "OSC 133;C — output start");
                }
            }
            "D" => {
                // Command finished
                let mut completion = None;
                if let Some(block) = self.blocks.last_mut() {
                    // Extend output range
                    if let Some(ref mut output) = block.output {
                        output.end = cursor_row.saturating_sub(1).max(output.start);
                    }
                    // Parse exit code from params (e.g., "D;0" or "D;1")
                    let exit_code = if params.is_empty() {
                        None
                    } else {
                        params.parse::<i32>().ok()
                    };
                    block.exit_code = exit_code;
                    block.state = BlockState::Completed;
                    self.generation += 1;

                    if let Some(started) = block.started_at {
                        completion = Some(CommandCompletion {
                            command: block.command_text.clone(),
                            exit_code,
                            duration: started.elapsed(),
                            block_id: block.id,
                        });
                    }

                    debug!(
                        id = block.id,
                        exit_code = ?exit_code,
                        "OSC 133;D — command complete"
                    );
                }
                if let Some(c) = completion {
                    self.pending_completions.push(c);
                }
            }
            _ => {
                trace!(marker, "Unknown OSC 133 sub-command");
            }
        }
    }

    /// Handle an OSC 7 (current working directory) sequence.
    pub fn handle_osc7(&mut self, uri: &str) {
        // OSC 7 format: file://hostname/path
        let path = if let Some(stripped) = uri.strip_prefix("file://") {
            // Skip hostname (everything up to the next /)
            if let Some(slash_idx) = stripped.find('/') {
                Some(stripped[slash_idx..].to_string())
            } else {
                Some(stripped.to_string())
            }
        } else {
            Some(uri.to_string())
        };

        if let Some(path) = path {
            debug!(cwd = %path, "Working directory updated via OSC 7");
            self.cwd = Some(path);
        }
    }

    /// Prune old blocks to limit memory. Keeps the most recent `max_blocks`.
    pub fn prune(&mut self, max_blocks: usize) {
        if self.blocks.len() > max_blocks {
            let drain_count = self.blocks.len() - max_blocks;
            self.blocks.drain(..drain_count);
        }
    }

    /// Get the number of tracked command blocks.
    pub fn len(&self) -> usize {
        self.blocks.len()
    }

    pub fn is_empty(&self) -> bool {
        self.blocks.is_empty()
    }

    /// Drain all pending command completions regardless of duration.
    pub fn drain_completions(&mut self) -> Vec<CommandCompletion> {
        std::mem::take(&mut self.pending_completions)
    }

    /// Drain only completions whose duration exceeds `threshold`.
    ///
    /// Completions shorter than the threshold are discarded.
    pub fn take_completion_if_long(&mut self, threshold: Duration) -> Vec<CommandCompletion> {
        let all = std::mem::take(&mut self.pending_completions);
        all.into_iter()
            .filter(|c| c.duration >= threshold)
            .collect()
    }
}

impl Default for ShellIntegration {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    #[test]
    fn test_full_command_cycle() {
        let mut si = ShellIntegration::new();

        // Prompt appears
        si.handle_osc133("A", "", 0);
        assert_eq!(si.blocks().len(), 1);
        assert_eq!(si.current_block().unwrap().state, BlockState::PromptActive);

        // User starts typing
        si.handle_osc133("B", "", 1);
        assert_eq!(si.current_block().unwrap().state, BlockState::InputActive);
        assert_eq!(
            si.current_block().unwrap().prompt.unwrap(),
            RowRange::new(0, 0)
        );

        // Command executes
        si.handle_osc133("C", "", 2);
        assert_eq!(si.current_block().unwrap().state, BlockState::Executing);
        assert_eq!(
            si.current_block().unwrap().input.unwrap(),
            RowRange::new(1, 1)
        );

        // Command finishes
        si.handle_osc133("D", "0", 5);
        assert_eq!(si.current_block().unwrap().state, BlockState::Completed);
        assert_eq!(si.current_block().unwrap().exit_code, Some(0));
        assert_eq!(
            si.current_block().unwrap().output.unwrap(),
            RowRange::new(2, 4)
        );
    }

    #[test]
    fn test_multiple_commands() {
        let mut si = ShellIntegration::new();

        si.handle_osc133("A", "", 0);
        si.handle_osc133("B", "", 1);
        si.handle_osc133("C", "", 1);
        si.handle_osc133("D", "0", 3);

        si.handle_osc133("A", "", 4);
        si.handle_osc133("B", "", 5);
        si.handle_osc133("C", "", 5);
        si.handle_osc133("D", "1", 10);

        assert_eq!(si.blocks().len(), 2);
        assert_eq!(si.blocks()[0].exit_code, Some(0));
        assert_eq!(si.blocks()[1].exit_code, Some(1));
        assert!(si.osc133_active);
    }

    #[test]
    fn test_osc7_cwd() {
        let mut si = ShellIntegration::new();
        si.handle_osc7("file://laptop/C:/Users/Pratik");
        assert_eq!(si.cwd(), Some("/C:/Users/Pratik"));
    }

    #[test]
    fn test_generation_tracking() {
        let mut si = ShellIntegration::new();
        assert_eq!(si.generation(), 0);

        si.handle_osc133("A", "", 0);
        assert_eq!(si.generation(), 1);

        si.handle_osc133("B", "", 1);
        assert_eq!(si.generation(), 2);

        si.handle_osc133("C", "", 2);
        assert_eq!(si.generation(), 3);

        si.handle_osc133("D", "0", 5);
        assert_eq!(si.generation(), 4);

        // Second command
        si.handle_osc133("A", "", 6);
        assert_eq!(si.generation(), 5);
    }

    #[test]
    fn test_current_block_mut() {
        let mut si = ShellIntegration::new();
        si.handle_osc133("A", "", 0);
        si.handle_osc133("B", "", 1);
        si.handle_osc133("C", "", 2);

        // Set command text via mutable reference
        if let Some(block) = si.current_block_mut() {
            block.command_text = Some("echo hello".to_string());
        }
        assert_eq!(
            si.current_block().unwrap().command_text.as_deref(),
            Some("echo hello")
        );
    }

    #[test]
    fn test_prune() {
        let mut si = ShellIntegration::new();
        for i in 0..100 {
            si.handle_osc133("A", "", i * 5);
            si.handle_osc133("D", "0", i * 5 + 3);
        }
        assert_eq!(si.len(), 100);
        si.prune(50);
        assert_eq!(si.len(), 50);
    }

    #[test]
    fn test_powershell_script_exists() {
        let manifest_dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
        let script = manifest_dir
            .parent()
            .unwrap()
            .parent()
            .unwrap()
            .join("scripts")
            .join("wixen.ps1");
        assert!(script.exists(), "PowerShell integration script must exist");
        let content = std::fs::read_to_string(&script).unwrap();
        // PowerShell script uses OSC133 variable, so check for the markers
        assert!(content.contains("OSC133"), "Script must reference OSC 133");
        assert!(content.contains("}A"), "Script must emit prompt start (A)");
        assert!(content.contains("}B"), "Script must emit command start (B)");
        assert!(content.contains("}C"), "Script must emit pre-command (C)");
        assert!(
            content.contains("}D"),
            "Script must emit command complete (D)"
        );
    }

    #[test]
    fn test_clink_script_exists() {
        let manifest_dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
        let script = manifest_dir
            .parent()
            .unwrap()
            .parent()
            .unwrap()
            .join("scripts")
            .join("wixen.lua");
        assert!(script.exists(), "Clink integration script must exist");
        let content = std::fs::read_to_string(&script).unwrap();
        assert!(content.contains("133;"), "Lua script must emit OSC 133");
    }

    // ── Command completion notification tests ──

    #[test]
    fn test_completion_fields_populated_correctly() {
        let completion = CommandCompletion {
            command: Some("cargo build".to_string()),
            exit_code: Some(0),
            duration: Duration::from_secs(15),
            block_id: 42,
        };
        assert_eq!(completion.command.as_deref(), Some("cargo build"));
        assert_eq!(completion.exit_code, Some(0));
        assert_eq!(completion.duration, Duration::from_secs(15));
        assert_eq!(completion.block_id, 42);
    }

    #[test]
    fn test_drain_completions_returns_all() {
        let mut si = ShellIntegration::new();

        // Push completions directly for deterministic testing
        si.pending_completions.push(CommandCompletion {
            command: Some("echo fast".to_string()),
            exit_code: Some(0),
            duration: Duration::from_millis(50),
            block_id: 1,
        });
        si.pending_completions.push(CommandCompletion {
            command: Some("sleep 30".to_string()),
            exit_code: Some(0),
            duration: Duration::from_secs(30),
            block_id: 2,
        });

        let drained = si.drain_completions();
        assert_eq!(drained.len(), 2);
        assert_eq!(drained[0].block_id, 1);
        assert_eq!(drained[1].block_id, 2);

        // Pending list is now empty
        assert!(si.drain_completions().is_empty());
    }

    #[test]
    fn test_take_completion_if_long_filters_short_commands() {
        let mut si = ShellIntegration::new();
        let threshold = Duration::from_secs(10);

        si.pending_completions.push(CommandCompletion {
            command: Some("echo hi".to_string()),
            exit_code: Some(0),
            duration: Duration::from_millis(200),
            block_id: 1,
        });
        si.pending_completions.push(CommandCompletion {
            command: Some("cargo build".to_string()),
            exit_code: Some(0),
            duration: Duration::from_secs(45),
            block_id: 2,
        });
        si.pending_completions.push(CommandCompletion {
            command: Some("ls".to_string()),
            exit_code: Some(0),
            duration: Duration::from_secs(1),
            block_id: 3,
        });

        let long_ones = si.take_completion_if_long(threshold);
        assert_eq!(long_ones.len(), 1);
        assert_eq!(long_ones[0].block_id, 2);
        assert_eq!(long_ones[0].duration, Duration::from_secs(45));

        // The list should now be empty (short ones discarded, long ones returned)
        assert!(si.drain_completions().is_empty());
    }

    #[test]
    fn test_multiple_completions_accumulate() {
        let mut si = ShellIntegration::new();

        si.pending_completions.push(CommandCompletion {
            command: Some("cmd1".to_string()),
            exit_code: Some(0),
            duration: Duration::from_secs(1),
            block_id: 1,
        });
        si.pending_completions.push(CommandCompletion {
            command: Some("cmd2".to_string()),
            exit_code: Some(1),
            duration: Duration::from_secs(2),
            block_id: 2,
        });
        si.pending_completions.push(CommandCompletion {
            command: Some("cmd3".to_string()),
            exit_code: None,
            duration: Duration::from_secs(3),
            block_id: 3,
        });

        assert_eq!(si.pending_completions.len(), 3);
        let all = si.drain_completions();
        assert_eq!(all.len(), 3);
        assert_eq!(all[0].command.as_deref(), Some("cmd1"));
        assert_eq!(all[1].command.as_deref(), Some("cmd2"));
        assert_eq!(all[2].command.as_deref(), Some("cmd3"));
    }

    #[test]
    fn test_osc133_cycle_records_completion() {
        let mut si = ShellIntegration::new();

        si.handle_osc133("A", "", 0);
        si.handle_osc133("B", "", 1);

        // Set command text before execution
        if let Some(block) = si.current_block_mut() {
            block.command_text = Some("cargo test".to_string());
        }

        si.handle_osc133("C", "", 2);

        // started_at should now be set
        assert!(si.current_block().unwrap().started_at.is_some());

        si.handle_osc133("D", "0", 5);

        // A completion should have been recorded
        let completions = si.drain_completions();
        assert_eq!(completions.len(), 1);
        assert_eq!(completions[0].command.as_deref(), Some("cargo test"));
        assert_eq!(completions[0].exit_code, Some(0));
        assert_eq!(completions[0].block_id, 1);
        // Duration should be very small since it's instant
        assert!(completions[0].duration < Duration::from_secs(1));
    }

    #[test]
    fn test_started_at_none_before_executing() {
        let mut si = ShellIntegration::new();
        si.handle_osc133("A", "", 0);
        assert!(si.current_block().unwrap().started_at.is_none());

        si.handle_osc133("B", "", 1);
        assert!(si.current_block().unwrap().started_at.is_none());
    }

    #[test]
    fn test_powershell_script_osc7() {
        let manifest_dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
        let script = manifest_dir
            .parent()
            .unwrap()
            .parent()
            .unwrap()
            .join("scripts")
            .join("wixen.ps1");
        let content = std::fs::read_to_string(&script).unwrap();
        assert!(content.contains("]7;"), "Script must emit OSC 7 for CWD");
        assert!(
            content.contains("WIXEN_TERMINAL"),
            "Script must check WIXEN_TERMINAL env var"
        );
    }

    // ── command_summary tests ──

    fn make_block(
        command_text: Option<&str>,
        exit_code: Option<i32>,
        lines: usize,
    ) -> CommandBlock {
        CommandBlock {
            id: 1,
            prompt: None,
            input: None,
            output: None,
            exit_code,
            cwd: None,
            command_text: command_text.map(String::from),
            state: BlockState::Completed,
            started_at: None,
            output_line_count: lines,
        }
    }

    #[test]
    fn test_summary_success_with_command() {
        let block = make_block(Some("cargo build"), Some(0), 42);
        assert_eq!(
            command_summary(&block),
            "cargo build completed: 42 lines of output"
        );
    }

    #[test]
    fn test_summary_failure_with_command() {
        let block = make_block(Some("cargo test"), Some(1), 100);
        assert_eq!(
            command_summary(&block),
            "cargo test failed (exit 1): 100 lines of output"
        );
    }

    #[test]
    fn test_summary_failure_negative_exit_code() {
        let block = make_block(Some("segfaulter"), Some(-11), 3);
        assert_eq!(
            command_summary(&block),
            "segfaulter failed (exit -11): 3 lines of output"
        );
    }

    #[test]
    fn test_summary_no_command_text() {
        let block = make_block(None, Some(0), 7);
        assert_eq!(command_summary(&block), "Command completed: 7 lines");
    }

    #[test]
    fn test_summary_no_command_text_no_exit_code() {
        let block = make_block(None, None, 0);
        assert_eq!(command_summary(&block), "Command completed: 0 lines");
    }

    #[test]
    fn test_summary_success_no_exit_code() {
        // exit_code None is treated as success
        let block = make_block(Some("echo hi"), None, 1);
        assert_eq!(
            command_summary(&block),
            "echo hi completed: 1 lines of output"
        );
    }

    #[test]
    fn test_summary_zero_lines() {
        let block = make_block(Some("true"), Some(0), 0);
        assert_eq!(command_summary(&block), "true completed: 0 lines of output");
    }

    #[test]
    fn test_output_line_count_field_initialized() {
        let mut si = ShellIntegration::new();
        si.handle_osc133("A", "", 0);
        assert_eq!(si.current_block().unwrap().output_line_count, 0);
    }

    // --- OSC 133 command text sanitization tests ---

    #[test]
    fn test_sanitize_plain_text_unchanged() {
        assert_eq!(
            sanitize_command_text("cargo build --release"),
            "cargo build --release"
        );
    }

    #[test]
    fn test_sanitize_strips_ansi_escape_sequences() {
        // ESC[31m = red color, ESC[0m = reset
        let input = "\x1b[31mERROR\x1b[0m: something failed";
        assert_eq!(sanitize_command_text(input), "ERROR: something failed");
    }

    #[test]
    fn test_sanitize_replaces_control_characters() {
        // \x01 (SOH) and \x02 (STX) should become spaces, but \t \n \r preserved
        let input = "hello\x01world\x02test\tok\nnewline\rcarriage";
        let result = sanitize_command_text(input);
        assert_eq!(result, "hello world test\tok\nnewline\rcarriage");
    }

    #[test]
    fn test_sanitize_truncates_long_text() {
        let long = "a".repeat(2000);
        let result = sanitize_command_text(&long);
        assert_eq!(result.len(), 1024);
    }

    #[test]
    fn test_sanitize_combined() {
        // Contains ANSI, control chars, and is long
        let mut input = format!("\x1b[1mBOLD\x1b[0m\x01hidden");
        input.push_str(&"x".repeat(2000));
        let result = sanitize_command_text(&input);
        assert!(result.starts_with("BOLD hidden"));
        assert_eq!(result.len(), 1024);
        assert!(!result.contains("\x1b"));
    }
}

#[cfg(test)]
mod proptests {
    use super::*;
    use proptest::prelude::*;

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(5_000))]

        /// handle_osc133 must never panic regardless of marker or params content.
        #[test]
        fn osc133_never_panics(
            marker in ".*",
            params in ".*",
            cursor_row in 0usize..100_000,
        ) {
            let mut si = ShellIntegration::new();
            si.handle_osc133(&marker, &params, cursor_row);
        }

        /// handle_osc7 must never panic on arbitrary URIs.
        #[test]
        fn osc7_never_panics(uri in ".*") {
            let mut si = ShellIntegration::new();
            si.handle_osc7(&uri);
        }

        /// A full cycle (A→B→C→D) at arbitrary rows must always produce exactly one block.
        #[test]
        fn full_cycle_produces_one_block(
            row_a in 0usize..10_000,
            row_gap1 in 1usize..100,
            row_gap2 in 1usize..100,
            row_gap3 in 1usize..1000,
            exit_code in -128i32..128,
        ) {
            let mut si = ShellIntegration::new();
            let row_b = row_a + row_gap1;
            let row_c = row_b + row_gap2;
            let row_d = row_c + row_gap3;

            si.handle_osc133("A", "", row_a);
            si.handle_osc133("B", "", row_b);
            si.handle_osc133("C", "", row_c);
            si.handle_osc133("D", &exit_code.to_string(), row_d);

            prop_assert_eq!(si.blocks().len(), 1);
            let block = &si.blocks()[0];
            prop_assert_eq!(block.state, BlockState::Completed);
            prop_assert_eq!(block.exit_code, Some(exit_code));
        }

        /// Prune must never leave more than max_blocks.
        #[test]
        fn prune_enforces_limit(
            block_count in 1usize..200,
            max_blocks in 1usize..100,
        ) {
            let mut si = ShellIntegration::new();
            for i in 0..block_count {
                si.handle_osc133("A", "", i * 5);
                si.handle_osc133("D", "0", i * 5 + 3);
            }
            si.prune(max_blocks);
            prop_assert!(si.len() <= max_blocks);
        }

        /// Generation counter must increase monotonically with each valid marker.
        #[test]
        fn generation_monotonically_increases(cycle_count in 1usize..20) {
            let mut si = ShellIntegration::new();
            let mut prev_generation = si.generation();
            for i in 0..cycle_count {
                let base = i * 10;
                for (marker, offset) in [("A", 0), ("B", 1), ("C", 2), ("D", 5)] {
                    si.handle_osc133(marker, if marker == "D" { "0" } else { "" }, base + offset);
                    let current = si.generation();
                    prop_assert!(current > prev_generation, "Generation must increase: {} > {}", current, prev_generation);
                    prev_generation = current;
                }
            }
        }
    }
}
