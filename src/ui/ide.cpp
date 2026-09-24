#include "ui/ide.hpp"

#include "render/font.hpp"
#include "ui/mu.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <system_error>

namespace vig {

namespace fs = std::filesystem;

namespace {

constexpr int kToolbarHeight = 34;
constexpr int kExplorerWidth = 240;
constexpr int kVmPanelWidth = 280;
constexpr int kOutputHeight = 180;

constexpr int kPanelOpts = MU_OPT_NOCLOSE | MU_OPT_NORESIZE;

constexpr const char* kWelcome =
    "# Welcome to the VIG IDE.\n"
    "#\n"
    "# Open a .vigas file from the explorer on the left, or start typing.\n"
    "# Ctrl+S saves, Ctrl+Z / Ctrl+Y undo and redo.\n"
    "# Assembling, running and debugging programs on the VIG VM\n"
    "# will be wired up here.\n"
    "\n"
    "start:\n"
    "    push 6\n"
    "    push 7\n"
    "    mul\n"
    "    print\n"
    "    halt\n";

// Opens (or keeps) a window pinned to `rect`, ignoring user drags/resizes.
bool begin_docked(mu_Context* ctx, const char* title, mu_Rect rect, int opts = kPanelOpts) {
    if (mu_Container* cnt = mu_get_container(ctx, title)) cnt->rect = rect;
    return mu_begin_window_ex(ctx, title, rect, opts) != 0;
}

}  // namespace

Ide::Ide(FontSystem& fonts) : fonts_(fonts) {
    std::error_code ec;
    fs::path start = fs::current_path(ec);
    change_directory(ec ? fs::path(".") : start);
    editor_.set_text(kWelcome);
    editor_.set_language(Language::VigAsm);
    editor_.request_focus();
    log("VIG IDE " VIGIDE_VERSION);
}

void Ide::log(std::string line) {
    log_.push_back(std::move(line));
    scroll_log_frames_ = 2;
}

void Ide::change_directory(const fs::path& dir) {
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(dir, ec);
    fs::directory_iterator it(ec ? dir : canonical, ec);
    if (ec) {
        log("Cannot open directory " + dir.string() + ": " + ec.message());
        return;
    }
    cwd_ = canonical.empty() ? dir : canonical;
    entries_.clear();
    for (const fs::directory_entry& e : it) {
        std::string name = e.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        entries_.push_back({name, e.is_directory(ec)});
    }
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir;
        return a.name < b.name;
    });
}

void Ide::open_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        log("Cannot open " + path.string());
        return;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    editor_.set_text(text);
    editor_.set_language(language_for_path(path.string()));
    editor_.request_focus();
    file_path_ = path;
    pending_open_.clear();
    log("Opened " + path.string());
}

void Ide::request_open(const fs::path& path) {
    if (editor_.modified() && pending_open_ != path) {
        pending_open_ = path;
        log("Unsaved changes - save first, or click " + path.filename().string() + " again to discard them");
        return;
    }
    open_file(path);
}

void Ide::save_file() {
    if (file_path_.empty()) {
        log("Save: nothing to save to yet - open a file first (Save As is not implemented)");
        return;
    }
    std::ofstream out(file_path_, std::ios::binary | std::ios::trunc);
    const std::string text = editor_.text();
    if (!out || !out.write(text.data(), std::streamsize(text.size()))) {
        log("Failed to save " + file_path_.string());
        return;
    }
    editor_.mark_saved();
    log("Saved " + file_path_.string());
}

void Ide::frame(mu_Context* ctx, const FrameInput& input, int width, int height) {
    const int body_h = std::max(0, height - kToolbarHeight - kOutputHeight);
    const int editor_w = std::max(0, width - kExplorerWidth - kVmPanelWidth);

    if (begin_docked(ctx, "Toolbar", mu_rect(0, 0, width, kToolbarHeight),
                     kPanelOpts | MU_OPT_NOTITLE | MU_OPT_NOSCROLL)) {
        toolbar(ctx);
        mu_end_window(ctx);
    }
    if (begin_docked(ctx, "Explorer", mu_rect(0, kToolbarHeight, kExplorerWidth, body_h))) {
        explorer(ctx);
        mu_end_window(ctx);
    }
    if (begin_docked(ctx, "Editor", mu_rect(kExplorerWidth, kToolbarHeight, editor_w, body_h),
                     kPanelOpts | MU_OPT_NOSCROLL)) {
        editor(ctx, input);
        mu_end_window(ctx);
    }
    if (begin_docked(ctx, "VM", mu_rect(kExplorerWidth + editor_w, kToolbarHeight, kVmPanelWidth, body_h))) {
        vm_panel(ctx);
        mu_end_window(ctx);
    }
    if (begin_docked(ctx, "Output", mu_rect(0, kToolbarHeight + body_h, width, kOutputHeight))) {
        output(ctx);
        mu_end_window(ctx);
    }
}

