// Minimal self-contained tests for the editor core (no GPU or window needed).

#include "editor/highlight.hpp"
#include "editor/search.hpp"
#include "editor/text_buffer.hpp"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

using vig::TextBuffer;
using vig::TextPos;

void test_round_trip() {
    TextBuffer b;
    b.set_text("one\ntwo\n");
    CHECK(b.line_count() == 3);
    CHECK(b.text() == "one\ntwo\n");

    b.set_text("a\r\nb");
    CHECK(b.line_count() == 2);
    CHECK(b.line(0) == "a");
    CHECK(b.text() == "a\r\nb");  // line ending style preserved
}

void test_insert_erase() {
    TextBuffer b;
    b.set_text("hello world");
    TextPos end = b.insert({0, 5}, ",\nbig");
    CHECK(b.text() == "hello,\nbig world");
    CHECK(end == (TextPos{1, 3}));
    CHECK(b.get({0, 5}, end) == ",\nbig");

    TextPos at = b.erase({0, 5}, end);
    CHECK(at == (TextPos{0, 5}));
    CHECK(b.text() == "hello world");

    b.insert(b.end(), "\n\n");
    CHECK(b.line_count() == 3);
    CHECK(b.end() == (TextPos{2, 0}));
    b.erase(b.begin(), b.end());
    CHECK(b.line_count() == 1 && b.line(0).empty());
}

void test_advance() {
    CHECK(TextBuffer::advance({2, 3}, "abc") == (TextPos{2, 6}));
    CHECK(TextBuffer::advance({2, 3}, "ab\ncd") == (TextPos{3, 2}));
    CHECK(TextBuffer::advance({0, 0}, "\n") == (TextPos{1, 0}));
}

void test_clamp() {
    TextBuffer b;
    b.set_text("ab\nc");
    CHECK(b.clamp({5, 9}) == (TextPos{1, 1}));
    CHECK(b.clamp({-1, -1}) == (TextPos{0, 0}));
}

void test_highlight_vigasm() {
    std::vector<vig::Span> spans;
    int state = 0;
    std::string line = "loop: push 0x10 ; done";
    vig::highlight_line(vig::Language::VigAsm, line, state, spans);
    auto token_at = [&](int col) {
        for (const vig::Span& s : spans) {
            if (col >= s.start && col < s.end) return s.token;
        }
        return vig::Token::Text;
    };
    CHECK(token_at(0) == vig::Token::Label);
    CHECK(token_at(6) == vig::Token::Keyword);
    CHECK(token_at(11) == vig::Token::Number);
    CHECK(token_at(18) == vig::Token::Comment);

    line = "  asciiz \"a # b\" # trailing";
    vig::highlight_line(vig::Language::VigAsm, line, state, spans);
    CHECK(token_at(2) == vig::Token::Keyword);
    CHECK(token_at(12) == vig::Token::String);  // '#' inside the string
    CHECK(token_at(19) == vig::Token::Comment);
}

void test_highlight_c_block_comment() {
    std::vector<vig::Span> spans;
    int state = 0;
    vig::highlight_line(vig::Language::C, "int x; /* start", state, spans);
    CHECK(state != 0);
    vig::highlight_line(vig::Language::C, "still */ return x;", state, spans);
    CHECK(state == 0);
    CHECK(spans.front().token == vig::Token::Comment);
    CHECK(spans.back().token == vig::Token::Punct);
}

std::vector<vig::SearchMatch> search(const TextBuffer& b, vig::SearchQuery q) {
    vig::Searcher s;
    std::string error;
    CHECK(s.compile(q, error));
    std::vector<vig::SearchMatch> out;
    s.find_all(b, out);
    return out;
}

void test_search_plain() {
    TextBuffer b;
    b.set_text("push x\nPUSH pushx\n  push");
    vig::SearchQuery q{"push"};
    auto m = search(b, q);
    CHECK(m.size() == 4);  // case-insensitive by default
    CHECK(m[1].start == (TextPos{1, 0}) && m[1].end == (TextPos{1, 4}));

    q.match_case = true;
    CHECK(search(b, q).size() == 3);

    q.whole_word = true;
    m = search(b, q);
    CHECK(m.size() == 2);  // "pushx" excluded
    CHECK(m[1].start == (TextPos{2, 2}));
}

void test_search_regex() {
    TextBuffer b;
    b.set_text("load globals+0\nstore globals+12");
    vig::SearchQuery q{"globals\\+(\\d+)", false, false, true};
    vig::Searcher s;
    std::string error;
    CHECK(s.compile(q, error));
    std::vector<vig::SearchMatch> m;
    s.find_all(b, m);
    CHECK(m.size() == 2);
    CHECK(s.replacement_for(b, m[1], "data[$1]") == "data[12]");

    // Empty matches are skipped and don't hang.
    q.text = "x*";
    CHECK(search(b, q).empty());

    q.text = "(";
    CHECK(!s.compile(q, error));
    CHECK(!error.empty());
}

}  // namespace

int main() {
    test_round_trip();
    test_insert_erase();
    test_advance();
    test_clamp();
    test_highlight_vigasm();
    test_highlight_c_block_comment();
    test_search_plain();
    test_search_regex();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all editor tests passed\n");
    return 0;
}
