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
#include "wixen/a11y/text_boundaries.h"
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
    TextRange *r = (TextRange *)this_;
    *pRetVal = NULL;
    if (!text || !r->state) return S_OK;

    /* Dynamic buffer — no truncation (P0.5 fix) */
    AcquireSRWLockShared(&r->state->lock);
    size_t flen = r->state->full_text_len;
    char *full = (char *)malloc(flen + 1);
    if (!full) { ReleaseSRWLockShared(&r->state->lock); return S_OK; }
    if (r->state->full_text) memcpy(full, r->state->full_text, flen);
    full[flen] = '\0';
    ReleaseSRWLockShared(&r->state->lock);

    /* Convert BSTR query to UTF-8 */
    int qlen = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (qlen <= 0) { free(full); return S_OK; }
    char *query = (char *)malloc(qlen);
    if (!query) { free(full); return S_OK; }
    WideCharToMultiByte(CP_UTF8, 0, text, -1, query, qlen, NULL, NULL);

    size_t start, end;
    size_t from = (backward || r->start < 0) ? 0 : (size_t)r->start;
    bool found = wixen_text_find(full, flen, query, from, backward != 0, ignoreCase != 0, &start, &end);
    free(query);
    free(full);

    if (found) {
        *pRetVal = (ITextRangeProvider *)create_range(r->state, r->enclosing, (int)start, (int)end);
    }
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
    TextRange *r = (TextRange *)this_;
    AcquireSRWLockShared(&r->state->lock);
    float cw = r->state->cell_width;
    float ch = r->state->cell_height;
    size_t flen = r->state->full_text_len;
    const char *text = r->state->full_text;

    if (cw <= 0 || ch <= 0 || !text || r->start >= r->end) {
        ReleaseSRWLockShared(&r->state->lock);
        *pRetVal = SafeArrayCreateVector(VT_R8, 0, 0);
        return S_OK;
    }

    /* Convert start/end to row/col */
    size_t sr, sc, er, ec;
    wixen_text_offset_to_rowcol(text, flen, (size_t)r->start, &sr, &sc);
    wixen_text_offset_to_rowcol(text, flen, (size_t)r->end, &er, &ec);

    POINT origin = { 0, 0 };
    ClientToScreen(r->state->hwnd, &origin);
    RECT client;
    GetClientRect(r->state->hwnd, &client);
    double win_w = (double)(client.right - client.left);
    ReleaseSRWLockShared(&r->state->lock);

    /* One rect per line in the range */
    size_t num_lines = er - sr + 1;
    *pRetVal = SafeArrayCreateVector(VT_R8, 0, (ULONG)(num_lines * 4));
    if (!*pRetVal) return S_OK;

    for (size_t line = 0; line < num_lines; line++) {
        size_t row = sr + line;
        double x, w;
        if (num_lines == 1) {
            /* Single line: exact col range */
            x = origin.x + sc * cw;
            w = (ec - sc) * cw;
        } else if (line == 0) {
            /* First line: from start col to end of line */
            x = origin.x + sc * cw;
            w = win_w - sc * cw;
        } else if (line == num_lines - 1) {
            /* Last line: from start to end col */
            x = origin.x;
            w = ec * cw;
        } else {
            /* Middle line: full width */
            x = origin.x;
            w = win_w;
        }
        double y = origin.y + row * ch;
        double h = ch;

        LONG idx;
        idx = (LONG)(line * 4);     SafeArrayPutElement(*pRetVal, &idx, &x);
        idx = (LONG)(line * 4 + 1); SafeArrayPutElement(*pRetVal, &idx, &y);
        idx = (LONG)(line * 4 + 2); SafeArrayPutElement(*pRetVal, &idx, &w);
        idx = (LONG)(line * 4 + 3); SafeArrayPutElement(*pRetVal, &idx, &h);
    }
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
    TextRange *r = (TextRange *)this_;
    *pRetVal = 0;
    if (count == 0 || !r->state) return S_OK;

    /* Dynamic buffer — no truncation (Copilot P6 fix) */
    AcquireSRWLockShared(&r->state->lock);
    size_t flen = r->state->full_text_len;
    char *full = (char *)malloc(flen + 1);
    if (!full) { ReleaseSRWLockShared(&r->state->lock); return S_OK; }
    if (r->state->full_text) memcpy(full, r->state->full_text, flen);
    full[flen] = '\0';
    ReleaseSRWLockShared(&r->state->lock);

    size_t pos = (r->start >= 0) ? (size_t)r->start : 0;
    WixenTextUnit tu = (WixenTextUnit)unit;
    int moved = wixen_text_move_by_unit(full, flen, &pos, tu, count);
    r->start = (int)pos;
    r->end = (int)pos; /* Degenerate range after move */
    *pRetVal = moved;
    free(full);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_MoveEndpointByUnit(
        ITextRangeProvider *this_, enum TextPatternRangeEndpoint endpoint,
        enum TextUnit unit, int count, int *pRetVal) {
    TextRange *r = (TextRange *)this_;
    *pRetVal = 0;
    if (count == 0 || !r->state) return S_OK;

    /* Dynamic buffer (Copilot P6 fix) */
    AcquireSRWLockShared(&r->state->lock);
    size_t flen = r->state->full_text_len;
    char *full = (char *)malloc(flen + 1);
    if (!full) { ReleaseSRWLockShared(&r->state->lock); return S_OK; }
    if (r->state->full_text) memcpy(full, r->state->full_text, flen);
    full[flen] = '\0';
    ReleaseSRWLockShared(&r->state->lock);

    int *target = (endpoint == TextPatternRangeEndpoint_Start) ? &r->start : &r->end;
    size_t pos = (*target >= 0) ? (size_t)*target : 0;
    /* Use wixen_text_move_endpoint which handles word/line end correctly */
    WixenTextUnit tu = (WixenTextUnit)unit;
    int moved = wixen_text_move_endpoint(full, flen, &pos, tu, count);
    *target = (int)pos;
    /* Ensure start <= end */
    if (r->start > r->end) {
        if (endpoint == TextPatternRangeEndpoint_Start) r->end = r->start;
        else r->start = r->end;
    }
    *pRetVal = moved;
    free(full);
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
    /* Lock-free cursor offset — updated atomically by main loop */
    int offset = wixen_a11y_get_cursor_offset();

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
    TextProvider *tp = (TextProvider *)this_;
    AcquireSRWLockShared(&tp->state->lock);
    float cw = tp->state->cell_width;
    float ch = tp->state->cell_height;
    size_t flen = tp->state->full_text_len;
    const char *text = tp->state->full_text;

    int offset = 0;
    if (cw > 0 && ch > 0 && text) {
        /* Convert screen point to row/col */
        POINT origin = { 0, 0 };
        ClientToScreen(tp->state->hwnd, &origin);
        double local_x = point.x - origin.x;
        double local_y = point.y - origin.y;
        size_t row = (local_y > 0) ? (size_t)(local_y / ch) : 0;
        size_t col = (local_x > 0) ? (size_t)(local_x / cw) : 0;
        offset = (int)wixen_text_rowcol_to_offset(text, flen, row, col);
    }
    ReleaseSRWLockShared(&tp->state->lock);

    *pRetVal = (ITextRangeProvider *)create_range(tp->state, tp->enclosing, offset, offset);
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
    *isActive = tp->state->has_focus ? TRUE : FALSE;
    int offset = wixen_a11y_get_cursor_offset();
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
