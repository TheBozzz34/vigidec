#include "ui/file_dialog.hpp"

#include "ui/mu.h"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cstring>
#include <system_error>

namespace vig {

namespace fs = std::filesystem;

namespace {

constexpr double kDoubleClickTime = 0.35;

void set_name(char* dst, size_t size, const std::string& s) {
    const size_t n = std::min(s.size(), size - 1);
    std::memcpy(dst, s.data(), n);
    dst[n] = '\0';
}

std::string trim(std::string s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

}  // namespace

void FileDialog::open(Mode mode, const fs::path& dir, const std::string& filename) {
    open_ = true;
    mode_ = mode;
    set_name(name_, sizeof name_, filename);
    message_.clear();
    confirm_overwrite_.clear();
    focus_name_ = true;
    navigate(dir.empty() ? fs::path(".") : dir);
}

void FileDialog::navigate(const fs::path& dir) {
    std::error_code ec;
    fs::path target = fs::weakly_canonical(dir, ec);
    if (ec || target.empty()) target = dir;
    fs::directory_iterator it(target, ec);
    if (ec) {
        message_ = "Cannot open " + target.string() + ": " + ec.message();
        return;
    }
    dir_ = target;
    entries_.clear();
    for (const fs::directory_entry& e : it) {
        std::string name = e.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        std::error_code type_ec;
        entries_.push_back({name, e.is_directory(type_ec)});
    }
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir;
        return a.name < b.name;
    });
    confirm_overwrite_.clear();
}

std::optional<fs::path> FileDialog::confirm() {
    const std::string name = trim(name_);
    if (name.empty()) {
        message_ = mode_ == Mode::Save ? "Enter a file name." : "Select a file.";
        return std::nullopt;
    }

    const fs::path typed(name);
    const fs::path path = typed.is_absolute() ? typed : dir_ / typed;
    std::error_code ec;
    if (fs::is_directory(path, ec)) {  // typing a folder name navigates into it
        navigate(path);
        name_[0] = '\0';
        return std::nullopt;
    }

    if (mode_ == Mode::Open) {
        if (!fs::is_regular_file(path, ec)) {
            message_ = "File not found: " + path.filename().string();
            return std::nullopt;
        }
    } else {
        const fs::path parent = path.parent_path();
        if (!parent.empty() && !fs::is_directory(parent, ec)) {
            message_ = "Folder does not exist: " + parent.string();
            return std::nullopt;
        }
        if (fs::exists(path, ec) && confirm_overwrite_ != path) {
            confirm_overwrite_ = path;
            message_ = path.filename().string() + " already exists. Click Overwrite to replace it.";
            return std::nullopt;
        }
    }

    open_ = false;
    return path;
}

std::optional<fs::path> FileDialog::update(mu_Context* ctx, const FrameInput& input, int screen_w, int screen_h) {
    if (!open_) return std::nullopt;

    for (const KeyEvent& ev : input.keys) {
        if (ev.key == Key::Escape) {
            open_ = false;
            return std::nullopt;
        }
    }

    const char* title = mode_ == Mode::Save ? "Save As" : "Open File";
    modal_backdrop(ctx, title, screen_w, screen_h);

    const mu_Rect rect = centered_rect(screen_w, screen_h, std::min(620, screen_w - 40), std::min(460, screen_h - 40));
    if (mu_Container* cnt = mu_get_container(ctx, title)) cnt->rect = rect;
    if (!mu_begin_window_ex(ctx, title, rect, MU_OPT_NOCLOSE | MU_OPT_NORESIZE | MU_OPT_NOSCROLL)) return std::nullopt;

    std::optional<fs::path> result;
    const int row_h = ctx->style->size.y + ctx->style->padding * 2;
    const int spacing = ctx->style->spacing;

    // Location row.
    const int loc_widths[] = {50, -1};
    mu_layout_row(ctx, 2, loc_widths, 0);
    const bool up = mu_button(ctx, "Up") != 0;
    const std::string where = dir_.string();
    mu_label(ctx, where.c_str());
    if (up) navigate(dir_.parent_path());

    // File list fills the space left above the three bottom rows.
    const int full[] = {-1};
    mu_layout_row(ctx, 1, full, -(3 * (row_h + spacing)));
    mu_begin_panel(ctx, "files");
    mu_layout_row(ctx, 1, full, 0);
    fs::path navigate_to;
    const std::string current_name = name_;
    for (const Entry& e : entries_) {
        const std::string label = e.is_dir ? e.name + "/" : e.name;
        if (selectable(ctx, label.c_str(), !e.is_dir && e.name == current_name)) {
            if (e.is_dir) {
                navigate_to = dir_ / e.name;
            } else {
                const bool dbl = last_clicked_ == e.name && input.time - last_click_time_ < kDoubleClickTime;
                set_name(name_, sizeof name_, e.name);
                confirm_overwrite_.clear();
                message_.clear();
                last_clicked_ = e.name;
                last_click_time_ = input.time;
                if (dbl) result = confirm();
            }
        }
    }
    mu_end_panel(ctx);
    if (!navigate_to.empty()) navigate(navigate_to);

    // Name row.
    const int name_widths[] = {50, -1};
    mu_layout_row(ctx, 2, name_widths, 0);
    mu_label(ctx, "Name");
    const int res = textbox(ctx, name_, sizeof name_, input, focus_name_);
    focus_name_ = false;
    if (res & MU_RES_CHANGE) confirm_overwrite_.clear();
    if ((res & MU_RES_SUBMIT) && !result) {
        result = confirm();
        if (!result) focus_name_ = true;
    }

    // Message row.
    mu_layout_row(ctx, 1, full, 0);
    mu_label(ctx, message_.c_str());

    // Buttons.
    const int button_widths[] = {-200, 96, 96};
    mu_layout_row(ctx, 3, button_widths, 0);
    mu_label(ctx, "");
    if (mu_button(ctx, "Cancel")) open_ = false;
    const char* ok_label = mode_ == Mode::Open ? "Open" : !confirm_overwrite_.empty() ? "Overwrite" : "Save";
    if (mu_button(ctx, ok_label) && !result) result = confirm();

    mu_end_window(ctx);
    return result;
}

}  // namespace vig
