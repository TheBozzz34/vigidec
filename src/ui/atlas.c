#include "ui/atlas.h"

#include "atlas.inl"

int vig_atlas_width(void) { return ATLAS_WIDTH; }
int vig_atlas_height(void) { return ATLAS_HEIGHT; }
const unsigned char* vig_atlas_pixels(void) { return atlas_texture; }

mu_Rect vig_atlas_white(void) { return atlas[ATLAS_WHITE]; }
mu_Rect vig_atlas_icon(int icon) { return atlas[icon]; }

mu_Rect vig_atlas_glyph(unsigned char c) {
    /* The atlas only covers ASCII; everything else maps to the last glyph. */
    if (c > 127) c = 127;
    return atlas[ATLAS_FONT + c];
}

int vig_text_width(mu_Font font, const char* text, int len) {
    const char* p;
    int width = 0;
    (void)font;
    for (p = text; *p && len--; p++) {
        if ((*p & 0xc0) == 0x80) continue; /* skip UTF-8 continuation bytes */
        width += vig_atlas_glyph((unsigned char)*p).w;
    }
    return width;
}

int vig_text_height(mu_Font font) {
    (void)font;
    return 18;
}
