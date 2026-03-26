/* test_red_text_change_registration.c -- RED-GREEN TDD for NVDA textChange registration
 *
 * NVDA refuses to register for textChange events from our window unless
 * certain UIA properties are set correctly. Specifically, NVDA's
 * winConsoleUIA module checks LocalizedControlType == "terminal".
 *
 * These tests verify that our provider exposes the properties NVDA needs
 * to register for textChange events, exercised through the COM vtable
 * function pointers without needing a real HWND or COM registration.
 *
 * Tests:
 * 1. ControlType is UIA_DocumentControlTypeId (50030)
 * 2. IsTextPatternAvailable returns VARIANT_TRUE
 * 3. LocalizedControlType is "terminal"
 * 4. GetPatternProvider returns non-NULL for UIA_TextPatternId
 */
#ifdef _WIN32

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <oleauto.h>
#include "greatest.h"

#define WIXEN_A11Y_INTERNAL
#include "wixen/a11y/provider.h"
#include "wixen/a11y/text_provider.h"
#include "wixen/a11y/state.h"

/* =========================================================
 * Helpers
 * ========================================================= */

static WixenA11yState *make_state(const char *text) {
    WixenA11yState *s = (WixenA11yState *)calloc(1, sizeof(WixenA11yState));
    if (!s) return NULL;
    InitializeSRWLock(&s->lock);
    s->full_text = _strdup(text ? text : "");
    s->full_text_len = text ? strlen(text) : 0;
    s->has_focus = true;
    s->hwnd = NULL;
    return s;
}

static void free_state(WixenA11yState *s) {
    if (!s) return;
    free(s->full_text);
    free(s);
}

/* Create provider without a real HWND. UiaHostProviderFromHwnd(NULL)
 * may fail, but that's fine -- we only test property/pattern logic. */
static IRawElementProviderSimple *make_provider(WixenA11yState *state) {
    return wixen_a11y_create_provider(NULL, state);
}

/* =========================================================
 * Test 1: ControlType is UIA_DocumentControlTypeId (50030)
 *
 * NVDA uses ControlType to determine the NVDAObject class.
 * Document type allows text reading and is compatible with
 * terminal recognition when paired with LocalizedControlType.
 * ========================================================= */

TEST control_type_is_document(void) {
    WixenA11yState *state = make_state("hello");
    IRawElementProviderSimple *provider = make_provider(state);
    ASSERT(provider != NULL);

    VARIANT val;
    VariantInit(&val);
    HRESULT hr = provider->lpVtbl->GetPropertyValue(
        provider, UIA_ControlTypePropertyId, &val);
    ASSERT_EQ(S_OK, hr);
    ASSERT_EQ(VT_I4, val.vt);
    ASSERT_EQ(UIA_DocumentControlTypeId, val.lVal);

    VariantClear(&val);
    provider->lpVtbl->Release(provider);
    free_state(state);
    PASS();
}

/* =========================================================
 * Test 2: IsTextPatternAvailable returns VARIANT_TRUE
 *
 * NVDA checks this property to decide whether to use text
 * navigation commands. If this is FALSE or missing, NVDA
 * may skip textChange event registration entirely.
 * ========================================================= */

TEST is_text_pattern_available(void) {
    WixenA11yState *state = make_state("hello");
    IRawElementProviderSimple *provider = make_provider(state);
    ASSERT(provider != NULL);

    VARIANT val;
    VariantInit(&val);
    HRESULT hr = provider->lpVtbl->GetPropertyValue(
        provider, UIA_IsTextPatternAvailablePropertyId, &val);
    ASSERT_EQ(S_OK, hr);
    ASSERT_EQ(VT_BOOL, val.vt);
    ASSERT_EQ(VARIANT_TRUE, val.boolVal);

    VariantClear(&val);
    provider->lpVtbl->Release(provider);
    free_state(state);
    PASS();
}

