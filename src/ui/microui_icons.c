#include "ui/microui_icons.h"

#include "atlas.inl"

const unsigned char* vig_mu_atlas_pixels(void) { return atlas_texture; }
int vig_mu_atlas_width(void) { return ATLAS_WIDTH; }
mu_Rect vig_mu_icon_rect(int icon) { return atlas[icon]; }
