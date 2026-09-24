#pragma once

#include "platform/input.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct mu_Context;

namespace vig {

// Modal Open / Save As dialog drawn with microui.
class FileDialog {
public:
    enum class Mode { Open, Save };

    void open(Mode mode, const std::filesystem::path& dir, const std::string& filename = {});
    void cancel() { open_ = false; }
    bool is_open() const { return open_; }
    Mode mode() const { return mode_; }

    // Draws the dialog while open. Returns the chosen path once, when the
    // user confirms; the dialog closes itself then (and on cancel).
    std::optional<std::filesystem::path> update(mu_Context* ctx, const FrameInput& input, int screen_w,
                                                int screen_h);

private:
    struct Entry {
        std::string name;
        bool is_dir;
    };

    void navigate(const std::filesystem::path& dir);
    std::optional<std::filesystem::path> confirm();

    bool open_ = false;
    Mode mode_ = Mode::Open;
    std::filesystem::path dir_;
    std::vector<Entry> entries_;
    char name_[512] = "";
    bool focus_name_ = false;
    std::string message_;
    std::filesystem::path confirm_overwrite_;
    std::string last_clicked_;
    double last_click_time_ = -1.0;
};

}  // namespace vig
