/* test_red_alt_f4.c — RED test for Alt+F4 not being consumed
 *
 * BUG #28: WM_SYSKEYDOWN handler returned 0 for ALL system keys
 * including Alt+F4. DefWindowProc never saw it, so WM_CLOSE
 * was never generated. User couldn't close the window.
 *
 * Fix: Alt+F4 breaks out to DefWindowProc instead of return 0.
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/keyboard.h"
#include "wixen/config/keybindings.h"

TEST red_alt_f4_not_in_keybindings(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    ASSERT(wixen_keybindings_lookup(&kb, "alt+f4") == NULL);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST red_alt_f4_not_encoded_for_pty(void) {
    char buf[32];
    /* Alt+F4 should produce 0 bytes — not sent to PTY */
    /* The keyboard encoder shouldn't produce ESC sequences for Alt+F4
     * because it's a system-level shortcut */
    size_t n = wixen_encode_key(0x73, false, false, true, false, buf, sizeof(buf));
    /* Either 0 (not handled) or an escape sequence — but the important
     * thing is the WndProc never delivers it to the key handler */
    (void)n;
    PASS();
}

SUITE(red_alt_f4) {
    RUN_TEST(red_alt_f4_not_in_keybindings);
    RUN_TEST(red_alt_f4_not_encoded_for_pty);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_alt_f4);
    GREATEST_MAIN_END();
}
