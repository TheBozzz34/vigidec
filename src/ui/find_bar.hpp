#pragma once

#include "editor/text_editor.hpp"
#include "platform/input.hpp"

struct mu_Context;

namespace vig {

// Find / replace / go-to-line bar shown above the editor.
class FindBar {
public:
    enum class Mode { Closed, Find, Replace, GoToLine };

    // Opens (or re-focuses) the bar. Find/Replace prefill the search field
    // from a single-line selection.
    void open(Mode mode, TextEditor& editor);
    void close(TextEditor& editor);
    bool is_open() const { return mode_ != Mode::Closed; }
    Mode mode() const { return mode_; }

    // Height of the rows update() will lay out, so the editor can take the rest.
    int height(mu_Context* ctx) const;

    // Lays out the bar rows in the current window and applies their actions.
    void update(mu_Context* ctx, const FrameInput& input, TextEditor& editor);

    // F3 / Shift+F3: jump using the current query (opens the bar if empty).
    void find_next(TextEditor& editor, bool backward);

private:
    SearchQuery query() const;

    Mode mode_ = Mode::Closed;
    char find_[256] = "";
    char replace_[256] = "";
    char line_[16] = "";
    bool match_case_ = false;
    bool whole_word_ = false;
    bool regex_ = false;
    TextPos origin_;  // where search-as-you-type starts from
    bool focus_find_ = false;
    bool focus_replace_ = false;
    bool focus_line_ = false;
    std::string last_status_;
};

}  // namespace vig
