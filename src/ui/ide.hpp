#pragma once

#include "editor/text_editor.hpp"
#include "platform/input.hpp"
#include "ui/find_bar.hpp"

#include <filesystem>
#include <string>
#include <vector>

struct mu_Context;

namespace vig {

class FontSystem;
class NativeFileDialogs;

// Top-level IDE UI. Holds editor/explorer state, routes global shortcuts
// and lays out the panels every frame. The VM integration is still to
// come; the VM panel and the build/run toolbar actions are placeholders.
class Ide {
public:
    Ide(FontSystem& fonts, NativeFileDialogs& dialogs);

    void frame(mu_Context* ctx, const FrameInput& input, int width, int height);

    // Window close button: quits, or asks about unsaved changes first.
    void request_quit();
    bool should_quit() const { return quit_; }

    void log(std::string line);

private:
    enum class Action { None, New, Open, Quit };
    enum class DialogRequest { None, Open, SaveAs };

    struct Entry {
        std::string name;
        bool is_dir;
    };

    // Shortcuts that work regardless of focus. Returns the input left over
    // for the focused widget.
    FrameInput route_shortcuts(const FrameInput& input);

    void toolbar(mu_Context* ctx);
    void explorer(mu_Context* ctx);
    void editor(mu_Context* ctx, const FrameInput& input);
    void vm_panel(mu_Context* ctx);
    void output(mu_Context* ctx);
    void unsaved_prompt(mu_Context* ctx, const FrameInput& input, int width, int height);

    // Runs `action` now, or first asks what to do with unsaved changes.
    void guarded(Action action, const std::filesystem::path& path = {});
    void perform(Action action, const std::filesystem::path& path);

    void new_file();
    bool open_file(const std::filesystem::path& path);
    bool save_file();  // falls back to Save As for untitled buffers
    void save_as();
    // Native dialogs block, so they run at the start of the next frame
    // rather than in the middle of laying out the UI.
    void run_dialog(mu_Context* ctx);
    bool write_file(const std::filesystem::path& path);
    std::string display_name() const;

    void change_directory(const std::filesystem::path& dir);

    FontSystem& fonts_;
    NativeFileDialogs& dialogs_;

    std::filesystem::path cwd_;
    std::vector<Entry> entries_;

    std::filesystem::path file_path_;
    TextEditor editor_;
    FindBar find_bar_;
    DialogRequest dialog_request_ = DialogRequest::None;

    // An action waiting on the unsaved-changes prompt (or on Save As).
    Action pending_ = Action::None;
    std::filesystem::path pending_path_;
    bool prompt_open_ = false;
    bool quit_ = false;

    std::vector<std::string> log_;
    int scroll_log_frames_ = 0;  // keep pinning to the bottom until layout catches up
};

}  // namespace vig
