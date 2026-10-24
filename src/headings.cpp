#include "htpd/headings.hpp"

#include <algorithm>
#include <ranges>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>

#include "htpd/text.hpp"

namespace htpd {

namespace {

constexpr bool is_digit(char c) { return c >= '0' && c <= '9'; }
constexpr bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
constexpr bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || is_upper(c); }
constexpr bool is_blank(char c) { return c == ' ' || c == '\t'; }

constexpr int kLetterBase = 1000;   // "A" -> 1000, "B" -> 1001 ...
constexpr std::size_t kMaxDepth = 6;
constexpr std::array<std::string_view, 5> kKeywords = {"chapter", "appendix", "annex", "section",
                                                       "part"};

// The next line of a title that wraps. A word broken at the end of the line
// ("condi-" / "tions") is joined again: a title is a name, and its words are
// looked for.
void append_title_line(std::string& title, std::string_view line) {
    const std::size_t n = title.size();
    if (n >= 2 && title[n - 1] == '-' && title[n - 2] >= 'a' && title[n - 2] <= 'z' && !line.empty() &&
        line[0] >= 'a' && line[0] <= 'z') {
        title.pop_back();
    } else if (!title.empty()) {
        title += ' ';
    }
    title += line;
}

std::string_view skip_blanks(std::string_view s) {
    while (!s.empty() && is_blank(s.front())) s.remove_prefix(1);
    return s;
}

// Reads a 1-3 digit number from the front of `s`; -1 if there is none.
int read_number(std::string_view& s) {
    std::size_t n = 0;
    int value = 0;
    while (n < s.size() && is_digit(s[n])) {
        value = value * 10 + (s[n] - '0');
        if (++n > 3) return -1;
    }
    if (n == 0) return -1;
    s.remove_prefix(n);
    return value;
}

int roman_value(std::string_view s) {
    if (s.empty() || s.size() > 7) return -1;
    int total = 0, previous = 0;
    for (std::size_t i = s.size(); i-- > 0;) {
        int v = 0;
        switch (s[i]) {
            case 'I': v = 1; break;
            case 'V': v = 5; break;
            case 'X': v = 10; break;
            case 'L': v = 50; break;
            case 'C': v = 100; break;
            default:  return -1;
        }
        total += v < previous ? -v : v;
        previous = std::max(previous, v);
    }
    return total > 0 ? total : -1;
}

bool starts_with_ignoring_case(std::string_view s, std::string_view lower_prefix) {
    if (s.size() < lower_prefix.size()) return false;
    for (std::size_t i = 0; i < lower_prefix.size(); ++i) {
        const char c = is_upper(s[i]) ? static_cast<char>(s[i] - 'A' + 'a') : s[i];
        if (c != lower_prefix[i]) return false;
    }
    return true;
}

// ".x.y" continuation of a dotted number.
bool read_dotted_tail(std::string_view& s, std::vector<int>& parts) {
    while (s.size() > 1 && s[0] == '.' && is_digit(s[1])) {
        s.remove_prefix(1);
        const int v = read_number(s);
        if (v < 0 || parts.size() >= kMaxDepth) return false;
        parts.push_back(v);
    }
    return true;
}

// After the number: an optional dot, then at least one blank, then the title.
bool read_title_after_number(std::string_view s, std::string_view& rest) {
    if (!s.empty() && s[0] == '.') s.remove_prefix(1);
    if (s.empty() || !is_blank(s[0])) return false;
    rest = skip_blanks(s);
    return !rest.empty();
}

std::optional<SectionNumber> parse_keyword_number(std::string_view s, std::string_view& rest) {
    for (const std::string_view keyword : kKeywords) {
        if (s.size() <= keyword.size() + 1 || !starts_with_ignoring_case(s, keyword) ||
            !is_blank(s[keyword.size()])) {
            continue;
        }
        std::string_view t = skip_blanks(s.substr(keyword.size()));
        std::size_t n = 0;
        while (n < t.size() && (is_alpha(t[n]) || is_digit(t[n]))) ++n;
        const std::string_view token = t.substr(0, n);
        if (token.empty() || token.size() > 4) return std::nullopt;

        SectionNumber number;
        number.keyword = true;
        number.text = std::string(token);
        const bool lettered = keyword == "appendix" || keyword == "annex";
        if (text::is_all_digits(token)) {
            std::string_view digits = token;
            const int v = read_number(digits);
            if (v < 0) return std::nullopt;
            number.parts = {v};
        } else if (lettered && token.size() == 1 && is_upper(token[0])) {
            number.parts = {kLetterBase + (token[0] - 'A')};
        } else if (const int roman = roman_value(token); roman > 0) {
            number.parts = {roman};
        } else if (token.size() == 1 && is_upper(token[0])) {
            number.parts = {kLetterBase + (token[0] - 'A')};
        } else {
            return std::nullopt;   // "Section describes ..." is ordinary prose
        }

        t = skip_blanks(t.substr(n));
        if (!t.empty() && (t[0] == '.' || t[0] == ':' || t[0] == '-')) t.remove_prefix(1);
        if (t.starts_with("\xE2\x80\x93") || t.starts_with("\xE2\x80\x94")) t.remove_prefix(3);   // dashes
        rest = skip_blanks(t);
        return number;
    }
    return std::nullopt;
}

bool has_dot_leaders(std::string_view s) {
    return s.contains("....") || s.contains(". . . .");
}

// A table-of-contents entry: a wide gap followed by only a page number.
bool looks_like_toc_entry(const Line& line) {
    if (line.gaps.empty()) return false;
    const std::string_view last_cell = std::string_view(line.text).substr(line.gaps.back() + 1);
    return text::is_all_digits(last_cell) || roman_value(last_cell) > 0;
}

// A title starts with a letter, with an identifier ("__arm_sme_state") or with
// a word that begins with digits ("64-bit mode"), not with a bare number.
bool title_start_ok(std::string_view s) {
    if (s.empty()) return false;
    if (s[0] == '_') return true;
    if (is_digit(s[0])) {
        const std::string_view first_word = s.substr(0, s.find(' '));
        return std::ranges::any_of(first_word, is_alpha);
    }
    std::size_t i = 0;
    const char32_t cp = text::decode_utf8(s, i);
    return cp < 0x80 ? is_alpha(static_cast<char>(cp)) : text::letter_count(s.substr(0, i)) == 1;
}

// "3 REAL THINGS PEOPLE BUILT": a title may start with a bare number when the
// line is set as a heading and words follow. A plain line such as "2. 5 V
// supply" stays text.
bool title_starts_with_count(std::string_view s, bool emphasised) {
    if (!emphasised) return false;
    const std::size_t space = s.find(' ');
    if (space == std::string_view::npos || !text::is_all_digits(s.substr(0, space))) return false;
    const std::string_view after = skip_blanks(s.substr(space));
    return title_start_ok(after) && text::word_count(after) >= 2;
}

bool plausible_numbered_title(std::string_view rest, const Line& line, bool emphasised) {
    if (line.text.size() > 160 || line.gaps.size() >= 2 || has_dot_leaders(line.text) ||
        looks_like_toc_entry(line)) {
        return false;   // too long, a table row, or a table-of-contents entry
    }
    if (!(title_start_ok(rest) || title_starts_with_count(rest, emphasised)) ||
        text::letter_count(rest) < 2) {
        return false;
    }
    const std::size_t words = text::word_count(rest);
    if (words > 20) return false;
    const char last = rest.back();
    if (last == ',' || last == ';') return false;
    return !(last == '.' && words > 10);
}

bool plausible_unnumbered_title(const Line& line) {
    const std::string_view s = line.text;
    if (s.size() > 120 || line.gaps.size() >= 2 || has_dot_leaders(s)) return false;
    if (text::letter_count(s) < 3 || text::word_count(s) > 14) return false;
    const char last = s.back();
    return last != '.' && last != ',' && last != ';' && last != ':';
}

struct Candidate {
    std::size_t line = 0;
    std::optional<SectionNumber> number;
    bool styled = false;      // set in a larger, bolder or different font than the body text
    bool font_only = false;   // ... and the font is the only difference
    bool bare_keyword = false;   // "Chapter 3" with nothing after the number
    int style_level = 0;      // the style sheet's level: set when only the style sheet makes this line a heading
};

int size_bucket(float size) {
    return static_cast<int>(std::lround(size * 2.0f));   // half points
}

// Font size and boldness of a line. Larger sizes give larger keys.
int size_key(const Line& line) {
    return size_bucket(line.size) * 2 + (line.bold ? 1 : 0);
}

// Two lines "look the same" when they share font, font size and boldness.
std::int64_t style_key(const Line& line) {
    return std::int64_t{size_key(line)} * 65536 + line.font;
}

// The font most of the text is set in.
std::uint16_t body_font(std::span<const Line> lines) {
    std::map<std::uint16_t, std::size_t> weight;   // font -> bytes of text
    for (const Line& line : lines) weight[line.font] += line.text.size();
    std::uint16_t best = 0;
    std::size_t best_weight = 0;
    for (const auto& [font, w] : weight) {
        if (w > best_weight) {
            best = font;
            best_weight = w;
        }
    }
    return best;
}

// A table of contents (or a numbered list, or a listing with line numbers) is
// a block of numbered lines in one style with nothing in between. Real
// headings are followed by text, so such blocks are not headings.
void drop_contents_blocks(std::vector<Candidate>& numbered, std::span<const Line> lines) {
    constexpr std::size_t kBlock = 5;
    std::vector<Candidate> kept;
    kept.reserve(numbered.size());
    for (std::size_t begin = 0; begin < numbered.size();) {
        std::size_t end = begin + 1;
        while (end < numbered.size() && numbered[end].line == numbered[end - 1].line + 1 &&
               style_key(lines[numbered[end].line]) == style_key(lines[numbered[begin].line])) {
            ++end;
        }
        if (end - begin < kBlock) {
            std::move(numbered.begin() + static_cast<std::ptrdiff_t>(begin),
                      numbered.begin() + static_cast<std::ptrdiff_t>(end), std::back_inserter(kept));
        }
        begin = end;
    }
    numbered = std::move(kept);
}

// Removes numbered candidates that break an otherwise consistent sequence,
// e.g. a bold table cell "3.3 V supply" between sections 2.1 and 2.2.
std::vector<Candidate> drop_sequence_outliers(const std::vector<Candidate>& in) {
    std::vector<Candidate> out;
    out.reserve(in.size());   // keeps `last` valid across push_back
    const std::vector<int>* last = nullptr;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const Candidate& c = in[i];
        bool keep = c.number->keyword || last == nullptr || is_successor(*last, c.number->parts);
        if (!keep) {
            bool chain_continues_later = false;
            for (std::size_t j = i + 1; j < std::min(in.size(), i + 4); ++j) {
                if (!in[j].number->keyword && is_successor(*last, in[j].number->parts)) {
                    chain_continues_later = true;
                    break;
                }
            }
            keep = !chain_continues_later;   // otherwise the sequence restarts here
        }
        if (keep) {
            out.push_back(c);
            last = &out.back().number->parts;
        }
    }
    return out;
}

