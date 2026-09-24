#include "platform/window.hpp"

#include <GLFW/glfw3.h>
#include "ui/mu.h"

#include <cstdio>
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
}

Window::~Window() {
    glfwDestroyWindow(window_);
    glfwTerminate();
}

void Window::poll_events() {
    glfwPollEvents();
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    while ((w == 0 || h == 0) && !glfwWindowShouldClose(window_)) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window_, &w, &h);
    }
}

bool Window::should_close() const { return glfwWindowShouldClose(window_); }

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

void Window::on_key(GLFWwindow* w, int key, int, int action, int) {
    mu_Context* ui = from(w)->ui_;
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
    mu_Context* ui = from(w)->ui_;
    if (!ui) return;
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
    mu_input_text(ui, buf);
}

void Window::on_framebuffer_size(GLFWwindow* w, int, int) { from(w)->resized_ = true; }

}  // namespace vig
