#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace vig {

enum class Token : uint8_t {
    Text,
    Comment,
    Keyword,  // assembly mnemonics, C keywords
    Label,
    Number,
    String,
    Directive,  // C preprocessor lines
    Punct,
};

struct Span {
    int start;  // byte offsets into the line
    int end;
    Token token;
};

enum class Language {
    Plain,
    VigAsm,  // .vigas
    C,       // vigcc sources
};

Language language_for_path(std::string_view path);
const char* language_name(Language lang);

// Tokenises one line into `out` (cleared first). `state` carries constructs
// that span lines, such as C block comments: pass the value produced by the
// previous line, starting from 0.
void highlight_line(Language lang, std::string_view line, int& state, std::vector<Span>& out);

}  // namespace vig
