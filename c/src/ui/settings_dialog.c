/* settings_dialog.c — Native Win32 settings dialog using PropertySheet
 *
 * Uses native Win32 controls for full screen reader accessibility:
 * - PropertySheet with tabs (auto-accessible)
 * - Edit controls, combo boxes, checkboxes, spinners
 * - NVDA/JAWS reads all controls natively
 */
#ifdef _WIN32

#include "wixen/ui/settings_dialog.h"
#include <commctrl.h>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")

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

/* --- Font Tab --- */

static INT_PTR CALLBACK font_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    (void)wp;
    switch (msg) {
    case WM_INITDIALOG: {
        /* Create controls programmatically */
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
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

        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->code == PSN_APPLY) {
            /* Read font size from edit control and store in dialog data */
            wchar_t buf[64];
            HWND hFontSize = GetDlgItem(hwnd, 1001);
            if (hFontSize) {
                GetWindowTextW(hFontSize, buf, 64);
                /* The caller retrieves the config from the property sheet */
            }
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

/* --- Terminal Tab --- */

static INT_PTR CALLBACK terminal_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
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
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
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

/* --- Template structures for property pages --- */
/* We use DLGTEMPLATE in memory since we don't have .rc resource files */

static DLGTEMPLATE *make_empty_dialog(void) {
    /* Minimal dialog template: no controls, just a container */
    size_t size = sizeof(DLGTEMPLATE) + 4 * sizeof(WORD); /* menu, class, title, font */
    DLGTEMPLATE *dt = (DLGTEMPLATE *)calloc(1, size + 16);
    if (!dt) return NULL;
    dt->style = DS_SETFONT | WS_CHILD | WS_DISABLED | DS_CONTROL;
    dt->cx = 300;
    dt->cy = 200;
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

    DLGTEMPLATE *dt1 = make_empty_dialog();
    DLGTEMPLATE *dt2 = make_empty_dialog();
    DLGTEMPLATE *dt3 = make_empty_dialog();
    if (!dt1 || !dt2 || !dt3) {
        free(dt1); free(dt2); free(dt3);
        return false;
    }

    PROPSHEETPAGEW pages[3] = {0};

    pages[0].dwSize = sizeof(PROPSHEETPAGEW);
    pages[0].dwFlags = PSP_DLGINDIRECT;
    pages[0].pResource = dt1;
    pages[0].pfnDlgProc = font_dlg_proc;
    pages[0].pszTitle = L"Font";

    pages[1].dwSize = sizeof(PROPSHEETPAGEW);
    pages[1].dwFlags = PSP_DLGINDIRECT;
    pages[1].pResource = dt2;
    pages[1].pfnDlgProc = terminal_dlg_proc;
    pages[1].pszTitle = L"Terminal";

    pages[2].dwSize = sizeof(PROPSHEETPAGEW);
    pages[2].dwFlags = PSP_DLGINDIRECT;
    pages[2].pResource = dt3;
    pages[2].pfnDlgProc = a11y_dlg_proc;
    pages[2].pszTitle = L"Accessibility";

    PROPSHEETHEADERW psh = {0};
    psh.dwSize = sizeof(psh);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW;
    psh.hwndParent = parent;
    psh.pszCaption = L"Wixen Terminal Settings";
    psh.nPages = 3;
    psh.ppsp = pages;

    INT_PTR result = PropertySheetW(&psh);

    free(dt1); free(dt2); free(dt3);

    return result > 0; /* Positive = OK pressed */
}

#endif /* _WIN32 */
