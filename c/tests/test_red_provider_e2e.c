/* test_red_provider_e2e.c — RED-GREEN TDD for end-to-end provider integration
 *
 * Exercises the full a11y pipeline at the C API level:
 *   state update -> text_provider -> ITextProvider/ITextRangeProvider vtable calls
 *
 * No real HWND or COM registration needed — we test the logic layer directly
 * through the COM vtable function pointers.
 */
#ifdef _WIN32

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <oleauto.h>
#include "greatest.h"

/* Need internal struct access for WixenA11yState fields used by text_provider */
#define WIXEN_A11Y_INTERNAL
#include "wixen/a11y/provider.h"
#include "wixen/a11y/text_provider.h"
#include "wixen/a11y/state.h"

/* =========================================================
 * Helpers
 * ========================================================= */

/* Create a WixenA11yState on the heap with text and focus set.
 * Uses the internal struct layout from provider.h. */
static WixenA11yState *make_state(const char *text) {
    WixenA11yState *s = (WixenA11yState *)calloc(1, sizeof(WixenA11yState));
    if (!s) return NULL;
    InitializeSRWLock(&s->lock);
    s->full_text = _strdup(text ? text : "");
    s->full_text_len = text ? strlen(text) : 0;
    s->has_focus = true;
    s->hwnd = NULL; /* No real window needed for logic tests */
    return s;
}

static void free_state(WixenA11yState *s) {
    if (!s) return;
    free(s->full_text);
    free(s);
}

/* Create text provider and return as ITextProvider*.
 * enclosing=NULL is fine for logic tests (no real UIA provider). */
static ITextProvider *make_text_provider(WixenA11yState *state) {
    IUnknown *unk = wixen_a11y_create_text_provider(state, NULL);
    if (!unk) return NULL;
    /* The returned IUnknown IS the ITextProvider (first vtable) */
    return (ITextProvider *)unk;
}

/* Extract BSTR to a static C string for assertions. Not thread-safe. */
static const char *bstr_to_cstr(BSTR b) {
    static char buf[4096];
    if (!b) { buf[0] = '\0'; return buf; }
    int len = WideCharToMultiByte(CP_UTF8, 0, b, -1, buf, sizeof(buf) - 1, NULL, NULL);
    if (len <= 0) buf[0] = '\0';
    return buf;
}

/* =========================================================
 * 1. Provider text visibility: after setting text on state,
 *    DocumentRange->GetText returns that text.
 * ========================================================= */

TEST provider_text_visibility(void) {
    WixenA11yState *state = make_state("Hello terminal");
    ITextProvider *tp = make_text_provider(state);
    ASSERT(tp != NULL);

    /* Get document range */
    ITextRangeProvider *doc_range = NULL;
    HRESULT hr = tp->lpVtbl->get_DocumentRange(tp, &doc_range);
    ASSERT_EQ(S_OK, hr);
    ASSERT(doc_range != NULL);

    /* GetText on the document range */
    BSTR text = NULL;
    hr = doc_range->lpVtbl->GetText(doc_range, -1, &text);
    ASSERT_EQ(S_OK, hr);
    ASSERT(text != NULL);
    ASSERT_STR_EQ("Hello terminal", bstr_to_cstr(text));

    SysFreeString(text);
    doc_range->lpVtbl->Release(doc_range);
    tp->lpVtbl->Release(tp);
    free_state(state);
    PASS();
}

/* =========================================================
 * 2. Caret range: after setting cursor offset via the global
 *    wixen_a11y_set_cursor_offset, GetCaretRange returns a
 *    degenerate range at that offset.
 * ========================================================= */

