/* text_provider.h — ITextProvider2 implementation for terminal text */
#ifndef WIXEN_A11Y_TEXT_PROVIDER_H
#define WIXEN_A11Y_TEXT_PROVIDER_H

#ifdef _WIN32

#ifndef WIXEN_A11Y_INTERNAL
#define WIXEN_A11Y_INTERNAL
#endif
#include "wixen/a11y/provider.h"
/* uiautomation.h is included via provider.h when WIXEN_A11Y_INTERNAL
 * is defined — do not include it again to avoid duplicate symbols. */

/* Create ITextProvider for the terminal.
 * Returns IUnknown* that can be cast to ITextProvider. Caller releases. */
IUnknown *wixen_a11y_create_text_provider(WixenA11yState *state,
                                           IRawElementProviderSimple *enclosing);

#endif /* _WIN32 */

/* Pure text boundary logic — no Win32 dependency.
 * Used by ITextRangeProvider::ExpandToEnclosingUnit. */

#include <stddef.h>

/* Find the line containing offset. Sets start/end to line boundaries. */
void wixen_text_line_at(const char *text, size_t text_len, size_t offset,
                         size_t *out_start, size_t *out_end);

/* Find the word containing offset. */
void wixen_text_word_at(const char *text, size_t text_len, size_t offset,
                         size_t *out_start, size_t *out_end);

/* Convert byte offset to row/col. */
void wixen_text_offset_to_rowcol(const char *text, size_t text_len, size_t offset,
                                   size_t *out_row, size_t *out_col);

/* Convert row/col to byte offset. */
size_t wixen_text_rowcol_to_offset(const char *text, size_t text_len,
                                     size_t row, size_t col);

#endif /* WIXEN_A11Y_TEXT_PROVIDER_H */
