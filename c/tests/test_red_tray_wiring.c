/* test_red_tray_wiring.c — RED tests for tray icon menu wiring
 *
 * Validates:
 * 1. Tray menu item IDs are distinct and non-zero
 * 2. Tray menu item count matches expected (Show/Hide, New Tab, Settings, Quit)
 * 3. WM_TRAY_CALLBACK is defined correctly
 * 4. WIXEN_EVT_TRAY_COMMAND event type exists and is distinct
 * 5. Tray action IDs don't collide with context menu action IDs
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/ui/tray.h"
#include "wixen/ui/window.h"

/* --- Tray menu item IDs are distinct and non-zero --- */

TEST red_tray_ids_nonzero(void) {
    ASSERT(WIXEN_TRAY_SHOW_HIDE != 0);
    ASSERT(WIXEN_TRAY_NEW_TAB != 0);
    ASSERT(WIXEN_TRAY_SETTINGS != 0);
    ASSERT(WIXEN_TRAY_EXIT != 0);
    PASS();
}

TEST red_tray_ids_distinct(void) {
    ASSERT(WIXEN_TRAY_SHOW_HIDE != WIXEN_TRAY_NEW_TAB);
    ASSERT(WIXEN_TRAY_SHOW_HIDE != WIXEN_TRAY_SETTINGS);
    ASSERT(WIXEN_TRAY_SHOW_HIDE != WIXEN_TRAY_EXIT);
    ASSERT(WIXEN_TRAY_NEW_TAB != WIXEN_TRAY_SETTINGS);
    ASSERT(WIXEN_TRAY_NEW_TAB != WIXEN_TRAY_EXIT);
    ASSERT(WIXEN_TRAY_SETTINGS != WIXEN_TRAY_EXIT);
    PASS();
}

/* --- Tray menu item count matches expected --- */

TEST red_tray_menu_item_count(void) {
    size_t count = 0;
    const WixenTrayMenuItem *items = wixen_tray_menu_items(&count);
    ASSERT(items != NULL);
    ASSERT_EQ(4, (int)count); /* Show/Hide, New Tab, Settings, Quit */
    PASS();
}

TEST red_tray_menu_items_have_labels(void) {
    size_t count = 0;
    const WixenTrayMenuItem *items = wixen_tray_menu_items(&count);
    for (size_t i = 0; i < count; i++) {
        ASSERT(items[i].label != NULL);
        ASSERT(strlen(items[i].label) > 0);
    }
    PASS();
}

TEST red_tray_menu_items_have_action_ids(void) {
    size_t count = 0;
    const WixenTrayMenuItem *items = wixen_tray_menu_items(&count);
    for (size_t i = 0; i < count; i++) {
        ASSERT(items[i].action_id != 0);
    }
    PASS();
}

TEST red_tray_menu_action_ids_match_enum(void) {
    size_t count = 0;
    const WixenTrayMenuItem *items = wixen_tray_menu_items(&count);
    /* Verify the platform-independent items match the enum values */
    ASSERT_EQ(WIXEN_TRAY_SHOW_HIDE, items[0].action_id);
    ASSERT_EQ(WIXEN_TRAY_NEW_TAB, items[1].action_id);
    ASSERT_EQ(WIXEN_TRAY_SETTINGS, items[2].action_id);
    ASSERT_EQ(WIXEN_TRAY_EXIT, items[3].action_id);
    PASS();
}

/* --- WM_TRAY_CALLBACK is defined correctly --- */

TEST red_wm_tray_callback_defined(void) {
    /* WM_TRAY_CALLBACK must be in the WM_APP range (>= 0x8000) */
    ASSERT(WM_TRAY_CALLBACK >= 0x8000);
    /* Must be WM_APP + 10 specifically */
    ASSERT_EQ(WM_TRAY_CALLBACK, (unsigned int)(0x8000 + 10));
    PASS();
}

/* --- WIXEN_EVT_TRAY_COMMAND event type exists --- */

