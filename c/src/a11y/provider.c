#define WIXEN_A11Y_INTERNAL
#include "wixen/a11y/text_provider.h"
/* provider.c — UIA accessibility provider for terminal area
 *
 * Manual COM vtable construction in C for:
 * - IRawElementProviderSimple
 * - IRawElementProviderFragment
 * - IRawElementProviderFragmentRoot
 *
 * Threading: UseComThreading marshals calls to UI thread.
 * State access: SRWLock for thread-safe reads.
 */
#ifdef _WIN32

#include "wixen/a11y/provider.h"
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "uiautomationcore.lib")

/* --- Provider object --- */

typedef struct TerminalProvider {
    IRawElementProviderSimpleVtbl *lpVtbl;
    IRawElementProviderFragmentVtbl *lpFragVtbl;
    IRawElementProviderFragmentRootVtbl *lpRootVtbl;
    LONG ref_count;
    HWND hwnd;
    WixenA11yState *state;
    IRawElementProviderSimple *host_provider; /* Cached, created on UI thread */
} TerminalProvider;

/* Offset helpers for interface casting */
#define FRAG_OFFSET offsetof(TerminalProvider, lpFragVtbl)
#define ROOT_OFFSET offsetof(TerminalProvider, lpRootVtbl)
#define PROVIDER_FROM_FRAG(ptr) ((TerminalProvider *)((char *)(ptr) - FRAG_OFFSET))
#define PROVIDER_FROM_ROOT(ptr) ((TerminalProvider *)((char *)(ptr) - ROOT_OFFSET))

/* --- IUnknown --- */

