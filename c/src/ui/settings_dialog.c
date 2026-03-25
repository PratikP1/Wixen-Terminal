/* settings_dialog.c — Native Win32 settings dialog using PropertySheet
 *
 * 4-tab layout (reorganized from 7 for less cognitive load):
 *   Appearance   — Font + Colors + Window chrome
 *   Terminal     — Cursor + Bell + Scrollback + Profiles + Renderer
 *   Accessibility — SR verbosity, audio, prompts, motion
 *   Keybindings  — Standalone listbox
 *
 * Uses native Win32 controls for full screen reader accessibility.
 */
#include "wixen/ui/settings_dialog.h"
#include <stddef.h>
#include <string.h>

/* --- Tab configuration (platform-independent, testable) --- */

static const char *tab_names[] = {
    "Appearance", "Terminal", "Accessibility", "Keybindings"
};

static const char *appearance_fields[] = {
    "Font family", "Font size", "Line height",
    "Color theme", "Foreground", "Background", "Cursor color", "Selection",
    "Opacity", "Dark title bar", "Scrollbar", "Tab bar"
};

static const char *terminal_fields[] = {
    "Cursor style", "Cursor blink", "Bell style",
    "Scrollback lines", "Auto-wrap",
    "Default profile", "Renderer"
};

static const char *a11y_fields[] = {
    "Screen reader verbosity", "Output debounce",
    "Announce exit codes", "Prompt detection", "Reduced motion",
    "Announce images", "Audio feedback", "Audio errors"
};

static const char *keybindings_fields[] = {
    "Keybinding list"
};

int wixen_settings_tab_count(void) { return 4; }

const char *wixen_settings_tab_name(int index) {
    if (index < 0 || index >= 4) return NULL;
    return tab_names[index];
}

const char **wixen_settings_tab_fields(int tab_index, size_t *out_count) {
    switch (tab_index) {
    case 0: *out_count = sizeof(appearance_fields) / sizeof(appearance_fields[0]); return appearance_fields;
    case 1: *out_count = sizeof(terminal_fields) / sizeof(terminal_fields[0]); return terminal_fields;
    case 2: *out_count = sizeof(a11y_fields) / sizeof(a11y_fields[0]); return a11y_fields;
    case 3: *out_count = sizeof(keybindings_fields) / sizeof(keybindings_fields[0]); return keybindings_fields;
    default: *out_count = 0; return NULL;
    }
}

/* --- Visual layout parameters --- */

const char *wixen_settings_dialog_font_name(void) { return "Segoe UI"; }
int wixen_settings_dialog_font_size(void) { return 9; }
int wixen_settings_dialog_width(void) { return 440; }
int wixen_settings_dialog_height(void) { return 340; }
int wixen_settings_dialog_margin(void) { return 10; }
int wixen_settings_dialog_control_spacing(void) { return 6; }

static const char *appearance_groups[] = { "Font", "Colors", "Window" };
const char **wixen_settings_appearance_groups(size_t *out_count) {
    *out_count = 3;
    return appearance_groups;
}

/* --- Win32 dialog implementation --- */
#ifdef _WIN32

#include "wixen/ui/settings_dialog.h"
#include <commctrl.h>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")

