#include "editor/text_editor.hpp"

#include "render/font.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace vig {

namespace {

constexpr int kTabWidth = 4;
constexpr int kScrollbarSize = 10;
constexpr int kTextPadding = 8;
constexpr double kDoubleClickTime = 0.35;
constexpr double kCoalesceTime = 1.0;
constexpr double kBlinkPeriod = 1.0;

const mu_Color kBackground = {30, 30, 33, 255};
const mu_Color kGutterBackground = {30, 30, 33, 255};
const mu_Color kLineNumber = {100, 100, 110, 255};
const mu_Color kLineNumberActive = {200, 200, 205, 255};
const mu_Color kCurrentLine = {40, 40, 46, 255};
const mu_Color kSelection = {38, 79, 120, 255};
const mu_Color kSelectionUnfocused = {58, 61, 68, 255};
const mu_Color kSearchMatch = {120, 90, 30, 150};
const mu_Color kCurrentMatch = {170, 110, 30, 220};
const mu_Color kCaret = {230, 230, 230, 255};
const mu_Color kScrollTrack = {35, 35, 39, 255};
const mu_Color kScrollThumb = {75, 75, 82, 255};
const mu_Color kScrollThumbActive = {100, 100, 110, 255};

mu_Color token_color(Token t) {
    switch (t) {
        case Token::Comment: return {106, 153, 85, 255};
        case Token::Keyword: return {86, 156, 214, 255};
        case Token::Label: return {220, 220, 170, 255};
        case Token::Number: return {181, 206, 168, 255};
        case Token::String: return {206, 145, 120, 255};
        case Token::Directive: return {197, 134, 192, 255};
        case Token::Punct: return {170, 170, 175, 255};
        default: return {212, 212, 212, 255};
    }
}

enum class CharClass { Space, Word, Punct };

CharClass classify(unsigned char c) {
    if (c == ' ' || c == '\t') return CharClass::Space;
    if (std::isalnum(c) || c == '_' || c >= 0x80) return CharClass::Word;
    return CharClass::Punct;
}

bool is_continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

std::string strip_cr(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c != '\r') out.push_back(c);
    }
    return out;
}

int leading_whitespace(const std::string& line) {
    int n = 0;
    while (n < int(line.size()) && (line[size_t(n)] == ' ' || line[size_t(n)] == '\t')) ++n;
    return n;
}

}  // namespace

TextEditor::TextEditor() = default;

void TextEditor::set_text(std::string_view text) {
    buffer_.set_text(text);
    cursor_ = anchor_ = {};
    want_vcol_ = -1;
    scroll_x_ = scroll_y_ = 0.0f;
    undo_.clear();
    undo_pos_ = 0;
    saved_pos_ = 0;
    ++version_;
    invalidate_from(0);
}

void TextEditor::set_language(Language lang) {
    language_ = lang;
    invalidate_from(0);
}

// --- Caches -------------------------------------------------------------------

void TextEditor::invalidate_from(int line) {
    highlight_valid_ = std::min(highlight_valid_, std::max(0, line));
    max_width_cache_ = -1;
}

void TextEditor::update_highlight_states(int up_to_line) {
    const int n = buffer_.line_count();
    up_to_line = std::min(up_to_line, n - 1);
    line_states_.resize(size_t(n) + 1);
    if (highlight_valid_ == 0) {
        line_states_[0] = 0;
        highlight_valid_ = 1;  // state at the start of line 0 is known
    }
    // line_states_[i] is the state entering line i; valid for i < highlight_valid_.
    for (int i = highlight_valid_ - 1; i < up_to_line; ++i) {
        int state = line_states_[size_t(i)];
        highlight_line(language_, buffer_.line(i), state, spans_);
        line_states_[size_t(i) + 1] = state;
        highlight_valid_ = i + 2;
    }
}

int TextEditor::max_visual_width() {
    if (max_width_cache_ < 0) {
        int widest = 0;
        for (int i = 0; i < buffer_.line_count(); ++i) {
            widest = std::max(widest, visual_column({i, int(buffer_.line(i).size())}));
        }
        max_width_cache_ = widest;
    }
    return max_width_cache_;
}

// --- Positions ----------------------------------------------------------------

int TextEditor::visual_column(TextPos p) const {
    const std::string& s = buffer_.line(p.line);
    int v = 0;
    for (int i = 0; i < p.col && i < int(s.size()); ++i) {
        const char c = s[size_t(i)];
        if (c == '\t') {
            v += kTabWidth - v % kTabWidth;
        } else if (!is_continuation(c)) {
            ++v;
        }
    }
    return v;
}

