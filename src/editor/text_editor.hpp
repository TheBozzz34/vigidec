#pragma once

#include "editor/highlight.hpp"
#include "editor/search.hpp"
#include "editor/text_buffer.hpp"
#include "platform/input.hpp"
#include "ui/mu.h"

#include <string>
#include <string_view>
#include <vector>

namespace vig {

class Font;

// A code editor widget drawn with microui primitives: monospace text with
// syntax highlighting, a line-number gutter, selection, clipboard, undo/redo
// and scrollbars. It takes keyboard input from FrameInput (microui only
// knows a few keys) while it has microui focus.
class TextEditor {
public:
    TextEditor();

    void set_text(std::string_view text);  // resets cursor and undo history
    std::string text() const { return buffer_.text(); }

    void set_language(Language lang);
    Language language() const { return language_; }

    bool modified() const { return saved_pos_ != static_cast<long>(undo_pos_); }
    void mark_saved() { saved_pos_ = static_cast<long>(undo_pos_); }

    TextPos cursor() const { return cursor_; }
    int visual_column(TextPos p) const;  // 0-based, tabs expanded
    int line_count() const { return buffer_.line_count(); }

    void request_focus() { focus_requested_ = true; }

    void undo();
    void redo();
    bool focused() const { return focused_; }

    // Selection.
    bool has_selection() const { return cursor_ != anchor_; }
    TextPos sel_start() const { return cursor_ < anchor_ ? cursor_ : anchor_; }
    TextPos sel_end() const { return cursor_ < anchor_ ? anchor_ : cursor_; }
    std::string selected_text() const { return buffer_.get(sel_start(), sel_end()); }
    void select(TextPos anchor, TextPos cursor);
    void go_to_line(int line);  // 0-based; clamps

    // Search. Matches are recomputed lazily when the query or text changes.
    void set_search(const SearchQuery& query);
    const SearchQuery& search_query() const { return query_; }
    const std::vector<SearchMatch>& matches();
    const std::string& search_error() const { return search_error_; }
    int match_index_at_selection();  // -1 unless the selection is exactly a match
    // Selects the first match starting at/after `from` (or the last one
    // before it when `backward`), wrapping around. False if nothing matches.
    bool find_from(TextPos from, bool backward);
    bool find_next(bool backward);
    // Replaces the selected match (then selects the next one), or just finds
    // the next match if the selection isn't one.
    void replace_current(std::string_view replacement);
    // Replaces every match as a single undo step; returns the count.
    int replace_all(std::string_view replacement);

    // Lays out in the next microui cell, handles input and draws.
    void update(mu_Context* ctx, const FrameInput& input, Font& font);

private:
    enum class EditKind { Typing, Other };
    enum class Drag { None, Select, Gutter, VScroll, HScroll };

    struct Edit {
        TextPos pos;
        std::string removed;
        std::string inserted;
        TextPos cursor_before, anchor_before;
        TextPos cursor_after;
        EditKind kind;
        double time;
        int group;  // edits sharing a group undo/redo together
    };

    struct Layout {
        mu_Rect bounds;
        mu_Rect gutter;
        mu_Rect text;  // visible text area (excludes gutter and scrollbars)
        mu_Rect vbar, hbar;
        float col_w;
        int line_h;
        int text_x;  // screen x of column 0 at scroll_x == 0
        int content_w, content_h;
    };

    // Editing.
    void replace(TextPos a, TextPos b, std::string_view text, EditKind kind);
    void insert_text(std::string_view text, EditKind kind);
    void delete_selection();
    void indent_lines(bool unindent);

    // Navigation helpers.
    TextPos prev_char(TextPos p) const;
    TextPos next_char(TextPos p) const;
    TextPos word_left(TextPos p) const;
    TextPos word_right(TextPos p) const;
    void word_at(TextPos p, TextPos& a, TextPos& b) const;
    int col_for_visual(int line, float vcol, bool nearest) const;
    void move_to(TextPos p, bool extend);

    // Frame steps.
    Layout compute_layout(mu_Rect bounds, Font& font);
    void handle_mouse(mu_Context* ctx, const Layout& lay);
    void handle_keys(const FrameInput& input, const Layout& lay);
    void scroll_to_cursor(const Layout& lay);
    void clamp_scroll(const Layout& lay);
    void draw(mu_Context* ctx, const Layout& lay, Font& font, bool focused);
    void draw_line_text(mu_Context* ctx, const Layout& lay, Font& font, int line, int y);
    TextPos hit_test(const Layout& lay, mu_Vec2 mouse) const;

    // Caches.
    void invalidate_from(int line);
    void update_highlight_states(int up_to_line);
    int max_visual_width();

    TextBuffer buffer_;
    Language language_ = Language::Plain;

    TextPos cursor_, anchor_;
    int want_vcol_ = -1;  // remembered column for vertical movement

    float scroll_x_ = 0.0f, scroll_y_ = 0.0f;
    bool ensure_cursor_visible_ = false;
    bool center_cursor_ = false;  // for jumps: centre the cursor if it's off-screen
    Drag drag_ = Drag::None;
    float drag_offset_ = 0.0f;
    int gutter_anchor_line_ = 0;

    double now_ = 0.0;
    double last_activity_ = 0.0;
    double last_click_time_ = -1.0;
    TextPos last_click_pos_;
    int click_count_ = 0;
    bool focus_requested_ = false;
    bool focused_ = false;

    std::vector<Edit> undo_;
    size_t undo_pos_ = 0;
    long saved_pos_ = 0;
    int next_group_ = 1;
    int open_group_ = 0;  // non-zero while batching edits into one undo step
    uint64_t version_ = 0;  // bumped on every text change

    SearchQuery query_;
    Searcher searcher_;
    std::string search_error_;
    std::vector<SearchMatch> matches_;
    uint64_t matches_version_ = ~uint64_t(0);

    // Highlighter state at the start of each line; valid below `highlight_valid_`.
    std::vector<int> line_states_;
    int highlight_valid_ = 0;
    std::vector<Span> spans_;

    int max_width_cache_ = -1;
};

}  // namespace vig
