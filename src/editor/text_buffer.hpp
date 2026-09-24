#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace vig {

// A position in a TextBuffer. `col` is a byte offset into the line (always
// on a UTF-8 code point boundary).
struct TextPos {
    int line = 0;
    int col = 0;

    friend bool operator==(TextPos a, TextPos b) { return a.line == b.line && a.col == b.col; }
    friend bool operator!=(TextPos a, TextPos b) { return !(a == b); }
    friend bool operator<(TextPos a, TextPos b) { return a.line < b.line || (a.line == b.line && a.col < b.col); }
    friend bool operator<=(TextPos a, TextPos b) { return !(b < a); }
};

// Line-based text storage. Always holds at least one (possibly empty) line.
// Line endings are normalised to '\n' internally; the original style is
// remembered and restored by text().
class TextBuffer {
public:
    TextBuffer();

    void set_text(std::string_view text);
    std::string text() const;

    int line_count() const { return static_cast<int>(lines_.size()); }
    const std::string& line(int index) const { return lines_[static_cast<size_t>(index)]; }

    TextPos begin() const { return {0, 0}; }
    TextPos end() const;
    TextPos clamp(TextPos p) const;

    std::string get(TextPos a, TextPos b) const;
    // Both return the position right after the change.
    TextPos erase(TextPos a, TextPos b);
    TextPos insert(TextPos at, std::string_view text);

    // Position reached by inserting `text` at `from` (without inserting it).
    static TextPos advance(TextPos from, std::string_view text);

private:
    std::vector<std::string> lines_;
    bool crlf_ = false;
};

}  // namespace vig