TEST provider_caret_range(void) {
    WixenA11yState *state = make_state("Line1\nLine2\nLine3");
    ITextProvider *tp = make_text_provider(state);
    ASSERT(tp != NULL);

    /* Set global cursor offset to position 8 (middle of "Line2") */
    wixen_a11y_set_cursor_offset(8);

    /* Get ITextProvider2 for GetCaretRange */
    ITextProvider2 *tp2 = NULL;
    HRESULT hr = tp->lpVtbl->QueryInterface(tp, &IID_ITextProvider2, (void **)&tp2);
    ASSERT_EQ(S_OK, hr);
    ASSERT(tp2 != NULL);

    BOOL is_active = FALSE;
    ITextRangeProvider *caret = NULL;
    hr = tp2->lpVtbl->GetCaretRange(tp2, &is_active, &caret);
    ASSERT_EQ(S_OK, hr);
    ASSERT(caret != NULL);
    ASSERT(is_active); /* has_focus is true */

    /* Caret range should be degenerate (start == end).
     * Expand to character to get a 1-char range we can read. */
    ITextRangeProvider *clone = NULL;
    hr = caret->lpVtbl->Clone(caret, &clone);
    ASSERT_EQ(S_OK, hr);
    hr = clone->lpVtbl->ExpandToEnclosingUnit(clone, TextUnit_Character);
    ASSERT_EQ(S_OK, hr);

    BSTR text = NULL;
    hr = clone->lpVtbl->GetText(clone, 1, &text);
    ASSERT_EQ(S_OK, hr);
    ASSERT(text != NULL);
    /* Offset 8 in "Line1\nLine2\nLine3" is 'n' in "Line2" */
    ASSERT_STR_EQ("n", bstr_to_cstr(text));

    SysFreeString(text);
    clone->lpVtbl->Release(clone);
    caret->lpVtbl->Release(caret);
    tp2->lpVtbl->Release(tp2);
    tp->lpVtbl->Release(tp);
    free_state(state);
    PASS();
}

/* =========================================================
 * 3. Selection changed: after setting cursor offset,
 *    GetSelection returns a range at the new position.
 * ========================================================= */

TEST provider_selection_updated(void) {
    WixenA11yState *state = make_state("ABCDEFGHIJ");
    ITextProvider *tp = make_text_provider(state);
    ASSERT(tp != NULL);

    /* Move cursor to offset 5 */
    wixen_a11y_set_cursor_offset(5);

    SAFEARRAY *sa = NULL;
    HRESULT hr = tp->lpVtbl->GetSelection(tp, &sa);
    ASSERT_EQ(S_OK, hr);
    ASSERT(sa != NULL);

    /* Should have exactly one range */
    LONG lb = 0, ub = 0;
    SafeArrayGetLBound(sa, 1, &lb);
    SafeArrayGetUBound(sa, 1, &ub);
    ASSERT_EQ(0, (int)lb);
    ASSERT_EQ(0, (int)ub); /* one element: index 0 */

    /* Extract the range and expand to character */
    IUnknown *unk = NULL;
    LONG idx = 0;
    SafeArrayGetElement(sa, &idx, &unk);
    ASSERT(unk != NULL);

    ITextRangeProvider *sel = NULL;
    hr = unk->lpVtbl->QueryInterface(unk, &IID_ITextRangeProvider, (void **)&sel);
    ASSERT_EQ(S_OK, hr);

    /* Expand degenerate range to character at offset 5 */
    hr = sel->lpVtbl->ExpandToEnclosingUnit(sel, TextUnit_Character);
    ASSERT_EQ(S_OK, hr);

    BSTR text = NULL;
    hr = sel->lpVtbl->GetText(sel, 1, &text);
    ASSERT_EQ(S_OK, hr);
    /* Offset 5 in "ABCDEFGHIJ" is 'F' */
    ASSERT_STR_EQ("F", bstr_to_cstr(text));

    SysFreeString(text);
    sel->lpVtbl->Release(sel);
    unk->lpVtbl->Release(unk);
    SafeArrayDestroy(sa);
    tp->lpVtbl->Release(tp);
    free_state(state);
    PASS();
}

/* =========================================================
 * 4. Line expansion: given multi-line text,
 *    ExpandToEnclosingUnit(Line) on a mid-line offset
 *    should expand to the full line boundaries.
 * ========================================================= */

TEST provider_line_expansion(void) {
    const char *text = "First line\nSecond line\nThird line";
    WixenA11yState *state = make_state(text);
    ITextProvider *tp = make_text_provider(state);
    ASSERT(tp != NULL);

    /* Get document range, clone it, set to mid "Second line" */
    ITextRangeProvider *doc = NULL;
    tp->lpVtbl->get_DocumentRange(tp, &doc);
    ASSERT(doc != NULL);

    ITextRangeProvider *range = NULL;
    doc->lpVtbl->Clone(doc, &range);
    ASSERT(range != NULL);

    /* Move range to offset 15 (inside "Second line": 'o' in "Second") */
    /* We use Move by character to position ourselves */
    int moved = 0;
    HRESULT hr = range->lpVtbl->Move(range, TextUnit_Character, 15, &moved);
    ASSERT_EQ(S_OK, hr);

    /* Expand to line */
    hr = range->lpVtbl->ExpandToEnclosingUnit(range, TextUnit_Line);
    ASSERT_EQ(S_OK, hr);

    /* Read the full line text */
    BSTR line_text = NULL;
    hr = range->lpVtbl->GetText(range, -1, &line_text);
    ASSERT_EQ(S_OK, hr);
    ASSERT(line_text != NULL);

    const char *got = bstr_to_cstr(line_text);
    /* Should be "Second line\n" (includes trailing newline) */
    ASSERT(strstr(got, "Second line") != NULL);
    /* Should NOT contain "First" or "Third" */
    ASSERT(strstr(got, "First") == NULL);
    ASSERT(strstr(got, "Third") == NULL);

    SysFreeString(line_text);
    range->lpVtbl->Release(range);
    doc->lpVtbl->Release(doc);
    tp->lpVtbl->Release(tp);
    free_state(state);
    PASS();
}

