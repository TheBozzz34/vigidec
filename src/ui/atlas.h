/* Access to microui's built-in font/icon atlas (demo/atlas.inl).
 *
 * The atlas source uses C99 designated array initialisers, which C++ does
 * not accept, so it is compiled as C behind this small interface. */
#ifndef VIGIDE_UI_ATLAS_H
#define VIGIDE_UI_ATLAS_H

#include "ui/mu.h"

#ifdef __cplusplus
extern "C" {
#endif

int vig_atlas_width(void);
int vig_atlas_height(void);
/* Single-channel (R8) coverage, vig_atlas_width() * vig_atlas_height() bytes. */
const unsigned char* vig_atlas_pixels(void);

mu_Rect vig_atlas_white(void);    /* fully opaque cell used for solid fills */
mu_Rect vig_atlas_icon(int icon); /* MU_ICON_* */
mu_Rect vig_atlas_glyph(unsigned char c);

/* Signatures match mu_Context::text_width / text_height. */
int vig_text_width(mu_Font font, const char* text, int len);
int vig_text_height(mu_Font font);

#ifdef __cplusplus
}
#endif

#endif
