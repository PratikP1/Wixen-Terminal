/* test_red_pty_post.c — RED tests for wixen_pty_post_output ownership semantics
 *
 * P0.4: The reader thread allocates WixenPtyOutputEvent + evt->data and posts
 * via PostMessageW, but never checks the return value.  If the window is
 * destroyed during shutdown the allocations leak.
 *
 * The helper wixen_pty_post_output must:
 *   - On success (post_fn returns TRUE): transfer ownership to receiver
 *   - On failure (post_fn returns FALSE): free evt->data and evt
 *   - On NULL hwnd: return false immediately, no allocations
 *   - On zero-length data: handle safely (return false, no crash)
 *   - Always call the injected post_fn, never PostMessageW directly
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "greatest.h"
#include "wixen/pty/pty.h"

#ifdef _WIN32

/* ---------- mock infrastructure ---------- */

static int g_post_call_count;
static HWND g_last_hwnd;
static UINT g_last_msg;
static LPARAM g_last_lparam;
static BOOL g_mock_return;

static void mock_reset(BOOL ret) {
    g_post_call_count = 0;
    g_last_hwnd = NULL;
    g_last_msg = 0;
    g_last_lparam = 0;
    g_mock_return = ret;
}

/* Mock that records its arguments and returns the configured value */
static BOOL WINAPI mock_post(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    (void)wp;
    g_post_call_count++;
    g_last_hwnd = hwnd;
    g_last_msg = msg;
    g_last_lparam = lp;
    return g_mock_return;
}

/* ---------- tests ---------- */

TEST red_post_success_transfers_ownership(void) {
    /* When post_fn returns TRUE the event pointer must have been posted
     * and ownership transferred — caller must NOT free it. */
    mock_reset(TRUE);
    HWND fake = (HWND)(uintptr_t)0xBEEF;
    const uint8_t payload[] = { 'H', 'e', 'l', 'l', 'o' };

    bool ok = wixen_pty_post_output(fake, payload, sizeof(payload),
                                    (WixenPostFn)mock_post);
    ASSERT(ok);
    ASSERT_EQ(1, g_post_call_count);
    ASSERT_EQ(fake, g_last_hwnd);
    ASSERT_EQ((UINT)WM_PTY_OUTPUT, g_last_msg);

    /* The LPARAM should be a valid WixenPtyOutputEvent pointer */
    WixenPtyOutputEvent *evt = (WixenPtyOutputEvent *)g_last_lparam;
    ASSERT(evt != NULL);
    ASSERT(evt->data != NULL);
    ASSERT_EQ(sizeof(payload), evt->len);
    ASSERT_MEM_EQ(payload, evt->data, sizeof(payload));

    /* Clean up — in production the WM_PTY_OUTPUT handler does this */
    free(evt->data);
    free(evt);
    PASS();
}

TEST red_post_failure_frees_both(void) {
    /* When post_fn returns FALSE, the helper must free evt->data and evt.
     * We can only verify it doesn't crash and returns false — no double-free. */
    mock_reset(FALSE);
    HWND fake = (HWND)(uintptr_t)0xDEAD;
    const uint8_t payload[] = { 0x01, 0x02, 0x03 };

    bool ok = wixen_pty_post_output(fake, payload, sizeof(payload),
                                    (WixenPostFn)mock_post);
    ASSERT(!ok);
    ASSERT_EQ(1, g_post_call_count);

    /* The helper freed both allocations — if it didn't, ASAN/valgrind catches it.
     * We also must NOT free g_last_lparam here (it should already be freed). */
    PASS();
}

TEST red_post_null_hwnd_returns_false(void) {
    /* NULL hwnd must return false immediately without calling post_fn */
    mock_reset(TRUE);
    const uint8_t payload[] = { 'X' };

    bool ok = wixen_pty_post_output(NULL, payload, sizeof(payload),
                                    (WixenPostFn)mock_post);
    ASSERT(!ok);
    ASSERT_EQ(0, g_post_call_count);
    PASS();
}

TEST red_post_zero_length_safe(void) {
    /* Zero-length data should return false (nothing useful to post) */
    mock_reset(TRUE);
    HWND fake = (HWND)(uintptr_t)0xCAFE;
    const uint8_t payload[] = { 0xFF };

    bool ok = wixen_pty_post_output(fake, payload, 0,
                                    (WixenPostFn)mock_post);
    ASSERT(!ok);
    ASSERT_EQ(0, g_post_call_count);
    PASS();
}

TEST red_post_uses_injected_fn(void) {
    /* The helper must call the provided post_fn, not PostMessageW directly.
     * We verify this by checking our mock was called exactly once. */
    mock_reset(TRUE);
    HWND fake = (HWND)(uintptr_t)0xF00D;
    const uint8_t payload[] = { 'A', 'B' };

    bool ok = wixen_pty_post_output(fake, payload, sizeof(payload),
                                    (WixenPostFn)mock_post);
    ASSERT(ok);
    ASSERT_EQ(1, g_post_call_count);

    /* Cleanup transferred event */
    WixenPtyOutputEvent *evt = (WixenPtyOutputEvent *)g_last_lparam;
    free(evt->data);
    free(evt);
    PASS();
}

SUITE(red_pty_post) {
    RUN_TEST(red_post_success_transfers_ownership);
    RUN_TEST(red_post_failure_frees_both);
    RUN_TEST(red_post_null_hwnd_returns_false);
    RUN_TEST(red_post_zero_length_safe);
    RUN_TEST(red_post_uses_injected_fn);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_pty_post);
    GREATEST_MAIN_END();
}

#else
#include "greatest.h"
GREATEST_MAIN_DEFS();
int main(int argc, char **argv) { GREATEST_MAIN_BEGIN(); GREATEST_MAIN_END(); }
#endif
