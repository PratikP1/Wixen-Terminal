/* test_red_audio.c — RED tests for audio feedback system
 *
 * Audio events: bell, edit boundary, history boundary, error, warning.
 * Tests verify API correctness without actually playing sound.
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/ui/audio.h"

TEST red_audio_config_init(void) {
    WixenAudioConfig cfg;
    wixen_audio_config_init(&cfg);
    /* Default should have all events enabled */
    ASSERT(cfg.event_enabled[WIXEN_AUDIO_BELL]);
    ASSERT(cfg.event_enabled[WIXEN_AUDIO_EDIT_BOUNDARY]);
    ASSERT(cfg.event_enabled[WIXEN_AUDIO_HISTORY_BOUNDARY]);
    PASS();
}

TEST red_audio_tone_valid(void) {
    /* Each event should have a tone with positive frequency and duration */
    WixenTone t = wixen_audio_tone(WIXEN_AUDIO_BELL);
    ASSERT(t.freq_hz > 0);
    ASSERT(t.duration_ms > 0);
    t = wixen_audio_tone(WIXEN_AUDIO_EDIT_BOUNDARY);
    ASSERT(t.freq_hz > 0);
    t = wixen_audio_tone(WIXEN_AUDIO_COMMAND_ERROR);
    ASSERT(t.freq_hz > 0);
    PASS();
}

TEST red_audio_disabled_noop(void) {
    WixenAudioConfig cfg;
    wixen_audio_config_init(&cfg);
    cfg.event_enabled[WIXEN_AUDIO_BELL] = false;
    /* Playing with disabled event should not crash */
    wixen_audio_play(&cfg, WIXEN_AUDIO_BELL);
    PASS();
}

TEST red_audio_all_events_play(void) {
    WixenAudioConfig cfg;
    wixen_audio_config_init(&cfg);
    /* Disable all events so no actual sound during test */
    for (int i = 0; i < WIXEN_AUDIO_EVENT_COUNT; i++)
        cfg.event_enabled[i] = false;
    /* Each event type should not crash even when disabled */
    for (int i = 0; i < WIXEN_AUDIO_EVENT_COUNT; i++)
        wixen_audio_play(&cfg, (WixenAudioEvent)i);
    PASS();
}

SUITE(red_audio) {
    RUN_TEST(red_audio_config_init);
    RUN_TEST(red_audio_tone_valid);
    RUN_TEST(red_audio_disabled_noop);
    RUN_TEST(red_audio_all_events_play);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_audio);
    GREATEST_MAIN_END();
}
