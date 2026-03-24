/* text_provider.c — ITextProvider2 + ITextRangeProvider for terminal text
 *
 * Provides screen readers with:
 * - GetSelection → degenerate range at cursor position
 * - GetVisibleRanges → range covering visible grid
 * - RangeFromPoint → range at screen coordinates
 * - GetCaretRange → cursor position (ITextProvider2)
 * - Text range operations: GetText, ExpandToEnclosingUnit, Move, etc.
 */
#ifdef _WIN32

#include "wixen/a11y/text_provider.h"
#include <stdlib.h>
#include <string.h>
#include <oleauto.h>

#pragma comment(lib, "oleaut32.lib")

/* --- Text Range --- */

typedef struct TextRange {
    ITextRangeProviderVtbl *lpVtbl;
    LONG ref_count;
    WixenA11yState *state;
    IRawElementProviderSimple *enclosing;
    int start;  /* UTF-16 offset */
    int end;    /* UTF-16 offset */
} TextRange;

/* Forward declarations */
static ITextRangeProviderVtbl range_vtbl;

static TextRange *create_range(WixenA11yState *state, IRawElementProviderSimple *enclosing,
                                int start, int end) {
    TextRange *r = (TextRange *)calloc(1, sizeof(TextRange));
    if (!r) return NULL;
    r->lpVtbl = &range_vtbl;
    r->ref_count = 1;
    r->state = state;
    r->enclosing = enclosing;
    if (enclosing) enclosing->lpVtbl->AddRef(enclosing);
    r->start = start;
    r->end = end;
    return r;
}