TEST red_tray_event_type_exists(void) {
    /* The tray command event type must exist in the event enum */
    WixenEventType t = WIXEN_EVT_TRAY_COMMAND;
    ASSERT(t != WIXEN_EVT_NONE);
    ASSERT(t != WIXEN_EVT_CONTEXT_MENU);
    ASSERT(t != WIXEN_EVT_CLOSE_REQUESTED);
    PASS();
}

TEST red_tray_event_type_distinct(void) {
    /* Must not collide with any existing event types */
    ASSERT(WIXEN_EVT_TRAY_COMMAND != WIXEN_EVT_RESIZED);
    ASSERT(WIXEN_EVT_TRAY_COMMAND != WIXEN_EVT_KEY_INPUT);
    ASSERT(WIXEN_EVT_TRAY_COMMAND != WIXEN_EVT_CHAR_INPUT);
    ASSERT(WIXEN_EVT_TRAY_COMMAND != WIXEN_EVT_MOUSE_WHEEL);
    ASSERT(WIXEN_EVT_TRAY_COMMAND != WIXEN_EVT_MOUSE_DOWN);
    ASSERT(WIXEN_EVT_TRAY_COMMAND != WIXEN_EVT_MOUSE_UP);
    ASSERT(WIXEN_EVT_TRAY_COMMAND != WIXEN_EVT_MOUSE_MOVE);
    ASSERT(WIXEN_EVT_TRAY_COMMAND != WIXEN_EVT_DOUBLE_CLICK);
    ASSERT(WIXEN_EVT_TRAY_COMMAND != WIXEN_EVT_FOCUS_GAINED);
    ASSERT(WIXEN_EVT_TRAY_COMMAND != WIXEN_EVT_FOCUS_LOST);
    ASSERT(WIXEN_EVT_TRAY_COMMAND != WIXEN_EVT_DPI_CHANGED);
    ASSERT(WIXEN_EVT_TRAY_COMMAND != WIXEN_EVT_FILES_DROPPED);
    PASS();
}

/* --- Tray action IDs don't collide with context menu IDs --- */

TEST red_tray_ids_no_collision_with_context(void) {
    /* Tray IDs use 200+ range, well above the context menu range (1-9) */
    int ctx_max = (int)WIXEN_CTX_SETTINGS;
    ASSERT((int)WIXEN_TRAY_SHOW_HIDE > ctx_max);
    ASSERT((int)WIXEN_TRAY_NEW_TAB > ctx_max);
    ASSERT((int)WIXEN_TRAY_SETTINGS > ctx_max);
    ASSERT((int)WIXEN_TRAY_EXIT > ctx_max);
    int evt_tray = (int)WIXEN_EVT_TRAY_COMMAND;
    int evt_ctx = (int)WIXEN_EVT_CONTEXT_MENU;
    ASSERT(evt_tray != evt_ctx);
    PASS();
}

/* --- Window event union has tray_action field --- */

TEST red_tray_event_carries_action(void) {
    WixenWindowEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = WIXEN_EVT_TRAY_COMMAND;
    evt.tray_action = WIXEN_TRAY_EXIT;
    ASSERT_EQ(WIXEN_TRAY_EXIT, (int)evt.tray_action);
    PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();

    RUN_TEST(red_tray_ids_nonzero);
    RUN_TEST(red_tray_ids_distinct);
    RUN_TEST(red_tray_menu_item_count);
    RUN_TEST(red_tray_menu_items_have_labels);
    RUN_TEST(red_tray_menu_items_have_action_ids);
    RUN_TEST(red_tray_menu_action_ids_match_enum);
    RUN_TEST(red_wm_tray_callback_defined);
    RUN_TEST(red_tray_event_type_exists);
    RUN_TEST(red_tray_event_type_distinct);
    RUN_TEST(red_tray_ids_no_collision_with_context);
    RUN_TEST(red_tray_event_carries_action);

    GREATEST_MAIN_END();
}