// For documents without any font styling: follow the numbering strictly.
// Every possible starting point is tried and the longest chain wins.
std::vector<Candidate> strict_sequence(const std::vector<Candidate>& in) {
    constexpr std::size_t kMaxStarts = 64;
    std::vector<Candidate> best;
    std::size_t starts = 0;
    for (std::size_t s = 0; s < in.size() && starts < kMaxStarts; ++s) {
        const std::vector<int>& first = in[s].number->parts;
        if (first.back() > 1 || first.size() > 2) continue;   // chains start at 1, 1.1, A.1 ...
        ++starts;
        std::vector<Candidate> chain{in[s]};
        for (std::size_t k = s + 1; k < in.size(); ++k) {
            if (is_successor(chain.back().number->parts, in[k].number->parts)) chain.push_back(in[k]);
        }
        if (chain.size() > best.size()) best = std::move(chain);
    }
    return best.size() >= 3 ? best : std::vector<Candidate>{};
}

struct TextColumn {
    float left = 0;
    float right = 0;
    bool known = false;
};

// Left and right edge that ordinary full lines of text reach.
TextColumn text_column(std::span<const Line> lines) {
    std::vector<float> lefts, rights;
    for (const Line& line : lines) {
        if (line.text.size() < 40) continue;
        lefts.push_back(line.x0);
        rights.push_back(line.x1);
    }
    if (rights.size() < 20) return {};
    const auto percentile = [](std::vector<float>& values, std::size_t percent) {
        const auto it = values.begin() + static_cast<std::ptrdiff_t>(values.size() * percent / 100);
        std::ranges::nth_element(values, it);
        return *it;
    };
    TextColumn column;
    column.left = percentile(lefts, 10);
    column.right = percentile(rights, 90);
    column.known = column.right > column.left;
    return column;
}

// Without font styling the only visual sign of a heading is a short line.
bool plain_heading_shape(const Line& line, const TextColumn& column) {
    if (line.text.ends_with('.')) return false;
    if (!column.known) return true;
    return line.x1 < column.left + 0.85f * (column.right - column.left);
}

