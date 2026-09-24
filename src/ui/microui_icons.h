/* The icon bitmaps (close, check, expand/collapse) from microui's demo
 * atlas. The atlas source uses C99 designated array initialisers, which C++
 * does not accept, so it is compiled as C behind this small interface. */
#ifndef VIGIDE_UI_MICROUI_ICONS_H
#define VIGIDE_UI_MICROUI_ICONS_H

#include "ui/mu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Row-major R8 coverage for the whole demo atlas. */
const unsigned char* vig_mu_atlas_pixels(void);
int vig_mu_atlas_width(void);
/* Location of MU_ICON_* inside that atlas. */
mu_Rect vig_mu_icon_rect(int icon);

#ifdef __cplusplus
}
#endif

#endif
