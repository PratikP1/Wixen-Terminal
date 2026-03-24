//! Audio feedback system — configurable beeps and tones for terminal events.
//!
//! Provides non-speech audio cues that work without a screen reader. Users can
//! enable audio feedback for progress updates, error detection, mode changes,
//! and command completion. All tones use the Windows `Beep()` API.

use std::collections::HashMap;

/// A single tone: frequency in Hz and duration in milliseconds.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Tone {
    /// Frequency in Hz (37–32767 on Windows).
    pub freq: u32,
    /// Duration in milliseconds.
    pub duration_ms: u32,
}

impl Tone {
    pub const fn new(freq: u32, duration_ms: u32) -> Self {
        Self { freq, duration_ms }
    }
}

/// Predefined tone patterns for common events.
pub mod tones {
    use super::Tone;

    /// Short high beep for successful command completion.
    pub const SUCCESS: Tone = Tone::new(800, 80);

    /// Low tone for command failure.
    pub const ERROR: Tone = Tone::new(220, 150);

    /// Medium tone for warnings.
    pub const WARNING: Tone = Tone::new(440, 100);

    /// Brief tick for progress updates.
    pub const PROGRESS_TICK: Tone = Tone::new(600, 30);

    /// Rising tone for progress completion (100%).
    pub const PROGRESS_COMPLETE: Tone = Tone::new(1000, 120);

    /// Click for mode toggle on.
    pub const MODE_ON: Tone = Tone::new(700, 50);

    /// Lower click for mode toggle off.
    pub const MODE_OFF: Tone = Tone::new(400, 50);

    /// Brief blip for echo suppression detected (password prompt).
    pub const PASSWORD_PROMPT: Tone = Tone::new(500, 60);

    /// Soft ping for tab/pane switch.
    pub const NAVIGATION: Tone = Tone::new(650, 40);

    /// Very brief tick for selection change.
    pub const SELECTION: Tone = Tone::new(550, 25);

    /// Low tone for history boundary (oldest/newest command).
    pub const HISTORY_BOUNDARY: Tone = Tone::new(350, 60);

    /// High snappy tone for edit boundary (start/end of line).
    pub const EDIT_BOUNDARY: Tone = Tone::new(900, 30);
}

/// Which audio events are enabled.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AudioConfig {
    /// Master switch for all audio feedback.
    pub enabled: bool,
    /// Per-event enable flags.
    pub events: HashMap<AudioEvent, bool>,
}

/// Categories of audio events that can be individually toggled.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum AudioEvent {
    /// Command completed successfully (exit 0).
    CommandSuccess,
    /// Command failed (exit != 0).
    CommandError,
    /// Warning detected in output.
    OutputWarning,
    /// Progress bar update.
    Progress,
    /// Progress reached 100%.
    ProgressComplete,
    /// Mode toggled (read-only, broadcast, zoom).
    ModeToggle,
    /// Password/no-echo prompt detected.
    PasswordPrompt,
    /// Tab or pane switched.
    Navigation,
    /// Text selection changed.
    Selection,
    /// Hit boundary of command history (oldest or newest entry).
    HistoryBoundary,
    /// Hit boundary of editable text (start or end of line).
    EditBoundary,
}

impl Default for AudioConfig {
    fn default() -> Self {
        let mut events = HashMap::new();
        // All events enabled by default when audio is on
        for event in [
            AudioEvent::CommandSuccess,
            AudioEvent::CommandError,
            AudioEvent::OutputWarning,
            AudioEvent::Progress,
            AudioEvent::ProgressComplete,
            AudioEvent::ModeToggle,
            AudioEvent::PasswordPrompt,
            AudioEvent::Navigation,
            AudioEvent::Selection,
            AudioEvent::HistoryBoundary,
            AudioEvent::EditBoundary,
        ] {
            events.insert(event, true);
        }
        Self {
            enabled: false, // Off by default — user opts in
            events,
        }
    }
}

impl AudioConfig {
    /// Check if a specific event type should produce audio.
    pub fn should_play(&self, event: AudioEvent) -> bool {
        self.enabled && self.events.get(&event).copied().unwrap_or(false)
    }

    /// Get the tone for an event.
    pub fn tone_for(&self, event: AudioEvent) -> Tone {
        match event {
            AudioEvent::CommandSuccess => tones::SUCCESS,
            AudioEvent::CommandError => tones::ERROR,
            AudioEvent::OutputWarning => tones::WARNING,
            AudioEvent::Progress => tones::PROGRESS_TICK,
            AudioEvent::ProgressComplete => tones::PROGRESS_COMPLETE,
            AudioEvent::ModeToggle => tones::MODE_ON,
            AudioEvent::PasswordPrompt => tones::PASSWORD_PROMPT,
            AudioEvent::Navigation => tones::NAVIGATION,
            AudioEvent::Selection => tones::SELECTION,
            AudioEvent::HistoryBoundary => tones::HISTORY_BOUNDARY,
            AudioEvent::EditBoundary => tones::EDIT_BOUNDARY,
        }
    }
}

