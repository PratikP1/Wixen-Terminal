/* palette_dialog.c — Native Win32 command palette
 *
 * Modeless dialog with:
 * - Edit control for fuzzy search (accessible to screen readers)
 * - ListView showing filtered results (accessible to screen readers)
 * - Enter to execute, Escape to cancel
 */
#ifdef _WIN32

#include "wixen/ui/palette_dialog.h"
#include <commctrl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#pragma comment(lib, "comctl32.lib")

#define IDC_SEARCH_EDIT  2001
#define IDC_RESULT_LIST  2002

/* Built-in palette entries */
typedef struct {
    const wchar_t *label;
    const char *action;
    const wchar_t *shortcut;
} PaletteEntry;

static const PaletteEntry all_entries[] = {
    { L"New Tab",              "new_tab",           L"Ctrl+Shift+T" },
    { L"Close Pane",           "close_pane",        L"Ctrl+Shift+W" },
    { L"Copy",                 "copy",              L"Ctrl+Shift+C" },
    { L"Paste",                "paste",             L"Ctrl+Shift+V" },
    { L"Find",                 "find",              L"Ctrl+Shift+F" },
    { L"Split Horizontal",     "split_horizontal",  L"Alt+Shift++" },
    { L"Split Vertical",       "split_vertical",    L"Alt+Shift+-" },
    { L"Settings",             "settings",          L"Ctrl+," },
    { L"Toggle Fullscreen",    "toggle_fullscreen", L"F11" },
    { L"Zoom In",              "zoom_in",           L"Ctrl++" },
    { L"Zoom Out",             "zoom_out",          L"Ctrl+-" },
    { L"Zoom Reset",           "zoom_reset",        L"Ctrl+0" },
    { L"Next Tab",             "next_tab",          L"Ctrl+Tab" },
    { L"Previous Tab",         "prev_tab",          L"Ctrl+Shift+Tab" },
    { L"Command Palette",      "command_palette",   L"Ctrl+Shift+P" },
    { L"New Window",           "new_window",        L"Ctrl+Shift+N" },
    { L"Scroll to Top",        "scroll_to_top",     L"Ctrl+Home" },
    { L"Scroll to Bottom",     "scroll_to_bottom",  L"Ctrl+End" },
    { L"Clear Terminal",       "clear",             L"Ctrl+Shift+L" },
    { L"Reload Config",        "reload_config",     NULL },
    { L"Open Config File",     "open_config_file",  NULL },
    { L"Restart Shell",        "restart_shell",     NULL },
    { L"Select All",           "select_all",        NULL },
};
#define ENTRY_COUNT (sizeof(all_entries) / sizeof(all_entries[0]))

/* Dialog state */
typedef struct {
    WixenPaletteResult *result;
    bool confirmed;
    int selected_idx;     /* Index into filtered list, or -1 */
    int *filtered;        /* Indices into all_entries */
    int filtered_count;
} PaletteState;

static char *wide_to_utf8(const wchar_t *ws) {
    if (!ws) return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL);
    char *s = malloc(len);
    if (s) WideCharToMultiByte(CP_UTF8, 0, ws, -1, s, len, NULL, NULL);
    return s;
}