/* =========================================================
 * 5. DocumentRange: should span the entire visible text.
 * ========================================================= */

TEST provider_document_range(void) {
    const char *text = "All the visible text here";
    WixenA11yState *state = make_state(text);
    ITextProvider *tp = make_text_provider(state);
    ASSERT(tp != NULL);

    ITextRangeProvider *doc = NULL;
    HRESULT hr = tp->lpVtbl->get_DocumentRange(tp, &doc);
    ASSERT_EQ(S_OK, hr);
    ASSERT(doc != NULL);

    BSTR bstr = NULL;
    hr = doc->lpVtbl->GetText(doc, -1, &bstr);
    ASSERT_EQ(S_OK, hr);
    ASSERT_STR_EQ("All the visible text here", bstr_to_cstr(bstr));

    SysFreeString(bstr);
    doc->lpVtbl->Release(doc);
    tp->lpVtbl->Release(tp);
    free_state(state);
    PASS();
}

/* =========================================================
 * 6. Visible ranges: GetVisibleRanges should return at
 *    least one range covering the text.
 * ========================================================= */

TEST provider_visible_ranges(void) {
    WixenA11yState *state = make_state("Visible content\nMore lines");
    ITextProvider *tp = make_text_provider(state);
    ASSERT(tp != NULL);

    SAFEARRAY *sa = NULL;
    HRESULT hr = tp->lpVtbl->GetVisibleRanges(tp, &sa);
    ASSERT_EQ(S_OK, hr);
    ASSERT(sa != NULL);

    LONG lb = 0, ub = -1;
    SafeArrayGetLBound(sa, 1, &lb);
    SafeArrayGetUBound(sa, 1, &ub);
    ASSERT(ub >= lb); /* At least one range */

    /* Extract first range and verify it has text */
    IUnknown *unk = NULL;
    LONG idx = 0;
    SafeArrayGetElement(sa, &idx, &unk);
    ASSERT(unk != NULL);

    ITextRangeProvider *vr = NULL;
    hr = unk->lpVtbl->QueryInterface(unk, &IID_ITextRangeProvider, (void **)&vr);
    ASSERT_EQ(S_OK, hr);

    BSTR text = NULL;
    hr = vr->lpVtbl->GetText(vr, -1, &text);
    ASSERT_EQ(S_OK, hr);
    ASSERT(text != NULL);
    /* Visible range should cover all text */
    ASSERT(wcslen(text) > 0);
    ASSERT(strstr(bstr_to_cstr(text), "Visible content") != NULL);

    SysFreeString(text);
    vr->lpVtbl->Release(vr);
    unk->lpVtbl->Release(unk);
    SafeArrayDestroy(sa);
    tp->lpVtbl->Release(tp);
    free_state(state);
    PASS();
}

/* =========================================================
 * 7. Non-ASCII text: provider should handle UTF-8 text
 *    without crashing.
 * ========================================================= */

TEST provider_non_ascii_text(void) {
    /* Mix of CJK, emoji, accented chars */
    const char *utf8 = "\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c";  /* 你好世界 */
    WixenA11yState *state = make_state(utf8);
    ITextProvider *tp = make_text_provider(state);
    ASSERT(tp != NULL);

    ITextRangeProvider *doc = NULL;
    HRESULT hr = tp->lpVtbl->get_DocumentRange(tp, &doc);
    ASSERT_EQ(S_OK, hr);
    ASSERT(doc != NULL);

    BSTR bstr = NULL;
    hr = doc->lpVtbl->GetText(doc, -1, &bstr);
    ASSERT_EQ(S_OK, hr);
    ASSERT(bstr != NULL);
    /* Should have produced some text (4 CJK characters = 4 UTF-16 units) */
    ASSERT(wcslen(bstr) > 0);

    /* Expand to line should not crash */
    ITextRangeProvider *clone = NULL;
    doc->lpVtbl->Clone(doc, &clone);
    hr = clone->lpVtbl->ExpandToEnclosingUnit(clone, TextUnit_Line);
    ASSERT_EQ(S_OK, hr);

    clone->lpVtbl->Release(clone);
    SysFreeString(bstr);
    doc->lpVtbl->Release(doc);
    tp->lpVtbl->Release(tp);
    free_state(state);
    PASS();
}