int TextEditor::col_for_visual(int line, float vcol, bool nearest) const {
    const std::string& s = buffer_.line(line);
    int v = 0;
    int i = 0;
    while (i < int(s.size())) {
        const int w = s[size_t(i)] == '\t' ? kTabWidth - v % kTabWidth : 1;
        const float threshold = nearest ? float(v) + float(w) * 0.5f : float(v + w);
        if (vcol < threshold) break;
        v += w;
        ++i;
        while (i < int(s.size()) && is_continuation(s[size_t(i)])) ++i;
    }
    return i;
}

TextPos TextEditor::prev_char(TextPos p) const {
    if (p.col == 0) return p.line == 0 ? p : TextPos{p.line - 1, int(buffer_.line(p.line - 1).size())};
    const std::string& s = buffer_.line(p.line);
    int c = p.col - 1;
    while (c > 0 && is_continuation(s[size_t(c)])) --c;
    return {p.line, c};
}

TextPos TextEditor::next_char(TextPos p) const {
    const std::string& s = buffer_.line(p.line);
    if (p.col >= int(s.size())) return p.line + 1 < buffer_.line_count() ? TextPos{p.line + 1, 0} : p;
    int c = p.col + 1;
    while (c < int(s.size()) && is_continuation(s[size_t(c)])) ++c;
    return {p.line, c};
}

TextPos TextEditor::word_left(TextPos p) const {
    if (p.col == 0) return prev_char(p);
    const std::string& s = buffer_.line(p.line);
    int c = p.col;
    while (c > 0 && classify(static_cast<unsigned char>(s[size_t(c - 1)])) == CharClass::Space) --c;
    if (c > 0) {
        const CharClass cls = classify(static_cast<unsigned char>(s[size_t(c - 1)]));
        while (c > 0 && classify(static_cast<unsigned char>(s[size_t(c - 1)])) == cls) --c;
    }
    return {p.line, c};
}

TextPos TextEditor::word_right(TextPos p) const {
    const std::string& s = buffer_.line(p.line);
    if (p.col >= int(s.size())) return next_char(p);
    int c = p.col;
    const CharClass cls = classify(static_cast<unsigned char>(s[size_t(c)]));
    while (c < int(s.size()) && classify(static_cast<unsigned char>(s[size_t(c)])) == cls) ++c;
    while (c < int(s.size()) && classify(static_cast<unsigned char>(s[size_t(c)])) == CharClass::Space) ++c;
    return {p.line, c};
}

void TextEditor::word_at(TextPos p, TextPos& a, TextPos& b) const {
    const std::string& s = buffer_.line(p.line);
    a = b = p;
    if (s.empty()) return;
    int probe = std::min(p.col, int(s.size()) - 1);
    const CharClass cls = classify(static_cast<unsigned char>(s[size_t(probe)]));
    int lo = probe, hi = probe + 1;
    while (lo > 0 && classify(static_cast<unsigned char>(s[size_t(lo - 1)])) == cls) --lo;
    while (hi < int(s.size()) && classify(static_cast<unsigned char>(s[size_t(hi)])) == cls) ++hi;
    a = {p.line, lo};
    b = {p.line, hi};
}

void TextEditor::move_to(TextPos p, bool extend) {
    cursor_ = buffer_.clamp(p);
    if (!extend) anchor_ = cursor_;
    want_vcol_ = -1;
    ensure_cursor_visible_ = true;
}

// --- Editing ------------------------------------------------------------------

void TextEditor::replace(TextPos a, TextPos b, std::string_view text, EditKind kind) {
    if (b < a) std::swap(a, b);
    a = buffer_.clamp(a);
    b = buffer_.clamp(b);
    const std::string inserted = strip_cr(text);
    if (a == b && inserted.empty()) return;

    const int group = open_group_ ? open_group_ : next_group_++;
    Edit e{a, buffer_.get(a, b), inserted, cursor_, anchor_, {}, kind, now_, group};
    ++version_;
    buffer_.erase(a, b);
    const TextPos end = buffer_.insert(a, inserted);
    cursor_ = anchor_ = end;
    e.cursor_after = end;
    invalidate_from(a.line);
    want_vcol_ = -1;
    ensure_cursor_visible_ = true;

    // Merge runs of typed characters into one undo step.
    if (kind == EditKind::Typing && !open_group_ && undo_pos_ > 0 && undo_pos_ == undo_.size() &&
        saved_pos_ != static_cast<long>(undo_pos_) && e.removed.empty()) {
        Edit& last = undo_.back();
        if (last.kind == EditKind::Typing && last.removed.empty() &&
            TextBuffer::advance(last.pos, last.inserted) == a && now_ - last.time < kCoalesceTime &&
            inserted != "\n") {
            last.inserted += inserted;
            last.cursor_after = end;
            last.time = now_;
            return;
        }
    }

    undo_.resize(undo_pos_);
    if (saved_pos_ > static_cast<long>(undo_pos_)) saved_pos_ = -1;  // saved state is no longer reachable
    undo_.push_back(std::move(e));
    ++undo_pos_;
}