/// Play a tone using the Windows Beep API.
///
/// On non-Windows platforms this is a no-op.
#[cfg(windows)]
pub fn play_tone(tone: Tone) {
    use windows::Win32::System::Diagnostics::Debug::Beep;
    unsafe {
        let _ = Beep(tone.freq, tone.duration_ms);
    }
}

/// Non-Windows stub — audio feedback requires Windows Beep API.
#[cfg(not(windows))]
pub fn play_tone(_tone: Tone) {}

/// Play audio feedback for an event if enabled in config.
pub fn play_event(config: &AudioConfig, event: AudioEvent) {
    if config.should_play(event) {
        play_tone(config.tone_for(event));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_config_audio_disabled() {
        let config = AudioConfig::default();
        assert!(!config.enabled);
        assert!(!config.should_play(AudioEvent::CommandSuccess));
    }

    #[test]
    fn enabled_config_plays_events() {
        let mut config = AudioConfig::default();
        config.enabled = true;
        assert!(config.should_play(AudioEvent::CommandSuccess));
        assert!(config.should_play(AudioEvent::CommandError));
        assert!(config.should_play(AudioEvent::Progress));
    }

    #[test]
    fn individual_event_disable() {
        let mut config = AudioConfig::default();
        config.enabled = true;
        config.events.insert(AudioEvent::Progress, false);
        assert!(!config.should_play(AudioEvent::Progress));
        assert!(config.should_play(AudioEvent::CommandSuccess));
    }

    #[test]
    fn tone_for_returns_correct_tones() {
        let config = AudioConfig::default();
        assert_eq!(config.tone_for(AudioEvent::CommandSuccess), tones::SUCCESS);
        assert_eq!(config.tone_for(AudioEvent::CommandError), tones::ERROR);
        assert_eq!(config.tone_for(AudioEvent::Progress), tones::PROGRESS_TICK);
        assert_eq!(
            config.tone_for(AudioEvent::ProgressComplete),
            tones::PROGRESS_COMPLETE
        );
    }

    #[test]
    fn tone_frequencies_are_valid() {
        // Windows Beep API requires 37-32767 Hz
        let all_tones = [
            tones::SUCCESS,
            tones::ERROR,
            tones::WARNING,
            tones::PROGRESS_TICK,
            tones::PROGRESS_COMPLETE,
            tones::MODE_ON,
            tones::MODE_OFF,
            tones::PASSWORD_PROMPT,
            tones::NAVIGATION,
            tones::SELECTION,
            tones::HISTORY_BOUNDARY,
            tones::EDIT_BOUNDARY,
        ];
        for tone in &all_tones {
            assert!(
                tone.freq >= 37 && tone.freq <= 32767,
                "Tone freq {} out of Beep API range",
                tone.freq
            );
            assert!(tone.duration_ms > 0 && tone.duration_ms <= 1000);
        }
    }

    #[test]
    fn play_event_noop_when_disabled() {
        let config = AudioConfig::default();
        // Should not panic even when disabled
        play_event(&config, AudioEvent::CommandSuccess);
    }

    #[test]
    fn mode_toggle_tones_differ() {
        assert_ne!(tones::MODE_ON, tones::MODE_OFF);
    }

    #[test]
    fn all_audio_events_have_defaults() {
        let config = AudioConfig::default();
        for event in [
            AudioEvent::CommandSuccess,
            AudioEvent::CommandError,
            AudioEvent::OutputWarning,
            AudioEvent::Progress,
            AudioEvent::ProgressComplete,
            AudioEvent::ModeToggle,
            AudioEvent::PasswordPrompt,
            AudioEvent::Navigation,
            AudioEvent::Selection,
            AudioEvent::HistoryBoundary,
            AudioEvent::EditBoundary,
        ] {
            assert!(
                config.events.contains_key(&event),
                "Missing default for {:?}",
                event
            );
        }
    }

    #[test]
    fn boundary_tones_are_distinct() {
        let config = AudioConfig::default();
        assert_ne!(
            config.tone_for(AudioEvent::HistoryBoundary),
            config.tone_for(AudioEvent::EditBoundary),
        );
    }

    #[test]
    fn boundary_events_enabled_by_default() {
        let mut config = AudioConfig::default();
        config.enabled = true;
        assert!(config.should_play(AudioEvent::HistoryBoundary));
        assert!(config.should_play(AudioEvent::EditBoundary));
    }
}
