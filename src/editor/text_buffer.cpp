#include "editor/text_buffer.hpp"

#include <algorithm>

namespace vig {

TextBuffer::TextBuffer() : lines_(1) {}

void TextBuffer::set_text(std::string_view text) {
    lines_.clear();
    crlf_ = text.find("\r\n") != std::string_view::npos;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            lines_.push_back(std::move(current));
            current.clear();
        } else if (c != '\r') {
            current.push_back(c);
        }
    }
    lines_.push_back(std::move(current));
}

std::string TextBuffer::text() const {
    const char* eol = crlf_ ? "\r\n" : "\n";
    std::string out;
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (i) out += eol;
        out += lines_[i];
    }
    return out;
}

TextPos TextBuffer::end() const {
    const int last = line_count() - 1;
    return {last, static_cast<int>(lines_.back().size())};
}

TextPos TextBuffer::clamp(TextPos p) const {
    p.line = std::clamp(p.line, 0, line_count() - 1);
    p.col = std::clamp(p.col, 0, static_cast<int>(line(p.line).size()));
    return p;
}

std::string TextBuffer::get(TextPos a, TextPos b) const {
    a = clamp(a);
    b = clamp(b);
    if (b < a) std::swap(a, b);
    if (a.line == b.line) return line(a.line).substr(size_t(a.col), size_t(b.col - a.col));

    std::string out = line(a.line).substr(size_t(a.col));
    for (int l = a.line + 1; l < b.line; ++l) {
        out += '\n';
        out += line(l);
    }
    out += '\n';
    out += line(b.line).substr(0, size_t(b.col));
    return out;
}

TextPos TextBuffer::erase(TextPos a, TextPos b) {
    a = clamp(a);
    b = clamp(b);
    if (b < a) std::swap(a, b);
    if (a == b) return a;

    std::string& first = lines_[size_t(a.line)];
    const std::string tail = line(b.line).substr(size_t(b.col));
    first.resize(size_t(a.col));
    first += tail;
    lines_.erase(lines_.begin() + a.line + 1, lines_.begin() + b.line + 1);
    return a;
}

TextPos TextBuffer::insert(TextPos at, std::string_view text) {
    at = clamp(at);
    if (text.empty()) return at;

    std::string& target = lines_[size_t(at.line)];
    const std::string tail = target.substr(size_t(at.col));
    target.resize(size_t(at.col));

    std::vector<std::string> added;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            added.push_back(std::move(current));
            current.clear();
        } else if (c != '\r') {
            current.push_back(c);
        }
    }

    if (added.empty()) {
        target += current;
        const int col = static_cast<int>(target.size());
        target += tail;
        return {at.line, col};
    }

    target += added[0];
    const int end_col = static_cast<int>(current.size());
    current += tail;
    added.erase(added.begin());
    added.push_back(std::move(current));
    lines_.insert(lines_.begin() + at.line + 1, std::make_move_iterator(added.begin()),
                  std::make_move_iterator(added.end()));
    return {at.line + static_cast<int>(added.size()), end_col};
}

TextPos TextBuffer::advance(TextPos from, std::string_view text) {
    int newlines = 0;
    size_t last_nl = std::string_view::npos;
    int cr_after_last_nl = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            ++newlines;
            last_nl = i;
            cr_after_last_nl = 0;
        } else if (text[i] == '\r') {
            ++cr_after_last_nl;
        }
    }
    if (newlines == 0) return {from.line, from.col + static_cast<int>(text.size()) - cr_after_last_nl};
    return {from.line + newlines, static_cast<int>(text.size() - last_nl - 1) - cr_after_last_nl};
}

}  // namespace vig
