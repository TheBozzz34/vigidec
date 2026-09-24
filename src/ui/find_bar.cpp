#include "ui/find_bar.hpp"

#include "ui/widgets.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace vig {

namespace {

constexpr int kControlsWidth = 300;  // everything right of the text field

void copy_to(char* dst, size_t size, const std::string& s) {
    const size_t n = std::min(s.size(), size - 1);
    std::memcpy(dst, s.data(), n);
    dst[n] = '\0';
}

}  // namespace

SearchQuery FindBar::query() const {
    SearchQuery q;
    q.text = find_;
    q.match_case = match_case_;
    q.whole_word = whole_word_;
    q.regex = regex_;
    return q;
}

void FindBar::open(Mode mode, TextEditor& editor) {
    if (mode == Mode::GoToLine) {
        line_[0] = '\0';
        focus_line_ = true;
    } else {
        if (editor.has_selection() && editor.sel_start().line == editor.sel_end().line) {
            copy_to(find_, sizeof find_, editor.selected_text());
        }
        origin_ = editor.sel_start();
        focus_find_ = true;
    }
    mode_ = mode;
}

void FindBar::close(TextEditor& editor) {
    mode_ = Mode::Closed;
    editor.set_search({});
    editor.request_focus();
}

int FindBar::height(mu_Context* ctx) const {
    const int row = ctx->style->size.y + ctx->style->padding * 2;
    const int rows = mode_ == Mode::Replace ? 2 : mode_ == Mode::Closed ? 0 : 1;
    return rows * (row + ctx->style->spacing);
}

void FindBar::find_next(TextEditor& editor, bool backward) {
    if (find_[0] == '\0') {
        open(Mode::Find, editor);
        return;
    }
    editor.set_search(query());
    editor.find_next(backward);
}

void FindBar::update(mu_Context* ctx, const FrameInput& input, TextEditor& editor) {
    if (mode_ == Mode::Closed) return;

    if (mode_ == Mode::GoToLine) {
        char range[48];
        std::snprintf(range, sizeof range, "(1 - %d)", editor.line_count());
        const int widths[] = {80, 120, -1};
        mu_layout_row(ctx, 3, widths, 0);
        mu_label(ctx, "Go to line");
        const int res = textbox(ctx, line_, sizeof line_, input, focus_line_);
        focus_line_ = false;
        mu_label(ctx, range);
        if (res & MU_RES_SUBMIT) {
            const int line = std::atoi(line_);
            if (line > 0) editor.go_to_line(line - 1);
            close(editor);
        }
        return;
    }

    // --- Find row ---
    const int find_widths[] = {-kControlsWidth, 34, 30, 30, 28, 28, 96, 26};
    mu_layout_row(ctx, 8, find_widths, 0);
    const int find_res = textbox(ctx, find_, sizeof find_, input, focus_find_);
    focus_find_ = false;
    toggle_button(ctx, "Aa", match_case_);
    toggle_button(ctx, "W", whole_word_);
    toggle_button(ctx, ".*", regex_);
    const bool prev = mu_button(ctx, "\xE2\x86\x91") != 0;  // up arrow
    const bool next = mu_button(ctx, "\xE2\x86\x93") != 0;  // down arrow

    // Search as you type: re-run from where the search started.
    const SearchQuery q = query();
    const bool changed = !(q == editor.search_query());
    editor.set_search(q);
    if (changed) last_status_.clear();
    if (changed && !q.empty()) editor.find_from(origin_, false);

    if (find_res & MU_RES_SUBMIT) {
        editor.find_next((ctx->key_down & MU_KEY_SHIFT) != 0);
        focus_find_ = true;  // microui drops focus on Enter; keep typing here
    }
    if (prev) editor.find_next(true);
    if (next) editor.find_next(false);

    // Status.
    char status[64] = "";
    if (!editor.search_error().empty()) {
        std::snprintf(status, sizeof status, "Invalid regex");
    } else if (!q.empty()) {
        const size_t count = editor.matches().size();
        const int current = editor.match_index_at_selection();
        const char* more = count >= Searcher::kMaxMatches ? "+" : "";
        if (count == 0) {
            std::snprintf(status, sizeof status, "No results");
        } else if (current >= 0) {
            std::snprintf(status, sizeof status, "%d of %zu%s", current + 1, count, more);
        } else {
            std::snprintf(status, sizeof status, "%zu%s found", count, more);
        }
    }
    mu_label(ctx, status);
    if (mu_button(ctx, "\xC3\x97")) {  // multiplication sign as a close glyph
        close(editor);
        return;
    }

    if (mode_ != Mode::Replace) return;

    // --- Replace row ---
    const int replace_widths[] = {-kControlsWidth, 84, 96, -1};
    mu_layout_row(ctx, 4, replace_widths, 0);
    const int replace_res = textbox(ctx, replace_, sizeof replace_, input, focus_replace_);
    focus_replace_ = false;
    const bool replace_one = mu_button(ctx, "Replace") != 0;
    const bool replace_all = mu_button(ctx, "Replace All") != 0;
    mu_label(ctx, last_status_.c_str());

    if (replace_res & MU_RES_SUBMIT) focus_replace_ = true;
    if (replace_one || (replace_res & MU_RES_SUBMIT)) {
        editor.replace_current(replace_);
        last_status_.clear();
    }
    if (replace_all) {
        const int n = editor.replace_all(replace_);
        last_status_ = "Replaced " + std::to_string(n);
    }
}

}  // namespace vig
