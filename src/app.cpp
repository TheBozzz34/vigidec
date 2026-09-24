#include "app.hpp"

#include "platform/file_dialogs.hpp"
#include "platform/window.hpp"
#include "render/font.hpp"
#include "render/ui_renderer.hpp"
#include "render/vk_context.hpp"
#include "ui/ide.hpp"

#include "ui/mu.h"

#include <memory>

namespace vig {

namespace {

void apply_style(mu_Context* ctx, FontSystem& fonts) {
    mu_Style* s = ctx->style;
    s->font = &fonts.ui();
    s->size = mu_vec2(68, 14);
    s->padding = 5;
    s->spacing = 4;
    s->title_height = 24;
    s->colors[MU_COLOR_TEXT] = mu_color(212, 212, 212, 255);
    s->colors[MU_COLOR_BORDER] = mu_color(20, 20, 22, 255);
    s->colors[MU_COLOR_WINDOWBG] = mu_color(37, 37, 40, 255);
    s->colors[MU_COLOR_TITLEBG] = mu_color(28, 28, 31, 255);
    s->colors[MU_COLOR_TITLETEXT] = mu_color(190, 190, 195, 255);
    s->colors[MU_COLOR_PANELBG] = mu_color(30, 30, 33, 255);
    s->colors[MU_COLOR_BUTTON] = mu_color(55, 55, 60, 255);
    s->colors[MU_COLOR_BUTTONHOVER] = mu_color(70, 70, 78, 255);
    s->colors[MU_COLOR_BUTTONFOCUS] = mu_color(0, 110, 190, 255);
    s->colors[MU_COLOR_BASE] = mu_color(25, 25, 28, 255);
    s->colors[MU_COLOR_BASEHOVER] = mu_color(32, 32, 36, 255);
    s->colors[MU_COLOR_BASEFOCUS] = mu_color(38, 38, 44, 255);
    s->colors[MU_COLOR_SCROLLBASE] = mu_color(30, 30, 33, 255);
    s->colors[MU_COLOR_SCROLLTHUMB] = mu_color(75, 75, 82, 255);
}

}  // namespace

int run_app() {
    Window window("VIG IDE", 1280, 800);
    VulkanContext vk(window.handle());

    // Rasterise glyphs at framebuffer resolution (2x on Retina displays).
    float raster_scale = 1.0f, unused = 1.0f;
    window.framebuffer_scale(raster_scale, unused);
    FontSystem fonts(raster_scale);
    UiRenderer renderer(vk, fonts);

    auto ui = std::make_unique<mu_Context>();
    mu_init(ui.get());
    ui->text_width = FontSystem::mu_text_width;
    ui->text_height = FontSystem::mu_text_height;
    apply_style(ui.get(), fonts);
    window.attach(ui.get());

    NativeFileDialogs dialogs(window.handle());
    Ide ide(fonts, dialogs);
    const VkClearColorValue clear{{0.09f, 0.09f, 0.10f, 1.0f}};

    while (!ide.should_quit()) {
        window.poll_events();
        if (window.consume_close_request()) ide.request_quit();
        if (window.consume_resized()) vk.request_resize();

        int width = 0, height = 0;
        window.size(width, height);

        mu_begin(ui.get());
        ide.frame(ui.get(), window.input(), width, height);
        mu_end(ui.get());

        if (VkCommandBuffer cmd = vk.begin_frame(clear)) {
            float sx = 1.0f, sy = 1.0f;
            window.framebuffer_scale(sx, sy);
            renderer.render(ui.get(), cmd, sx, sy);
            vk.end_frame();
        }
    }

    vk.wait_idle();
    return 0;
}

}  // namespace vig