/* Create the proper dialog font (Segoe UI 9pt) */
static HFONT create_dialog_font(void) {
    return CreateFontW(
        -12, /* 9pt at 96 DPI = -MulDiv(9, 96, 72) = -12 */
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

/* Layout constants matching test expectations */
#define DLG_MARGIN 10
#define DLG_SPACING 6
#define DLG_LABEL_H 18
#define DLG_EDIT_H 22
#define DLG_COMBO_H 22
#define DLG_CHECK_H 20
#define DLG_GROUP_PAD 18

/* Control IDs */
#define IDC_FONT_FAMILY    1001
#define IDC_FONT_SIZE      1002
#define IDC_CURSOR_STYLE   1003
#define IDC_CURSOR_BLINK   1004
#define IDC_BELL_STYLE     1005
#define IDC_AUTO_WRAP      1006
#define IDC_SCROLL_LINES   1007
#define IDC_SR_VERBOSITY   1008
#define IDC_OUTPUT_DEBOUNCE 1009
/* Window tab */
#define IDC_WIN_WIDTH      1010
#define IDC_WIN_HEIGHT     1011
#define IDC_WIN_OPACITY    1012
#define IDC_WIN_RENDERER   1013
#define IDC_WIN_DARK_TITLE 1014
#define IDC_WIN_SCROLLBAR  1015
#define IDC_WIN_TAB_BAR    1016
/* Colors tab */
#define IDC_COLOR_THEME    1020
#define IDC_COLOR_FG       1021
#define IDC_COLOR_BG       1022
#define IDC_COLOR_CURSOR   1023
#define IDC_COLOR_SELECTION 1024
/* Font extras */
#define IDC_FONT_LINE_HEIGHT 1030
/* Terminal extras */
#define IDC_SCROLLBACK_LINES 1031
#define IDC_BLINK_INTERVAL   1032
/* Accessibility extras */
#define IDC_ANNOUNCE_EXIT    1040
#define IDC_LIVE_REGION      1041
#define IDC_PROMPT_DETECT    1042
#define IDC_HIGH_CONTRAST    1043
#define IDC_REDUCED_MOTION   1044
#define IDC_ANNOUNCE_IMAGES  1045
#define IDC_AUDIO_FEEDBACK   1046
#define IDC_AUDIO_ERRORS     1047

/* --- Font Tab --- */

static INT_PTR CALLBACK font_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    (void)wp;
    switch (msg) {
    case WM_INITDIALOG: {
        /* Create controls programmatically */
        HFONT hFont = create_dialog_font();
        int y = 10;

        /* Font family label + edit */
        HWND lbl = CreateWindowExW(0, L"STATIC", L"Font &family:",
            WS_CHILD | WS_VISIBLE, 10, y, 100, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"Cascadia Mono",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            120, y, 200, 22, hwnd, (HMENU)(UINT_PTR)IDC_FONT_FAMILY, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 30;

        /* Font size label + edit */
        lbl = CreateWindowExW(0, L"STATIC", L"Font &size:",
            WS_CHILD | WS_VISIBLE, 10, y, 100, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"14",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
            120, y, 60, 22, hwnd, (HMENU)(UINT_PTR)IDC_FONT_SIZE, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 30;

        /* Line height */
        lbl = CreateWindowExW(0, L"STATIC", L"&Line height:",
            WS_CHILD | WS_VISIBLE, 10, y, 100, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1.2",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            120, y, 60, 22, hwnd, (HMENU)(UINT_PTR)IDC_FONT_LINE_HEIGHT, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 30;

        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->code == PSN_APPLY) {
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

/* --- Window Tab --- */

static INT_PTR CALLBACK window_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    (void)wp;
    switch (msg) {
    case WM_INITDIALOG: {
        HFONT hFont = create_dialog_font();
        int y = 10;

        HWND lbl = CreateWindowExW(0, L"STATIC", L"&Width:",
            WS_CHILD | WS_VISIBLE, 10, y, 80, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1200",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
            100, y, 80, 22, hwnd, (HMENU)(UINT_PTR)IDC_WIN_WIDTH, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 30;

        lbl = CreateWindowExW(0, L"STATIC", L"&Height:",
            WS_CHILD | WS_VISIBLE, 10, y, 80, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"800",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
            100, y, 80, 22, hwnd, (HMENU)(UINT_PTR)IDC_WIN_HEIGHT, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 30;

        lbl = CreateWindowExW(0, L"STATIC", L"&Opacity:",
            WS_CHILD | WS_VISIBLE, 10, y, 80, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1.0",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            100, y, 60, 22, hwnd, (HMENU)(UINT_PTR)IDC_WIN_OPACITY, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 30;

        lbl = CreateWindowExW(0, L"STATIC", L"&Renderer:",
            WS_CHILD | WS_VISIBLE, 10, y, 80, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        HWND combo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            100, y, 150, 100, hwnd, (HMENU)(UINT_PTR)IDC_WIN_RENDERER, NULL, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Auto");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"GPU (D3D11)");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Software (GDI)");
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        y += 30;

        CreateWindowExW(0, L"BUTTON", L"&Dark title bar",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            10, y, 200, 20, hwnd, (HMENU)(UINT_PTR)IDC_WIN_DARK_TITLE, NULL, NULL);
        y += 25;

        lbl = CreateWindowExW(0, L"STATIC", L"&Scrollbar:",
            WS_CHILD | WS_VISIBLE, 10, y, 80, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        combo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            100, y, 150, 100, hwnd, (HMENU)(UINT_PTR)IDC_WIN_SCROLLBAR, NULL, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Visible");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Hidden");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Auto");
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        y += 30;

        lbl = CreateWindowExW(0, L"STATIC", L"Ta&b bar:",
            WS_CHILD | WS_VISIBLE, 10, y, 80, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        combo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            100, y, 150, 100, hwnd, (HMENU)(UINT_PTR)IDC_WIN_TAB_BAR, NULL, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Always");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Multiple tabs");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Never");
        SendMessageW(combo, CB_SETCURSEL, 1, 0);

        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->code == PSN_APPLY) {
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
    (void)wp;
}

/* --- Colors Tab --- */

static INT_PTR CALLBACK colors_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    (void)wp;
    switch (msg) {
    case WM_INITDIALOG: {
        HFONT hFont = create_dialog_font();
        int y = 10;

        HWND lbl = CreateWindowExW(0, L"STATIC", L"Color &theme:",
            WS_CHILD | WS_VISIBLE, 10, y, 100, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        HWND combo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            120, y, 200, 200, hwnd, (HMENU)(UINT_PTR)IDC_COLOR_THEME, NULL, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Default");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Solarized Dark");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Solarized Light");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Dracula");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"One Dark");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Monokai");
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        y += 30;

        lbl = CreateWindowExW(0, L"STATIC", L"&Foreground:",
            WS_CHILD | WS_VISIBLE, 10, y, 100, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"#cccccc",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            120, y, 100, 22, hwnd, (HMENU)(UINT_PTR)IDC_COLOR_FG, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 30;

        lbl = CreateWindowExW(0, L"STATIC", L"&Background:",
            WS_CHILD | WS_VISIBLE, 10, y, 100, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"#1e1e1e",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            120, y, 100, 22, hwnd, (HMENU)(UINT_PTR)IDC_COLOR_BG, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 30;

        lbl = CreateWindowExW(0, L"STATIC", L"&Cursor color:",
            WS_CHILD | WS_VISIBLE, 10, y, 100, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"#ffffff",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            120, y, 100, 22, hwnd, (HMENU)(UINT_PTR)IDC_COLOR_CURSOR, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 30;

        lbl = CreateWindowExW(0, L"STATIC", L"&Selection:",
            WS_CHILD | WS_VISIBLE, 10, y, 100, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"#264f78",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            120, y, 100, 22, hwnd, (HMENU)(UINT_PTR)IDC_COLOR_SELECTION, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);

        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->code == PSN_APPLY) {
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
    (void)wp;
}

/* --- Profiles Tab --- */

static INT_PTR CALLBACK profiles_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    (void)wp;
    switch (msg) {
    case WM_INITDIALOG: {
        HFONT hFont = create_dialog_font();
        int y = 10;

        CreateWindowExW(0, L"STATIC", L"Shell profiles define which programs can be launched as new tabs.",
            WS_CHILD | WS_VISIBLE, 10, y, 350, 20, hwnd, NULL, NULL, NULL);
        y += 30;

        HWND lbl = CreateWindowExW(0, L"STATIC", L"Default &profile:",
            WS_CHILD | WS_VISIBLE, 10, y, 120, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        HWND combo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            140, y, 200, 200, hwnd, (HMENU)2001, NULL, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"PowerShell");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Command Prompt");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Git Bash");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"WSL");
        SendMessageW(combo, CB_SETCURSEL, 0, 0);

        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->code == PSN_APPLY) {
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
    (void)wp;
}

/* --- Keybindings Tab --- */

static INT_PTR CALLBACK keybindings_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    (void)wp;
    switch (msg) {
    case WM_INITDIALOG: {
        HFONT hFont = create_dialog_font();
        int y = 10;

        CreateWindowExW(0, L"STATIC",
            L"Keybindings can be customized in config.toml or via Lua scripts.",
            WS_CHILD | WS_VISIBLE, 10, y, 350, 20, hwnd, NULL, NULL, NULL);
        y += 30;

        /* List of current keybindings (read-only for now) */
        HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
            10, y, 350, 150, hwnd, (HMENU)3001, NULL, NULL);
        SendMessageW(list, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"Ctrl+Shift+P: Command Palette");
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"Ctrl+,: Settings");
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"Ctrl+Shift+T: New Tab");
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"Ctrl+Shift+W: Close Tab");
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"Ctrl+Tab: Next Tab");
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"Ctrl+Shift+Tab: Previous Tab");
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"F11: Toggle Fullscreen");
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"Ctrl+Shift+F: Search");
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"Ctrl+C: Copy");
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"Ctrl+V: Paste");

        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->code == PSN_APPLY) {
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
    (void)wp;
}

/* --- Terminal Tab --- */

static INT_PTR CALLBACK terminal_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        HFONT hFont = create_dialog_font();
        int y = 10;

        /* Cursor style label + combo */
        HWND lbl = CreateWindowExW(0, L"STATIC", L"Cursor &style:",
            WS_CHILD | WS_VISIBLE, 10, y, 100, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        HWND combo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            120, y, 150, 200, hwnd, (HMENU)(UINT_PTR)IDC_CURSOR_STYLE, NULL, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Block");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Underline");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Bar");
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        y += 30;

        /* Cursor blink checkbox */
        HWND chk = CreateWindowExW(0, L"BUTTON", L"Cursor &blinks",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            10, y, 200, 20, hwnd, (HMENU)(UINT_PTR)IDC_CURSOR_BLINK, NULL, NULL);
        SendMessageW(chk, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(chk, BM_SETCHECK, BST_CHECKED, 0);
        y += 30;

        /* Auto-wrap checkbox */
        chk = CreateWindowExW(0, L"BUTTON", L"Auto-&wrap",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            10, y, 200, 20, hwnd, (HMENU)(UINT_PTR)IDC_AUTO_WRAP, NULL, NULL);
        SendMessageW(chk, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(chk, BM_SETCHECK, BST_CHECKED, 0);
        y += 30;

        /* Scrollback lines */
        lbl = CreateWindowExW(0, L"STATIC", L"Scroll&back lines:",
            WS_CHILD | WS_VISIBLE, 10, y, 120, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"10000",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
            140, y, 80, 22, hwnd, (HMENU)(UINT_PTR)IDC_SCROLLBACK_LINES, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 30;

        /* Bell style */
        lbl = CreateWindowExW(0, L"STATIC", L"Be&ll style:",
            WS_CHILD | WS_VISIBLE, 10, y, 100, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        combo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            120, y, 150, 100, hwnd, (HMENU)(UINT_PTR)IDC_BELL_STYLE, NULL, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Audible");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Visual");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Both");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Mute");
        SendMessageW(combo, CB_SETCURSEL, 0, 0);

        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->code == PSN_APPLY) {
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
    (void)wp;
}

/* --- Accessibility Tab --- */

static INT_PTR CALLBACK a11y_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        HFONT hFont = create_dialog_font();
        int y = 10;

        /* Screen reader verbosity */
        HWND lbl = CreateWindowExW(0, L"STATIC", L"Screen reader &verbosity:",
            WS_CHILD | WS_VISIBLE, 10, y, 180, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        HWND combo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            200, y, 120, 200, hwnd, (HMENU)(UINT_PTR)IDC_SR_VERBOSITY, NULL, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"All");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Basic");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"None");
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        y += 30;

        /* Output debounce */
        lbl = CreateWindowExW(0, L"STATIC", L"Output &debounce (ms):",
            WS_CHILD | WS_VISIBLE, 10, y, 180, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"100",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
            200, y, 60, 22, hwnd, (HMENU)(UINT_PTR)IDC_OUTPUT_DEBOUNCE, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 30;

        /* Announce exit codes */
        HWND chk = CreateWindowExW(0, L"BUTTON", L"Announce &exit codes",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            10, y, 250, 20, hwnd, (HMENU)(UINT_PTR)IDC_ANNOUNCE_EXIT, NULL, NULL);
        SendMessageW(chk, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(chk, BM_SETCHECK, BST_CHECKED, 0);
        y += 25;

        /* Prompt detection */
        lbl = CreateWindowExW(0, L"STATIC", L"&Prompt detection:",
            WS_CHILD | WS_VISIBLE, 10, y, 130, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        combo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            150, y, 170, 100, hwnd, (HMENU)(UINT_PTR)IDC_PROMPT_DETECT, NULL, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"OSC 133 + Heuristic");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"OSC 133 Only");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Heuristic Only");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Disabled");
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        y += 30;

        /* Reduced motion */
        lbl = CreateWindowExW(0, L"STATIC", L"Reduced &motion:",
            WS_CHILD | WS_VISIBLE, 10, y, 130, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        combo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            150, y, 120, 100, hwnd, (HMENU)(UINT_PTR)IDC_REDUCED_MOTION, NULL, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"System");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Always");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Never");
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        y += 30;

        /* Announce images */
        chk = CreateWindowExW(0, L"BUTTON", L"Announce &images",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            10, y, 250, 20, hwnd, (HMENU)(UINT_PTR)IDC_ANNOUNCE_IMAGES, NULL, NULL);
        SendMessageW(chk, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(chk, BM_SETCHECK, BST_CHECKED, 0);
        y += 25;

        /* Audio feedback */
        chk = CreateWindowExW(0, L"BUTTON", L"Audio &feedback",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            10, y, 250, 20, hwnd, (HMENU)(UINT_PTR)IDC_AUDIO_FEEDBACK, NULL, NULL);
        SendMessageW(chk, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(chk, BM_SETCHECK, BST_CHECKED, 0);
        y += 25;

        /* Audio errors */
        chk = CreateWindowExW(0, L"BUTTON", L"Audio e&rrors",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            10, y, 250, 20, hwnd, (HMENU)(UINT_PTR)IDC_AUDIO_ERRORS, NULL, NULL);
        SendMessageW(chk, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(chk, BM_SETCHECK, BST_CHECKED, 0);

        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->code == PSN_APPLY) {
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
    (void)wp;
}

/* --- Merged Appearance Tab (Font + Colors + Window) --- */

static INT_PTR CALLBACK appearance_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    (void)wp;
    switch (msg) {
    case WM_INITDIALOG: {
        HFONT hFont = create_dialog_font();
        int y = 5;

        /* -- Font section -- */
        CreateWindowExW(0, L"BUTTON", L"Font",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 5, y, 370, 80, hwnd, NULL, NULL, NULL);
        y += 18;
        HWND lbl = CreateWindowExW(0, L"STATIC", L"Font &family:",
            WS_CHILD | WS_VISIBLE, 15, y, 80, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"Cascadia Mono",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            100, y, 160, 22, hwnd, (HMENU)(UINT_PTR)IDC_FONT_FAMILY, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        lbl = CreateWindowExW(0, L"STATIC", L"&Size:",
            WS_CHILD | WS_VISIBLE, 270, y, 35, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"14",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
            310, y, 50, 22, hwnd, (HMENU)(UINT_PTR)IDC_FONT_SIZE, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 28;
        lbl = CreateWindowExW(0, L"STATIC", L"&Line height:",
            WS_CHILD | WS_VISIBLE, 15, y, 80, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1.2",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            100, y, 50, 22, hwnd, (HMENU)(UINT_PTR)IDC_FONT_LINE_HEIGHT, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 35;

        /* -- Colors section -- */
        CreateWindowExW(0, L"BUTTON", L"Colors",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 5, y, 370, 75, hwnd, NULL, NULL, NULL);
        y += 18;
        lbl = CreateWindowExW(0, L"STATIC", L"&Theme:",
            WS_CHILD | WS_VISIBLE, 15, y, 50, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        HWND combo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            70, y, 130, 200, hwnd, (HMENU)(UINT_PTR)IDC_COLOR_THEME, NULL, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Default");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Solarized Dark");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Dracula");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"One Dark");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Monokai");
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        lbl = CreateWindowExW(0, L"STATIC", L"F&G:",
            WS_CHILD | WS_VISIBLE, 210, y, 25, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"#cccccc",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            240, y, 70, 22, hwnd, (HMENU)(UINT_PTR)IDC_COLOR_FG, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        lbl = CreateWindowExW(0, L"STATIC", L"&BG:",
            WS_CHILD | WS_VISIBLE, 315, y, 25, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"#1e1e1e",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            345, y, 70, 22, hwnd, (HMENU)(UINT_PTR)IDC_COLOR_BG, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 28;
        lbl = CreateWindowExW(0, L"STATIC", L"C&ursor:",
            WS_CHILD | WS_VISIBLE, 15, y, 50, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"#ffffff",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            70, y, 70, 22, hwnd, (HMENU)(UINT_PTR)IDC_COLOR_CURSOR, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        lbl = CreateWindowExW(0, L"STATIC", L"Se&lection:",
            WS_CHILD | WS_VISIBLE, 150, y, 60, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"#264f78",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            215, y, 70, 22, hwnd, (HMENU)(UINT_PTR)IDC_COLOR_SELECTION, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 35;

        /* -- Window section -- */
        CreateWindowExW(0, L"BUTTON", L"Window",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 5, y, 370, 75, hwnd, NULL, NULL, NULL);
        y += 18;
        lbl = CreateWindowExW(0, L"STATIC", L"&Opacity:",
            WS_CHILD | WS_VISIBLE, 15, y, 55, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1.0",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            75, y, 45, 22, hwnd, (HMENU)(UINT_PTR)IDC_WIN_OPACITY, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        CreateWindowExW(0, L"BUTTON", L"&Dark title bar",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            135, y, 120, 20, hwnd, (HMENU)(UINT_PTR)IDC_WIN_DARK_TITLE, NULL, NULL);
        lbl = CreateWindowExW(0, L"STATIC", L"Sc&rollbar:",
            WS_CHILD | WS_VISIBLE, 265, y, 60, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        combo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            330, y, 80, 100, hwnd, (HMENU)(UINT_PTR)IDC_WIN_SCROLLBAR, NULL, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Visible");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Hidden");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Auto");
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        y += 28;
        lbl = CreateWindowExW(0, L"STATIC", L"Ta&b bar:",
            WS_CHILD | WS_VISIBLE, 15, y, 55, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        combo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            75, y, 120, 100, hwnd, (HMENU)(UINT_PTR)IDC_WIN_TAB_BAR, NULL, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Always");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Multiple tabs");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Never");
        SendMessageW(combo, CB_SETCURSEL, 1, 0);

        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->code == PSN_APPLY) {
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

/* --- Merged Terminal Tab (Cursor + Bell + Scrollback + Profiles + Renderer) --- */

static INT_PTR CALLBACK terminal_merged_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    (void)wp;
    switch (msg) {
    case WM_INITDIALOG: {
        HFONT hFont = create_dialog_font();
        int y = 10;
        HWND lbl, combo, edit, chk;

        lbl = CreateWindowExW(0, L"STATIC", L"Cursor &style:",
            WS_CHILD | WS_VISIBLE, 10, y, 100, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        combo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            120, y, 120, 100, hwnd, (HMENU)(UINT_PTR)IDC_CURSOR_STYLE, NULL, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Block");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Underline");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Bar");
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        chk = CreateWindowExW(0, L"BUTTON", L"Cursor &blinks",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            260, y, 120, 20, hwnd, (HMENU)(UINT_PTR)IDC_CURSOR_BLINK, NULL, NULL);
        SendMessageW(chk, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(chk, BM_SETCHECK, BST_CHECKED, 0);
        y += 30;

        lbl = CreateWindowExW(0, L"STATIC", L"Be&ll style:",
            WS_CHILD | WS_VISIBLE, 10, y, 100, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        combo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            120, y, 120, 100, hwnd, (HMENU)(UINT_PTR)IDC_BELL_STYLE, NULL, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Audible");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Visual");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Both");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Mute");
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        y += 30;

        lbl = CreateWindowExW(0, L"STATIC", L"Scroll&back lines:",
            WS_CHILD | WS_VISIBLE, 10, y, 110, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"10000",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
            130, y, 80, 22, hwnd, (HMENU)(UINT_PTR)IDC_SCROLLBACK_LINES, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        chk = CreateWindowExW(0, L"BUTTON", L"Auto-&wrap",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            230, y, 120, 20, hwnd, (HMENU)(UINT_PTR)IDC_AUTO_WRAP, NULL, NULL);
        SendMessageW(chk, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(chk, BM_SETCHECK, BST_CHECKED, 0);
        y += 30;

        lbl = CreateWindowExW(0, L"STATIC", L"Default &profile:",
            WS_CHILD | WS_VISIBLE, 10, y, 110, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        combo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            130, y, 180, 200, hwnd, (HMENU)2001, NULL, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"PowerShell");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Command Prompt");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Git Bash");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"WSL");
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        y += 30;

        lbl = CreateWindowExW(0, L"STATIC", L"&Renderer:",
            WS_CHILD | WS_VISIBLE, 10, y, 80, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        combo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            100, y, 150, 100, hwnd, (HMENU)(UINT_PTR)IDC_WIN_RENDERER, NULL, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Auto");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"GPU (D3D11)");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Software (GDI)");
        SendMessageW(combo, CB_SETCURSEL, 0, 0);

        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->code == PSN_APPLY) {
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

/* --- Template structures for property pages --- */
/* We use DLGTEMPLATE in memory since we don't have .rc resource files */

static DLGTEMPLATE *make_empty_dialog(void) {
    /* Minimal dialog template: no controls, just a container */
    size_t size = sizeof(DLGTEMPLATE) + 4 * sizeof(WORD); /* menu, class, title, font */
    DLGTEMPLATE *dt = (DLGTEMPLATE *)calloc(1, size + 16);
    if (!dt) return NULL;
    dt->style = DS_SETFONT | WS_CHILD | WS_DISABLED | DS_CONTROL;
    dt->cx = 420;
    dt->cy = 300;
    /* Skip menu (WORD 0), class (WORD 0), title (WORD 0) */
    WORD *pw = (WORD *)(dt + 1);
    pw[0] = 0; /* No menu */
    pw[1] = 0; /* Default class */
    pw[2] = 0; /* No title */
    return dt;
}

/* --- Show dialog --- */

bool wixen_settings_dialog_show(HWND parent) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES };
    InitCommonControlsEx(&icc);

    #define NUM_TABS 4
    DLGTEMPLATE *dt[NUM_TABS];
    for (int i = 0; i < NUM_TABS; i++) {
        dt[i] = make_empty_dialog();
        if (!dt[i]) {
            for (int j = 0; j < i; j++) free(dt[j]);
            return false;
        }
    }

    PROPSHEETPAGEW pages[NUM_TABS] = {0};
    struct { DLGPROC proc; const wchar_t *title; } tab_info[NUM_TABS] = {
        { appearance_dlg_proc,       L"Appearance" },
        { terminal_merged_dlg_proc,  L"Terminal" },
        { a11y_dlg_proc,             L"Accessibility" },
        { keybindings_dlg_proc,      L"Keybindings" },
    };
    for (int i = 0; i < NUM_TABS; i++) {
        pages[i].dwSize = sizeof(PROPSHEETPAGEW);
        pages[i].dwFlags = PSP_DLGINDIRECT | PSP_USETITLE;
        pages[i].pResource = dt[i];
        pages[i].pfnDlgProc = tab_info[i].proc;
        pages[i].pszTitle = tab_info[i].title;
    }

    PROPSHEETHEADERW psh = {0};
    psh.dwSize = sizeof(psh);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW;
    psh.hwndParent = parent;
    psh.pszCaption = L"Wixen Terminal Settings";
    psh.nPages = NUM_TABS;
    psh.ppsp = pages;

    INT_PTR result = PropertySheetW(&psh);

    for (int i = 0; i < NUM_TABS; i++) free(dt[i]);

    return result > 0; /* Positive = OK pressed */
}

#endif /* _WIN32 */
