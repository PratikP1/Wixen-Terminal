/* provider.h — UIA accessibility provider for terminal area
 *
 * Two API levels:
 * 1. High-level convenience functions (no UIA headers needed)
 * 2. Low-level COM provider API (requires uiautomation.h)
 *
 * main.c only uses the high-level API.
 */
#ifndef WIXEN_A11Y_PROVIDER_H
#define WIXEN_A11Y_PROVIDER_H

#ifdef _WIN32

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

/* --- High-level convenience API (no UIA headers needed) --- */

void wixen_a11y_provider_init_minimal(HWND hwnd);
LRESULT wixen_a11y_handle_wm_getobject(HWND hwnd, WPARAM wparam, LPARAM lparam);
void wixen_a11y_provider_init(HWND hwnd, void *terminal);
void wixen_a11y_provider_shutdown(HWND hwnd);
void wixen_a11y_update_cursor(const void *grid);
void wixen_a11y_raise_selection_changed(HWND hwnd);
void wixen_a11y_raise_focus_changed(HWND hwnd);
void wixen_a11y_raise_notification(HWND hwnd, const char *text, const char *activity_id);
void wixen_a11y_state_update_text_global(const char *text, size_t len);
void wixen_a11y_state_update_focus_global(bool has_focus);
void wixen_a11y_state_update_title_global(const wchar_t *title);
void wixen_a11y_raise_structure_changed_global(void);
void wixen_a11y_raise_text_changed_global(void);
void wixen_a11y_set_tree(void *tree); /* Pass WixenA11yTree* */
void wixen_a11y_set_cursor_offset(int32_t utf16_offset);
int32_t wixen_a11y_get_cursor_offset(void);
void wixen_a11y_pump_messages(HWND hwnd);

/* --- Low-level COM provider API (only for provider.c and text_provider.c) --- */
#ifdef WIXEN_A11Y_INTERNAL

#include <uiautomation.h>

typedef struct WixenA11yState WixenA11yState;

IRawElementProviderSimple *wixen_a11y_create_provider(HWND hwnd, WixenA11yState *state);

LRESULT wixen_a11y_handle_getobject(HWND hwnd, WPARAM wparam, LPARAM lparam,
                                     IRawElementProviderSimple *provider);

void wixen_a11y_raise_focus_changed_provider(IRawElementProviderSimple *provider);
void wixen_a11y_raise_text_changed(IRawElementProviderSimple *provider);
void wixen_a11y_raise_structure_changed(IRawElementProviderSimple *provider);

struct WixenA11yState {
    SRWLOCK lock;
    char *full_text;
    size_t full_text_len;
    bool has_focus;
    wchar_t *title;
    size_t cursor_row;
    size_t cursor_col;
    float cell_width;
    float cell_height;
    uint32_t window_width;
    uint32_t window_height;
    HWND hwnd; /* For coordinate conversions (ClientToScreen) */
};

void wixen_a11y_state_init(WixenA11yState *state);
void wixen_a11y_state_free(WixenA11yState *state);
void wixen_a11y_state_update_text(WixenA11yState *state, const char *text, size_t len);
void wixen_a11y_state_update_cursor(WixenA11yState *state, size_t row, size_t col);
void wixen_a11y_state_update_focus(WixenA11yState *state, bool has_focus);

#endif /* WIXEN_A11Y_INTERNAL */

#endif /* _WIN32 */
#endif /* WIXEN_A11Y_PROVIDER_H */
