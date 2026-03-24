/* audio.c — Audio feedback using Windows Beep() */
#include "wixen/ui/audio.h"
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* Predefined tones */
static const WixenTone tones[WIXEN_AUDIO_EVENT_COUNT] = {
    [WIXEN_AUDIO_COMMAND_SUCCESS]    = { 800,  80 },
    [WIXEN_AUDIO_COMMAND_ERROR]      = { 220, 150 },
    [WIXEN_AUDIO_OUTPUT_WARNING]     = { 440, 100 },
    [WIXEN_AUDIO_PROGRESS]           = { 600,  30 },
    [WIXEN_AUDIO_PROGRESS_COMPLETE]  = { 1000, 120 },
    [WIXEN_AUDIO_MODE_TOGGLE]        = { 700,  50 },
    [WIXEN_AUDIO_PASSWORD_PROMPT]    = { 500,  60 },
    [WIXEN_AUDIO_NAVIGATION]         = { 650,  40 },
    [WIXEN_AUDIO_SELECTION]          = { 550,  25 },
    [WIXEN_AUDIO_HISTORY_BOUNDARY]   = { 350,  60 },
    [WIXEN_AUDIO_EDIT_BOUNDARY]      = { 900,  30 },
    [WIXEN_AUDIO_BELL]               = { 440,  80 },
};

void wixen_audio_config_init(WixenAudioConfig *cfg) {
    cfg->enabled = true;
    for (int i = 0; i < WIXEN_AUDIO_EVENT_COUNT; i++) {
        cfg->event_enabled[i] = true;
    }
}

WixenTone wixen_audio_tone(WixenAudioEvent event) {
    if (event >= 0 && event < WIXEN_AUDIO_EVENT_COUNT)
        return tones[event];
    return (WixenTone){ 440, 50 }; /* Default fallback */
}

void wixen_audio_play(const WixenAudioConfig *cfg, WixenAudioEvent event) {
    if (!cfg->enabled) return;
    if (event < 0 || event >= WIXEN_AUDIO_EVENT_COUNT) return;
    if (!cfg->event_enabled[event]) return;

    WixenTone tone = tones[event];
#ifdef _WIN32
    Beep(tone.freq_hz, tone.duration_ms);
#else
    (void)tone;
#endif
}
