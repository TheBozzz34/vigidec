#pragma once

#include "platform/input.hpp"
#include "ui/mu.h"

namespace vig {

// microui textbox with extras microui lacks: Ctrl+V pastes (first line of
// the clipboard), Ctrl+Backspace clears. `focus` gives it keyboard focus.
// Returns microui result flags (MU_RES_CHANGE / MU_RES_SUBMIT).
int textbox(mu_Context* ctx, char* buf, int size, const FrameInput& input, bool focus = false);

// Button drawn in the accent colour while `value` is set; toggles on click.
bool toggle_button(mu_Context* ctx, const char* label, bool& value);

// Full-width list row, highlighted when selected or hovered. Returns true
// when clicked.
bool selectable(mu_Context* ctx, const char* label, bool selected);

// Draws a dimmed, input-swallowing layer over the whole window and keeps
// the dialog window `dialog_title` above it. Call once per frame, before
// beginning the dialog window, while the dialog is open.
void modal_backdrop(mu_Context* ctx, const char* dialog_title, int width, int height);

// Centres `dialog_title` on screen with the given size.
mu_Rect centered_rect(int screen_w, int screen_h, int w, int h);

}  // namespace vig
