/* text_provider.h — ITextProvider2 implementation for terminal text */
#ifndef WIXEN_A11Y_TEXT_PROVIDER_H
#define WIXEN_A11Y_TEXT_PROVIDER_H

#ifdef _WIN32

#ifndef WIXEN_A11Y_INTERNAL
#define WIXEN_A11Y_INTERNAL
#endif
#include "wixen/a11y/provider.h"
#include <uiautomation.h>

/* Create ITextProvider for the terminal.
 * Returns IUnknown* that can be cast to ITextProvider. Caller releases. */
IUnknown *wixen_a11y_create_text_provider(WixenA11yState *state,
                                           IRawElementProviderSimple *enclosing);

#endif /* _WIN32 */
#endif /* WIXEN_A11Y_TEXT_PROVIDER_H */
