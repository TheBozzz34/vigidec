#include "render/font.hpp"

#include "assets.h"
#include "ui/microui_icons.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace vig {

namespace {

constexpr float kUiFontSize = 15.0f;
constexpr float kMonoFontSize = 15.0f;

void ft_check(FT_Error err, const char* what) {
    if (err) throw std::runtime_error(std::string("FreeType: ") + what + " failed (" + std::to_string(err) + ")");
}

}  // namespace

uint32_t utf8_decode(const char*& p, const char* end) {
    const auto* s = reinterpret_cast<const unsigned char*>(p);
    const auto* e = reinterpret_cast<const unsigned char*>(end);
    uint32_t c = s[0];
    int extra = 0;
    if (c < 0x80) {
        p += 1;
        return c;
    } else if ((c & 0xE0) == 0xC0) {
        c &= 0x1F;
        extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
        c &= 0x0F;
        extra = 2;
    } else if ((c & 0xF8) == 0xF0) {
        c &= 0x07;
        extra = 3;
    } else {
        p += 1;
        return 0xFFFD;
    }
    if (e - s <= extra) {
        p += 1;
        return 0xFFFD;
    }
    for (int i = 1; i <= extra; ++i) {
        if ((s[i] & 0xC0) != 0x80) {
            p += 1;
            return 0xFFFD;
        }
        c = (c << 6) | (s[i] & 0x3F);
    }
    p += extra + 1;
    return c;
}

// --- GlyphAtlas -------------------------------------------------------------

GlyphAtlas::GlyphAtlas(int width, int height)
    : width_(width), height_(height), pixels_(size_t(width) * size_t(height), 0) {}

bool GlyphAtlas::allocate(int w, int h, int& x, int& y) {
    if (w + 2 * kPadding > width_) return false;
    if (shelf_x_ + w + kPadding > width_) {
        shelf_y_ += shelf_h_ + kPadding;
        shelf_x_ = kPadding;
        shelf_h_ = 0;
    }
    while (shelf_y_ + h + kPadding > height_) {
        if (height_ * 2 > kMaxHeight) return false;
        height_ *= 2;
        pixels_.resize(size_t(width_) * size_t(height_), 0);
        ++version_;
    }
    x = shelf_x_;
    y = shelf_y_;
    shelf_x_ += w + kPadding;
    shelf_h_ = std::max(shelf_h_, h);
    return true;
}

void GlyphAtlas::blit(int x, int y, int w, int h, const uint8_t* src, int src_pitch) {
    for (int row = 0; row < h; ++row) {
        std::memcpy(&pixels_[size_t(y + row) * width_ + x], src + ptrdiff_t(row) * src_pitch, size_t(w));
    }
    ++version_;
}

void GlyphAtlas::fill(int x, int y, int w, int h, uint8_t value) {
    for (int row = 0; row < h; ++row) std::memset(&pixels_[size_t(y + row) * width_ + x], value, size_t(w));
    ++version_;
}

// --- Font -------------------------------------------------------------------

Font::Font(FT_Library library, GlyphAtlas& atlas, const unsigned char* data, size_t size, float pixel_size,
           float raster_scale)
    : atlas_(atlas), raster_scale_(raster_scale) {
    ft_check(FT_New_Memory_Face(library, data, FT_Long(size), 0, &face_), "FT_New_Memory_Face");
    const FT_UInt px = FT_UInt(std::lround(pixel_size * raster_scale));
    ft_check(FT_Set_Pixel_Sizes(face_, 0, px), "FT_Set_Pixel_Sizes");

    const FT_Size_Metrics& m = face_->size->metrics;
    ascent_px_ = int((m.ascender + 63) >> 6);
    line_height_px_ = int((m.height + 63) >> 6);
}

Font::~Font() {
    if (face_) FT_Done_Face(face_);
}

int Font::height() const { return int(std::ceil(float(line_height_px_) / raster_scale_)); }

const Glyph& Font::glyph(uint32_t cp) {
    if (cp < 128) {
        if (!ascii_loaded_[cp]) {
            ascii_[cp] = load(cp);
            ascii_loaded_[cp] = true;
        }
        return ascii_[cp];
    }
    auto it = glyphs_.find(cp);
    if (it == glyphs_.end()) it = glyphs_.emplace(cp, load(cp)).first;
    return it->second;
}

Glyph Font::load(uint32_t cp) {
    Glyph g;
    // Tabs are laid out by callers; give them a space's metrics.
    const uint32_t lookup = (cp == '\t') ? ' ' : cp;
    if (FT_Load_Char(face_, lookup, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) != 0) return g;

    FT_GlyphSlot slot = face_->glyph;
    const FT_Bitmap& bm = slot->bitmap;
    g.advance = int(std::lround(slot->advance.x / 64.0));
    g.left = slot->bitmap_left;
    g.top = slot->bitmap_top;
    if (bm.width > 0 && bm.rows > 0 && bm.pixel_mode == FT_PIXEL_MODE_GRAY) {
        int x = 0, y = 0;
        if (atlas_.allocate(int(bm.width), int(bm.rows), x, y)) {
            atlas_.blit(x, y, int(bm.width), int(bm.rows), bm.buffer, bm.pitch);
            g.width = int(bm.width);
            g.height = int(bm.rows);
            g.atlas_x = x;
            g.atlas_y = y;
        }
    }
    return g;
}

float Font::advance(uint32_t cp) { return float(glyph(cp).advance) / raster_scale_; }

int Font::text_width(const char* text, int len) {
    const char* p = text;
    const char* end = len < 0 ? text + std::strlen(text) : text + len;
    int px = 0;
    while (p < end && *p) px += glyph(utf8_decode(p, end)).advance;
    return int(std::ceil(float(px) / raster_scale_));
}

// --- FontSystem -------------------------------------------------------------

FontSystem::FontSystem(float raster_scale) : atlas_(512, 512) {
    ft_check(FT_Init_FreeType(&library_), "FT_Init_FreeType");

    // A 3x3 opaque block; sampling its centre texel gives solid fills.
    int x = 0, y = 0;
    atlas_.allocate(3, 3, x, y);
    atlas_.fill(x, y, 3, 3, 255);
    white_ = mu_rect(x + 1, y + 1, 1, 1);

    const unsigned char* icon_pixels = vig_mu_atlas_pixels();
    const int icon_pitch = vig_mu_atlas_width();
    for (int icon = 1; icon < MU_ICON_MAX; ++icon) {
        mu_Rect src = vig_mu_icon_rect(icon);
        if (!atlas_.allocate(src.w, src.h, x, y)) throw std::runtime_error("glyph atlas too small for icons");
        atlas_.blit(x, y, src.w, src.h, icon_pixels + src.y * icon_pitch + src.x, icon_pitch);
        icons_[icon] = mu_rect(x, y, src.w, src.h);
    }

    ui_ = std::make_unique<Font>(library_, atlas_, vigide_font_ui, vigide_font_ui_size, kUiFontSize, raster_scale);
    mono_ = std::make_unique<Font>(library_, atlas_, vigide_font_mono, vigide_font_mono_size, kMonoFontSize,
                                   raster_scale);
}

FontSystem::~FontSystem() {
    ui_.reset();
    mono_.reset();
    if (library_) FT_Done_FreeType(library_);
}

int FontSystem::mu_text_width(mu_Font font, const char* text, int len) {
    return static_cast<Font*>(font)->text_width(text, len);
}

int FontSystem::mu_text_height(mu_Font font) { return static_cast<Font*>(font)->height(); }

}  // namespace vig