HeadingResult detect_from_text(std::span<const Line> lines, const StyleSheet* styles = nullptr,
                               Producer producer = Producer::Unknown) {
    HeadingResult result;
    result.body_size = body_font_size(lines);
    const float body = result.body_size;

    // When most of the text is bold, bold says nothing about headings.
    std::size_t bold_bytes = 0, all_bytes = 0;
    for (const Line& line : lines) {
        all_bytes += line.text.size();
        if (line.bold) bold_bytes += line.text.size();
    }
    const bool bold_is_signal = bold_bytes * 10 < all_bytes * 4;
    // Some documents set headings in another font at the SAME size as the text.
    const std::uint16_t text_font = body_font(lines);

    // Text on the same line to the right of line i: another cell of its row.
    const auto cell_beside = [&](std::size_t i) {
        const Line& line = lines[i];
        const float tolerance = 0.3f * std::max(line.size, 1.0f);
        const std::size_t from = i > 40 ? i - 40 : 0, to = std::min(lines.size(), i + 41);
        for (std::size_t k = from; k < to; ++k) {
            const Line& other = lines[k];
            if (k == i || other.page != line.page || other.text.empty() || other.figure > 0) continue;
            if (std::fabs(other.baseline - line.baseline) < tolerance && other.x0 > line.x1) return true;
        }
        return false;
    };

    // 1. Collect candidates.
    std::vector<Candidate> numbered, unnumbered;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const Line& line = lines[i];
        if (line.text.empty() || line.ocr == Line::Ocr::Picture) continue;   // text of a figure
        const bool larger = line.size >= body * 1.08f;
        const bool other_font = line.font != 0 && text_font != 0 && line.font != text_font;
        const bool emphasised = larger || (line.bold && bold_is_signal);
        const bool styled = emphasised || other_font;

        std::string_view rest;
        if (auto number = parse_section_number(line.text, rest)) {
            // A line that starts like a numbered heading but fails the checks
            // (a contents entry, a table row, a sentence) is no heading at
            // all, however large it is set. A row of a table is one too:
            // its columns line up with the rows around it, or another cell
            // stands beside it on its line ("24 notification_payload_f" in
            // the column "Tag #") - unless it is set larger than the text.
            const bool title_ok = !line.table && (larger || !cell_beside(i)) &&
                                  (rest.empty() ? number->keyword : plausible_numbered_title(rest, line, emphasised));
            if (title_ok) {
                const bool bare = rest.empty();
                numbered.push_back({i, std::move(number), styled, styled && !emphasised, bare});
            }
            continue;
        }
        // On a scanned page the size comes from the height of the letters
        // (ocr.cpp): a heading there must stand out more, and be short.
        const bool large = line.ocr == Line::Ocr::None
                               ? line.size >= body * 1.15f
                               : text::word_count(line.text) <= 8 &&
                                     (line.size >= body * 1.12f || (line.bold && line.size >= body * 0.95f));
        // A row of a table (its columns line up with the rows around it) is no
        // heading; a title read by OCR starts with a capital or a digit
        // (a lower-case start is the rest of a wrapped table cell).
        const bool capital_start = !line.text.empty() && ((line.text[0] >= 'A' && line.text[0] <= 'Z') ||
                                                          (line.text[0] >= '0' && line.text[0] <= '9'));
        if (large && !line.table && (line.ocr == Line::Ocr::None || capital_start) && plausible_unnumbered_title(line)) {
            unnumbered.push_back({i, std::nullopt, true, false});
        } else if (styles != nullptr && !large && !line.table && line.ocr == Line::Ocr::None &&
                   plausible_unnumbered_title(line) && !starts_like_caption(line.text)) {
            // Not large, but set in a style that is a heading style of this document: rare, short,
            // standing alone before body text (a colour, a weight or a font of its own).
            if (const int level = styles->heading_level(line); level > 0) {
                unnumbered.push_back({i, std::nullopt, true, false, false, level});
            }
        }
    }
    drop_contents_blocks(numbered, lines);

    // A note, a quotation or a table cell may be set in another font as a
    // whole, numbered list included. A heading stands alone: the line before
    // it looks different, unless that line is a heading too.
    {
        std::vector<bool> is_candidate(lines.size(), false);
        for (const Candidate& c : numbered) is_candidate[c.line] = true;
        std::erase_if(numbered, [&](const Candidate& c) {
            if (!c.font_only || c.line == 0) return false;
            const Line& before = lines[c.line - 1];
            return before.page == lines[c.line].page && !is_candidate[c.line - 1] &&
                   style_key(before) == style_key(lines[c.line]);
        });
    }

    // 2. Numbered headings: by style when the document has styling, otherwise
    //    by the numbering sequence alone.
    std::vector<Candidate> styled_numbered;
    std::ranges::copy_if(numbered, std::back_inserter(styled_numbered), &Candidate::styled);
    std::vector<Candidate> accepted;
    if (styled_numbered.size() >= 3) {
        accepted = drop_sequence_outliers(styled_numbered);
    } else {
        const TextColumn column = text_column(lines);
        std::vector<Candidate> plain;
        std::ranges::copy_if(numbered, std::back_inserter(plain), [&](const Candidate& c) {
            return c.number->keyword || plain_heading_shape(lines[c.line], column);
        });
        accepted = strict_sequence(plain);
    }

    // 3. Font size -> heading level, learned from the numbered headings.
    std::map<int, int> level_of_size;
    for (const Candidate& c : accepted) {
        const int level = c.number->keyword ? 1 : static_cast<int>(c.number->parts.size());
        const auto [it, inserted] = level_of_size.try_emplace(size_key(lines[c.line]), level);
        if (!inserted) it->second = std::min(it->second, level);
    }

    // 4. Unnumbered headings.
    std::vector<Candidate> accepted_unnumbered;
    if (!accepted.empty()) {
        // Only lines in the size of a known heading ("Revision history"),
        // not every large line (cover page titles).
        // A style smaller than every numbered heading, but still larger than
        // the text ("Performance Characteristics Table" in 12 pt bold under
        // "5.2 ..." in 15 pt), used for three titles or more: the level below
        // the deepest numbered one.
        const int smallest = level_of_size.begin()->first;
        int deepest = 1;
        for (const auto& [key, level] : level_of_size) deepest = std::max(deepest, level);
        std::map<int, std::size_t> smaller;
        for (const Candidate& c : unnumbered) {
            if (c.style_level > 0) continue;
            const int key = size_key(lines[c.line]);
            if (key < smallest) ++smaller[key];
        }
        for (const auto& [key, count] : smaller) {
            if (count >= 3) level_of_size[key] = std::min(deepest + 1, static_cast<int>(kMaxDepth));
        }
        for (const Candidate& c : unnumbered) {
            if (c.style_level > 0 || level_of_size.contains(size_key(lines[c.line]))) accepted_unnumbered.push_back(c);
        }
    } else {
        // No numbering to learn from: the larger the font, the higher the level.
        accepted_unnumbered = unnumbered;
        std::vector<int> sizes;
        for (const Candidate& c : unnumbered) {
            if (c.style_level == 0) sizes.push_back(size_key(lines[c.line]));
        }
        std::ranges::sort(sizes, std::greater{});
        sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
        for (std::size_t rank = 0; rank < sizes.size(); ++rank) {
            level_of_size[sizes[rank]] = static_cast<int>(std::min<std::size_t>(rank + 1, kMaxDepth));
        }
    }

    // Lines that only the style sheet makes headings: one level below the nearest larger heading
    // style; two such styles of the same size keep the order the style sheet gives them.
    std::map<int, std::vector<int>> style_levels_at;   // size key -> style sheet levels seen there
    for (const Candidate& c : accepted_unnumbered) {
        if (c.style_level == 0) continue;
        auto& seen = style_levels_at[size_key(lines[c.line])];
        if (std::ranges::find(seen, c.style_level) == seen.end()) seen.push_back(c.style_level);
    }
    std::map<int, int> style_base;       // size key -> level of its first style
    {
        std::map<int, int> top;          // size key -> deepest level it takes
        for (auto it = style_levels_at.rbegin(); it != style_levels_at.rend(); ++it) {
            const int key = it->first;
            int base = 1;
            for (const auto& [k, level] : level_of_size) {
                if (k > key) base = std::max(base, level + 1);
            }
            for (const auto& [k, deepest] : top) {
                if (k > key) base = std::max(base, deepest + 1);
            }
            std::ranges::sort(it->second);
            style_base[key] = base;
            top[key] = base + static_cast<int>(it->second.size()) - 1;
        }
    }
    const auto level_of_style_line = [&](const Candidate& c) {
        const int key = size_key(lines[c.line]);
        const std::vector<int>& seen = style_levels_at[key];
        const auto rank = std::ranges::find(seen, c.style_level) - seen.begin();
        return std::min(style_base[key] + static_cast<int>(rank), static_cast<int>(kMaxDepth));
    };

    result.source = !accepted.empty()              ? HeadingSource::Numbering
                    : !accepted_unnumbered.empty() ? HeadingSource::FontSize
                                                   : HeadingSource::None;

    // 5. Build the result. `kind`: 0 = no heading, 1 = numbered, 2 = unnumbered.
    std::vector<char> kind(lines.size(), 0);
    for (const Candidate& c : accepted) kind[c.line] = 1;
    for (const Candidate& c : accepted_unnumbered) kind[c.line] = 2;
    accepted.insert(accepted.end(), accepted_unnumbered.begin(), accepted_unnumbered.end());
    std::ranges::sort(accepted, {}, &Candidate::line);

    // True when line `index` continues the title that ends on the line before
    // it: same style, directly below, and not a numbered heading itself.
    const auto continues_title = [&](std::size_t index) {
        const Line& prev = lines[index - 1];
        const Line& next = lines[index];
        std::string_view unused;
        return next.page == prev.page && style_key(next) == style_key(prev) &&
               next.baseline > prev.baseline && next.baseline - prev.baseline < 1.6f * prev.size &&
               kind[index] != 1 && next.gaps.size() < 2 && !parse_section_number(next.text, unused);
    };

    constexpr std::size_t kMaxTitleLines = 3;
    constexpr std::size_t kMaxTitleBytes = 200;
    std::vector<bool> consumed(lines.size(), false);   // part of an earlier title or text block
    for (const Candidate& c : accepted) {
        if (consumed[c.line]) continue;

        // A long title wraps onto following lines set in the same style. Only
        // styled headings can wrap: without styling the next line cannot be
        // told apart from body text.
        std::size_t count = 1;
        std::size_t bytes = lines[c.line].text.size();
        bool joined = false;
        // LaTeX sets a chapter as two lines: "Chapter 3" above its title, in a larger size.
        if (producer == Producer::LaTeX && c.bare_keyword && c.line + 1 < lines.size()) {
            const Line& chapter = lines[c.line];
            const Line& title = lines[c.line + 1];
            std::string_view unused;
            if (title.page == chapter.page && title.figure == 0 && !title.table && title.size >= chapter.size &&
                title.baseline > chapter.baseline && title.baseline - chapter.baseline < 4.0f * chapter.size &&
                title.gaps.size() < 2 && !parse_section_number(title.text, unused) && plausible_unnumbered_title(title)) {
                bytes += title.text.size() + 1;
                count = 2;
                joined = true;
            }
        }
        while (!joined && c.styled && c.line + count < lines.size() && continues_title(c.line + count)) {
            bytes += lines[c.line + count].text.size() + 1;
            ++count;
        }
        if (count > kMaxTitleLines || bytes > kMaxTitleBytes) {
            // Not a heading but a block of text in a large or different font
            // (a quotation, a note). None of its lines is a heading.
            for (std::size_t k = 0; k < count; ++k) consumed[c.line + k] = true;
            continue;
        }

        const Line& first = lines[c.line];
        Heading h;
        h.page = first.page;
        h.line_index = c.line;
        h.line_count = count;
        for (std::size_t k = 0; k < count; ++k) {
            append_title_line(h.title, text::collapse_spaces(lines[c.line + k].text));
            consumed[c.line + k] = true;
        }
        if (c.number) {
            h.number = c.number->text;
            h.level = c.number->keyword ? 1 : static_cast<int>(c.number->parts.size());
            h.source = HeadingSource::Numbering;
        } else {
            h.level = c.style_level > 0 ? level_of_style_line(c) : level_of_size.at(size_key(first));
            h.source = HeadingSource::FontSize;
        }
        result.headings.push_back(std::move(h));
    }
    if (result.headings.empty()) result.source = HeadingSource::None;
    return result;
}

