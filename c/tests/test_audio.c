/* test_audio.c — Tests for audio feedback */
#include <stdbool.h>
#include "greatest.h"
#include "wixen/ui/audio.h"

TEST audio_config_defaults(void) {
    WixenAudioConfig cfg;
    wixen_audio_config_init(&cfg);
    ASSERT(cfg.enabled);
    for (int i = 0; i < WIXEN_AUDIO_EVENT_COUNT; i++) {
        ASSERT(cfg.event_enabled[i]);
    }
    PASS();
}

TEST audio_tones_defined(void) {
    /* All events should have non-zero frequency */
    for (int i = 0; i < WIXEN_AUDIO_EVENT_COUNT; i++) {
        WixenTone t = wixen_audio_tone((WixenAudioEvent)i);
        ASSERT(t.freq_hz > 0);
        ASSERT(t.duration_ms > 0);
    }
    PASS();
}

TEST audio_tone_success(void) {
    WixenTone t = wixen_audio_tone(WIXEN_AUDIO_COMMAND_SUCCESS);
    ASSERT_EQ(800, (int)t.freq_hz);
    ASSERT_EQ(80, (int)t.duration_ms);
    PASS();
}

TEST audio_tone_error(void) {
    WixenTone t = wixen_audio_tone(WIXEN_AUDIO_COMMAND_ERROR);
    ASSERT_EQ(220, (int)t.freq_hz);
    ASSERT_EQ(150, (int)t.duration_ms);
    PASS();
}

TEST audio_tone_boundary(void) {
    WixenTone hist = wixen_audio_tone(WIXEN_AUDIO_HISTORY_BOUNDARY);
    WixenTone edit = wixen_audio_tone(WIXEN_AUDIO_EDIT_BOUNDARY);
    /* History and edit boundaries should have different tones */
    ASSERT(hist.freq_hz != edit.freq_hz || hist.duration_ms != edit.duration_ms);
    PASS();
}

TEST audio_disabled_config(void) {
    WixenAudioConfig cfg;
    wixen_audio_config_init(&cfg);
    cfg.enabled = false;
    /* play should be a no-op (doesn't crash) */
    wixen_audio_play(&cfg, WIXEN_AUDIO_BELL);
    PASS();
}

TEST audio_event_disabled(void) {
    WixenAudioConfig cfg;
    wixen_audio_config_init(&cfg);
    cfg.event_enabled[WIXEN_AUDIO_BELL] = false;
    /* play should be a no-op for disabled event */
    wixen_audio_play(&cfg, WIXEN_AUDIO_BELL);
    PASS();
}

SUITE(audio_tests) {
    RUN_TEST(audio_config_defaults);
    RUN_TEST(audio_tones_defined);
    RUN_TEST(audio_tone_success);
    RUN_TEST(audio_tone_error);
    RUN_TEST(audio_tone_boundary);
    RUN_TEST(audio_disabled_config);
    RUN_TEST(audio_event_disabled);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(audio_tests);
    GREATEST_MAIN_END();
}
