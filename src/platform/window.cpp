#include "platform/window.hpp"

#include <GLFW/glfw3.h>
#include "ui/mu.h"

#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>

namespace vig {

namespace {

Window* from(GLFWwindow* w) { return static_cast<Window*>(glfwGetWindowUserPointer(w)); }

void on_glfw_error(int code, const char* description) {
    std::fprintf(stderr, "[glfw %d] %s\n", code, description);
}

int map_mouse_button(int button) {
    switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT: return MU_MOUSE_LEFT;
        case GLFW_MOUSE_BUTTON_RIGHT: return MU_MOUSE_RIGHT;
        case GLFW_MOUSE_BUTTON_MIDDLE: return MU_MOUSE_MIDDLE;
        default: return 0;
    }
}

int map_key(int key) {
    switch (key) {
        case GLFW_KEY_LEFT_SHIFT:
        case GLFW_KEY_RIGHT_SHIFT: return MU_KEY_SHIFT;
        case GLFW_KEY_LEFT_CONTROL:
        case GLFW_KEY_RIGHT_CONTROL: return MU_KEY_CTRL;
        case GLFW_KEY_LEFT_ALT:
        case GLFW_KEY_RIGHT_ALT: return MU_KEY_ALT;
        case GLFW_KEY_BACKSPACE: return MU_KEY_BACKSPACE;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER: return MU_KEY_RETURN;
        default: return 0;
    }
}

std::optional<Key> map_editor_key(int key) {
    switch (key) {
        case GLFW_KEY_LEFT: return Key::Left;
        case GLFW_KEY_RIGHT: return Key::Right;
        case GLFW_KEY_UP: return Key::Up;
        case GLFW_KEY_DOWN: return Key::Down;
        case GLFW_KEY_HOME: return Key::Home;
        case GLFW_KEY_END: return Key::End;
        case GLFW_KEY_PAGE_UP: return Key::PageUp;
        case GLFW_KEY_PAGE_DOWN: return Key::PageDown;
        case GLFW_KEY_BACKSPACE: return Key::Backspace;
        case GLFW_KEY_DELETE: return Key::Delete;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER: return Key::Enter;
        case GLFW_KEY_TAB: return Key::Tab;
        case GLFW_KEY_ESCAPE: return Key::Escape;
        case GLFW_KEY_F3: return Key::F3;
        case GLFW_KEY_A: return Key::A;
        case GLFW_KEY_C: return Key::C;
        case GLFW_KEY_D: return Key::D;
        case GLFW_KEY_F: return Key::F;
        case GLFW_KEY_G: return Key::G;
        case GLFW_KEY_H: return Key::H;
        case GLFW_KEY_N: return Key::N;
        case GLFW_KEY_O: return Key::O;
        case GLFW_KEY_S: return Key::S;
        case GLFW_KEY_V: return Key::V;
        case GLFW_KEY_X: return Key::X;
        case GLFW_KEY_Y: return Key::Y;
        case GLFW_KEY_Z: return Key::Z;
        default: return std::nullopt;
    }
}

std::string get_clipboard(void* user) {
    const char* s = glfwGetClipboardString(static_cast<GLFWwindow*>(user));
    return s ? std::string(s) : std::string();
}

void set_clipboard(void* user, const std::string& text) {
    glfwSetClipboardString(static_cast<GLFWwindow*>(user), text.c_str());
}

}  // namespace

Window::Window(const char* title, int width, int height) {
    glfwSetErrorCallback(on_glfw_error);
    if (!glfwInit()) throw std::runtime_error("Failed to initialise GLFW");
    if (!glfwVulkanSupported()) {
        glfwTerminate();
        throw std::runtime_error("No Vulkan loader/ICD found");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("Failed to create window");
    }

    glfwSetWindowUserPointer(window_, this);
    glfwSetCursorPosCallback(window_, on_cursor_pos);
    glfwSetMouseButtonCallback(window_, on_mouse_button);
    glfwSetScrollCallback(window_, on_scroll);
    glfwSetKeyCallback(window_, on_key);
    glfwSetCharCallback(window_, on_char);
    glfwSetFramebufferSizeCallback(window_, on_framebuffer_size);
    glfwSetWindowCloseCallback(window_, on_close);

    input_.get_clipboard = get_clipboard;
    input_.set_clipboard = set_clipboard;
    input_.clipboard_user = window_;
}

