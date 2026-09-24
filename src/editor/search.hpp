#pragma once

#include "editor/text_buffer.hpp"

#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace vig {

struct SearchQuery {
    std::string text;
    bool match_case = false;
    bool whole_word = false;
    bool regex = false;

    bool empty() const { return text.empty(); }
    friend bool operator==(const SearchQuery&, const SearchQuery&) = default;
};

struct SearchMatch {
    TextPos start;
    TextPos end;
};

// Compiled search. Matches never span lines and are never empty.
class Searcher {
public:
    static constexpr size_t kMaxMatches = 100000;

    // Returns false (with a message in `error`) for an invalid regex.
    bool compile(const SearchQuery& query, std::string& error);

    void find_all(const TextBuffer& buffer, std::vector<SearchMatch>& out) const;

    // Text that replaces `match`. For regex queries `$1`, `$&` etc. in
    // `replacement` expand to the match's groups.
    std::string replacement_for(const TextBuffer& buffer, const SearchMatch& match,
                                std::string_view replacement) const;

private:
    void find_in_line(const std::string& line, int line_index, std::vector<SearchMatch>& out) const;

    SearchQuery query_;
    std::string needle_;  // lower-cased when !match_case
    std::unique_ptr<std::regex> regex_;
};

}  // namespace vig
