// Files embedded into the executable at build time (see vigide_embed_files).
#pragma once

#include <cstddef>

extern "C" {
extern const unsigned char vigide_font_mono[];
extern const size_t vigide_font_mono_size;
extern const unsigned char vigide_font_ui[];
extern const size_t vigide_font_ui_size;
}