// ---- the PDF's own tags -----------------------------------------------------

// The headings of a tagged PDF (H, H1-H6): the lines the tags step marked,
// the lines of one element together. The level is the one the tags give.
HeadingResult detect_from_tags(std::span<const Line> lines) {
    HeadingResult result;
    result.source = HeadingSource::Tags;
    result.body_size = body_font_size(lines);
    for (std::size_t i = 0; i < lines.size();) {
        const Line& first = lines[i];
        if (first.tag_heading == 0 || first.tag <= 0 || first.text.empty() || !first.cells.empty()) {
            ++i;
            continue;
        }
        std::size_t end = i + 1;
        while (end < lines.size() && lines[end].tag == first.tag && lines[end].page == first.page &&
               lines[end].tag_heading == first.tag_heading && !lines[end].text.empty()) {
            ++end;
        }
        Heading h;
        h.level = first.tag_heading;
        h.page = first.page;
        h.line_index = i;
        h.line_count = end - i;
        h.source = HeadingSource::Tags;
        for (std::size_t k = i; k < end; ++k) append_title_line(h.title, text::collapse_spaces(lines[k].text));
        std::string_view rest;
        if (const auto number = parse_section_number(h.title, rest)) h.number = number->text;
        result.headings.push_back(std::move(h));
        i = end;
    }
    return result;
}

// ---- bookmarks -----------------------------------------------------------

// Skeleton without its leading section number: "521clocktree" -> "clocktree",
// "appendixaregistermap" -> "registermap", "a1statusregister" -> "statusregister".
std::string_view strip_numbering(std::string_view sk, bool keywords = true) {
    std::string_view s = sk;
    for (const std::string_view keyword : kKeywords) {
        if (!keywords) break;
        if (s.starts_with(keyword)) {
            s.remove_prefix(keyword.size());
            if (!s.empty() && is_alpha(s[0]) && (s.size() == 1 || !is_digit(s[1]))) {
                s.remove_prefix(1);   // the letter of "Appendix A"
            }
            break;
        }
    }
    if (keywords && s.size() > 1 && is_alpha(s[0]) && is_digit(s[1])) s.remove_prefix(1);   // "A.1"
    while (!s.empty() && is_digit(s[0])) s.remove_prefix(1);
    return s;
}

// Both ways of reading a skeleton without its number: with a keyword or a
// letter taken off ("appendixaregistermap" -> "registermap", "a1status" ->
// "status") and with only the digits taken off, for a title that only starts
// like one ("partidentification" is "Part identification", not "Part I
// dentification"; "i2cswitching" is "I2C switching").
bool titles_match(std::string_view line_sk, std::string_view title_sk) {
    if (line_sk == title_sk) return true;
    for (const bool lk : {true, false}) {
        for (const bool tk : {true, false}) {
            const std::string_view line_bare = strip_numbering(line_sk, lk);
            const std::string_view title_bare = strip_numbering(title_sk, tk);
            if (line_bare.size() >= 3 && title_bare.size() >= 3 && line_bare == title_bare) return true;
        }
    }
    return false;
}

bool could_be_start_of(std::string_view line_sk, std::string_view title_sk) {
    if (line_sk.empty()) return false;
    if (title_sk.starts_with(line_sk)) return true;
    for (const bool lk : {true, false}) {
        for (const bool tk : {true, false}) {
            const std::string_view line_bare = strip_numbering(line_sk, lk);
            if (line_bare.size() >= 3 && strip_numbering(title_sk, tk).starts_with(line_bare)) return true;
        }
    }
    return false;
}

struct Match {
    std::size_t line = 0;
    std::size_t count = 0;
};