void TextEditor::insert_text(std::string_view text, EditKind kind) { replace(sel_start(), sel_end(), text, kind); }

void TextEditor::delete_selection() {
    if (has_selection()) replace(sel_start(), sel_end(), {}, EditKind::Other);
}

void TextEditor::undo() {
    if (undo_pos_ == 0) return;
    const int group = undo_[undo_pos_ - 1].group;
    while (undo_pos_ > 0 && undo_[undo_pos_ - 1].group == group) {
        const Edit& e = undo_[--undo_pos_];
        buffer_.erase(e.pos, TextBuffer::advance(e.pos, e.inserted));
        buffer_.insert(e.pos, e.removed);
        cursor_ = e.cursor_before;
        anchor_ = e.anchor_before;
        invalidate_from(e.pos.line);
    }
    ++version_;
    want_vcol_ = -1;
    ensure_cursor_visible_ = true;
}

void TextEditor::redo() {
    if (undo_pos_ >= undo_.size()) return;
    const int group = undo_[undo_pos_].group;
    while (undo_pos_ < undo_.size() && undo_[undo_pos_].group == group) {
        const Edit& e = undo_[undo_pos_++];
        buffer_.erase(e.pos, TextBuffer::advance(e.pos, e.removed));
        buffer_.insert(e.pos, e.inserted);
        cursor_ = anchor_ = e.cursor_after;
        invalidate_from(e.pos.line);
    }
    ++version_;
    want_vcol_ = -1;
    ensure_cursor_visible_ = true;
}

// --- Selection & search -------------------------------------------------------

void TextEditor::select(TextPos anchor, TextPos cursor) {
    anchor_ = buffer_.clamp(anchor);
    cursor_ = buffer_.clamp(cursor);
    want_vcol_ = -1;
    ensure_cursor_visible_ = true;
    center_cursor_ = true;
}

void TextEditor::go_to_line(int line) {
    line = std::clamp(line, 0, buffer_.line_count() - 1);
    const int indent = leading_whitespace(buffer_.line(line));
    select({line, indent}, {line, indent});
}

void TextEditor::set_search(const SearchQuery& query) {
    if (query == query_) return;
    query_ = query;
    searcher_.compile(query_, search_error_);
    matches_version_ = ~uint64_t(0);
}

const std::vector<SearchMatch>& TextEditor::matches() {
    if (matches_version_ != version_) {
        searcher_.find_all(buffer_, matches_);
        matches_version_ = version_;
    }
    return matches_;
}

int TextEditor::match_index_at_selection() {
    const std::vector<SearchMatch>& ms = matches();
    const TextPos a = sel_start(), b = sel_end();
    auto it = std::lower_bound(ms.begin(), ms.end(), a,
                               [](const SearchMatch& m, TextPos p) { return m.start < p; });
    if (it != ms.end() && it->start == a && it->end == b) return int(it - ms.begin());
    return -1;
}

bool TextEditor::find_from(TextPos from, bool backward) {
    const std::vector<SearchMatch>& ms = matches();
    if (ms.empty()) return false;
    const SearchMatch* hit = nullptr;
    if (backward) {
        auto it = std::lower_bound(ms.begin(), ms.end(), from,
                                   [](const SearchMatch& m, TextPos p) { return m.start < p; });
        hit = it == ms.begin() ? &ms.back() : &*(it - 1);
    } else {
        auto it = std::lower_bound(ms.begin(), ms.end(), from,
                                   [](const SearchMatch& m, TextPos p) { return m.start < p; });
        hit = it == ms.end() ? &ms.front() : &*it;
    }
    select(hit->start, hit->end);
    return true;
}

bool TextEditor::find_next(bool backward) {
    if (backward) return find_from(sel_start(), true);
    // Step past the current match so repeated "next" advances.
    const int current = match_index_at_selection();
    return find_from(current >= 0 ? next_char(sel_start()) : sel_end(), false);
}

void TextEditor::replace_current(std::string_view replacement) {
    const int index = match_index_at_selection();
    if (index < 0) {
        find_next(false);
        return;
    }
    const SearchMatch m = matches()[size_t(index)];
    const std::string text = searcher_.replacement_for(buffer_, m, replacement);
    replace(m.start, m.end, text, EditKind::Other);
    find_from(cursor_, false);
}

