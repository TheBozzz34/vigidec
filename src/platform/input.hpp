#pragma once

#include <string>
#include <vector>

namespace vig {

// Keys the IDE handles directly (microui only knows a handful of keys).
enum class Key {
    Left, Right, Up, Down,
    Home, End, PageUp, PageDown,
    Backspace, Delete, Enter, Tab, Escape, F3,
    A, C, D, F, G, H, N, O, S, V, X, Y, Z,
};

struct KeyEvent {
    Key key;
    bool shift = false;
    bool ctrl = false;  // Command on macOS
    bool alt = false;
};

// Keyboard input gathered since the previous frame, plus a few platform
// services widgets need. Cleared by Window::poll_events().
struct FrameInput {
    std::vector<KeyEvent> keys;
    std::string text;  // UTF-8 text typed this frame
    double time = 0.0;  // seconds

    std::string (*get_clipboard)(void* user) = nullptr;
    void (*set_clipboard)(void* user, const std::string& text) = nullptr;
    void* clipboard_user = nullptr;

    std::string clipboard() const { return get_clipboard ? get_clipboard(clipboard_user) : std::string(); }
    void set_clipboard_text(const std::string& s) const {
        if (set_clipboard) set_clipboard(clipboard_user, s);
    }
};

}  // namespace vig