// Finds the heading text of a bookmark among lines [begin, end).
// `skeletons[i]` is text::skeleton of line i.
//
// The same words can occur several times on a page: in a small table of
// contents at the start of a chapter, as a term in the text, and as the
// heading itself. The line in the largest font wins, because that is the
// heading (the running header "Ratings" in 9 points loses to "1 Ratings" in
// 17); among lines of about the same size (less than 1.3 times apart) one
// that equals the whole bookmark title beats one that only matches without
// its section number, then the larger and the bold one.
std::optional<Match> find_title(std::span<const Line> lines, std::span<const std::string> skeletons,
                                std::size_t begin, std::size_t end, std::string_view title_sk) {
    if (title_sk.size() < 2) return std::nullopt;
    struct Found {
        Match match;
        bool exact;
    };
    std::vector<Found> found;
    for (std::size_t i = begin; i < end; ++i) {
        std::string joined;
        for (std::size_t k = 0; k < 3 && i + k < end; ++k) {
            joined += skeletons[i + k];
            const bool exact = joined == title_sk;
            if (exact || titles_match(joined, title_sk)) {
                found.push_back({Match{i, k + 1}, exact});
                break;
            }
            if (!could_be_start_of(joined, title_sk)) break;
        }
    }
    if (found.empty()) return std::nullopt;
    float largest = 0;
    for (const Found& f : found) largest = std::max(largest, lines[f.match.line].size);
    std::optional<Match> best;
    std::int64_t best_rank = -1;
    for (const Found& f : found) {
        const Line& line = lines[f.match.line];
        if (line.size * 1.3f < largest) continue;
        const std::int64_t rank = (f.exact ? std::int64_t{1} << 40 : 0) + std::int64_t{size_bucket(line.size)} * 2 +
                                  (line.bold ? 1 : 0);
        if (rank > best_rank) {
            best = f.match;
            best_rank = rank;
        }
    }
    return best;
}

HeadingResult detect_from_bookmarks(std::span<const Line> lines,
                                    std::span<const OutlineEntry> outline,
                                    std::span<const Affine> page_transforms) {
    HeadingResult result;
    result.source = HeadingSource::Bookmark;
    result.body_size = body_font_size(lines);

    const std::size_t page_count = page_transforms.size();
    // first_line[p] = index of the first line on page p or later.
    std::vector<std::size_t> first_line(page_count + 1, lines.size());
    for (std::size_t i = lines.size(); i-- > 0;) {
        const auto page = static_cast<std::size_t>(std::max(lines[i].page, 0));
        if (page < page_count) first_line[page] = i;
    }
    for (std::size_t p = page_count; p-- > 0;) first_line[p] = std::min(first_line[p], first_line[p + 1]);

    std::vector<std::string> skeletons(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) skeletons[i] = text::skeleton(lines[i].text);

    std::vector<bool> taken(lines.size(), false);
    std::size_t cursor = 0;   // end of the previously located heading
    for (const OutlineEntry& entry : outline) {
        if (entry.page < 0 || static_cast<std::size_t>(entry.page) >= page_count || entry.title.empty()) {
            continue;
        }
        ++result.bookmarks_total;
        const auto page = static_cast<std::size_t>(entry.page);
        const std::size_t page_begin = first_line[page];
        const std::size_t page_end = first_line[page + 1];
        const std::size_t ordered_begin = std::clamp(cursor, page_begin, page_end);
        const std::string title_sk = text::skeleton(entry.title);

        Heading h;
        h.level = std::max(entry.level, 1);
        h.page = entry.page;
        h.source = HeadingSource::Bookmark;

        auto match = find_title(lines, skeletons, ordered_begin, page_end, title_sk);
        if (!match && ordered_begin > page_begin) {
            match = find_title(lines, skeletons, page_begin, page_end, title_sk);
        }
        // A bookmark that points at the end of a page whose heading stands at
        // the top of the next one.
        if (!match && page + 1 < page_count) {
            const std::size_t next_end = std::min(first_line[page + 1] + 3, first_line[page + 2]);
            match = find_title(lines, skeletons, std::max(first_line[page + 1], ordered_begin), next_end, title_sk);
        }
        if (match && taken[match->line]) match.reset();   // two bookmarks, one line

        if (match) {
            ++result.bookmarks_located;
            h.page = lines[match->line].page;
            h.line_index = match->line;
            h.line_count = match->count;
            for (std::size_t k = 0; k < match->count; ++k) {
                append_title_line(h.title, text::collapse_spaces(lines[match->line + k].text));
                taken[match->line + k] = true;
            }
            cursor = match->line + match->count;
        } else {
            // The title is not in the page text (e.g. it is part of a figure).
            // Place the section start by the bookmark's position on the page.
            h.title = entry.title;
            h.line_index = ordered_begin;
            if (entry.user_y) {
                float x = 0, y = 0;
                page_transforms[page].apply(entry.user_x.value_or(0.0f), *entry.user_y, x, y);
                std::size_t i = page_begin;
                while (i < page_end && lines[i].y1 < y - 2.0f) ++i;
                h.line_index = i;
            }
        }
        std::string_view rest;
        if (const auto number = parse_section_number(h.title, rest)) h.number = number->text;
        result.headings.push_back(std::move(h));
    }

    std::ranges::stable_sort(result.headings, {}, &Heading::line_index);
    return result;
}

// Adds the numbered headings found in the text below the bookmarks they
// belong to. A text heading is only taken when its number extends the number
// of the bookmark it sits under ("9.1.2" and "9.1.2.1" under "9.1"), which
// keeps numbered lists and table rows out.
void refine_with_text_headings(HeadingResult& result, const HeadingResult& from_text,
                               std::size_t line_count) {
    std::vector<bool> taken(line_count, false);
    for (const Heading& h : result.headings) {
        for (std::size_t k = 0; k < h.line_count && h.line_index + k < line_count; ++k) {
            taken[h.line_index + k] = true;
        }
    }

    std::vector<Heading> added;
    std::size_t next_bookmark = 0;
    const Heading* enclosing = nullptr;        // the bookmark heading this text heading sits under
    std::optional<SectionNumber> enclosing_number;
    for (const Heading& t : from_text.headings) {   // both lists are sorted by line_index
        while (next_bookmark < result.headings.size() &&
               result.headings[next_bookmark].line_index <= t.line_index) {
            enclosing = &result.headings[next_bookmark++];
            std::string_view rest;
            enclosing_number = parse_section_number(enclosing->title, rest);
        }
        if (enclosing == nullptr || !enclosing_number || t.source != HeadingSource::Numbering) continue;

        bool overlaps = false;
        for (std::size_t k = 0; k < t.line_count && t.line_index + k < line_count; ++k) {
            overlaps = overlaps || taken[t.line_index + k];
        }
        if (overlaps) continue;   // this line already is a bookmark heading

        std::string_view rest;
        const auto number = parse_section_number(t.title, rest);
        const std::vector<int>& outer = enclosing_number->parts;
        if (!number || number->parts.size() <= outer.size() ||
            !std::ranges::equal(outer, std::span(number->parts).first(outer.size()))) {
            continue;
        }
        Heading h = t;
        h.level = enclosing->level + static_cast<int>(number->parts.size() - outer.size());
        added.push_back(std::move(h));
    }

    result.text_added = added.size();
    result.headings.insert(result.headings.end(), std::make_move_iterator(added.begin()),
                           std::make_move_iterator(added.end()));
    // Stable: where a bookmark and a text heading start at the same line, the bookmark comes first.
    std::ranges::stable_sort(result.headings, {}, &Heading::line_index);
}