int TextEditor::replace_all(std::string_view replacement) {
    const std::vector<SearchMatch> ms = matches();
    if (ms.empty()) return 0;

    // Compute every replacement against the original text first (regex
    // groups may look at text that earlier replacements would change), then
    // apply back to front so earlier positions stay valid.
    std::vector<std::string> texts;
    texts.reserve(ms.size());
    for (const SearchMatch& m : ms) texts.push_back(searcher_.replacement_for(buffer_, m, replacement));

    open_group_ = next_group_++;
    for (size_t i = ms.size(); i-- > 0;) replace(ms[i].start, ms[i].end, texts[i], EditKind::Other);
    open_group_ = 0;
    return int(ms.size());
}

void TextEditor::indent_lines(bool unindent) {
    const TextPos s = sel_start();
    const TextPos e = sel_end();
    const int first = s.line;
    const int last = (e.col == 0 && e.line > s.line) ? e.line - 1 : e.line;

    std::string text;
    for (int l = first; l <= last; ++l) {
        std::string line = buffer_.line(l);
        if (unindent) {
            int remove = 0;
            if (!line.empty() && line[0] == '\t') {
                remove = 1;
            } else {
                while (remove < kTabWidth && remove < int(line.size()) && line[size_t(remove)] == ' ') ++remove;
            }
            line.erase(0, size_t(remove));
        } else if (!line.empty()) {
            line.insert(0, size_t(kTabWidth), ' ');
        }
        if (l > first) text += '\n';
        text += line;
    }

    const TextPos a{first, 0};
    const TextPos b{last, int(buffer_.line(last).size())};
    if (buffer_.get(a, b) == text) return;
    replace(a, b, text, EditKind::Other);
    anchor_ = a;
    cursor_ = {last, int(buffer_.line(last).size())};
}

// --- Frame ----------------------------------------------------------------------

TextEditor::Layout TextEditor::compute_layout(mu_Rect bounds, Font& font) {
    Layout lay{};
    lay.bounds = bounds;
    lay.col_w = font.advance(' ');
    lay.line_h = font.height() + 3;

    int digits = 1;
    for (int n = buffer_.line_count(); n >= 10; n /= 10) ++digits;
    const int gutter_w = int(std::ceil(float(std::max(digits, 3) + 3) * lay.col_w));

    lay.gutter = mu_rect(bounds.x, bounds.y, gutter_w, bounds.h - kScrollbarSize);
    lay.text = mu_rect(bounds.x + gutter_w, bounds.y, std::max(0, bounds.w - gutter_w - kScrollbarSize),
                       std::max(0, bounds.h - kScrollbarSize));
    lay.vbar = mu_rect(bounds.x + bounds.w - kScrollbarSize, bounds.y, kScrollbarSize, lay.text.h);
    lay.hbar = mu_rect(lay.text.x, bounds.y + bounds.h - kScrollbarSize, lay.text.w, kScrollbarSize);
    lay.text_x = lay.text.x + kTextPadding;
    lay.content_h = buffer_.line_count() * lay.line_h + lay.line_h;
    lay.content_w = int(std::ceil(float(max_visual_width() + 4) * lay.col_w)) + kTextPadding;
    return lay;
}

TextPos TextEditor::hit_test(const Layout& lay, mu_Vec2 mouse) const {
    const float y = float(mouse.y - lay.text.y) + scroll_y_;
    int line = int(std::floor(y / float(lay.line_h)));
    if (line < 0) return {0, 0};
    if (line >= buffer_.line_count()) return buffer_.end();
    const float vcol = (float(mouse.x - lay.text_x) + scroll_x_) / lay.col_w;
    return {line, col_for_visual(line, vcol, true)};
}

void TextEditor::clamp_scroll(const Layout& lay) {
    const float max_y = float(std::max(0, lay.content_h - lay.text.h));
    const float max_x = float(std::max(0, lay.content_w - lay.text.w));
    scroll_y_ = std::clamp(scroll_y_, 0.0f, max_y);
    scroll_x_ = std::clamp(scroll_x_, 0.0f, max_x);
}

void TextEditor::scroll_to_cursor(const Layout& lay) {
    const float y = float(cursor_.line * lay.line_h);
    const bool offscreen = y < scroll_y_ || y + float(lay.line_h) > scroll_y_ + float(lay.text.h);
    if (center_cursor_ && offscreen) scroll_y_ = y - float(lay.text.h - lay.line_h) * 0.5f;
    if (y < scroll_y_) scroll_y_ = y;
    if (y + float(lay.line_h) > scroll_y_ + float(lay.text.h)) scroll_y_ = y + float(lay.line_h) - float(lay.text.h);

    const float margin = 4.0f * lay.col_w;
    const float x = float(visual_column(cursor_)) * lay.col_w;
    const float view_w = float(lay.text.w - kTextPadding);
    if (x - margin < scroll_x_) scroll_x_ = std::max(0.0f, x - margin);
    if (x + margin > scroll_x_ + view_w) scroll_x_ = x + margin - view_w;
}

