#include "ui/ide.hpp"

#include "platform/file_dialogs.hpp"
#include "render/font.hpp"
#include "ui/mu.h"
#include "ui/widgets.hpp"

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
constexpr const char* kPromptTitle = "Unsaved Changes";

constexpr const char* kWelcome =
    "# Welcome to the VIG IDE.\n"
    "#\n"
    "# Open a .vigas file from the explorer on the left, or start typing.\n"
    "# Ctrl+S saves, Ctrl+F finds, Ctrl+H replaces, Ctrl+G goes to a line.\n"
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

Ide::Ide(FontSystem& fonts, NativeFileDialogs& dialogs) : fonts_(fonts), dialogs_(dialogs) {
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

// --- Files ----------------------------------------------------------------------

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

std::string Ide::display_name() const { return file_path_.empty() ? "untitled" : file_path_.filename().string(); }

void Ide::new_file() {
    editor_.set_text("");
    editor_.set_language(Language::VigAsm);
    editor_.request_focus();
    file_path_.clear();
    log("New file");
}

bool Ide::open_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        log("Cannot open " + path.string());
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    editor_.set_text(text);
    editor_.set_language(language_for_path(path.string()));
    editor_.request_focus();
    file_path_ = path;
    log("Opened " + path.string());
    return true;
}

bool Ide::write_file(const fs::path& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const std::string text = editor_.text();
    if (!out || !out.write(text.data(), std::streamsize(text.size()))) {
        log("Failed to save " + path.string());
        return false;
    }
    editor_.mark_saved();
    log("Saved " + path.string());
    return true;
}

bool Ide::save_file() {
    if (file_path_.empty()) {
        save_as();
        return false;  // completes when the dialog does
    }
    return write_file(file_path_);
}

void Ide::save_as() { dialog_request_ = DialogRequest::SaveAs; }

void Ide::run_dialog(mu_Context* ctx) {
    const DialogRequest request = dialog_request_;
    dialog_request_ = DialogRequest::None;

    std::optional<fs::path> chosen;
    if (request == DialogRequest::SaveAs) {
        const fs::path dir = file_path_.empty() ? cwd_ : file_path_.parent_path();
        const std::string name = file_path_.empty() ? "untitled.vigas" : file_path_.filename().string();
        chosen = dialogs_.save_file(dir, name);
    } else {
        chosen = dialogs_.open_file(cwd_);
    }

    // The dialog swallowed the button/key releases for our window; don't
    // leave microui thinking they're still held.
    ctx->mouse_down = 0;
    ctx->key_down = 0;

    if (!chosen) {
        if (!dialogs_.error().empty()) log("File dialog: " + dialogs_.error());
        pending_ = Action::None;  // e.g. Save As from the unsaved-changes prompt was cancelled
        return;
    }

    if (request == DialogRequest::SaveAs) {
        if (write_file(*chosen)) {
            file_path_ = *chosen;
            editor_.set_language(language_for_path(chosen->string()));
            if (chosen->parent_path() == cwd_) change_directory(cwd_);
            if (pending_ != Action::None) perform(pending_, pending_path_);
        } else {
            pending_ = Action::None;
        }
    } else {
        guarded(Action::Open, *chosen);
    }
    editor_.request_focus();
}

void Ide::guarded(Action action, const fs::path& path) {
    if (!editor_.modified()) {
        perform(action, path);
        return;
    }
    pending_ = action;
    pending_path_ = path;
    prompt_open_ = true;
}

void Ide::perform(Action action, const fs::path& path) {
    pending_ = Action::None;
    switch (action) {
        case Action::New: new_file(); break;
        case Action::Open: open_file(path); break;
        case Action::Quit: quit_ = true; break;
        case Action::None: break;
    }
}

void Ide::request_quit() {
    if (!prompt_open_) guarded(Action::Quit);
}

// --- Frame ----------------------------------------------------------------------