// Adds the headings of the PDF's structure tags that lie deeper than the bookmarks (bookmarks often
// stop at the second level). A tag heading is taken only when
//   - it lies below a bookmark heading that is a tag heading itself, with a smaller tag level, and
//   - the style sheet agrees: its line is set in a heading style, and in a deeper one than the
//     line of that bookmark.
// Its level is the level of the bookmark plus the difference of the tag levels.
void refine_with_tag_headings(HeadingResult& result, const HeadingResult& from_tags, std::span<const Line> lines,
                              const StyleSheet* styles) {
    if (styles == nullptr || from_tags.headings.empty()) return;
    std::vector<bool> taken(lines.size(), false);
    for (const Heading& h : result.headings) {
        for (std::size_t k = 0; k < std::max<std::size_t>(h.line_count, 1) && h.line_index + k < lines.size(); ++k) {
            taken[h.line_index + k] = true;
        }
    }
    std::vector<Heading> added;
    std::size_t next_bookmark = 0;
    const Heading* enclosing = nullptr;
    for (const Heading& t : from_tags.headings) {
        while (next_bookmark < result.headings.size() && result.headings[next_bookmark].line_index <= t.line_index) {
            enclosing = &result.headings[next_bookmark++];
        }
        if (enclosing == nullptr || enclosing->line_count == 0 || enclosing->line_index >= lines.size()) continue;
        if (std::ranges::any_of(std::views::iota(std::size_t{0}, std::max<std::size_t>(t.line_count, 1)),
                                [&](std::size_t k) { return t.line_index + k < lines.size() && taken[t.line_index + k]; })) {
            continue;
        }
        const Line& above = lines[enclosing->line_index];
        if (above.tag_heading == 0 || t.level <= above.tag_heading) continue;
        const int sheet = styles->heading_level(lines[t.line_index]);
        const int sheet_above = styles->heading_level(above);
        if (sheet == 0 || (sheet_above > 0 && sheet <= sheet_above)) continue;
        Heading h = t;
        h.level = std::min(enclosing->level + (t.level - above.tag_heading), static_cast<int>(kMaxDepth));
        added.push_back(std::move(h));
    }
    result.tags_added = added.size();
    result.headings.insert(result.headings.end(), std::make_move_iterator(added.begin()),
                           std::make_move_iterator(added.end()));
    std::ranges::stable_sort(result.headings, {}, &Heading::line_index);
}

// ---- a printed table of contents --------------------------------------
//
// Many PDFs have no bookmarks but print a table of contents, and their
// headings in the text may have no number at all (a web page printed to PDF:
// "4.1 iMX93 Processor" in the contents, "iMX93 Processor" above the text).
// The entries of the contents are read - also several on one line - and each
// title is looked for later in the text, in order.

struct NumberToken {
    std::size_t line = 0;
    std::size_t word = 0;               // index of the word in the line
    std::vector<int> parts;
    std::string text;                   // "3.2"
    std::string glued;                  // "X1-CCS1" of "8.1X1-CCS1", else ""
};

// "3", "3.2", "3.2.", "A.1", and "8.1X1-CCS1" (a number with the title glued to it).
std::optional<NumberToken> number_token(std::string_view w) {
    NumberToken t;
    std::size_t i = 0;
    if (w.size() >= 3 && is_upper(w[0]) && w[1] == '.' && is_digit(w[2])) {
        t.parts.push_back(kLetterBase + (w[0] - 'A'));
        i = 2;
    }
    while (true) {
        std::size_t digits = 0;
        int value = 0;
        while (i < w.size() && is_digit(w[i]) && digits < 3) {
            value = value * 10 + (w[i] - '0');
            ++i;
            ++digits;
        }
        if (digits == 0 || (i < w.size() && is_digit(w[i]))) return std::nullopt;
        t.parts.push_back(value);
        if (i + 1 < w.size() && w[i] == '.' && is_digit(w[i + 1])) {
            ++i;
            continue;
        }
        break;
    }
    if (t.parts.size() > kMaxDepth || t.parts.front() > 99 || t.parts.front() == 0) return std::nullopt;
    t.text = std::string(w.substr(0, i));
    if (i < w.size() && w[i] == '.') ++i;
    if (i < w.size()) {
        if (t.parts.size() < 2 || !is_upper(w[i])) return std::nullopt;   // "3D", "5V" are no numbers of a title
        t.glued = std::string(w.substr(i));
    }
    return t;
}

std::vector<std::string_view> split_words(std::string_view s) {
    std::vector<std::string_view> out;
    for (std::size_t i = 0; i < s.size();) {
        while (i < s.size() && s[i] == ' ') ++i;
        const std::size_t start = i;
        while (i < s.size() && s[i] != ' ') ++i;
        if (i > start) out.push_back(s.substr(start, i - start));
    }
    return out;
}

bool is_leader_or_page(std::string_view w) {
    if (text::is_all_digits(w) || roman_value(w) > 0) return true;
    return !w.empty() && std::ranges::all_of(w, [](char c) { return c == '.' || c == '_' || c == '-'; });
}

// `next` comes after `prev` in contents order with only a few entries left
// out between them (4.5 -> 6, 6 -> 7.2): entries hidden or lost on the page.
bool skips_little(std::span<const int> prev, std::span<const int> next) {
    std::size_t d = 0;
    while (d < prev.size() && d < next.size() && prev[d] == next[d]) ++d;
    if (d == next.size()) return false;                         // the same number, or a level up of it
    if (d < prev.size() && (next[d] <= prev[d] || next[d] - prev[d] > 3)) return false;
    if (d == prev.size() && next[d] > 3) return false;          // deeper: starts near 1
    for (std::size_t k = d + 1; k < next.size(); ++k) {
        if (next[k] > 3) return false;
    }
    return true;
}

struct ContentsEntry {
    NumberToken number;
    std::string title;
    std::string first_line_title;   // the title as far as the entry's own line goes (the rest may be other text)
    std::size_t last_line = 0;   // the last line of the contents that holds this entry
};

