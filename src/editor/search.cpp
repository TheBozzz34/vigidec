#include "editor/search.hpp"

#include <cctype>

namespace vig {

namespace {

bool is_word_byte(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

std::string to_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool whole_word_at(const std::string& line, size_t start, size_t end) {
    const bool left_ok = start == 0 || !is_word_byte(static_cast<unsigned char>(line[start - 1]));
    const bool right_ok = end >= line.size() || !is_word_byte(static_cast<unsigned char>(line[end]));
    return left_ok && right_ok;
}

}  // namespace

bool Searcher::compile(const SearchQuery& query, std::string& error) {
    query_ = query;
    regex_.reset();
    error.clear();
    needle_ = query.match_case ? query.text : to_lower(query.text);
    if (!query.regex || query.text.empty()) return true;

    auto flags = std::regex::ECMAScript;
    if (!query.match_case) flags |= std::regex::icase;
    try {
        regex_ = std::make_unique<std::regex>(query.text, flags);
    } catch (const std::regex_error& e) {
        error = e.what();
        return false;
    }
    return true;
}

void Searcher::find_in_line(const std::string& line, int line_index, std::vector<SearchMatch>& out) const {
    if (regex_) {
        auto begin = line.cbegin();
        auto it = begin;
        std::smatch m;
        auto flags = std::regex_constants::match_default;
        while (it <= line.cend() && std::regex_search(it, line.cend(), m, *regex_, flags)) {
            const size_t start = size_t(m[0].first - begin);
            const size_t end = size_t(m[0].second - begin);
            if (end > start && (!query_.whole_word || whole_word_at(line, start, end))) {
                out.push_back({{line_index, int(start)}, {line_index, int(end)}});
                if (out.size() >= kMaxMatches) return;
            }
            if (m[0].second == line.cend()) break;
            // Step past empty matches so patterns like `x*` terminate.
            it = end > start ? m[0].second : m[0].second + 1;
            flags = std::regex_constants::match_prev_avail;
        }
        return;
    }

    const std::string haystack = query_.match_case ? std::string() : to_lower(line);
    const std::string& h = query_.match_case ? line : haystack;
    size_t pos = 0;
    while ((pos = h.find(needle_, pos)) != std::string::npos) {
        const size_t end = pos + needle_.size();
        if (!query_.whole_word || whole_word_at(line, pos, end)) {
            out.push_back({{line_index, int(pos)}, {line_index, int(end)}});
            if (out.size() >= kMaxMatches) return;
            pos = end;
        } else {
            ++pos;
        }
    }
}

void Searcher::find_all(const TextBuffer& buffer, std::vector<SearchMatch>& out) const {
    out.clear();
    if (query_.text.empty() || (query_.regex && !regex_)) return;
    for (int l = 0; l < buffer.line_count() && out.size() < kMaxMatches; ++l) find_in_line(buffer.line(l), l, out);
}

std::string Searcher::replacement_for(const TextBuffer& buffer, const SearchMatch& match,
                                      std::string_view replacement) const {
    if (!regex_) return std::string(replacement);

    // Re-run the regex at the match to recover its capture groups.
    const std::string& line = buffer.line(match.start.line);
    auto first = line.cbegin() + match.start.col;
    std::smatch m;
    const auto flags = match.start.col > 0 ? std::regex_constants::match_prev_avail
                                           : std::regex_constants::match_default;
    if (std::regex_search(first, line.cend(), m, *regex_, flags | std::regex_constants::match_continuous)) {
        return m.format(std::string(replacement));
    }
    return std::string(replacement);
}

}  // namespace vig
