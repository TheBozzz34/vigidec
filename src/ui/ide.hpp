#pragma once

#include "editor/text_editor.hpp"
#include "platform/input.hpp"

#include <filesystem>
#include <string>
#include <vector>

struct mu_Context;

namespace vig {

class FontSystem;

// Top-level IDE UI. Holds editor/explorer state and lays out the panels
// every frame. The VM integration is still to come; the VM panel and the
// toolbar build/run actions are placeholders.
class Ide {
public:
    explicit Ide(FontSystem& fonts);

    void frame(mu_Context* ctx, const FrameInput& input, int width, int height);

    void log(std::string line);
    void open_file(const std::filesystem::path& path);
    void save_file();

private:
    struct Entry {
        std::string name;
        bool is_dir;
    };

    void toolbar(mu_Context* ctx);
    void explorer(mu_Context* ctx);
    void editor(mu_Context* ctx, const FrameInput& input);
    void vm_panel(mu_Context* ctx);
    void output(mu_Context* ctx);

    void change_directory(const std::filesystem::path& dir);
    void request_open(const std::filesystem::path& path);

    FontSystem& fonts_;

    std::filesystem::path cwd_;
    std::vector<Entry> entries_;

    std::filesystem::path file_path_;
    TextEditor editor_;
    std::filesystem::path pending_open_;  // waiting for "discard changes?" confirmation

    std::vector<std::string> log_;
    int scroll_log_frames_ = 0;  // keep pinning to the bottom until layout catches up
};

}  // namespace vig