void TextEditor::handle_mouse(mu_Context* ctx, const Layout& lay) {
    const mu_Vec2 m = ctx->mouse_pos;

    if (mu_mouse_over(ctx, lay.bounds) && (ctx->scroll_delta.x != 0 || ctx->scroll_delta.y != 0)) {
        if (ctx->key_down & MU_KEY_SHIFT) {
            scroll_x_ += float(ctx->scroll_delta.y);
        } else {
            scroll_y_ += float(ctx->scroll_delta.y);
        }
        scroll_x_ += float(ctx->scroll_delta.x);
        ctx->scroll_delta = mu_vec2(0, 0);  // don't also scroll the parent window
    }

    const float max_y = float(std::max(0, lay.content_h - lay.text.h));
    const float max_x = float(std::max(0, lay.content_w - lay.text.w));
    const float thumb_h = max_y > 0 ? std::max(20.0f, float(lay.vbar.h) * float(lay.text.h) / float(lay.content_h)) : 0;
    const float thumb_w = max_x > 0 ? std::max(20.0f, float(lay.hbar.w) * float(lay.text.w) / float(lay.content_w)) : 0;

    if ((ctx->mouse_pressed & MU_MOUSE_LEFT) && mu_mouse_over(ctx, lay.bounds)) {
        last_activity_ = now_;
        auto inside = [&](mu_Rect r) { return m.x >= r.x && m.x < r.x + r.w && m.y >= r.y && m.y < r.y + r.h; };
        if (inside(lay.vbar)) {
            if (max_y > 0) {
                const float thumb_y = float(lay.vbar.y) + (float(lay.vbar.h) - thumb_h) * scroll_y_ / max_y;
                const bool on_thumb = float(m.y) >= thumb_y && float(m.y) < thumb_y + thumb_h;
                drag_offset_ = on_thumb ? float(m.y) - thumb_y : thumb_h * 0.5f;
                drag_ = Drag::VScroll;
            }
        } else if (inside(lay.hbar)) {
            if (max_x > 0) {
                const float thumb_x = float(lay.hbar.x) + (float(lay.hbar.w) - thumb_w) * scroll_x_ / max_x;
                const bool on_thumb = float(m.x) >= thumb_x && float(m.x) < thumb_x + thumb_w;
                drag_offset_ = on_thumb ? float(m.x) - thumb_x : thumb_w * 0.5f;
                drag_ = Drag::HScroll;
            }
        } else if (inside(lay.gutter)) {
            const TextPos p = hit_test(lay, m);
            gutter_anchor_line_ = p.line;
            drag_ = Drag::Gutter;
        } else {
            const TextPos p = hit_test(lay, m);
            const bool repeat = now_ - last_click_time_ < kDoubleClickTime && p == last_click_pos_;
            click_count_ = repeat ? click_count_ % 3 + 1 : 1;
            last_click_time_ = now_;
            last_click_pos_ = p;

            if (click_count_ == 2) {
                TextPos a, b;
                word_at(p, a, b);
                anchor_ = a;
                cursor_ = b;
            } else if (click_count_ == 3) {
                anchor_ = {p.line, 0};
                cursor_ = p.line + 1 < buffer_.line_count() ? TextPos{p.line + 1, 0}
                                                            : TextPos{p.line, int(buffer_.line(p.line).size())};
            } else {
                move_to(p, (ctx->key_down & MU_KEY_SHIFT) != 0);
            }
            want_vcol_ = -1;
            drag_ = Drag::Select;
        }
    }

    if (!(ctx->mouse_down & MU_MOUSE_LEFT)) {
        drag_ = Drag::None;
        return;
    }

    switch (drag_) {
        case Drag::VScroll:
            if (lay.vbar.h > thumb_h) {
                scroll_y_ = (float(m.y) - drag_offset_ - float(lay.vbar.y)) / (float(lay.vbar.h) - thumb_h) * max_y;
            }
            break;
        case Drag::HScroll:
            if (lay.hbar.w > thumb_w) {
                scroll_x_ = (float(m.x) - drag_offset_ - float(lay.hbar.x)) / (float(lay.hbar.w) - thumb_w) * max_x;
            }
            break;
        case Drag::Gutter: {
            const int line = hit_test(lay, m).line;
            const int lo = std::min(line, gutter_anchor_line_);
            const int hi = std::max(line, gutter_anchor_line_);
            const TextPos start{lo, 0};
            const TextPos end = hi + 1 < buffer_.line_count() ? TextPos{hi + 1, 0}
                                                              : TextPos{hi, int(buffer_.line(hi).size())};
            anchor_ = line < gutter_anchor_line_ ? end : start;
            cursor_ = line < gutter_anchor_line_ ? start : end;
            ensure_cursor_visible_ = true;
            break;
        }
        case Drag::Select:
            if (click_count_ == 1) {
                cursor_ = hit_test(lay, m);
                ensure_cursor_visible_ = true;
            }
            break;
        default:
            break;
    }
}