// The longest run of numbers in contents order (1, 2, 2.1, 2.2, 3 ...), each
// close to the one before it: a table of contents. Empty when there is none.
std::vector<ContentsEntry> find_contents(std::span<const Line> lines) {
    constexpr std::size_t kMaxGapWords = 20;   // words of one title (and its page number)
    constexpr std::size_t kMaxGapLines = 6;
    constexpr std::size_t kMinEntries = 6;

    std::vector<std::vector<std::string_view>> words(lines.size());
    std::vector<NumberToken> tokens;
    for (std::size_t li = 0; li < lines.size(); ++li) {
        if (lines[li].figure != 0 || lines[li].ocr == Line::Ocr::Picture) continue;
        words[li] = split_words(lines[li].text);
        for (std::size_t wi = 0; wi < words[li].size(); ++wi) {
            if (auto t = number_token(words[li][wi])) {
                t->line = li;
                t->word = wi;
                tokens.push_back(std::move(*t));
            }
        }
    }
    // Words from token a to token b (a before b).
    const auto words_between = [&](const NumberToken& a, const NumberToken& b) {
        if (a.line == b.line) return b.word - a.word - 1;
        std::size_t n = words[a.line].size() - a.word - 1;
        for (std::size_t li = a.line + 1; li < b.line; ++li) n += words[li].size();
        return n + b.word;
    };

    std::vector<std::size_t> best;
    for (std::size_t s = 0; s < tokens.size(); ++s) {
        if (tokens[s].parts != std::vector<int>{1} && tokens[s].parts != std::vector<int>{1, 1}) continue;
        std::vector<std::size_t> chain{s};
        for (std::size_t k = s + 1; k < tokens.size(); ++k) {
            const NumberToken& last = tokens[chain.back()];
            if (tokens[k].line > last.line + kMaxGapLines || words_between(last, tokens[k]) > kMaxGapWords) break;
            if (is_successor(last.parts, tokens[k].parts) || skips_little(last.parts, tokens[k].parts)) chain.push_back(k);
        }
        if (chain.size() >= kMinEntries) s = chain.back();   // the next contents can only start after this one
        if (chain.size() > best.size()) best = std::move(chain);
    }
    if (best.size() < kMinEntries) return {};

    std::vector<ContentsEntry> out;
    for (std::size_t c = 0; c < best.size(); ++c) {
        const NumberToken& t = tokens[best[c]];
        ContentsEntry e{t, {}, {}, t.line};
        std::vector<std::string_view> title;
        std::size_t own = 0;   // words of the title on the entry's own line
        if (!t.glued.empty()) {
            title.push_back(t.glued);
            ++own;
        }
        std::size_t li = t.line, wi = t.word + 1;
        const NumberToken* next = c + 1 < best.size() ? &tokens[best[c + 1]] : nullptr;
        while (li < lines.size()) {
            if (next && li == next->line && wi >= next->word) break;
            if (wi >= words[li].size()) {
                if (!next) break;   // the last entry ends with its line
                ++li;
                wi = 0;
                continue;
            }
            title.push_back(words[li][wi]);
            if (li == t.line) ++own;
            e.last_line = li;
            ++wi;
        }
        const auto join = [&](std::size_t n) {
            std::vector<std::string_view> w(title.begin(), title.begin() + static_cast<std::ptrdiff_t>(n));
            while (!w.empty() && is_leader_or_page(w.back())) w.pop_back();   // ". . . . 12"
            std::string joined;
            for (const std::string_view x : w) {
                if (!joined.empty()) joined += ' ';
                joined += x;
            }
            return joined;
        };
        e.title = join(title.size());
        e.first_line_title = join(own);
        if (text::word_count(e.title) > 14) return {};   // text, not a contents entry
        out.push_back(std::move(e));
    }
    std::size_t words_total = 0;
    for (const ContentsEntry& e : out) words_total += text::word_count(e.title);
    if (words_total > 8 * out.size()) return {};
    return out;
}

HeadingResult detect_from_contents(std::span<const Line> lines) {
    HeadingResult result;
    result.body_size = body_font_size(lines);
    const std::vector<ContentsEntry> entries = find_contents(lines);
    if (entries.empty()) return result;
    const std::size_t after = entries.back().last_line + 1;   // headings come after the contents

    std::vector<std::string> skeletons(lines.size());
    for (std::size_t i = after; i < lines.size(); ++i) skeletons[i] = text::skeleton(lines[i].text);

    // Every place each title stands alone on a line (or two), after the contents.
    struct Place {
        std::size_t line, count;
        double weight;
    };
    std::vector<std::vector<Place>> places(entries.size());
    for (std::size_t e = 0; e < entries.size(); ++e) {
        const std::string number_sk = text::skeleton(entries[e].number.text);
        std::vector<std::string> wanted;   // the title, with and without its number
        for (const std::string& t : {entries[e].title, entries[e].first_line_title}) {
            const std::string sk = text::skeleton(t);
            if (sk.size() < 2) continue;
            wanted.push_back(sk);
            wanted.push_back(number_sk + sk);
        }
        if (wanted.empty()) continue;
        const auto equal_any = [&](const std::string& x) { return std::ranges::find(wanted, x) != wanted.end(); };
        const auto prefix_any = [&](const std::string& x) {
            return std::ranges::any_of(wanted, [&](const std::string& w) { return w.starts_with(x); });
        };
        for (std::size_t i = after; i < lines.size(); ++i) {
            if (lines[i].figure != 0 || lines[i].ocr == Line::Ocr::Picture || skeletons[i].empty()) continue;
            std::string joined;
            for (std::size_t k = 0; k < 2 && i + k < lines.size(); ++k) {
                joined += skeletons[i + k];
                if (equal_any(joined)) {
                    const Line& l = lines[i];
                    const double style = (l.size >= result.body_size * 1.08f ? 0.2 : 0.0) + (l.bold ? 0.1 : 0.0);
                    places[e].push_back({i, k + 1, 1.0 + style});
                    break;
                }
                if (!prefix_any(joined)) break;
            }
        }
    }

    // The best choice of places in the order of the contents (weighted
    // longest increasing sequence of lines).
    struct Pick {
        double score = 0;
        long prev_entry = -1, prev_place = -1;
    };
    std::vector<std::vector<Pick>> pick(entries.size());
    double best_score = 0;
    long best_e = -1, best_p = -1;
    for (std::size_t e = 0; e < entries.size(); ++e) {
        pick[e].resize(places[e].size());
        for (std::size_t q = 0; q < places[e].size(); ++q) {
            Pick here{places[e][q].weight, -1, -1};
            for (std::size_t f = 0; f < e; ++f) {
                for (std::size_t r = 0; r < places[f].size(); ++r) {
                    if (places[f][r].line + places[f][r].count <= places[e][q].line &&
                        pick[f][r].score + places[e][q].weight > here.score) {
                        here = {pick[f][r].score + places[e][q].weight, static_cast<long>(f), static_cast<long>(r)};
                    }
                }
            }
            pick[e][q] = here;
            if (here.score > best_score) {
                best_score = here.score;
                best_e = static_cast<long>(e);
                best_p = static_cast<long>(q);
            }
        }
    }
    std::vector<std::pair<std::size_t, std::size_t>> chosen;   // entry, place
    for (long e = best_e, q = best_p; e >= 0;) {
        chosen.emplace_back(static_cast<std::size_t>(e), static_cast<std::size_t>(q));
        const Pick& pk = pick[static_cast<std::size_t>(e)][static_cast<std::size_t>(q)];
        e = pk.prev_entry;
        q = pk.prev_place;
    }
    // Most titles of the contents must be found: otherwise it was a list, not the contents.
    if (chosen.size() < 4 || 2 * chosen.size() < entries.size()) return result;
    std::ranges::reverse(chosen);

    for (const auto& [e, q] : chosen) {
        const ContentsEntry& entry = entries[e];
        const Place& place = places[e][q];
        Heading h;
        h.page = lines[place.line].page;
        h.line_index = place.line;
        h.line_count = place.count;
        std::string text;
        for (std::size_t k = 0; k < place.count; ++k) {
            if (k > 0) text += ' ';
            text += text::collapse_spaces(lines[place.line + k].text);
        }
        std::string_view rest;
        h.title = parse_section_number(text, rest) ? text : entry.number.text + " " + text;
        h.number = entry.number.text;
        h.level = static_cast<int>(std::min(entry.number.parts.size(), kMaxDepth));
        h.source = HeadingSource::Contents;
        result.headings.push_back(std::move(h));
    }
    result.source = HeadingSource::Contents;
    result.contents_entries = entries.size();
    return result;
}

}  // namespace