FrameInput Ide::route_shortcuts(const FrameInput& input) {
    FrameInput rest = input;
    rest.keys.clear();

    // While the prompt is up it owns the keyboard (it reads `input` itself).
    if (prompt_open_) {
        rest.text.clear();
        return rest;
    }

    for (const KeyEvent& ev : input.keys) {
        if (ev.ctrl) {
            switch (ev.key) {
                case Key::N: guarded(Action::New); continue;
                case Key::O: dialog_request_ = DialogRequest::Open; continue;
                case Key::S:
                    if (ev.shift) {
                        save_as();
                    } else {
                        save_file();
                    }
                    continue;
                case Key::F: find_bar_.open(FindBar::Mode::Find, editor_); continue;
                case Key::H: find_bar_.open(FindBar::Mode::Replace, editor_); continue;
                case Key::G: find_bar_.open(FindBar::Mode::GoToLine, editor_); continue;
                // Text fields have no undo of their own, so these always
                // target the document (e.g. undoing Replace All from the bar).
                case Key::Z:
                    if (ev.shift) {
                        editor_.redo();
                    } else {
                        editor_.undo();
                    }
                    continue;
                case Key::Y: editor_.redo(); continue;
                default: break;
            }
        } else if (ev.key == Key::F3) {
            find_bar_.find_next(editor_, ev.shift);
            continue;
        } else if (ev.key == Key::Escape && find_bar_.is_open()) {
            find_bar_.close(editor_);
            continue;
        }
        rest.keys.push_back(ev);
    }
    return rest;
}

void Ide::frame(mu_Context* ctx, const FrameInput& input, int width, int height) {
    if (dialog_request_ != DialogRequest::None) run_dialog(ctx);
    const FrameInput widget_input = route_shortcuts(input);

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
        editor(ctx, widget_input);
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

    // Modals last, on top of everything.
    if (prompt_open_) unsaved_prompt(ctx, input, width, height);
}

void Ide::unsaved_prompt(mu_Context* ctx, const FrameInput& input, int width, int height) {
    for (const KeyEvent& ev : input.keys) {
        if (ev.key == Key::Escape) {
            prompt_open_ = false;
            pending_ = Action::None;
            return;
        }
    }

    modal_backdrop(ctx, kPromptTitle, width, height);
    const mu_Rect rect = centered_rect(width, height, 460, 130);
    if (mu_Container* cnt = mu_get_container(ctx, kPromptTitle)) cnt->rect = rect;
    if (!mu_begin_window_ex(ctx, kPromptTitle, rect, MU_OPT_NOCLOSE | MU_OPT_NORESIZE | MU_OPT_NOSCROLL)) return;

    const int full[] = {-1};
    mu_layout_row(ctx, 1, full, 0);
    const std::string question = "Save changes to " + display_name() + "?";
    mu_label(ctx, question.c_str());
    mu_label(ctx, "Your changes will be lost if you don't save them.");

    const int widths[] = {-312, 100, 100, 100};
    mu_layout_row(ctx, 4, widths, 0);
    mu_label(ctx, "");
    const bool save = mu_button(ctx, "Save") != 0;
    const bool discard = mu_button(ctx, "Don't Save") != 0;
    const bool cancel = mu_button(ctx, "Cancel") != 0;
    mu_end_window(ctx);

    if (save) {
        prompt_open_ = false;
        // Untitled buffers go through Save As; the pending action runs after.
        if (save_file()) perform(pending_, pending_path_);
    } else if (discard) {
        prompt_open_ = false;
        perform(pending_, pending_path_);
    } else if (cancel) {
        prompt_open_ = false;
        pending_ = Action::None;
    }
}

void Ide::toolbar(mu_Context* ctx) {
    static const int widths[] = {56, 56, 56, 72, 10, 84, 56, 56, 56, -1};
    mu_layout_row(ctx, 10, widths, -1);
    if (mu_button(ctx, "New")) guarded(Action::New);
    if (mu_button(ctx, "Open")) dialog_request_ = DialogRequest::Open;
    if (mu_button(ctx, "Save")) save_file();
    if (mu_button(ctx, "Save As")) save_as();
    mu_label(ctx, "");
    if (mu_button(ctx, "Assemble")) log("Assemble: not implemented yet");
    if (mu_button(ctx, "Run")) log("Run: not implemented yet");
    if (mu_button(ctx, "Step")) log("Step: not implemented yet");
    if (mu_button(ctx, "Stop")) log("Stop: not implemented yet");
    std::string title = display_name();
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
    if (!open.empty()) guarded(Action::Open, open);
}

void Ide::editor(mu_Context* ctx, const FrameInput& input) {
    const int full[] = {-1};
    find_bar_.update(ctx, input, editor_);

    const int status_h = ctx->text_height(ctx->style->font);
    mu_layout_row(ctx, 1, full, -(status_h + ctx->style->spacing + 1));
    editor_.update(ctx, input, fonts_.mono());

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