/* =========================================================
 * Test 3: LocalizedControlType is "terminal"
 *
 * This is the KEY property. NVDA's winConsoleUIA module checks:
 *   if obj.UIAElement.cachedLocalizedControlType == "terminal":
 * If this matches, NVDA treats us as a terminal and registers
 * for textChange events via _get__shouldRegisterForUiTextChangeEvents.
 * ========================================================= */

TEST localized_control_type_is_terminal(void) {
    WixenA11yState *state = make_state("hello");
    IRawElementProviderSimple *provider = make_provider(state);
    ASSERT(provider != NULL);

    VARIANT val;
    VariantInit(&val);
    HRESULT hr = provider->lpVtbl->GetPropertyValue(
        provider, UIA_LocalizedControlTypePropertyId, &val);
    ASSERT_EQ(S_OK, hr);
    ASSERT_EQ(VT_BSTR, val.vt);
    ASSERT(val.bstrVal != NULL);

    /* Compare BSTR (wide string) to expected value */
    ASSERT_EQ(0, wcscmp(val.bstrVal, L"terminal"));

    VariantClear(&val);
    provider->lpVtbl->Release(provider);
    free_state(state);
    PASS();
}

/* =========================================================
 * Test 4: GetPatternProvider returns non-NULL for TextPattern
 *
 * NVDA calls GetPatternProvider(UIA_TextPatternId) to get the
 * ITextProvider. If this returns NULL, there's no text pattern
 * and NVDA won't register for textChange events.
 * ========================================================= */

TEST text_pattern_provider_exists(void) {
    WixenA11yState *state = make_state("hello terminal");
    IRawElementProviderSimple *provider = make_provider(state);
    ASSERT(provider != NULL);

    IUnknown *pattern = NULL;
    HRESULT hr = provider->lpVtbl->GetPatternProvider(
        provider, UIA_TextPatternId, &pattern);
    ASSERT_EQ(S_OK, hr);
    ASSERT(pattern != NULL);

    /* Also verify TextPattern2 works */
    IUnknown *pattern2 = NULL;
    hr = provider->lpVtbl->GetPatternProvider(
        provider, UIA_TextPattern2Id, &pattern2);
    ASSERT_EQ(S_OK, hr);
    ASSERT(pattern2 != NULL);

    if (pattern2) pattern2->lpVtbl->Release(pattern2);
    if (pattern) pattern->lpVtbl->Release(pattern);
    provider->lpVtbl->Release(provider);
    free_state(state);
    PASS();
}

/* =========================================================
 * Test 5: IsTextPattern2Available returns VARIANT_TRUE
 *
 * TextPattern2 extends TextPattern with additional methods.
 * NVDA may check this for advanced text change notifications.
 * ========================================================= */

TEST is_text_pattern2_available(void) {
    WixenA11yState *state = make_state("hello");
    IRawElementProviderSimple *provider = make_provider(state);
    ASSERT(provider != NULL);

    VARIANT val;
    VariantInit(&val);
    HRESULT hr = provider->lpVtbl->GetPropertyValue(
        provider, UIA_IsTextPattern2AvailablePropertyId, &val);
    ASSERT_EQ(S_OK, hr);
    ASSERT_EQ(VT_BOOL, val.vt);
    ASSERT_EQ(VARIANT_TRUE, val.boolVal);

    VariantClear(&val);
    provider->lpVtbl->Release(provider);
    free_state(state);
    PASS();
}

SUITE(text_change_registration) {
    RUN_TEST(control_type_is_document);
    RUN_TEST(is_text_pattern_available);
    RUN_TEST(localized_control_type_is_terminal);
    RUN_TEST(text_pattern_provider_exists);
    RUN_TEST(is_text_pattern2_available);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(text_change_registration);
    GREATEST_MAIN_END();
}

#else /* !_WIN32 */

#include "greatest.h"
GREATEST_MAIN_DEFS();

TEST skip_non_windows(void) { PASS(); }

SUITE(text_change_registration) { RUN_TEST(skip_non_windows); }

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(text_change_registration);
    GREATEST_MAIN_END();
}

#endif /* _WIN32 */
