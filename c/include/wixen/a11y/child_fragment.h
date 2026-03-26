/* child_fragment.h — Command block child fragment for UIA navigation
 *
 * Each command block in the a11y tree gets a lightweight fragment
 * that screen readers can navigate to via the provider's Navigate().
 * No Win32 dependency — pure data for testability.
 */
#ifndef WIXEN_A11Y_CHILD_FRAGMENT_H
#define WIXEN_A11Y_CHILD_FRAGMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Uses WixenA11yTree from tree.h — include tree.h before this header */
#include "wixen/a11y/tree.h"

typedef struct WixenChildFragment {
    WixenA11yTree *tree;   /* Back-pointer to parent tree */
    size_t block_index;     /* Which command block this represents */
} WixenChildFragment;

/* Lifecycle */
WixenChildFragment *wixen_child_fragment_create(WixenA11yTree *tree, size_t block_index);
void wixen_child_fragment_destroy(WixenChildFragment *cf);

/* Properties */
const char *wixen_child_fragment_name(const WixenChildFragment *cf);
int32_t wixen_child_fragment_runtime_id(const WixenChildFragment *cf);
bool wixen_child_fragment_is_error(const WixenChildFragment *cf);
void wixen_child_fragment_row_range(const WixenChildFragment *cf,
                                     size_t *out_start, size_t *out_end);

/* Sibling navigation (returns index or -1) */
int wixen_child_fragment_next_sibling_index(const WixenA11yTree *tree, size_t index);
int wixen_child_fragment_prev_sibling_index(const WixenA11yTree *tree, size_t index);

#endif /* WIXEN_A11Y_CHILD_FRAGMENT_H */
