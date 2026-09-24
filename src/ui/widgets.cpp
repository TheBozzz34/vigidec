#include "ui/widgets.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace vig {

int textbox(mu_Context* ctx, char* buf, int size, const FrameInput& input, bool focus) {
    // Same id microui derives inside mu_textbox_ex (the buffer pointer).
    const mu_Id id = mu_get_id(ctx, &buf, sizeof(buf));
    if (focus) mu_set_focus(ctx, id);

    int res = 0;
    if (ctx->focus == id) {
        for (const KeyEvent& ev : input.keys) {
            if (!ev.ctrl) continue;
            if (ev.key == Key::V) {
                std::string clip = input.clipboard();
                clip = clip.substr(0, clip.find_first_of("\r\n"));
                const size_t len = std::strlen(buf);
                const size_t n = std::min(clip.size(), size_t(size) - 1 - len);
                std::memcpy(buf + len, clip.data(), n);
                buf[len + n] = '\0';
                res |= MU_RES_CHANGE;
            } else if (ev.key == Key::Backspace && buf[0]) {
                buf[0] = '\0';
                res |= MU_RES_CHANGE;
            }
        }
    }
    return res | mu_textbox_ex(ctx, buf, size, 0);
}

bool toggle_button(mu_Context* ctx, const char* label, bool& value) {
    mu_Color* colors = ctx->style->colors;
    const mu_Color saved[2] = {colors[MU_COLOR_BUTTON], colors[MU_COLOR_BUTTONHOVER]};
    if (value) {
        colors[MU_COLOR_BUTTON] = colors[MU_COLOR_BUTTONFOCUS];
        colors[MU_COLOR_BUTTONHOVER] = colors[MU_COLOR_BUTTONFOCUS];
    }
    const bool clicked = mu_button_ex(ctx, label, 0, MU_OPT_ALIGNCENTER) != 0;
    colors[MU_COLOR_BUTTON] = saved[0];
    colors[MU_COLOR_BUTTONHOVER] = saved[1];
    if (clicked) value = !value;
    return clicked;
}

bool selectable(mu_Context* ctx, const char* label, bool selected) {
    const mu_Id id = mu_get_id(ctx, label, int(std::strlen(label)));
    const mu_Rect r = mu_layout_next(ctx);
    mu_update_control(ctx, id, r, 0);
    const bool clicked = (ctx->mouse_pressed & MU_MOUSE_LEFT) && ctx->focus == id;

    if (selected) {
        mu_draw_rect(ctx, r, ctx->style->colors[MU_COLOR_BUTTONFOCUS]);
    } else if (ctx->hover == id) {
        mu_draw_rect(ctx, r, ctx->style->colors[MU_COLOR_BUTTONHOVER]);
    }
    mu_draw_control_text(ctx, label, r, MU_COLOR_TEXT, 0);
    return clicked;
}

void modal_backdrop(mu_Context* ctx, const char* dialog_title, int width, int height) {
    const char* kBackdrop = "##modal-backdrop";
    mu_Container* backdrop = mu_get_container(ctx, kBackdrop);
    backdrop->rect = mu_rect(0, 0, width, height);
    mu_bring_to_front(ctx, backdrop);
    mu_bring_to_front(ctx, mu_get_container(ctx, dialog_title));

    const int opts = MU_OPT_NOTITLE | MU_OPT_NORESIZE | MU_OPT_NOCLOSE | MU_OPT_NOFRAME | MU_OPT_NOSCROLL;
    if (mu_begin_window_ex(ctx, kBackdrop, backdrop->rect, opts)) {
        // Being the hover root is what blocks input to the panels beneath.
        mu_draw_rect(ctx, backdrop->rect, mu_color(0, 0, 0, 140));
        mu_end_window(ctx);
    }
}

mu_Rect centered_rect(int screen_w, int screen_h, int w, int h) {
    return mu_rect((screen_w - w) / 2, (screen_h - h) / 2, w, h);
}

}  // namespace vig
