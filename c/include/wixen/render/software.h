/* software.h — GDI-based software renderer fallback */
#ifndef WIXEN_RENDER_SOFTWARE_H
#define WIXEN_RENDER_SOFTWARE_H

#ifdef _WIN32

#include "wixen/render/colors.h"
#include "wixen/core/grid.h"
#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

typedef struct WixenSoftwareRenderer WixenSoftwareRenderer;

WixenSoftwareRenderer *wixen_soft_create(HWND hwnd, uint32_t width, uint32_t height,
                                          const WixenColorScheme *colors);
void wixen_soft_destroy(WixenSoftwareRenderer *r);
void wixen_soft_resize(WixenSoftwareRenderer *r, uint32_t width, uint32_t height);
void wixen_soft_render(WixenSoftwareRenderer *r, const WixenGrid *grid);

#endif /* _WIN32 */
#endif /* WIXEN_RENDER_SOFTWARE_H */