void Ide::toolbar(mu_Context* ctx) {
    static const int widths[] = {70, 70, 90, 70, 70, 70, -1};
    mu_layout_row(ctx, 7, widths, -1);
    if (mu_button(ctx, "Open")) log("Open: pick a file in the explorer");
    if (mu_button(ctx, "Save")) save_file();
    if (mu_button(ctx, "Assemble")) log("Assemble: not implemented yet");
    if (mu_button(ctx, "Run")) log("Run: not implemented yet");
    if (mu_button(ctx, "Step")) log("Step: not implemented yet");
    if (mu_button(ctx, "Stop")) log("Stop: not implemented yet");
    std::string title = file_path_.empty() ? "untitled" : file_path_.filename().string();
    if (editor_.modified()) title += " *";
    mu_label(ctx, title.c_str());
}

void Ide::explorer(mu_Context* ctx) {
    const int full[] = {-1};
    mu_layout_row(ctx, 1, full, 0);
    mu_text(ctx, cwd_.string().c_str());

    fs::path navigate;
    fs::path open;

    if (mu_button_ex(ctx, "..", 0, 0)) navigate = cwd_.parent_path();
    for (const Entry& e : entries_) {
        mu_push_id(ctx, e.name.data(), static_cast<int>(e.name.size()));
        std::string label = e.is_dir ? e.name + "/" : e.name;
        if (mu_button_ex(ctx, label.c_str(), 0, 0)) {
            (e.is_dir ? navigate : open) = cwd_ / e.name;
        }
        mu_pop_id(ctx);
    }

    // Mutate after iterating so entries_ is not invalidated mid-loop.
    if (!navigate.empty()) change_directory(navigate);
    if (!open.empty()) request_open(open);
}

void Ide::editor(mu_Context* ctx, const FrameInput& input) {
    const int full[] = {-1};
    const int status_h = ctx->text_height(ctx->style->font);
    mu_layout_row(ctx, 1, full, -(status_h + ctx->style->spacing + 1));
    if (editor_.update(ctx, input, fonts_.mono())) save_file();

    char status[128];
    const TextPos cur = editor_.cursor();
    std::snprintf(status, sizeof status, "Ln %d, Col %d    %d lines    %s%s", cur.line + 1,
                  editor_.visual_column(cur) + 1, editor_.line_count(), language_name(editor_.language()),
                  editor_.modified() ? "    modified" : "");
    mu_layout_row(ctx, 1, full, status_h);
    mu_label(ctx, status);
}

void Ide::vm_panel(mu_Context* ctx) {
    const int widths[] = {90, -1};
    mu_layout_row(ctx, 2, widths, 0);
    mu_label(ctx, "Status");
    mu_label(ctx, "not running");
    mu_label(ctx, "ABI");
    mu_label(ctx, "VIG32 / VIG64");
    mu_label(ctx, "PC");
    mu_label(ctx, "-");
    mu_label(ctx, "SP");
    mu_label(ctx, "-");

    const int full[] = {-1};
    mu_layout_row(ctx, 1, full, 0);
    if (mu_header_ex(ctx, "Stack", MU_OPT_EXPANDED)) mu_label(ctx, "(empty)");
    if (mu_header_ex(ctx, "Globals", MU_OPT_EXPANDED)) mu_label(ctx, "(empty)");
    if (mu_header(ctx, "Imports")) mu_label(ctx, "(none)");
}

void Ide::output(mu_Context* ctx) {
    const int full[] = {-1};
    mu_Font previous = ctx->style->font;
    ctx->style->font = &fonts_.mono();
    mu_layout_row(ctx, 1, full, ctx->text_height(ctx->style->font));
    for (const std::string& line : log_) mu_label(ctx, line.c_str());
    ctx->style->font = previous;

    if (scroll_log_frames_ > 0) {
        // microui clamps this against the content height on the next frame.
        mu_get_current_container(ctx)->scroll.y = 1 << 24;
        --scroll_log_frames_;
    }
}

}  // namespace vig