void TextEditor::handle_keys(const FrameInput& input, const Layout& lay) {
    const int page = std::max(1, lay.text.h / lay.line_h - 1);

    for (const KeyEvent& ev : input.keys) {
        last_activity_ = now_;
        const bool shift = ev.shift;
        if (ev.ctrl) {
            switch (ev.key) {
                case Key::A:
                    anchor_ = buffer_.begin();
                    cursor_ = buffer_.end();
                    break;
                case Key::C:
                case Key::X:
                    if (has_selection()) {
                        input.set_clipboard_text(buffer_.get(sel_start(), sel_end()));
                        if (ev.key == Key::X) delete_selection();
                    } else {  // no selection: copy/cut the whole line
                        const int l = cursor_.line;
                        const int len = int(buffer_.line(l).size());
                        input.set_clipboard_text(buffer_.line(l) + "\n");
                        if (ev.key == Key::X) {
                            if (l + 1 < buffer_.line_count()) {
                                replace({l, 0}, {l + 1, 0}, {}, EditKind::Other);
                            } else if (l > 0) {
                                replace({l - 1, int(buffer_.line(l - 1).size())}, {l, len}, {}, EditKind::Other);
                            } else {
                                replace({0, 0}, {0, len}, {}, EditKind::Other);
                            }
                        }
                    }
                    break;
                case Key::V: insert_text(input.clipboard(), EditKind::Other); break;
                case Key::D: {  // duplicate selection or line
                    if (has_selection()) {
                        const std::string text = buffer_.get(sel_start(), sel_end());
                        const TextPos end = sel_end();
                        replace(end, end, text, EditKind::Other);
                        anchor_ = end;
                    } else {
                        const int l = cursor_.line;
                        const TextPos cur = cursor_;
                        const TextPos eol{l, int(buffer_.line(l).size())};
                        replace(eol, eol, "\n" + buffer_.line(l), EditKind::Other);
                        cursor_ = anchor_ = {l + 1, cur.col};
                    }
                    break;
                }
                case Key::Left: move_to(word_left(cursor_), shift); break;
                case Key::Right: move_to(word_right(cursor_), shift); break;
                case Key::Home: move_to(buffer_.begin(), shift); break;
                case Key::End: move_to(buffer_.end(), shift); break;
                case Key::Backspace:
                    if (has_selection()) {
                        delete_selection();
                    } else {
                        replace(word_left(cursor_), cursor_, {}, EditKind::Other);
                    }
                    break;
                case Key::Delete:
                    if (has_selection()) {
                        delete_selection();
                    } else {
                        replace(cursor_, word_right(cursor_), {}, EditKind::Other);
                    }
                    break;
                case Key::Up: scroll_y_ -= float(lay.line_h); break;
                case Key::Down: scroll_y_ += float(lay.line_h); break;
                default: break;
            }
            continue;
        }

        switch (ev.key) {
            case Key::Left:
                if (has_selection() && !shift) {
                    move_to(sel_start(), false);
                } else {
                    move_to(prev_char(cursor_), shift);
                }
                break;
            case Key::Right:
                if (has_selection() && !shift) {
                    move_to(sel_end(), false);
                } else {
                    move_to(next_char(cursor_), shift);
                }
                break;
            case Key::Up:
            case Key::Down:
            case Key::PageUp:
            case Key::PageDown: {
                const int delta = ev.key == Key::Up     ? -1
                                  : ev.key == Key::Down ? 1
                                  : ev.key == Key::PageUp ? -page
                                                          : page;
                const int want = want_vcol_ >= 0 ? want_vcol_ : visual_column(cursor_);
                const int target = cursor_.line + delta;
                TextPos p;
                if (target < 0) {
                    p = buffer_.begin();
                } else if (target >= buffer_.line_count()) {
                    p = buffer_.end();
                } else {
                    p = {target, col_for_visual(target, float(want), false)};
                }
                move_to(p, shift);
                want_vcol_ = want;
                if (ev.key == Key::PageUp || ev.key == Key::PageDown) scroll_y_ += float(delta * lay.line_h);
                break;
            }
            case Key::Home: {  // smart home: toggle between indentation and column 0
                const int indent = leading_whitespace(buffer_.line(cursor_.line));
                move_to({cursor_.line, cursor_.col == indent ? 0 : indent}, shift);
                break;
            }
            case Key::End: move_to({cursor_.line, int(buffer_.line(cursor_.line).size())}, shift); break;
            case Key::Backspace:
                if (has_selection()) {
                    delete_selection();
                } else {
                    replace(prev_char(cursor_), cursor_, {}, EditKind::Other);
                }
                break;
            case Key::Delete:
                if (has_selection()) {
                    delete_selection();
                } else {
                    replace(cursor_, next_char(cursor_), {}, EditKind::Other);
                }
                break;
            case Key::Enter: {
                const std::string& line = buffer_.line(sel_start().line);
                const int indent = std::min(leading_whitespace(line), sel_start().col);
                insert_text("\n" + line.substr(0, size_t(indent)), EditKind::Other);
                break;
            }
            case Key::Tab:
                if (shift || (has_selection() && sel_start().line != sel_end().line)) {
                    indent_lines(shift);
                } else {
                    const int v = visual_column(sel_start());
                    insert_text(std::string(size_t(kTabWidth - v % kTabWidth), ' '), EditKind::Other);
                }
                break;
            case Key::Escape: anchor_ = cursor_; break;
            default: break;
        }
    }

    // Typed text (control characters are handled as keys above).
    std::string typed;
    for (char c : input.text) {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 0x20 && u != 0x7F) typed.push_back(c);
    }
    if (!typed.empty()) {
        last_activity_ = now_;
        insert_text(typed, EditKind::Typing);
    }
}