/* Case-insensitive wide string contains */
static bool wcs_contains_ci(const wchar_t *haystack, const wchar_t *needle) {
    if (!needle[0]) return true;
    size_t nlen = wcslen(needle);
    for (const wchar_t *p = haystack; *p; p++) {
        bool match = true;
        for (size_t i = 0; i < nlen; i++) {
            if (!p[i] || towlower(p[i]) != towlower(needle[i])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

static void update_filter(HWND hwnd, PaletteState *state) {
    wchar_t query[256] = {0};
    GetDlgItemTextW(hwnd, IDC_SEARCH_EDIT, query, 256);

    HWND list = GetDlgItem(hwnd, IDC_RESULT_LIST);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);

    state->filtered_count = 0;
    for (int i = 0; i < (int)ENTRY_COUNT; i++) {
        if (query[0] == L'\0' || wcs_contains_ci(all_entries[i].label, query)) {
            state->filtered[state->filtered_count++] = i;

            LVITEMW lvi = {0};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = state->filtered_count - 1;
            lvi.pszText = (LPWSTR)all_entries[i].label;
            ListView_InsertItem(list, &lvi);

            if (all_entries[i].shortcut) {
                ListView_SetItemText(list, state->filtered_count - 1, 1,
                                     (LPWSTR)all_entries[i].shortcut);
            }
        }
    }

    SendMessageW(list, WM_SETREDRAW, TRUE, 0);

    /* Select first item */
    if (state->filtered_count > 0) {
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        state->selected_idx = 0;
    } else {
        state->selected_idx = -1;
    }
}

static INT_PTR CALLBACK palette_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PaletteState *state = (PaletteState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_INITDIALOG: {
        state = (PaletteState *)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);

        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        /* Search edit */
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            10, 10, 460, 24, hwnd, (HMENU)(UINT_PTR)IDC_SEARCH_EDIT, NULL, NULL);
        SendMessageW(edit, WM_SETFONT, (WPARAM)hFont, TRUE);

        /* Accessible label for search box */
        HWND lbl = CreateWindowExW(0, L"STATIC", L"Search commands:",
            WS_CHILD | WS_VISIBLE, 10, 0, 0, 0, hwnd, NULL, NULL, NULL);
        (void)lbl; /* Label provides accessible name via grouping */

        /* Results ListView */
        HWND list = CreateWindowExW(0, WC_LISTVIEWW, NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL
            | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER,
            10, 40, 460, 300, hwnd, (HMENU)(UINT_PTR)IDC_RESULT_LIST, NULL, NULL);
        SendMessageW(list, WM_SETFONT, (WPARAM)hFont, TRUE);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);

        /* Columns */
        LVCOLUMNW col = {0};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = L"Action";
        col.cx = 320;
        ListView_InsertColumn(list, 0, &col);
        col.pszText = L"Shortcut";
        col.cx = 130;
        ListView_InsertColumn(list, 1, &col);

        /* Initial filter (show all) */
        update_filter(hwnd, state);

        SetFocus(edit);
        return FALSE; /* We set focus ourselves */
    }

    case WM_COMMAND:
        if (HIWORD(wp) == EN_CHANGE && LOWORD(wp) == IDC_SEARCH_EDIT) {
            update_filter(hwnd, state);
        }
        break;

    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->idFrom == IDC_RESULT_LIST && nm->code == NM_DBLCLK) {
            /* Double-click on item */
            if (state->selected_idx >= 0 && state->selected_idx < state->filtered_count) {
                int entry_idx = state->filtered[state->selected_idx];
                state->result->action = wide_to_utf8(all_entries[entry_idx].label);
                /* Store the action ID, not the label */
                free(state->result->action);
                size_t len = strlen(all_entries[entry_idx].action);
                state->result->action = malloc(len + 1);
                if (state->result->action)
                    memcpy(state->result->action, all_entries[entry_idx].action, len + 1);
                state->confirmed = true;
                EndDialog(hwnd, IDOK);
            }
        }
        if (nm->idFrom == IDC_RESULT_LIST && nm->code == LVN_ITEMCHANGED) {
            NMLISTVIEW *nmlv = (NMLISTVIEW *)lp;
            if (nmlv->uNewState & LVIS_SELECTED) {
                state->selected_idx = nmlv->iItem;
            }
        }
        break;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        if (wp == VK_RETURN) {
            if (state && state->selected_idx >= 0 && state->selected_idx < state->filtered_count) {
                int entry_idx = state->filtered[state->selected_idx];
                size_t len = strlen(all_entries[entry_idx].action);
                state->result->action = malloc(len + 1);
                if (state->result->action)
                    memcpy(state->result->action, all_entries[entry_idx].action, len + 1);
                state->confirmed = true;
                EndDialog(hwnd, IDOK);
            }
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

/* --- Public API --- */

bool wixen_palette_dialog_show(HWND parent, WixenPaletteResult *out_result) {
    memset(out_result, 0, sizeof(*out_result));

    /* Create dialog template in memory */
    typedef struct {
        DLGTEMPLATE dt;
        WORD menu;
        WORD cls;
        WORD title;
    } DlgTmpl;

    DlgTmpl tmpl = {0};
    tmpl.dt.style = DS_CENTER | DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_SETFONT;
    tmpl.dt.cx = 320; /* Dialog units */
    tmpl.dt.cy = 240;

    PaletteState state = {0};
    state.result = out_result;
    state.selected_idx = -1;
    state.filtered = calloc(ENTRY_COUNT, sizeof(int));
    if (!state.filtered) return false;

    INT_PTR result = DialogBoxIndirectParamW(
        GetModuleHandleW(NULL),
        &tmpl.dt,
        parent,
        palette_dlg_proc,
        (LPARAM)&state);

    free(state.filtered);
    return result == IDOK && state.confirmed;
}

#endif /* _WIN32 */