Window::~Window() {
    glfwDestroyWindow(window_);
    glfwTerminate();
}

void Window::poll_events() {
    input_.keys.clear();
    input_.text.clear();
    glfwPollEvents();
    input_.time = glfwGetTime();
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    while ((w == 0 || h == 0) && !close_requested_) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window_, &w, &h);
    }
}

bool Window::consume_close_request() {
    bool r = close_requested_;
    close_requested_ = false;
    return r;
}

bool Window::consume_resized() {
    bool r = resized_;
    resized_ = false;
    return r;
}

void Window::size(int& width, int& height) const { glfwGetWindowSize(window_, &width, &height); }

void Window::framebuffer_scale(float& x, float& y) const {
    int w = 0, h = 0, fw = 0, fh = 0;
    glfwGetWindowSize(window_, &w, &h);
    glfwGetFramebufferSize(window_, &fw, &fh);
    x = w > 0 ? float(fw) / float(w) : 1.0f;
    y = h > 0 ? float(fh) / float(h) : 1.0f;
}

void Window::on_cursor_pos(GLFWwindow* w, double x, double y) {
    if (mu_Context* ui = from(w)->ui_) mu_input_mousemove(ui, int(x), int(y));
}

void Window::on_mouse_button(GLFWwindow* w, int button, int action, int) {
    mu_Context* ui = from(w)->ui_;
    int b = map_mouse_button(button);
    if (!ui || !b) return;
    double x, y;
    glfwGetCursorPos(w, &x, &y);
    if (action == GLFW_PRESS) {
        mu_input_mousedown(ui, int(x), int(y), b);
    } else if (action == GLFW_RELEASE) {
        mu_input_mouseup(ui, int(x), int(y), b);
    }
}

void Window::on_scroll(GLFWwindow* w, double dx, double dy) {
    if (mu_Context* ui = from(w)->ui_) mu_input_scroll(ui, int(dx * -30.0), int(dy * -30.0));
}

void Window::on_key(GLFWwindow* w, int key, int, int action, int mods) {
    Window* self = from(w);
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (std::optional<Key> k = map_editor_key(key)) {
            KeyEvent ev{*k};
            ev.shift = (mods & GLFW_MOD_SHIFT) != 0;
#ifdef __APPLE__
            ev.ctrl = (mods & GLFW_MOD_SUPER) != 0;
#else
            ev.ctrl = (mods & GLFW_MOD_CONTROL) != 0;
#endif
            ev.alt = (mods & GLFW_MOD_ALT) != 0;
            self->input_.keys.push_back(ev);
        }
    }

    mu_Context* ui = self->ui_;
    int k = map_key(key);
    if (!ui || !k) return;
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        // microui treats keydown as a one-frame "pressed" event, so repeats
        // re-trigger actions like backspace.
        mu_input_keydown(ui, k);
    } else if (action == GLFW_RELEASE) {
        mu_input_keyup(ui, k);
    }
}

void Window::on_char(GLFWwindow* w, unsigned int cp) {
    Window* self = from(w);
    char buf[5] = {};
    if (cp < 0x80) {
        buf[0] = char(cp);
    } else if (cp < 0x800) {
        buf[0] = char(0xC0 | (cp >> 6));
        buf[1] = char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        buf[0] = char(0xE0 | (cp >> 12));
        buf[1] = char(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = char(0x80 | (cp & 0x3F));
    } else {
        buf[0] = char(0xF0 | (cp >> 18));
        buf[1] = char(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = char(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = char(0x80 | (cp & 0x3F));
    }
    self->input_.text += buf;

    // microui's per-frame text buffer is small and asserts on overflow.
    mu_Context* ui = self->ui_;
    if (ui && std::strlen(ui->input_text) + std::strlen(buf) < sizeof(ui->input_text)) mu_input_text(ui, buf);
}

void Window::on_framebuffer_size(GLFWwindow* w, int, int) { from(w)->resized_ = true; }

void Window::on_close(GLFWwindow* w) {
    // Let the app decide (it may need to ask about unsaved changes).
    glfwSetWindowShouldClose(w, GLFW_FALSE);
    from(w)->close_requested_ = true;
}

}  // namespace vig