void TextEditor::draw_line_text(mu_Context* ctx, const Layout& lay, Font& font, int line, int y) {
    int state = line_states_[size_t(line)];
    const std::string& s = buffer_.line(line);
    highlight_line(language_, s, state, spans_);

    const int text_y = y + (lay.line_h - font.height()) / 2;
    const float x0 = float(lay.text_x) - scroll_x_;
    const float right = float(lay.text.x + lay.text.w);

    for (const Span& span : spans_) {
        // Draw between tabs so each piece starts at its tab-expanded column.
        int a = span.start;
        while (a < span.end) {
            if (s[size_t(a)] == '\t') {
                ++a;
                continue;
            }
            int b = a;
            while (b < span.end && s[size_t(b)] != '\t') ++b;
            const float x = x0 + float(visual_column({line, a})) * lay.col_w;
            if (x > right) return;
            mu_draw_text(ctx, &font, s.data() + a, b - a, mu_vec2(int(std::lround(x)), text_y),
                         token_color(span.token));
            a = b;
        }
    }
}

void TextEditor::draw(mu_Context* ctx, const Layout& lay, Font& font, bool focused) {
    mu_draw_rect(ctx, lay.bounds, kBackground);
    mu_draw_rect(ctx, lay.gutter, kGutterBackground);

    const int first = std::max(0, int(scroll_y_) / lay.line_h);
    const int last = std::min(buffer_.line_count() - 1, int(scroll_y_ + float(lay.text.h)) / lay.line_h);
    const auto line_y = [&](int l) { return lay.text.y + l * lay.line_h - int(scroll_y_); };
    const auto col_x = [&](int l, int col) {
        return int(std::lround(float(lay.text_x) - scroll_x_ + float(visual_column({l, col})) * lay.col_w));
    };

    update_highlight_states(last);

    mu_push_clip_rect(ctx, lay.text);

    if (!has_selection()) {
        mu_draw_rect(ctx, mu_rect(lay.text.x, line_y(cursor_.line), lay.text.w, lay.line_h), kCurrentLine);
    }

    if (!query_.empty()) {
        const std::vector<SearchMatch>& ms = matches();
        auto it = std::lower_bound(ms.begin(), ms.end(), TextPos{first, 0},
                                   [](const SearchMatch& m, TextPos p) { return m.start < p; });
        for (; it != ms.end() && it->start.line <= last; ++it) {
            const int x0 = col_x(it->start.line, it->start.col);
            const int x1 = col_x(it->end.line, it->end.col);
            mu_draw_rect(ctx, mu_rect(x0, line_y(it->start.line), x1 - x0, lay.line_h), kSearchMatch);
        }
    }

    if (has_selection()) {
        // With focus in the find bar, show the current match prominently.
        const mu_Color color = focused                          ? kSelection
                               : match_index_at_selection() >= 0 ? kCurrentMatch
                                                                 : kSelectionUnfocused;
        const TextPos s = sel_start(), e = sel_end();
        for (int l = std::max(first, s.line); l <= std::min(last, e.line); ++l) {
            const int x0 = col_x(l, l == s.line ? s.col : 0);
            // Selected line breaks show as a half-column sliver.
            const int x1 = l == e.line ? col_x(l, e.col)
                                       : col_x(l, int(buffer_.line(l).size())) + int(lay.col_w * 0.5f);
            mu_draw_rect(ctx, mu_rect(x0, line_y(l), x1 - x0, lay.line_h), color);
        }
    }

    for (int l = first; l <= last; ++l) draw_line_text(ctx, lay, font, l, line_y(l));

    const bool caret_on = std::fmod(now_ - last_activity_, kBlinkPeriod) < kBlinkPeriod * 0.5;
    if (focused && caret_on) {
        mu_draw_rect(ctx, mu_rect(col_x(cursor_.line, cursor_.col), line_y(cursor_.line), 2, lay.line_h), kCaret);
    }
    mu_pop_clip_rect(ctx);

    // Gutter line numbers.
    mu_push_clip_rect(ctx, lay.gutter);
    char number[16];
    for (int l = first; l <= last; ++l) {
        std::snprintf(number, sizeof number, "%d", l + 1);
        const int w = font.text_width(number, -1);
        const int x = lay.gutter.x + lay.gutter.w - kTextPadding - w;
        const int y = line_y(l) + (lay.line_h - font.height()) / 2;
        mu_draw_text(ctx, &font, number, -1, mu_vec2(x, y), l == cursor_.line ? kLineNumberActive : kLineNumber);
    }
    mu_pop_clip_rect(ctx);

    // Scrollbars.
    const float max_y = float(std::max(0, lay.content_h - lay.text.h));
    const float max_x = float(std::max(0, lay.content_w - lay.text.w));
    mu_draw_rect(ctx, lay.vbar, kScrollTrack);
    mu_draw_rect(ctx, lay.hbar, kScrollTrack);
    mu_draw_rect(ctx, mu_rect(lay.bounds.x, lay.hbar.y, lay.gutter.w, kScrollbarSize), kScrollTrack);
    mu_draw_rect(ctx, mu_rect(lay.vbar.x, lay.hbar.y, kScrollbarSize, kScrollbarSize), kScrollTrack);
    if (max_y > 0) {
        const float h = std::max(20.0f, float(lay.vbar.h) * float(lay.text.h) / float(lay.content_h));
        const float y = float(lay.vbar.y) + (float(lay.vbar.h) - h) * scroll_y_ / max_y;
        mu_draw_rect(ctx, mu_rect(lay.vbar.x + 2, int(y), kScrollbarSize - 4, int(h)),
                     drag_ == Drag::VScroll ? kScrollThumbActive : kScrollThumb);
    }
    if (max_x > 0) {
        const float w = std::max(20.0f, float(lay.hbar.w) * float(lay.text.w) / float(lay.content_w));
        const float x = float(lay.hbar.x) + (float(lay.hbar.w) - w) * scroll_x_ / max_x;
        mu_draw_rect(ctx, mu_rect(int(x), lay.hbar.y + 2, int(w), kScrollbarSize - 4),
                     drag_ == Drag::HScroll ? kScrollThumbActive : kScrollThumb);
    }
}

void TextEditor::update(mu_Context* ctx, const FrameInput& input, Font& font) {
    now_ = input.time;

    const mu_Rect bounds = mu_layout_next(ctx);
    const mu_Id id = mu_get_id(ctx, &buffer_, sizeof(&buffer_));
    mu_update_control(ctx, id, bounds, MU_OPT_HOLDFOCUS);
    // Keys this frame belonged to whatever had focus before (e.g. the Enter
    // that confirmed "go to line"), so don't act on them.
    const bool focus_just_requested = focus_requested_;
    if (focus_requested_) {
        mu_set_focus(ctx, id);
        focus_requested_ = false;
        last_activity_ = now_;
    }
    Layout lay = compute_layout(bounds, font);
    handle_mouse(ctx, lay);

    const bool focused = ctx->focus == id;
    focused_ = focused;
    if (focused && !focus_just_requested) handle_keys(input, lay);

    lay = compute_layout(bounds, font);  // content may have changed
    if (ensure_cursor_visible_) {
        scroll_to_cursor(lay);
        ensure_cursor_visible_ = false;
        center_cursor_ = false;
    }
    clamp_scroll(lay);

    draw(ctx, lay, font, focused);
}

}  // namespace vig
