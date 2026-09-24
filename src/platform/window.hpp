#pragma once

struct GLFWwindow;
struct mu_Context;

namespace vig {

// GLFW window that forwards its input events to a microui context.
class Window {
public:
    Window(const char* title, int width, int height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void attach(mu_Context* ctx) { ui_ = ctx; }

    // Processes pending events; blocks while the window is minimised.
    void poll_events();
    bool should_close() const;

    // True once after the framebuffer size changed.
    bool consume_resized();

    GLFWwindow* handle() const { return window_; }
    void size(int& width, int& height) const;
    void framebuffer_scale(float& x, float& y) const;

private:
    static void on_cursor_pos(GLFWwindow* w, double x, double y);
    static void on_mouse_button(GLFWwindow* w, int button, int action, int mods);
    static void on_scroll(GLFWwindow* w, double dx, double dy);
    static void on_key(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void on_char(GLFWwindow* w, unsigned int codepoint);
    static void on_framebuffer_size(GLFWwindow* w, int width, int height);

    GLFWwindow* window_ = nullptr;
    mu_Context* ui_ = nullptr;
    bool resized_ = false;
};

}  // namespace vig