/* =========================================================
 * 8. Empty text: provider should handle empty/NULL text
 *    gracefully without crashing.
 * ========================================================= */

TEST provider_empty_text(void) {
    WixenA11yState *state = make_state("");
    ITextProvider *tp = make_text_provider(state);
    ASSERT(tp != NULL);

    /* DocumentRange on empty text */
    ITextRangeProvider *doc = NULL;
    HRESULT hr = tp->lpVtbl->get_DocumentRange(tp, &doc);
    ASSERT_EQ(S_OK, hr);
    ASSERT(doc != NULL);

    BSTR bstr = NULL;
    hr = doc->lpVtbl->GetText(doc, -1, &bstr);
    ASSERT_EQ(S_OK, hr);
    /* Empty string is acceptable */
    ASSERT(bstr != NULL);
    ASSERT_EQ(0, (int)wcslen(bstr));

    /* GetSelection on empty text should not crash */
    SAFEARRAY *sa = NULL;
    wixen_a11y_set_cursor_offset(0);
    hr = tp->lpVtbl->GetSelection(tp, &sa);
    ASSERT_EQ(S_OK, hr);
    ASSERT(sa != NULL);
    SafeArrayDestroy(sa);

    /* GetVisibleRanges on empty text should not crash */
    sa = NULL;
    hr = tp->lpVtbl->GetVisibleRanges(tp, &sa);
    ASSERT_EQ(S_OK, hr);
    ASSERT(sa != NULL);
    SafeArrayDestroy(sa);

    /* ExpandToEnclosingUnit(Line) on empty text should not crash */
    hr = doc->lpVtbl->ExpandToEnclosingUnit(doc, TextUnit_Line);
    ASSERT_EQ(S_OK, hr);

    SysFreeString(bstr);
    doc->lpVtbl->Release(doc);
    tp->lpVtbl->Release(tp);
    free_state(state);
    PASS();
}

/* Also test NULL text pointer */
TEST provider_null_text(void) {
    WixenA11yState *state = make_state(NULL);
    ITextProvider *tp = make_text_provider(state);
    ASSERT(tp != NULL);

    ITextRangeProvider *doc = NULL;
    HRESULT hr = tp->lpVtbl->get_DocumentRange(tp, &doc);
    ASSERT_EQ(S_OK, hr);

    BSTR bstr = NULL;
    hr = doc->lpVtbl->GetText(doc, -1, &bstr);
    ASSERT_EQ(S_OK, hr);

    /* GetCaretRange should not crash */
    ITextProvider2 *tp2 = NULL;
    hr = tp->lpVtbl->QueryInterface(tp, &IID_ITextProvider2, (void **)&tp2);
    ASSERT_EQ(S_OK, hr);
    BOOL active = FALSE;
    ITextRangeProvider *caret = NULL;
    hr = tp2->lpVtbl->GetCaretRange(tp2, &active, &caret);
    ASSERT_EQ(S_OK, hr);

    if (caret) caret->lpVtbl->Release(caret);
    tp2->lpVtbl->Release(tp2);
    SysFreeString(bstr);
    doc->lpVtbl->Release(doc);
    tp->lpVtbl->Release(tp);
    free_state(state);
    PASS();
}

/* =========================================================
 * Suite and main
 * ========================================================= */

SUITE(red_provider_e2e) {
    RUN_TEST(provider_text_visibility);
    RUN_TEST(provider_caret_range);
    RUN_TEST(provider_selection_updated);
    RUN_TEST(provider_line_expansion);
    RUN_TEST(provider_document_range);
    RUN_TEST(provider_visible_ranges);
    RUN_TEST(provider_non_ascii_text);
    RUN_TEST(provider_empty_text);
    RUN_TEST(provider_null_text);
}

static void com_init(void) {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    com_init();
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_provider_e2e);
    GREATEST_MAIN_END();
}

#else /* !_WIN32 */

#include "greatest.h"
GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    /* No tests on non-Windows */
    GREATEST_MAIN_END();
}

#endif /* _WIN32 */