static HRESULT STDMETHODCALLTYPE provider_QueryInterface(
        IRawElementProviderSimple *this_, REFIID riid, void **ppv) {
    TerminalProvider *p = (TerminalProvider *)this_;
    if (IsEqualIID(riid, &IID_IUnknown)
        || IsEqualIID(riid, &IID_IRawElementProviderSimple)) {
        *ppv = &p->lpVtbl;
    } else if (IsEqualIID(riid, &IID_IRawElementProviderFragment)) {
        *ppv = &p->lpFragVtbl;
    } else if (IsEqualIID(riid, &IID_IRawElementProviderFragmentRoot)) {
        *ppv = &p->lpRootVtbl;
    } else {
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&p->ref_count);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE provider_AddRef(IRawElementProviderSimple *this_) {
    TerminalProvider *p = (TerminalProvider *)this_;
    return InterlockedIncrement(&p->ref_count);
}

static ULONG STDMETHODCALLTYPE provider_Release(IRawElementProviderSimple *this_) {
    TerminalProvider *p = (TerminalProvider *)this_;
    LONG count = InterlockedDecrement(&p->ref_count);
    if (count == 0) {
        if (p->host_provider)
            p->host_provider->lpVtbl->Release(p->host_provider);
        free(p);
    }
    return count;
}

/* Fragment IUnknown (delegates to main) */
static HRESULT STDMETHODCALLTYPE frag_QI(IRawElementProviderFragment *this_, REFIID riid, void **ppv) {
    return provider_QueryInterface((IRawElementProviderSimple *)PROVIDER_FROM_FRAG(this_), riid, ppv);
}
static ULONG STDMETHODCALLTYPE frag_AddRef(IRawElementProviderFragment *this_) {
    return provider_AddRef((IRawElementProviderSimple *)PROVIDER_FROM_FRAG(this_));
}
static ULONG STDMETHODCALLTYPE frag_Release(IRawElementProviderFragment *this_) {
    return provider_Release((IRawElementProviderSimple *)PROVIDER_FROM_FRAG(this_));
}

/* Root IUnknown */
static HRESULT STDMETHODCALLTYPE root_QI(IRawElementProviderFragmentRoot *this_, REFIID riid, void **ppv) {
    return provider_QueryInterface((IRawElementProviderSimple *)PROVIDER_FROM_ROOT(this_), riid, ppv);
}
static ULONG STDMETHODCALLTYPE root_AddRef(IRawElementProviderFragmentRoot *this_) {
    return provider_AddRef((IRawElementProviderSimple *)PROVIDER_FROM_ROOT(this_));
}
static ULONG STDMETHODCALLTYPE root_Release(IRawElementProviderFragmentRoot *this_) {
    return provider_Release((IRawElementProviderSimple *)PROVIDER_FROM_ROOT(this_));
}

/* --- IRawElementProviderSimple --- */

static HRESULT STDMETHODCALLTYPE provider_get_ProviderOptions(
        IRawElementProviderSimple *this_, enum ProviderOptions *pRetVal) {
    (void)this_;
    /* UseComThreading: UIA marshals calls to the UI thread. Required for
     * NVDA to properly register our window — without it, UIA's initial
     * probe runs on the MTA thread and may abandon our provider.
     * The UI thread must pump messages during init to avoid timeouts. */
    *pRetVal = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE provider_GetPatternProvider(
        IRawElementProviderSimple *this_, PATTERNID patternId, IUnknown **pRetVal) {
    TerminalProvider *p = (TerminalProvider *)this_;
    *pRetVal = NULL;
    if (patternId == UIA_TextPatternId || patternId == UIA_TextPattern2Id) {
        *pRetVal = wixen_a11y_create_text_provider(p->state, this_);
    } else if (patternId == UIA_GridPatternId) {
        /* Grid pattern for TUI apps — expose row/column count */
        /* Uses the same provider with grid methods */
        /* For now, return the provider itself as IGridProvider is optional */
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE provider_GetPropertyValue(
        IRawElementProviderSimple *this_, PROPERTYID propertyId, VARIANT *pRetVal) {
    TerminalProvider *p = (TerminalProvider *)this_;
    VariantInit(pRetVal);

    if (propertyId == UIA_ControlTypePropertyId) {
        pRetVal->vt = VT_I4;
        pRetVal->lVal = UIA_DocumentControlTypeId;
    } else if (propertyId == UIA_NamePropertyId) {
        AcquireSRWLockShared(&p->state->lock);
        if (p->state->title) {
            pRetVal->vt = VT_BSTR;
            pRetVal->bstrVal = SysAllocString(p->state->title);
        }
        ReleaseSRWLockShared(&p->state->lock);
    } else if (propertyId == UIA_AutomationIdPropertyId) {
        pRetVal->vt = VT_BSTR;
        pRetVal->bstrVal = SysAllocString(L"WixenTerminalView");
    } else if (propertyId == UIA_ClassNamePropertyId) {
        pRetVal->vt = VT_BSTR;
        pRetVal->bstrVal = SysAllocString(L"WixenTerminalWindow");
    } else if (propertyId == UIA_IsContentElementPropertyId
               || propertyId == UIA_IsControlElementPropertyId
               || propertyId == UIA_IsKeyboardFocusablePropertyId) {
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = VARIANT_TRUE;
    } else if (propertyId == UIA_HasKeyboardFocusPropertyId) {
        AcquireSRWLockShared(&p->state->lock);
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = p->state->has_focus ? VARIANT_TRUE : VARIANT_FALSE;
        ReleaseSRWLockShared(&p->state->lock);
    } else if (propertyId == UIA_LocalizedControlTypePropertyId) {
        pRetVal->vt = VT_BSTR;
        pRetVal->bstrVal = SysAllocString(L"terminal");
    } else if (propertyId == UIA_NativeWindowHandlePropertyId) {
        pRetVal->vt = VT_I4;
        pRetVal->lVal = (LONG)(LONG_PTR)p->hwnd;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE provider_get_HostRawElementProvider(
        IRawElementProviderSimple *this_, IRawElementProviderSimple **pRetVal) {
    TerminalProvider *p = (TerminalProvider *)this_;
    /* Return cached host provider. Created on UI thread during init,
     * safe to return from any thread (AddRef is always thread-safe). */
    if (p->host_provider) {
        p->host_provider->lpVtbl->AddRef(p->host_provider);
        *pRetVal = p->host_provider;
        return S_OK;
    }
    *pRetVal = NULL;
    return S_OK;
}

/* --- IRawElementProviderFragment --- */

static HRESULT STDMETHODCALLTYPE frag_Navigate(
        IRawElementProviderFragment *this_,
        enum NavigateDirection direction, IRawElementProviderFragment **pRetVal) {
    (void)this_; (void)direction;
    *pRetVal = NULL;
    /* Terminal is currently a leaf — command block children are a future enhancement */
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE frag_GetRuntimeId(
        IRawElementProviderFragment *this_, SAFEARRAY **pRetVal) {
    (void)this_;
    *pRetVal = NULL;
    /* Root doesn't need a runtime ID — it has a window handle */
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE frag_get_BoundingRectangle(
        IRawElementProviderFragment *this_, struct UiaRect *pRetVal) {
    TerminalProvider *p = PROVIDER_FROM_FRAG(this_);
    RECT rc;
    GetClientRect(p->hwnd, &rc);
    POINT pt = {0, 0};
    ClientToScreen(p->hwnd, &pt);
    pRetVal->left = pt.x;
    pRetVal->top = pt.y;
    pRetVal->width = rc.right - rc.left;
    pRetVal->height = rc.bottom - rc.top;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE frag_GetEmbeddedFragmentRoots(
        IRawElementProviderFragment *this_, SAFEARRAY **pRetVal) {
    (void)this_;
    *pRetVal = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE frag_SetFocus(IRawElementProviderFragment *this_) {
    TerminalProvider *p = PROVIDER_FROM_FRAG(this_);
    SetFocus(p->hwnd);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE frag_get_FragmentRoot(
        IRawElementProviderFragment *this_,
        IRawElementProviderFragmentRoot **pRetVal) {
    TerminalProvider *p = PROVIDER_FROM_FRAG(this_);
    *pRetVal = (IRawElementProviderFragmentRoot *)&p->lpRootVtbl;
    InterlockedIncrement(&p->ref_count);
    return S_OK;
}

/* --- IRawElementProviderFragmentRoot --- */

static HRESULT STDMETHODCALLTYPE root_ElementProviderFromPoint(
        IRawElementProviderFragmentRoot *this_, double x, double y,
        IRawElementProviderFragment **pRetVal) {
    (void)x; (void)y;
    TerminalProvider *p = PROVIDER_FROM_ROOT(this_);
    *pRetVal = (IRawElementProviderFragment *)&p->lpFragVtbl;
    InterlockedIncrement(&p->ref_count);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE root_GetFocus(
        IRawElementProviderFragmentRoot *this_,
        IRawElementProviderFragment **pRetVal) {
    TerminalProvider *p = PROVIDER_FROM_ROOT(this_);
    *pRetVal = (IRawElementProviderFragment *)&p->lpFragVtbl;
    InterlockedIncrement(&p->ref_count);
    return S_OK;
}

/* --- Vtables --- */

static IRawElementProviderSimpleVtbl simple_vtbl = {
    provider_QueryInterface,
    provider_AddRef,
    provider_Release,
    provider_get_ProviderOptions,
    provider_GetPatternProvider,
    provider_GetPropertyValue,
    provider_get_HostRawElementProvider,
};

static IRawElementProviderFragmentVtbl fragment_vtbl = {
    frag_QI, frag_AddRef, frag_Release,
    frag_Navigate,
    frag_GetRuntimeId,
    frag_get_BoundingRectangle,
    frag_GetEmbeddedFragmentRoots,
    frag_SetFocus,
    frag_get_FragmentRoot,
};

static IRawElementProviderFragmentRootVtbl root_vtbl = {
    root_QI, root_AddRef, root_Release,
    root_ElementProviderFromPoint,
    root_GetFocus,
};

/* --- Public API --- */

IRawElementProviderSimple *wixen_a11y_create_provider(HWND hwnd, WixenA11yState *state) {
    TerminalProvider *p = calloc(1, sizeof(TerminalProvider));
    if (!p) return NULL;
    p->lpVtbl = &simple_vtbl;
    p->lpFragVtbl = &fragment_vtbl;
    p->lpRootVtbl = &root_vtbl;
    p->ref_count = 1;
    p->hwnd = hwnd;
    p->state = state;
    /* Cache host provider on the UI thread. Without UseComThreading,
     * HostRawElementProvider may be called from NVDA's UIA thread.
     * UiaHostProviderFromHwnd must be called from the HWND's thread. */
    UiaHostProviderFromHwnd(hwnd, &p->host_provider);
    return (IRawElementProviderSimple *)p;
}

bool wixen_wm_getobject_matches(LPARAM lparam) {
    return (DWORD)lparam == (DWORD)UiaRootObjectId;
}

LRESULT wixen_a11y_handle_getobject(HWND hwnd, WPARAM wparam, LPARAM lparam,
                                     IRawElementProviderSimple *provider) {
    if (wixen_wm_getobject_matches(lparam)) {
        return UiaReturnRawElementProvider(hwnd, wparam, lparam, provider);
    }
    return DefWindowProcW(hwnd, WM_GETOBJECT, wparam, lparam);
}

LRESULT wixen_a11y_handle_wm_getobject(HWND hwnd, WPARAM wparam, LPARAM lparam) {
    IRawElementProviderSimple *provider =
        (IRawElementProviderSimple *)GetPropW(hwnd, L"WixenUiaProvider");
    if (provider && wixen_wm_getobject_matches(lparam)) {
        return UiaReturnRawElementProvider(hwnd, wparam, lparam, provider);
    }
    return DefWindowProcW(hwnd, WM_GETOBJECT, wparam, lparam);
}

void wixen_a11y_raise_focus_changed_provider(IRawElementProviderSimple *provider) {
    UiaRaiseAutomationEvent(provider, UIA_AutomationFocusChangedEventId);
}

/* wixen_a11y_raise_focus_changed(HWND) is defined below, after g_provider */

void wixen_a11y_raise_text_changed(IRawElementProviderSimple *provider) {
    UiaRaiseAutomationEvent(provider, UIA_Text_TextChangedEventId);
}

void wixen_a11y_raise_structure_changed(IRawElementProviderSimple *provider) {
    int ids[] = {0};
    UiaRaiseStructureChangedEvent(provider, StructureChangeType_ChildrenInvalidated,
                                   ids, 0);
}

/* --- Shared state --- */

void wixen_a11y_state_init(WixenA11yState *state) {
    memset(state, 0, sizeof(*state));
    InitializeSRWLock(&state->lock);
}

void wixen_a11y_state_free(WixenA11yState *state) {
    free(state->full_text);
    free(state->title);
    state->full_text = NULL;
    state->title = NULL;
}

void wixen_a11y_state_update_text(WixenA11yState *state, const char *text, size_t len) {
    AcquireSRWLockExclusive(&state->lock);
    free(state->full_text);
    state->full_text = malloc(len + 1);
    if (state->full_text) {
        memcpy(state->full_text, text, len);
        state->full_text[len] = '\0';
        state->full_text_len = len;
    }
    ReleaseSRWLockExclusive(&state->lock);
}

void wixen_a11y_state_update_cursor(WixenA11yState *state, size_t row, size_t col) {
    AcquireSRWLockExclusive(&state->lock);
    state->cursor_row = row;
    state->cursor_col = col;
    ReleaseSRWLockExclusive(&state->lock);
}

void wixen_a11y_state_update_focus(WixenA11yState *state, bool has_focus) {
    AcquireSRWLockExclusive(&state->lock);
    state->has_focus = has_focus;
    ReleaseSRWLockExclusive(&state->lock);
}

/* --- High-level convenience functions for main.c --- */

#include "wixen/core/grid.h"

/* Global provider state (single-window app) */
static IRawElementProviderSimple *g_provider = NULL;
static WixenA11yState g_a11y_state;

void wixen_a11y_provider_init_minimal(HWND hwnd) {
    /* Called from WM_CREATE — registers a minimal provider so NVDA
     * doesn't cache the HWND as non-UIA. Full init happens later. */
    if (g_provider) return; /* Already initialized */
    wixen_a11y_state_init(&g_a11y_state);
    g_provider = wixen_a11y_create_provider(hwnd, &g_a11y_state);
    SetPropW(hwnd, L"WixenUiaProvider", (HANDLE)g_provider);
}

void wixen_a11y_provider_init(HWND hwnd, void *terminal) {
    (void)terminal;
    wixen_a11y_state_init(&g_a11y_state);
    g_provider = wixen_a11y_create_provider(hwnd, &g_a11y_state);
    /* Store provider as window property for WM_GETOBJECT */
    SetPropW(hwnd, L"WixenUiaProvider", (HANDLE)g_provider);
}

void wixen_a11y_provider_shutdown(HWND hwnd) {
    RemovePropW(hwnd, L"WixenUiaProvider");
    if (g_provider) {
        g_provider->lpVtbl->Release(g_provider);
        g_provider = NULL;
    }
    wixen_a11y_state_free(&g_a11y_state);
}

void wixen_a11y_update_cursor(const void *grid_ptr) {
    if (!grid_ptr) return;
    const WixenGrid *grid = (const WixenGrid *)grid_ptr;
    wixen_a11y_state_update_cursor(&g_a11y_state, grid->cursor.row, grid->cursor.col);
}

void wixen_a11y_raise_focus_changed(HWND hwnd) {
    (void)hwnd;
    if (g_provider) {
        UiaRaiseAutomationEvent(g_provider, UIA_AutomationFocusChangedEventId);
    }
}

void wixen_a11y_raise_selection_changed(HWND hwnd) {
    (void)hwnd;
    if (g_provider) {
        UiaRaiseAutomationEvent(g_provider, UIA_Text_TextSelectionChangedEventId);
    }
}

void wixen_a11y_raise_notification(HWND hwnd, const char *text, const char *activity_id) {
    (void)hwnd;
    if (!g_provider || !text || !text[0]) return;

    /* Convert text to wide string */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *wtext = malloc((size_t)wlen * sizeof(wchar_t));
    if (!wtext) return;
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, wlen);

    int alen = MultiByteToWideChar(CP_UTF8, 0, activity_id, -1, NULL, 0);
    wchar_t *waid = malloc((size_t)alen * sizeof(wchar_t));
    if (!waid) { free(wtext); return; }
    MultiByteToWideChar(CP_UTF8, 0, activity_id, -1, waid, alen);

    UiaRaiseNotificationEvent(g_provider,
        NotificationKind_ItemAdded,
        NotificationProcessing_All,
        wtext, waid);

    free(wtext);
    free(waid);
}

void wixen_a11y_state_update_text_global(const char *text, size_t len) {
    wixen_a11y_state_update_text(&g_a11y_state, text, len);
}

/* Lock-free cursor offset for GetSelection/GetCaretRange */
static volatile LONG g_cursor_offset = 0;

void wixen_a11y_set_cursor_offset(int32_t utf16_offset) {
    InterlockedExchange(&g_cursor_offset, (LONG)utf16_offset);
}

int32_t wixen_a11y_get_cursor_offset(void) {
    return (int32_t)InterlockedCompareExchange(&g_cursor_offset, 0, 0);
}

void wixen_a11y_pump_messages(HWND hwnd) {
    MSG pump_msg;
    while (PeekMessageW(&pump_msg, hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&pump_msg);
        DispatchMessageW(&pump_msg);
    }
}

#endif /* _WIN32 */