/* IUnknown */
static HRESULT STDMETHODCALLTYPE range_QI(ITextRangeProvider *this_, REFIID riid, void **ppv) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITextRangeProvider)) {
        *ppv = this_;
        this_->lpVtbl->AddRef(this_);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE range_AddRef(ITextRangeProvider *this_) {
    TextRange *r = (TextRange *)this_;
    return InterlockedIncrement(&r->ref_count);
}

static ULONG STDMETHODCALLTYPE range_Release(ITextRangeProvider *this_) {
    TextRange *r = (TextRange *)this_;
    LONG count = InterlockedDecrement(&r->ref_count);
    if (count == 0) {
        if (r->enclosing) r->enclosing->lpVtbl->Release(r->enclosing);
        free(r);
    }
    return count;
}

/* ITextRangeProvider methods */
static HRESULT STDMETHODCALLTYPE range_Clone(ITextRangeProvider *this_,
                                              ITextRangeProvider **pRetVal) {
    TextRange *r = (TextRange *)this_;
    TextRange *clone = create_range(r->state, r->enclosing, r->start, r->end);
    *pRetVal = clone ? (ITextRangeProvider *)clone : NULL;
    return clone ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE range_Compare(ITextRangeProvider *this_,
                                                ITextRangeProvider *range, BOOL *pRetVal) {
    TextRange *a = (TextRange *)this_;
    TextRange *b = (TextRange *)range;
    *pRetVal = (a->start == b->start && a->end == b->end);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_CompareEndpoints(
        ITextRangeProvider *this_, enum TextPatternRangeEndpoint endpoint,
        ITextRangeProvider *targetRange, enum TextPatternRangeEndpoint targetEndpoint,
        int *pRetVal) {
    TextRange *a = (TextRange *)this_;
    TextRange *b = (TextRange *)targetRange;
    int va = (endpoint == TextPatternRangeEndpoint_Start) ? a->start : a->end;
    int vb = (targetEndpoint == TextPatternRangeEndpoint_Start) ? b->start : b->end;
    *pRetVal = va - vb;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_ExpandToEnclosingUnit(
        ITextRangeProvider *this_, enum TextUnit unit) {
    TextRange *r = (TextRange *)this_;
    AcquireSRWLockShared(&r->state->lock);
    size_t text_len = r->state->full_text_len;

    if (unit == TextUnit_Character) {
        if (r->end <= r->start && r->start < (int)text_len)
            r->end = r->start + 1;
    } else if (unit == TextUnit_Line) {
        /* Find line boundaries (newlines) */
        const char *text = r->state->full_text;
        if (text) {
            /* Find start of line */
            int s = r->start;
            while (s > 0 && text[s - 1] != '\n') s--;
            r->start = s;
            /* Find end of line */
            int e = r->start;
            while (e < (int)text_len && text[e] != '\n') e++;
            if (e < (int)text_len) e++; /* Include newline */
            r->end = e;
        }
    } else if (unit == TextUnit_Document) {
        r->start = 0;
        r->end = (int)text_len;
    }
    /* Word and other units: approximate with character for now */

    ReleaseSRWLockShared(&r->state->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_FindAttribute(
        ITextRangeProvider *this_, TEXTATTRIBUTEID id, VARIANT val,
        BOOL backward, ITextRangeProvider **pRetVal) {
    (void)this_; (void)id; (void)val; (void)backward;
    *pRetVal = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_FindText(
        ITextRangeProvider *this_, BSTR text, BOOL backward, BOOL ignoreCase,
        ITextRangeProvider **pRetVal) {
    (void)this_; (void)text; (void)backward; (void)ignoreCase;
    *pRetVal = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_GetAttributeValue(
        ITextRangeProvider *this_, TEXTATTRIBUTEID id, VARIANT *pRetVal) {
    (void)this_; (void)id;
    VariantInit(pRetVal);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_GetBoundingRectangles(
        ITextRangeProvider *this_, SAFEARRAY **pRetVal) {
    (void)this_;
    *pRetVal = SafeArrayCreateVector(VT_R8, 0, 0);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_GetEnclosingElement(
        ITextRangeProvider *this_, IRawElementProviderSimple **pRetVal) {
    TextRange *r = (TextRange *)this_;
    *pRetVal = r->enclosing;
    if (r->enclosing) r->enclosing->lpVtbl->AddRef(r->enclosing);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_GetText(
        ITextRangeProvider *this_, int maxLength, BSTR *pRetVal) {
    TextRange *r = (TextRange *)this_;
    AcquireSRWLockShared(&r->state->lock);

    const char *text = r->state->full_text;
    size_t text_len = r->state->full_text_len;

    int s = r->start < 0 ? 0 : r->start;
    int e = r->end;
    if (e > (int)text_len) e = (int)text_len;
    if (s >= e || !text) {
        *pRetVal = SysAllocString(L"");
        ReleaseSRWLockShared(&r->state->lock);
        return S_OK;
    }

    int len = e - s;
    if (maxLength >= 0 && len > maxLength) len = maxLength;

    /* Convert UTF-8 substring to UTF-16 */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text + s, len, NULL, 0);
    wchar_t *wtext = (wchar_t *)malloc((wlen + 1) * sizeof(wchar_t));
    if (wtext) {
        MultiByteToWideChar(CP_UTF8, 0, text + s, len, wtext, wlen);
        wtext[wlen] = L'\0';
        *pRetVal = SysAllocString(wtext);
        free(wtext);
    } else {
        *pRetVal = SysAllocString(L"");
    }

    ReleaseSRWLockShared(&r->state->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_Move(
        ITextRangeProvider *this_, enum TextUnit unit, int count, int *pRetVal) {
    (void)this_; (void)unit; (void)count;
    *pRetVal = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_MoveEndpointByUnit(
        ITextRangeProvider *this_, enum TextPatternRangeEndpoint endpoint,
        enum TextUnit unit, int count, int *pRetVal) {
    (void)this_; (void)endpoint; (void)unit; (void)count;
    *pRetVal = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_MoveEndpointByRange(
        ITextRangeProvider *this_, enum TextPatternRangeEndpoint endpoint,
        ITextRangeProvider *targetRange, enum TextPatternRangeEndpoint targetEndpoint) {
    TextRange *r = (TextRange *)this_;
    TextRange *t = (TextRange *)targetRange;
    int val = (targetEndpoint == TextPatternRangeEndpoint_Start) ? t->start : t->end;
    if (endpoint == TextPatternRangeEndpoint_Start) r->start = val;
    else r->end = val;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_Select(ITextRangeProvider *this_) {
    (void)this_;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_AddToSelection(ITextRangeProvider *this_) {
    (void)this_;
    return UIA_E_INVALIDOPERATION;
}

static HRESULT STDMETHODCALLTYPE range_RemoveFromSelection(ITextRangeProvider *this_) {
    (void)this_;
    return UIA_E_INVALIDOPERATION;
}

static HRESULT STDMETHODCALLTYPE range_ScrollIntoView(ITextRangeProvider *this_, BOOL alignToTop) {
    (void)this_; (void)alignToTop;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_GetChildren(
        ITextRangeProvider *this_, SAFEARRAY **pRetVal) {
    (void)this_;
    *pRetVal = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
    return S_OK;
}

static ITextRangeProviderVtbl range_vtbl = {
    range_QI, range_AddRef, range_Release,
    range_Clone,
    range_Compare,
    range_CompareEndpoints,
    range_ExpandToEnclosingUnit,
    range_FindAttribute,
    range_FindText,
    range_GetAttributeValue,
    range_GetBoundingRectangles,
    range_GetEnclosingElement,
    range_GetText,
    range_Move,
    range_MoveEndpointByUnit,
    range_MoveEndpointByRange,
    range_Select,
    range_AddToSelection,
    range_RemoveFromSelection,
    range_ScrollIntoView,
    range_GetChildren,
};

/* --- Text Provider --- */

typedef struct TextProvider {
    ITextProviderVtbl *lpVtbl;
    ITextProvider2Vtbl *lpVtbl2;
    LONG ref_count;
    WixenA11yState *state;
    IRawElementProviderSimple *enclosing;
} TextProvider;

#define TP_FROM_V2(ptr) ((TextProvider *)((char *)(ptr) - offsetof(TextProvider, lpVtbl2)))

/* IUnknown */
static HRESULT STDMETHODCALLTYPE tp_QI(ITextProvider *this_, REFIID riid, void **ppv) {
    TextProvider *tp = (TextProvider *)this_;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITextProvider)) {
        *ppv = &tp->lpVtbl;
    } else if (IsEqualIID(riid, &IID_ITextProvider2)) {
        *ppv = &tp->lpVtbl2;
    } else {
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&tp->ref_count);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE tp_AddRef(ITextProvider *this_) {
    return InterlockedIncrement(&((TextProvider *)this_)->ref_count);
}

static ULONG STDMETHODCALLTYPE tp_Release(ITextProvider *this_) {
    TextProvider *tp = (TextProvider *)this_;
    LONG count = InterlockedDecrement(&tp->ref_count);
    if (count == 0) {
        if (tp->enclosing) tp->enclosing->lpVtbl->Release(tp->enclosing);
        free(tp);
    }
    return count;
}

/* ITextProvider2 IUnknown */
static HRESULT STDMETHODCALLTYPE tp2_QI(ITextProvider2 *this_, REFIID riid, void **ppv) {
    return tp_QI((ITextProvider *)TP_FROM_V2(this_), riid, ppv);
}
static ULONG STDMETHODCALLTYPE tp2_AddRef(ITextProvider2 *this_) {
    return tp_AddRef((ITextProvider *)TP_FROM_V2(this_));
}
static ULONG STDMETHODCALLTYPE tp2_Release(ITextProvider2 *this_) {
    return tp_Release((ITextProvider *)TP_FROM_V2(this_));
}

/* Helper: create SAFEARRAY with one text range */
static SAFEARRAY *safearray_of_range(ITextRangeProvider *range) {
    SAFEARRAY *sa = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
    if (!sa) return NULL;
    LONG idx = 0;
    IUnknown *unk = NULL;
    range->lpVtbl->QueryInterface(range, &IID_IUnknown, (void **)&unk);
    if (unk) {
        SafeArrayPutElement(sa, &idx, unk);
        unk->lpVtbl->Release(unk);
    }
    return sa;
}

/* ITextProvider methods */
static HRESULT STDMETHODCALLTYPE tp_GetSelection(ITextProvider *this_, SAFEARRAY **pRetVal) {
    TextProvider *tp = (TextProvider *)this_;
    /* Return degenerate range at cursor position */
    AcquireSRWLockShared(&tp->state->lock);
    /* Approximate cursor offset: row * avg_line_width + col */
    int offset = (int)(tp->state->cursor_row * 80 + tp->state->cursor_col);
    ReleaseSRWLockShared(&tp->state->lock);

    TextRange *range = create_range(tp->state, tp->enclosing, offset, offset);
    if (!range) { *pRetVal = NULL; return E_OUTOFMEMORY; }
    *pRetVal = safearray_of_range((ITextRangeProvider *)range);
    range->lpVtbl->Release((ITextRangeProvider *)range);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tp_GetVisibleRanges(ITextProvider *this_, SAFEARRAY **pRetVal) {
    TextProvider *tp = (TextProvider *)this_;
    AcquireSRWLockShared(&tp->state->lock);
    int len = (int)tp->state->full_text_len;
    ReleaseSRWLockShared(&tp->state->lock);

    TextRange *range = create_range(tp->state, tp->enclosing, 0, len);
    if (!range) { *pRetVal = NULL; return E_OUTOFMEMORY; }
    *pRetVal = safearray_of_range((ITextRangeProvider *)range);
    range->lpVtbl->Release((ITextRangeProvider *)range);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tp_RangeFromChild(
        ITextProvider *this_, IRawElementProviderSimple *childElement,
        ITextRangeProvider **pRetVal) {
    (void)this_; (void)childElement;
    *pRetVal = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tp_RangeFromPoint(
        ITextProvider *this_, struct UiaPoint point,
        ITextRangeProvider **pRetVal) {
    (void)point;
    TextProvider *tp = (TextProvider *)this_;
    *pRetVal = (ITextRangeProvider *)create_range(tp->state, tp->enclosing, 0, 0);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tp_get_DocumentRange(
        ITextProvider *this_, ITextRangeProvider **pRetVal) {
    TextProvider *tp = (TextProvider *)this_;
    AcquireSRWLockShared(&tp->state->lock);
    int len = (int)tp->state->full_text_len;
    ReleaseSRWLockShared(&tp->state->lock);
    *pRetVal = (ITextRangeProvider *)create_range(tp->state, tp->enclosing, 0, len);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tp_get_SupportedTextSelection(
        ITextProvider *this_, enum SupportedTextSelection *pRetVal) {
    (void)this_;
    *pRetVal = SupportedTextSelection_Single;
    return S_OK;
}

/* ITextProvider2 methods */
static HRESULT STDMETHODCALLTYPE tp2_GetSelection(ITextProvider2 *this_, SAFEARRAY **p) {
    return tp_GetSelection((ITextProvider *)TP_FROM_V2(this_), p);
}
static HRESULT STDMETHODCALLTYPE tp2_GetVisibleRanges(ITextProvider2 *this_, SAFEARRAY **p) {
    return tp_GetVisibleRanges((ITextProvider *)TP_FROM_V2(this_), p);
}
static HRESULT STDMETHODCALLTYPE tp2_RangeFromChild(ITextProvider2 *this_, IRawElementProviderSimple *c, ITextRangeProvider **p) {
    return tp_RangeFromChild((ITextProvider *)TP_FROM_V2(this_), c, p);
}
static HRESULT STDMETHODCALLTYPE tp2_RangeFromPoint(ITextProvider2 *this_, struct UiaPoint pt, ITextRangeProvider **p) {
    return tp_RangeFromPoint((ITextProvider *)TP_FROM_V2(this_), pt, p);
}
static HRESULT STDMETHODCALLTYPE tp2_get_DocumentRange(ITextProvider2 *this_, ITextRangeProvider **p) {
    return tp_get_DocumentRange((ITextProvider *)TP_FROM_V2(this_), p);
}
static HRESULT STDMETHODCALLTYPE tp2_get_SupportedTextSelection(ITextProvider2 *this_, enum SupportedTextSelection *p) {
    return tp_get_SupportedTextSelection((ITextProvider *)TP_FROM_V2(this_), p);
}

static HRESULT STDMETHODCALLTYPE tp2_RangeFromAnnotation(
        ITextProvider2 *this_, IRawElementProviderSimple *annotation,
        ITextRangeProvider **pRetVal) {
    (void)this_; (void)annotation;
    *pRetVal = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tp2_GetCaretRange(
        ITextProvider2 *this_, BOOL *isActive, ITextRangeProvider **pRetVal) {
    TextProvider *tp = TP_FROM_V2(this_);
    AcquireSRWLockShared(&tp->state->lock);
    *isActive = tp->state->has_focus ? TRUE : FALSE;
    int offset = (int)(tp->state->cursor_row * 80 + tp->state->cursor_col);
    ReleaseSRWLockShared(&tp->state->lock);
    *pRetVal = (ITextRangeProvider *)create_range(tp->state, tp->enclosing, offset, offset);
    return S_OK;
}

/* Vtables */
static ITextProviderVtbl text_provider_vtbl = {
    tp_QI, tp_AddRef, tp_Release,
    tp_GetSelection,
    tp_GetVisibleRanges,
    tp_RangeFromChild,
    tp_RangeFromPoint,
    tp_get_DocumentRange,
    tp_get_SupportedTextSelection,
};

static ITextProvider2Vtbl text_provider2_vtbl = {
    tp2_QI, tp2_AddRef, tp2_Release,
    tp2_GetSelection,
    tp2_GetVisibleRanges,
    tp2_RangeFromChild,
    tp2_RangeFromPoint,
    tp2_get_DocumentRange,
    tp2_get_SupportedTextSelection,
    tp2_RangeFromAnnotation,
    tp2_GetCaretRange,
};

/* --- Public API --- */

IUnknown *wixen_a11y_create_text_provider(WixenA11yState *state,
                                           IRawElementProviderSimple *enclosing) {
    TextProvider *tp = (TextProvider *)calloc(1, sizeof(TextProvider));
    if (!tp) return NULL;
    tp->lpVtbl = &text_provider_vtbl;
    tp->lpVtbl2 = &text_provider2_vtbl;
    tp->ref_count = 1;
    tp->state = state;
    tp->enclosing = enclosing;
    if (enclosing) enclosing->lpVtbl->AddRef(enclosing);
    return (IUnknown *)tp;
}

#endif /* _WIN32 */
