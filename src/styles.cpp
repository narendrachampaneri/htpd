#include "htpd/styles.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

#include "htpd/headings.hpp"
#include "htpd/text.hpp"

namespace htpd {

namespace {

bool contains_any(std::string_view haystack, std::initializer_list<std::string_view> needles) {
    return std::ranges::any_of(needles, [&](std::string_view n) { return haystack.contains(n); });
}

std::uint64_t pack(std::uint16_t font, int size2, bool bold, bool italic, std::uint32_t color) {
    return std::uint64_t{font} | (std::uint64_t(std::clamp(size2, 0, 4095)) << 16) | (std::uint64_t{bold} << 28) |
           (std::uint64_t{italic} << 29) | (std::uint64_t{color & 0xFFFFFFu} << 32);
}

bool italic_font(std::span<const FontFacts> fonts, std::uint16_t font) {
    return font < fonts.size() && fonts[font].italic;
}

bool monospace_font(std::span<const FontFacts> fonts, std::uint16_t font) {
    return font < fonts.size() && fonts[font].monospace;
}

// A line the style sheet looks at: text of the PDF, not a figure mark and not text read from a picture.
bool counts(const Line& line) {
    return !line.text.empty() && line.figure == 0 && line.ocr != Line::Ocr::Picture;
}

std::uint64_t key_of(const Line& line, std::span<const FontFacts> fonts) {
    return pack(line.font, static_cast<int>(std::lround(line.size * 2.0f)), line.bold, italic_font(fonts, line.font),
                line.color);
}

// One run of lines of a style on a page: a heading, a paragraph, a table row ...
struct Block {
    int style = -1;
    int next = -1;           // style of the line after it on the page, -1 when none
    std::size_t length = 0;
    std::size_t words = 0;
    bool open = true;        // ends without a full stop, comma or semicolon
};

bool ends_closed(std::string_view s) {
    return !s.empty() && (s.back() == '.' || s.back() == ',' || s.back() == ';');
}

std::string pt_text(int size2) {
    return size2 % 2 == 0 ? std::format("{} pt", size2 / 2) : std::format("{}.5 pt", size2 / 2);
}

}  // namespace

const char* to_string(Producer producer) {
    switch (producer) {
        case Producer::Unknown:     return "unknown";
        case Producer::Word:        return "Word";
        case Producer::LibreOffice: return "LibreOffice";
        case Producer::LaTeX:       return "LaTeX";
        case Producer::Chrome:      return "Chrome/Skia";
        case Producer::Wkhtmltopdf: return "wkhtmltopdf";
        case Producer::InDesign:    return "InDesign";
        case Producer::Other:       return "other";
    }
    return "unknown";
}

const char* to_string(Role role) {
    switch (role) {
        case Role::Other:     return "other";
        case Role::Body:      return "body";
        case Role::Heading:   return "heading";
        case Role::Caption:   return "caption";
        case Role::TableText: return "table text";
        case Role::Code:      return "code";
        case Role::Furniture: return "header/footer";
        case Role::Footnote:  return "footnote";
    }
    return "other";
}

Producer classify_producer(std::string_view creator, std::string_view producer) {
    const std::string c = text::to_lower_ascii(creator), p = text::to_lower_ascii(producer);
    const auto either = [&](std::initializer_list<std::string_view> needles) {
        return contains_any(c, needles) || contains_any(p, needles);
    };
    if (c.empty() && p.empty()) return Producer::Unknown;
    if (either({"pdftex", "xetex", "luatex", "latex", "dvipdfm", "dvips"})) return Producer::LaTeX;
    if (either({"wkhtmltopdf"})) return Producer::Wkhtmltopdf;
    if (either({"skia/pdf", "headlesschrome", "chromium", "chrome/"})) return Producer::Chrome;
    if (either({"indesign"})) return Producer::InDesign;
    if (either({"libreoffice", "openoffice"}) || c == "writer" || c == "impress" || c == "calc") {
        return Producer::LibreOffice;
    }
    if (either({"microsoft"}) && either({"word"})) return Producer::Word;
    if (either({"pdfmaker for word"})) return Producer::Word;
    return Producer::Other;
}

bool starts_like_caption(std::string_view text) {
    const std::string lower = text::to_lower_ascii(text.substr(0, 24));
    std::size_t i = 0;
    constexpr std::string_view words[] = {"figure", "fig.", "fig", "table", "tab.", "abbildung", "tabelle"};
    bool matched = false;
    for (const std::string_view w : words) {
        if (lower.starts_with(w)) {
            i = w.size();
            matched = true;
            break;
        }
    }
    if (!matched) return false;
    while (i < lower.size() && lower[i] == ' ') ++i;
    if (i >= lower.size()) return false;
    const auto digit = [&](std::size_t k) { return k < lower.size() && lower[k] >= '0' && lower[k] <= '9'; };
    std::size_t k = i;
    if (digit(k)) {
        while (digit(k) || (k < lower.size() && (lower[k] == '.' || lower[k] == '-') && digit(k + 1))) ++k;
    } else if (lower[k] >= 'a' && lower[k] <= 'z' && k + 2 < lower.size() &&
               (lower[k + 1] == '-' || lower[k + 1] == '.') && digit(k + 2)) {
        k += 2;   // "Table A-1", "Figure B.2"
        while (digit(k) || (k < lower.size() && (lower[k] == '.' || lower[k] == '-') && digit(k + 1))) ++k;
    } else {
        return false;
    }
    return k >= lower.size() || lower[k] == ' ' || lower[k] == '.' || lower[k] == ':' || lower[k] == '-';
}

int StyleSheet::find(const Line& line) const {
    // `index` was filled with the key of build_style_sheet; the italic bit needs the fonts, so the
    // sheet remembers it per style and looks the line up by all other parts.
    if (!counts(line)) return -1;
    const std::uint64_t base = pack(line.font, static_cast<int>(std::lround(line.size * 2.0f)), line.bold, false, line.color);
    const auto plain = index.find(base);
    if (plain != index.end()) return plain->second;
    const auto slanted = index.find(base | (std::uint64_t{1} << 29));
    return slanted == index.end() ? -1 : slanted->second;
}

Role StyleSheet::role_of(const Line& line) const {
    const int s = find(line);
    return s < 0 ? Role::Other : styles[static_cast<std::size_t>(s)].role;
}

int StyleSheet::heading_level(const Line& line) const {
    const int s = find(line);
    return s < 0 ? 0 : styles[static_cast<std::size_t>(s)].level;
}

StyleSheet build_style_sheet(std::span<const std::vector<Line>> pages, std::span<const float> heights,
                             std::span<const FontFacts> fonts, Producer producer, const StyleOptions& options) {
    StyleSheet sheet;
    sheet.producer = producer;
    std::vector<Style>& styles = sheet.styles;

    // 1. Every line into its style, with what can be counted on the line alone.
    std::vector<std::size_t> last_page;   // per style: the last page counted
    std::size_t line_number = 0, total_bytes = 0, page_with_text = 0;
    std::vector<std::vector<int>> style_of(pages.size());
    for (std::size_t p = 0; p < pages.size(); ++p) {
        const float height = p < heights.size() ? heights[p] : 0.0f;
        style_of[p].assign(pages[p].size(), -1);
        bool any = false;
        for (std::size_t i = 0; i < pages[p].size(); ++i) {
            const Line& line = pages[p][i];
            ++line_number;
            if (!counts(line)) continue;
            any = true;
            const std::uint64_t key = key_of(line, fonts);
            const auto [it, fresh] = sheet.index.try_emplace(key, static_cast<int>(styles.size()));
            if (fresh) {
                Style s;
                s.font = line.font;
                s.size2 = static_cast<int>(std::lround(line.size * 2.0f));
                s.bold = line.bold;
                s.italic = italic_font(fonts, line.font);
                s.color = line.color;
                s.first_line = line_number;
                styles.push_back(s);
                last_page.push_back(pages.size());
            }
            const auto s_index = static_cast<std::size_t>(it->second);
            style_of[p][i] = it->second;
            Style& s = styles[s_index];
            ++s.lines;
            s.bytes += line.text.size();
            total_bytes += line.text.size();
            if (last_page[s_index] != p) {
                last_page[s_index] = p;
                ++s.pages;
            }
            if (height > 0) {
                const double centre = (line.y0 + line.y1) / 2.0;
                if (centre < options.band * height || centre > (1.0 - options.band) * height) ++s.edge;
                if (centre > 0.67 * height) ++s.low;
            }
            std::string_view rest;
            if (const auto number = parse_section_number(line.text, rest)) {
                ++s.numbered;
                s.depth_sum += number->keyword ? 1 : number->parts.size();
            }
            if (starts_like_caption(line.text)) ++s.caption_like;
            if (line.table || line.gaps.size() >= 2) ++s.table_like;
        }
        page_with_text += any;
    }
    if (styles.empty()) return sheet;

    // 2. The body style: the most text, not code, not at the page edge.
    {
        std::size_t best = 0;
        for (std::size_t s = 0; s < styles.size(); ++s) {
            const Style& st = styles[s];
            if (monospace_font(fonts, st.font) || st.edge * 2 > st.lines) continue;
            if (st.bytes > best) {
                best = st.bytes;
                sheet.body = static_cast<int>(s);
            }
        }
        if (sheet.body < 0) return sheet;
        sheet.body_size = static_cast<float>(styles[static_cast<std::size_t>(sheet.body)].size2) / 2.0f;
    }
    const int body2 = styles[static_cast<std::size_t>(sheet.body)].size2;

    // 3. Roles that need no neighbours.
    for (std::size_t s = 0; s < styles.size(); ++s) {
        Style& st = styles[s];
        if (static_cast<int>(s) == sheet.body) {
            st.role = Role::Body;
        } else if (monospace_font(fonts, st.font)) {
            st.role = Role::Code;
        } else if (st.lines >= 3 && st.pages >= 3 && st.edge * 10 >= st.lines * 9 && st.size2 * 20 <= body2 * 19) {
            st.role = Role::Furniture;   // small, in the top or bottom band, on three pages or more
        } else if (st.lines >= 3 && st.size2 * 25 <= body2 * 23 && st.low * 10 >= st.lines * 7 &&
                   st.edge * 10 < st.lines * 9) {
            st.role = Role::Footnote;
        } else if (st.caption_like >= 2 && st.caption_like * 10 >= st.lines * 6) {
            st.role = Role::Caption;
        } else if (st.lines >= 3 && st.table_like * 10 >= st.lines * 6) {
            st.role = Role::TableText;
        }
    }

    // 4. Blocks: runs of lines of one style on a page, and what follows each.
    std::vector<Block> blocks;
    for (std::size_t p = 0; p < pages.size(); ++p) {
        std::vector<std::size_t> seq;   // the lines of the page that have a style
        for (std::size_t i = 0; i < pages[p].size(); ++i) {
            if (style_of[p][i] >= 0) seq.push_back(i);
        }
        for (std::size_t k = 0; k < seq.size();) {
            const Line& first = pages[p][seq[k]];
            std::size_t end = k + 1;
            while (end < seq.size() && style_of[p][seq[end]] == style_of[p][seq[k]]) {
                const Line& before = pages[p][seq[end - 1]];
                const Line& cur = pages[p][seq[end]];
                const float step = cur.baseline - before.baseline;
                if (step <= 0 || step > 1.8f * std::max(cur.size, 1.0f)) break;
                ++end;
            }
            Block b;
            b.style = style_of[p][seq[k]];
            b.next = end < seq.size() ? style_of[p][seq[end]] : -1;
            b.length = end - k;
            for (std::size_t m = k; m < end; ++m) b.words += text::word_count(pages[p][seq[m]].text);
            b.open = !ends_closed(pages[p][seq[end - 1]].text);
            (void)first;
            blocks.push_back(b);
            k = end;
        }
    }
    for (const Block& b : blocks) {
        Style& st = styles[static_cast<std::size_t>(b.style)];
        ++st.blocks;
        st.short_blocks += b.length <= 3 && b.words <= 20;
        st.open_blocks += b.open;
        st.before_body += b.next == sheet.body;
    }

    // 5. Heading styles: rare, short, standing alone before text, not a caption or a header.
    const std::size_t min_blocks = options.page_count > 0 && options.page_count <= 3 ? std::max<std::size_t>(2, options.min_blocks - 1)
                                                                                      : options.min_blocks;
    std::vector<char> candidate(styles.size(), 0);
    for (std::size_t s = 0; s < styles.size(); ++s) {
        const Style& st = styles[s];
        if (st.role != Role::Other || st.blocks < min_blocks) continue;
        if (options.page_count >= 4 && st.pages < 2) continue;
        if (st.short_blocks * 10 < st.blocks * 9 || st.open_blocks * 10 < st.blocks * 8) continue;
        if (st.bytes * 100 > total_bytes * 12) continue;
        if ((st.edge * 10 >= st.lines * 9 && st.size2 <= body2) || st.table_like * 10 >= st.lines * 3) continue;
        candidate[s] = 1;
    }
    for (const Block& b : blocks) {
        if (!candidate[static_cast<std::size_t>(b.style)]) continue;
        const bool text_after = b.next >= 0 && (styles[static_cast<std::size_t>(b.next)].role == Role::Body ||
                                                 styles[static_cast<std::size_t>(b.next)].role == Role::TableText ||
                                                 styles[static_cast<std::size_t>(b.next)].role == Role::Code ||
                                                 candidate[static_cast<std::size_t>(b.next)]);
        styles[static_cast<std::size_t>(b.style)].before_text += text_after;
    }
    // A style with no size to show for itself must stand directly before body text, or before a style that does
    // (a heading of the first level followed at once by one of the second).
    const auto sized = [&](const Style& st) { return st.size2 * 100 >= body2 * 108; };
    std::vector<char> strong(styles.size(), 0);
    for (std::size_t s = 0; s < styles.size(); ++s) {
        const Style& st = styles[s];
        strong[s] = candidate[s] && st.before_text * 10 >= st.blocks * 8 &&
                    (sized(st) || (st.before_body * 10 >= st.blocks * 8 && st.blocks >= min_blocks + 1));
    }
    std::vector<std::size_t> before_strong(styles.size(), 0);
    for (const Block& b : blocks) {
        if (candidate[static_cast<std::size_t>(b.style)] && b.next >= 0 && strong[static_cast<std::size_t>(b.next)]) {
            ++before_strong[static_cast<std::size_t>(b.style)];
        }
    }
    std::vector<std::size_t> headings;
    for (std::size_t s = 0; s < styles.size(); ++s) {
        if (!candidate[s]) continue;
        Style& st = styles[s];
        const bool leads_to_strong = (st.before_body + before_strong[s]) * 10 >= st.blocks * 8 && st.blocks >= min_blocks + 1;
        if (!strong[s] && !(leads_to_strong && st.before_text * 10 >= st.blocks * 8)) continue;
        st.role = Role::Heading;
        headings.push_back(s);
    }

    // 6. Levels: bigger first, then bold, then the style that comes first where two follow each other.
    std::map<std::pair<int, int>, std::size_t> follows;   // (a, b): a block of a directly before one of b
    for (const Block& b : blocks) {
        if (b.next >= 0 && styles[static_cast<std::size_t>(b.style)].role == Role::Heading &&
            styles[static_cast<std::size_t>(b.next)].role == Role::Heading) {
            ++follows[{b.style, b.next}];
        }
    }
    const auto tier = [&](std::size_t s) { return std::pair{styles[s].size2, styles[s].bold}; };
    const auto depth = [&](std::size_t s) {
        const Style& st = styles[s];
        return st.numbered * 10 >= st.lines * 8 && st.numbered > 0 ? static_cast<double>(st.depth_sum) / static_cast<double>(st.numbered) : 0.0;
    };
    // a is above b: the evidence of the numbers, or of what follows what.
    const auto above = [&](std::size_t a, std::size_t b) {
        if (depth(a) > 0 && depth(b) > 0 && depth(a) != depth(b)) return depth(a) < depth(b);
        const std::size_t ab = follows.contains({static_cast<int>(a), static_cast<int>(b)}) ? follows[{static_cast<int>(a), static_cast<int>(b)}] : 0;
        const std::size_t ba = follows.contains({static_cast<int>(b), static_cast<int>(a)}) ? follows[{static_cast<int>(b), static_cast<int>(a)}] : 0;
        return ab > ba;
    };
    std::ranges::sort(headings, [&](std::size_t a, std::size_t b) {
        if (tier(a) != tier(b)) return tier(a) > tier(b);
        return styles[a].first_line < styles[b].first_line;
    });
    // Within a group of the same size and weight: put what is above first (a simple insertion).
    for (std::size_t i = 1; i < headings.size(); ++i) {
        for (std::size_t k = i; k > 0 && tier(headings[k]) == tier(headings[k - 1]) && above(headings[k], headings[k - 1]); --k) {
            std::swap(headings[k], headings[k - 1]);
        }
    }
    int level = 0;
    for (std::size_t i = 0; i < headings.size(); ++i) {
        if (i == 0) {
            level = 1;
        } else {
            const std::size_t a = headings[i - 1], b = headings[i];
            if (tier(a) != tier(b) || above(a, b) || above(b, a)) level = std::min(level + 1, 6);
        }
        styles[headings[i]].level = level;
    }
    return sheet;
}

std::vector<std::string> StyleSheet::describe(std::span<const FontFacts> fonts) const {
    const auto name_of = [&](const Style& st) {
        std::string name = st.font < fonts.size() && !fonts[st.font].name.empty() ? fonts[st.font].name : "unnamed font";
        std::erase_if(name, [](char c) { return c == '`' || c == '*' || c == '|' || c == '\n'; });
        if (name.size() > 40) name.resize(40);
        const std::string lower = text::to_lower_ascii(name);
        if (st.bold && !lower.contains("bold") && !lower.contains("black")) name += " bold";
        if (st.italic && !lower.contains("italic") && !lower.contains("oblique")) name += " italic";
        return std::format("{} {}{}", name, pt_text(st.size2), st.color != 0 ? std::format(" #{:06X}", st.color) : std::string{});
    };
    std::vector<std::string> out;
    const auto line_for = [&](std::string label, auto matches) {
        std::vector<const Style*> found;
        for (const Style& st : styles) {
            if (matches(st)) found.push_back(&st);
        }
        if (found.empty()) return;
        std::ranges::sort(found, [](const Style* a, const Style* b) { return a->lines > b->lines; });
        std::string text = std::format("{}: ", label);
        std::size_t lines = 0;
        for (std::size_t i = 0; i < found.size(); ++i) {
            lines += found[i]->lines;
            if (i < 2) text += std::format("{}{}", i > 0 ? "; " : "", name_of(*found[i]));
        }
        if (found.size() > 2) text += std::format("; {} more styles", found.size() - 2);
        text += std::format(", {} lines", lines);
        out.push_back(std::move(text));
    };
    line_for("body", [](const Style& st) { return st.role == Role::Body; });
    for (int level = 1; level <= 6; ++level) {
        line_for(std::format("heading {}", level), [&](const Style& st) { return st.role == Role::Heading && st.level == level; });
    }
    line_for("caption", [](const Style& st) { return st.role == Role::Caption; });
    line_for("table text", [](const Style& st) { return st.role == Role::TableText; });
    line_for("code", [](const Style& st) { return st.role == Role::Code; });
    line_for("header/footer", [](const Style& st) { return st.role == Role::Furniture; });
    line_for("footnote", [](const Style& st) { return st.role == Role::Footnote; });
    return out;
}

}  // namespace htpd