std::optional<SectionNumber> parse_section_number(std::string_view line, std::string_view& rest) {
    const std::string_view s = skip_blanks(line);
    if (s.empty()) return std::nullopt;

    if (auto keyword = parse_keyword_number(s, rest)) return keyword;

    SectionNumber number;
    std::string_view t = s;
    // "Register 30.2.43: I2C_2_REG_42 ...": a register heading of a manual
    // carries the number of its section, one level below it.
    if (s.starts_with("Register ") && s.size() > 9 && is_digit(s[9])) {
        std::string_view u = s.substr(9);
        const int first = read_number(u);
        std::vector<int> parts = {first};
        if (first >= 0 && read_dotted_tail(u, parts) && parts.size() >= 2) {
            number.parts = std::move(parts);
            number.text = std::string(s.substr(9, s.size() - 9 - u.size()));
            if (!u.empty() && u[0] == ':') u.remove_prefix(1);
            if (!u.empty() && is_blank(u[0])) {
                rest = skip_blanks(u);
                if (!rest.empty()) return number;
            }
        }
        return std::nullopt;
    }
    if (s.size() >= 3 && is_upper(s[0]) && s[1] == '.' && is_digit(s[2])) {   // "A.1"
        number.parts.push_back(kLetterBase + (s[0] - 'A'));
        t.remove_prefix(1);
    } else if (is_digit(s[0])) {                                               // "5.2.1"
        const int first = read_number(t);
        if (first < 0) return std::nullopt;
        number.parts.push_back(first);
    } else {
        return std::nullopt;
    }
    if (!read_dotted_tail(t, number.parts)) return std::nullopt;
    number.text = std::string(s.substr(0, s.size() - t.size()));
    if (!read_title_after_number(t, rest)) return std::nullopt;
    return number;
}

// The title printed on the first page with text: its line or lines (at most
// three, one below the other) in the largest font, clearly larger than the
// text. "" when there is none.
std::string printed_title(std::span<const Line> lines, float body_size) {
    std::size_t first = 0;
    while (first < lines.size() && lines[first].text.empty()) ++first;
    if (first == lines.size()) return {};
    const int page = lines[first].page;
    std::size_t largest = lines.size();
    for (std::size_t i = first; i < lines.size() && lines[i].page == page; ++i) {
        if (lines[i].text.empty() || lines[i].ocr == Line::Ocr::Picture) continue;
        if (largest == lines.size() || lines[i].size > lines[largest].size) largest = i;
    }
    if (largest == lines.size() || lines[largest].size < 1.3f * body_size) return {};
    std::string title = text::collapse_spaces(lines[largest].text);
    std::size_t count = 1;
    for (std::size_t i = largest + 1; i < lines.size() && count < 3; ++i) {
        const Line& prev = lines[i - 1];
        const Line& line = lines[i];
        if (line.page != page || std::fabs(line.size - prev.size) > 0.05f * prev.size || line.bold != prev.bold ||
            line.baseline <= prev.baseline || line.baseline - prev.baseline > 1.6f * prev.size) {
            break;
        }
        title += ' ';
        title += text::collapse_spaces(line.text);
        ++count;
    }
    return title.size() <= 200 ? text::sanitize_utf8(title) : std::string{};
}

bool is_successor(std::span<const int> prev, std::span<const int> next) {
    if (prev.empty() || next.empty()) return false;
    if (next.size() == prev.size() + 1) {   // one level deeper: 2.1 -> 2.1.1
        return std::ranges::equal(prev, next.first(prev.size())) && (next.back() == 1 || next.back() == 0);
    }
    if (next.size() <= prev.size()) {       // same or shallower level: +1 at that level
        const std::size_t depth = next.size();
        return std::ranges::equal(prev.first(depth - 1), next.first(depth - 1)) &&
               next[depth - 1] == prev[depth - 1] + 1;
    }
    return false;
}

float body_font_size(std::span<const Line> lines) {
    std::map<int, std::size_t> weight;   // half-point bucket -> bytes of text
    for (const Line& line : lines) weight[size_bucket(line.size)] += line.text.size();
    int best = size_bucket(10.0f);
    std::size_t best_weight = 0;
    for (const auto& [bucket, w] : weight) {
        if (w > best_weight) {
            best = bucket;
            best_weight = w;
        }
    }
    return static_cast<float>(best) / 2.0f;
}

HeadingQuality assess_headings(const HeadingResult& result, int page_count) {
    const std::size_t n = result.headings.size();
    if (n == 0) return HeadingQuality::Poor;
    // A document of three pages or more should show a few headings, and one more for every fifteen pages.
    const std::size_t expected = page_count <= 2 ? 1 : 2 + static_cast<std::size_t>(page_count) / 15;
    if (n < expected) return HeadingQuality::Poor;
    // All on one level in a long text (not a slide deck, which has a title on every page or two).
    const bool one_level = std::ranges::all_of(result.headings, [](const Heading& h) { return h.level == 1; });
    if (one_level && n >= 8 && static_cast<double>(page_count) >= 1.5 * static_cast<double>(n)) return HeadingQuality::Poor;
    return result.source == HeadingSource::Bookmark ? HeadingQuality::GoodFromBookmarks : HeadingQuality::Good;
}

HeadingResult detect_headings(std::span<const Line> lines, std::span<const OutlineEntry> outline,
                              std::span<const Affine> page_transforms,
                              const HeadingOptions& options) {
    if (options.mode == HeadingMode::None) {
        HeadingResult none;
        none.body_size = body_font_size(lines);
        return none;
    }
    const auto usable = static_cast<std::size_t>(std::ranges::count_if(outline, [&](const OutlineEntry& e) {
        return e.page >= 0 && static_cast<std::size_t>(e.page) < page_transforms.size() && !e.title.empty();
    }));
    const bool use_bookmarks = options.mode == HeadingMode::Bookmarks ||
                               (options.mode == HeadingMode::Auto && usable >= options.min_bookmarks);
    if (!use_bookmarks || usable == 0) {
        HeadingResult from_text = detect_from_text(lines, options.styles, options.producer);
        // A printed table of contents whose titles are found in the text wins
        // over headings found by their look, unless those are as many and numbered.
        HeadingResult from_contents = detect_from_contents(lines);
        const bool numbered = from_text.source == HeadingSource::Numbering;
        // The tags of the PDF say what is a heading, with its level: they win unless
        // the other ways found more headings (then the tags are not complete).
        if (options.mode == HeadingMode::Auto) {
            HeadingResult from_tags = detect_from_tags(lines);
            if (!from_tags.headings.empty() && from_tags.headings.size() >= from_contents.headings.size() &&
                (!numbered || from_tags.headings.size() >= from_text.headings.size())) {
                return from_tags;
            }
        }
        if (!from_contents.headings.empty() &&
            (!numbered || from_contents.headings.size() > from_text.headings.size())) {
            return from_contents;
        }
        return from_text;
    }

    HeadingResult result = detect_from_bookmarks(lines, outline, page_transforms);
    if (options.mode == HeadingMode::Auto) {
        refine_with_text_headings(result, detect_from_text(lines, options.styles, options.producer), lines.size());
        refine_with_tag_headings(result, detect_from_tags(lines), lines, options.styles);
    }
    return result;
}

}  // namespace htpd
