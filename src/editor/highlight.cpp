#include "editor/highlight.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace vig {

namespace {

constexpr int kInBlockComment = 1;

bool is_ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.'; }
bool is_ident(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'; }
bool is_digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool is_space(char c) { return c == ' ' || c == '\t'; }

// Scans a number starting at i (0x.., 0b.., decimal, optional suffixes).
size_t scan_number(std::string_view s, size_t i) {
    while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == '.')) ++i;
    return i;
}

size_t scan_string(std::string_view s, size_t i) {
    const char quote = s[i++];
    while (i < s.size()) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            i += 2;
        } else if (s[i++] == quote) {
            break;
        }
    }
    return i;
}

void push(std::vector<Span>& out, size_t a, size_t b, Token t) {
    if (b <= a) return;
    if (!out.empty() && out.back().token == t && out.back().end == int(a)) {
        out.back().end = int(b);
    } else {
        out.push_back({int(a), int(b), t});
    }
}

// Mirrors the VIG assembler: ';' or '#' start a comment outside strings,
// `label:` defines a label, and the first word of a statement is the
// mnemonic (or a data directive such as asciiz).
void highlight_vigasm(std::string_view s, std::vector<Span>& out) {
    size_t i = 0;
    bool seen_mnemonic = false;
    while (i < s.size()) {
        const char c = s[i];
        if (is_space(c)) {
            size_t j = i;
            while (j < s.size() && is_space(s[j])) ++j;
            push(out, i, j, Token::Text);
            i = j;
        } else if (c == ';' || c == '#') {
            push(out, i, s.size(), Token::Comment);
            break;
        } else if (c == '"' || c == '\'') {
            size_t j = scan_string(s, i);
            push(out, i, j, Token::String);
            i = j;
        } else if (is_digit(c) || ((c == '-' || c == '+') && i + 1 < s.size() && is_digit(s[i + 1]))) {
            size_t j = scan_number(s, i + 1);
            push(out, i, j, Token::Number);
            i = j;
        } else if (is_ident_start(c)) {
            size_t j = i;
            while (j < s.size() && is_ident(s[j])) ++j;
            if (!seen_mnemonic && j < s.size() && s[j] == ':') {
                push(out, i, j + 1, Token::Label);
                i = j + 1;
                continue;
            }
            push(out, i, j, seen_mnemonic ? Token::Text : Token::Keyword);
            seen_mnemonic = true;
            i = j;
        } else {
            push(out, i, i + 1, Token::Punct);
            ++i;
        }
    }
}

constexpr std::array<std::string_view, 44> kCKeywords = {
    "auto",     "break",    "case",     "char",     "const",    "continue", "default",  "do",       "double",
    "else",     "enum",     "extern",   "float",    "for",      "goto",     "if",       "inline",   "int",
    "long",     "register", "restrict", "return",   "short",    "signed",   "sizeof",   "static",   "struct",
    "switch",   "typedef",  "union",    "unsigned", "void",     "volatile", "while",    "_Bool",    "_Static_assert",
    "bool",     "true",     "false",    "NULL",     "int8_t",   "int32_t",  "uint8_t",  "uint32_t",
};

bool is_c_keyword(std::string_view w) {
    return std::find(kCKeywords.begin(), kCKeywords.end(), w) != kCKeywords.end();
}

void highlight_c(std::string_view s, int& state, std::vector<Span>& out) {
    size_t i = 0;
    if (state == kInBlockComment) {
        size_t close = s.find("*/");
        if (close == std::string_view::npos) {
            push(out, 0, s.size(), Token::Comment);
            return;
        }
        push(out, 0, close + 2, Token::Comment);
        i = close + 2;
        state = 0;
    }

    size_t first = s.find_first_not_of(" \t", i);
    const bool preprocessor = first != std::string_view::npos && s[first] == '#';

    while (i < s.size()) {
        const char c = s[i];
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            push(out, i, s.size(), Token::Comment);
            break;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            size_t close = s.find("*/", i + 2);
            if (close == std::string_view::npos) {
                push(out, i, s.size(), Token::Comment);
                state = kInBlockComment;
                break;
            }
            push(out, i, close + 2, Token::Comment);
            i = close + 2;
            continue;
        }
        if (preprocessor) {
            push(out, i, i + 1, Token::Directive);
            ++i;
            continue;
        }
        if (c == '"' || c == '\'') {
            size_t j = scan_string(s, i);
            push(out, i, j, Token::String);
            i = j;
        } else if (is_digit(c)) {
            size_t j = scan_number(s, i);
            push(out, i, j, Token::Number);
            i = j;
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t j = i;
            while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
            push(out, i, j, is_c_keyword(s.substr(i, j - i)) ? Token::Keyword : Token::Text);
            i = j;
        } else if (is_space(c)) {
            push(out, i, i + 1, Token::Text);
            ++i;
        } else {
            push(out, i, i + 1, Token::Punct);
            ++i;
        }
    }
}

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

}  // namespace

Language language_for_path(std::string_view path) {
    if (ends_with(path, ".vigas") || ends_with(path, ".vigs") || ends_with(path, ".asm")) return Language::VigAsm;
    if (ends_with(path, ".c") || ends_with(path, ".h")) return Language::C;
    return Language::Plain;
}

const char* language_name(Language lang) {
    switch (lang) {
        case Language::VigAsm: return "VIG assembly";
        case Language::C: return "C";
        default: return "Plain text";
    }
}

void highlight_line(Language lang, std::string_view line, int& state, std::vector<Span>& out) {
    out.clear();
    switch (lang) {
        case Language::VigAsm: highlight_vigasm(line, out); break;
        case Language::C: highlight_c(line, state, out); break;
        default: push(out, 0, line.size(), Token::Text); break;
    }
}

}  // namespace vig
