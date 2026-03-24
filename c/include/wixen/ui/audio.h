/* audio.h — Audio feedback for terminal events */
#ifndef WIXEN_UI_AUDIO_H
#define WIXEN_UI_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t freq_hz;       /* 37-32767 */
    uint32_t duration_ms;
} WixenTone;

typedef enum {
    WIXEN_AUDIO_COMMAND_SUCCESS = 0,
    WIXEN_AUDIO_COMMAND_ERROR,
    WIXEN_AUDIO_OUTPUT_WARNING,
    WIXEN_AUDIO_PROGRESS,
    WIXEN_AUDIO_PROGRESS_COMPLETE,
    WIXEN_AUDIO_MODE_TOGGLE,
    WIXEN_AUDIO_PASSWORD_PROMPT,
    WIXEN_AUDIO_NAVIGATION,
    WIXEN_AUDIO_SELECTION,
    WIXEN_AUDIO_HISTORY_BOUNDARY,
    WIXEN_AUDIO_EDIT_BOUNDARY,
    WIXEN_AUDIO_BELL,
    WIXEN_AUDIO_EVENT_COUNT,
} WixenAudioEvent;

typedef struct {
    bool enabled;
    bool event_enabled[WIXEN_AUDIO_EVENT_COUNT];
} WixenAudioConfig;

/* Initialize with all events enabled */
void wixen_audio_config_init(WixenAudioConfig *cfg);

/* Get the tone for an event */
WixenTone wixen_audio_tone(WixenAudioEvent event);

/* Play an audio event (Windows Beep) */
void wixen_audio_play(const WixenAudioConfig *cfg, WixenAudioEvent event);

#endif /* WIXEN_UI_AUDIO_H */
