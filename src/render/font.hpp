#pragma once

#include "ui/mu.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;

namespace vig {

// Decodes one UTF-8 code point and advances `p`; malformed input yields
// U+FFFD and consumes one byte.
uint32_t utf8_decode(const char*& p, const char* end);

// CPU-side single-channel (R8) atlas with shelf packing. It grows
// vertically when full, so pixel coordinates stay valid; renderers compare
// version() to know when to re-upload.
class GlyphAtlas {
public:
    GlyphAtlas(int width, int height);

    bool allocate(int w, int h, int& x, int& y);
    void blit(int x, int y, int w, int h, const uint8_t* src, int src_pitch);
    void fill(int x, int y, int w, int h, uint8_t value);

    int width() const { return width_; }
    int height() const { return height_; }
    const uint8_t* pixels() const { return pixels_.data(); }
    uint64_t version() const { return version_; }

private:
    static constexpr int kMaxHeight = 4096;
    static constexpr int kPadding = 1;

    int width_;
    int height_;
    std::vector<uint8_t> pixels_;
    int shelf_x_ = kPadding;
    int shelf_y_ = kPadding;
    int shelf_h_ = 0;
    uint64_t version_ = 1;
};

struct Glyph {
    // Bitmap placement relative to the pen on the baseline, in raster pixels.
    int left = 0, top = 0;  // top is the offset above the baseline
    int width = 0, height = 0;
    int atlas_x = 0, atlas_y = 0;
    int advance = 0;  // raster pixels
};

// A FreeType face at one pixel size. Glyphs are rasterised into the shared
// atlas on first use. `raster_scale` is framebuffer pixels per UI unit, so
// text stays sharp on high-DPI displays while layout uses UI units.
class Font {
public:
    Font(FT_Library library, GlyphAtlas& atlas, const unsigned char* data, size_t size, float pixel_size,
         float raster_scale);
    ~Font();

    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    const Glyph& glyph(uint32_t codepoint);

    float raster_scale() const { return raster_scale_; }
    int ascent_px() const { return ascent_px_; }
    int line_height_px() const { return line_height_px_; }

    // UI units.
    int height() const;
    int text_width(const char* text, int len);
    float advance(uint32_t codepoint);

private:
    Glyph load(uint32_t codepoint);

    FT_Face face_ = nullptr;
    GlyphAtlas& atlas_;
    float raster_scale_;
    int ascent_px_ = 0;
    int line_height_px_ = 0;

    std::array<Glyph, 128> ascii_{};
    std::array<bool, 128> ascii_loaded_{};
    std::unordered_map<uint32_t, Glyph> glyphs_;
};

// Owns FreeType, the glyph atlas and the IDE's two fonts. The atlas also
// holds a solid white cell and microui's icons so the UI draws with a single
// texture.
class FontSystem {
public:
    explicit FontSystem(float raster_scale);
    ~FontSystem();

    FontSystem(const FontSystem&) = delete;
    FontSystem& operator=(const FontSystem&) = delete;

    Font& ui() { return *ui_; }
    Font& mono() { return *mono_; }
    GlyphAtlas& atlas() { return atlas_; }

    // Atlas regions in pixels.
    mu_Rect white_rect() const { return white_; }
    mu_Rect icon_rect(int icon) const { return icons_[icon]; }

    // mu_Context::text_width / text_height; mu_Font is a Font*.
    static int mu_text_width(mu_Font font, const char* text, int len);
    static int mu_text_height(mu_Font font);

private:
    FT_Library library_ = nullptr;
    GlyphAtlas atlas_;
    std::unique_ptr<Font> ui_;
    std::unique_ptr<Font> mono_;
    mu_Rect white_{};
    std::array<mu_Rect, MU_ICON_MAX> icons_{};
};

}  // namespace vig
