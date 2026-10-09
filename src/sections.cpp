#include "htpd/sections.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "htpd/text.hpp"

namespace htpd {

namespace {

constexpr bool is_digit(char c) { return c >= '0' && c <= '9'; }
constexpr bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
constexpr bool is_alpha(char c) { return is_lower(c) || (c >= 'A' && c <= 'Z'); }
constexpr bool is_alnum(char c) { return is_alpha(c) || is_digit(c); }

// A line with at least two wide gaps is treated as a table row.
bool is_row(const Line& line) {
    return line.gaps.size() >= 2;
}

// "• item", "- item", "1. item", "a) item", "(2) item", "[3] item"
bool starts_list_item(std::string_view s) {
    static constexpr std::string_view kBullets[] = {
        "\xE2\x80\xA2", "\xE2\x96\xAA", "\xE2\x96\xA0", "\xE2\x80\xA3", "\xE2\x97\xA6",
        "\xE2\x97\x8F", "\xE2\x97\x8B", "\xE2\x80\x93", "\xE2\x80\x94", "\xC2\xB7", "-", "*"};
    for (const std::string_view bullet : kBullets) {
        if (s.size() > bullet.size() && s.starts_with(bullet) && s[bullet.size()] == ' ') return true;
    }
    std::size_t i = 0;
    const bool paren = s.starts_with('(');
    const bool bracket = s.starts_with('[');
    if (paren || bracket) i = 1;
    std::size_t digits = 0;
    while (i < s.size() && is_digit(s[i]) && digits < 3) {
        ++i;
        ++digits;
    }
    bool letter = false;
    if (digits == 0 && i < s.size() && is_alpha(s[i])) {
        ++i;
        letter = true;
    }
    if (digits == 0 && !letter) return false;
    if (i + 1 >= s.size() || s[i + 1] != ' ') return false;
    const char close = s[i];
    if (bracket) return close == ']';
    if (paren) return close == ')';
    return close == ')' || (close == '.' && !letter);
}

// Appends the next line of a paragraph. A term broken at a hyphen at the end
// of a line ("inter-" / "rupt", "RS-" / "485") continues without a space.
void join_line(std::string& paragraph, std::string_view line, bool dehyphenate) {
    if (paragraph.empty() || line.empty()) {
        paragraph += line;
        return;
    }
    const std::size_t n = paragraph.size();
    const bool broken_at_hyphen = n > 1 && paragraph[n - 1] == '-' && is_alnum(paragraph[n - 2]) &&
                                  is_alnum(line.front());
    if (broken_at_hyphen) {
        const bool plain_word = is_lower(paragraph[n - 2]) && is_lower(line.front());
        if (dehyphenate && plain_word) paragraph.pop_back();
    } else {
        paragraph.push_back(' ');
    }
    paragraph += line;
}

// A block of monospace lines as text: one line each, with the spacing of the
// page. In a monospace font every character is one column wide, so the pen
// positions say exactly how many columns a line is indented and how many
// blanks lie between two words (aligned comments, diagrams made of characters).
std::string code_text(std::span<const Line* const> block, std::span<const FontFacts> fonts) {
    constexpr long kMaxBlanks = 200;
    float left = block.front()->origin_x;
    float usual_step = 0;   // smallest distance between two lines of the block; 0 = not known
    for (std::size_t i = 0; i < block.size(); ++i) {
        left = std::min(left, block[i]->origin_x);
        if (i == 0) continue;
        const float step = block[i]->baseline - block[i - 1]->baseline;
        if (step > 0 && (usual_step <= 0 || step < usual_step)) usual_step = step;
    }

    std::string out;
    for (std::size_t i = 0; i < block.size(); ++i) {
        const Line& line = *block[i];
        if (i > 0) {
            out.push_back('\n');
            const float step = line.baseline - block[i - 1]->baseline;
            if (usual_step > 0 && step > 1.7f * usual_step) out.push_back('\n');   // an empty line in the listing
        }
        // Width of one column in points.
        const float column = (line.font < fonts.size() ? fonts[line.font].advance : 0.6f) * line.size;
        const auto columns = [&](float distance) {
            return column > 0.1f ? std::clamp(std::lround(distance / column), 0L, kMaxBlanks) : 0L;
        };
        out.append(static_cast<std::size_t>(columns(line.origin_x - left)), ' ');

        std::size_t space = 0;   // number of the next space in the line
        for (const char c : line.text) {
            if (c != ' ') {
                out.push_back(c);
                continue;
            }
            // From one character to the next is one column; every column more is a blank.
            const long blanks = space < line.space_spans.size() ? columns(line.space_spans[space]) - 1 : 1;
            out.append(static_cast<std::size_t>(std::max(blanks, 1L)), ' ');
            ++space;
        }
    }
    return out;
}

}  // namespace

std::string row_text(const Line& line) {
    std::string out;
    out.reserve(line.text.size() + line.gaps.size() * 2);
    std::size_t next_gap = 0;
    for (std::size_t i = 0; i < line.text.size(); ++i) {
        if (next_gap < line.gaps.size() && line.gaps[next_gap] == i) {
            out += " | ";
            ++next_gap;
        } else {
            out.push_back(line.text[i]);
        }
    }
    return out;
}

namespace {

struct Cell {
    float x0 = 0, x1 = 0;
    std::string_view text;
};

// The cells of a row with their horizontal extent; nothing when the line
// does not carry the positions of its gaps.
std::vector<Cell> cells_of(const Line& line) {
    const std::size_t gaps = line.gaps.size();
    if (line.gap_begin.size() != gaps || line.gap_end.size() != gaps) return {};
    std::vector<Cell> cells;
    cells.reserve(gaps + 1);
    const std::string_view text = line.text;
    std::size_t start = 0;
    float x0 = line.x0;
    for (std::size_t k = 0; k < gaps; ++k) {
        if (line.gaps[k] < start || line.gaps[k] >= text.size()) return {};
        cells.push_back({x0, line.gap_begin[k], text.substr(start, line.gaps[k] - start)});
        start = line.gaps[k] + 1;
        x0 = line.gap_end[k];
    }
    cells.push_back({x0, line.x1, text.substr(start)});
    return cells;
}

}  // namespace

std::string rows_text(std::span<const Line* const> rows) {
    const auto plain = [&] {
        std::string out;
        for (const Line* line : rows) {
            if (!out.empty()) out.push_back('\n');
            out += row_text(*line);
        }
        return out;
    };

    std::vector<std::vector<Cell>> table;
    std::size_t columns = 0, complete = 0;
    for (const Line* line : rows) {
        table.push_back(cells_of(*line));
        if (table.back().empty()) return plain();
        columns = std::max(columns, table.back().size());
    }
    for (const auto& row : table) {
        if (row.size() == columns) ++complete;
    }
    if (complete == table.size()) return plain();   // every row has every cell

    // Where each column lies, taken from the rows that have every cell.
    std::vector<float> left(columns, 1e30f), right(columns, -1e30f);
    for (const auto& row : table) {
        if (row.size() != columns) continue;
        for (std::size_t c = 0; c < columns; ++c) {
            left[c] = std::min(left[c], row[c].x0);
            right[c] = std::max(right[c], row[c].x1);
        }
    }
    for (std::size_t c = 0; c + 1 < columns; ++c) {
        if (right[c] >= left[c + 1]) return plain();   // columns run into each other: not sure enough
    }

    constexpr float kSlack = 1.0f;   // points
    std::string out;
    std::vector<std::string_view> placed;
    for (const auto& row : table) {
        placed.assign(columns, {});
        std::size_t first_free = 0;
        for (const Cell& cell : row) {
            // The column whose middle line between its neighbours encloses the centre of the cell.
            const float centre = (cell.x0 + cell.x1) / 2;
            std::size_t c = 0;
            while (c + 1 < columns && centre > (right[c] + left[c + 1]) / 2) ++c;
            // To be sure, the cell must sit in its column: its centre inside
            // the text of the complete rows there, or their centre inside the
            // cell. (The names in a drawing of a register lie BETWEEN the bit
            // numbers above them; such rows are not a table and stay as they are.)
            const float column_centre = (left[c] + right[c]) / 2;
            const bool in_column = (centre >= left[c] && centre <= right[c]) ||
                                   (column_centre >= cell.x0 && column_centre <= cell.x1);
            // It must also stay clear of the text of the columns beside it,
            // and the cells of a row go from left to right, one per column.
            const bool clear_left = c == 0 || cell.x0 >= right[c - 1] - kSlack;
            const bool clear_right = c + 1 == columns || cell.x1 <= left[c + 1] + kSlack;
            if (c < first_free || !in_column || !clear_left || !clear_right) return plain();
            placed[c] = cell.text;
            first_free = c + 1;
        }
        if (!out.empty()) out.push_back('\n');
        std::string line;
        for (std::size_t c = 0; c < columns; ++c) {
            if (c > 0) line += " | ";
            line += placed[c];
        }
        // No blanks at the ends of the line when the first or last cell is empty.
        if (const std::size_t first = line.find_first_not_of(' '); first != std::string::npos) {
            out += line.substr(first, line.find_last_not_of(' ') - first + 1);
        }
    }
    return out;
}

float typical_line_pitch(std::span<const Line> lines) {
    std::vector<float> ratios;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const Line& a = lines[i - 1];
        const Line& b = lines[i];
        if (a.page != b.page || a.size <= 0 || std::fabs(a.size - b.size) > 0.05f * a.size) continue;
        const float ratio = (b.baseline - a.baseline) / a.size;
        if (ratio > 0.8f && ratio < 3.0f) ratios.push_back(ratio);
    }
    if (ratios.size() < 5) return 1.2f;
    const auto middle = ratios.begin() + static_cast<std::ptrdiff_t>(ratios.size() / 2);
    std::ranges::nth_element(ratios, middle);
    return std::clamp(*middle, 1.0f, 2.6f);
}

std::vector<Paragraph> build_paragraphs(std::span<const Line> lines, float line_pitch,
                                        const SectionOptions& options) {
    std::vector<Paragraph> paragraphs;
    Paragraph current;
    std::vector<const Line*> code;   // the lines of `current` when it is a code block
    std::vector<const Line*> rows;   // the lines of `current` when it holds table rows
    const Line* previous = nullptr;

    // What each line is. A listing has at least two lines: one monospace line
    // between ordinary lines is a term or a file name inside the text, and
    // stays part of its paragraph.
    const auto is_monospace = [&](std::size_t i) {
        return lines[i].font < options.fonts.size() && options.fonts[lines[i].font].monospace;
    };
    const auto listing_neighbour = [&](std::size_t i, std::size_t other) {
        return other < lines.size() && lines[other].page == lines[i].page && is_monospace(other);
    };
    std::vector<ParagraphKind> kinds(lines.size(), ParagraphKind::Text);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const bool listing = is_monospace(i) &&
                             ((i > 0 && listing_neighbour(i, i - 1)) || listing_neighbour(i, i + 1));
        kinds[i] = listing ? ParagraphKind::Code : is_row(lines[i]) ? ParagraphKind::Rows : ParagraphKind::Text;
    }
    const auto flush = [&] {
        if (code.size() == 1) {
            // One monospace line on its own (a term, a file name) is ordinary text.
            current.kind = ParagraphKind::Text;
            current.text = code.front()->text;
        } else if (!code.empty()) {
            current.text = code_text(code, options.fonts);
        }
        code.clear();
        if (!rows.empty()) current.text = rows_text(rows);
        rows.clear();
        if (!current.text.empty()) paragraphs.push_back(std::move(current));
        current = Paragraph{};
    };
    const auto is_empty = [&] { return current.text.empty() && code.empty() && rows.empty(); };

    for (std::size_t index = 0; index < lines.size(); ++index) {
        const Line& line = lines[index];
        if (line.text.empty()) continue;
        const ParagraphKind kind = kinds[index];

        bool start_new = previous == nullptr || is_empty() || kind != current.kind;
        if (!start_new && kind == ParagraphKind::Rows) {
            start_new = line.page != previous->page;
        } else if (!start_new && kind == ParagraphKind::Code) {
            // A listing ends at the page end, at a jump back up (next column)
            // or after a large vertical gap.
            const float step = line.baseline - previous->baseline;
            start_new = line.page != previous->page || step < 0 || step > 4.0f * std::max(previous->size, 1.0f);
        } else if (!start_new) {
            const float size = std::max(previous->size, 1.0f);
            const float step = line.baseline - previous->baseline;
            const bool moved_to_new_area = line.page != previous->page || step < -0.5f * size;
            if (starts_list_item(line.text) || std::fabs(line.size - previous->size) > 0.1f * size) {
                start_new = true;
            } else if (moved_to_new_area) {
                // Next page or next column: the paragraph continues unless the
                // previous line ended a sentence.
                start_new = text::ends_with_sentence_punct(previous->text);
            } else {
                start_new = step > line_pitch * 1.35f * size;
            }
        }

        if (start_new) {
            flush();
            current.page = line.page;
            current.kind = kind;
        }
        if (kind == ParagraphKind::Code) {
            code.push_back(&line);
        } else if (kind == ParagraphKind::Rows) {
            rows.push_back(&line);
        } else {
            join_line(current.text, line.text, options.dehyphenate);
        }
        previous = &line;
    }
    flush();
    return paragraphs;
}

std::vector<Section> build_sections(std::span<const Line> lines, std::span<const Heading> headings,
                                    const SectionOptions& options) {
    std::vector<Section> sections;
    const float pitch = typical_line_pitch(lines);

    const auto add = [&](std::string title, std::string number, int level, HeadingSource source,
                         int heading_page, std::size_t begin, std::size_t end) {
        Section s;
        s.order = sections.size();
        s.title = std::move(title);
        s.number = std::move(number);
        s.level = std::max(level, 1);
        s.source = source;
        begin = std::min(begin, lines.size());
        end = std::max(begin, std::min(end, lines.size()));
        s.page_start = heading_page;
        s.page_end = end > begin ? std::max(heading_page, lines[end - 1].page) : heading_page;
        if (end > begin) s.page_start = std::min(s.page_start, lines[begin].page);
        s.span_page_end = s.page_end;
        s.paragraphs = build_paragraphs(lines.subspan(begin, end - begin), pitch, options);
        sections.push_back(std::move(s));
    };

    if (headings.empty()) {
        // No structure found: fall back to one section per page.
        for (std::size_t begin = 0; begin < lines.size();) {
            std::size_t end = begin;
            while (end < lines.size() && lines[end].page == lines[begin].page) ++end;
            add(std::format("Page {}", lines[begin].page + 1), {}, 1, HeadingSource::None,
                lines[begin].page, begin, end);
            begin = end;
        }
        return sections;
    }

    const std::size_t first = std::min(headings.front().line_index, lines.size());
    if (first > 0) add("Front matter", {}, 1, HeadingSource::None, lines.front().page, 0, first);

    std::size_t title_end = 0;   // end of the previous heading's own title lines
    for (std::size_t i = 0; i < headings.size(); ++i) {
        const Heading& h = headings[i];
        // A heading never starts inside the title lines of the one before it.
        const std::size_t begin = std::max(h.line_index + h.line_count, title_end);
        const std::size_t end = i + 1 < headings.size() ? headings[i + 1].line_index : lines.size();
        add(h.title, h.number, h.level, h.source, h.page, begin, end);
        title_end = begin;
    }

    // Parent/child links from the heading levels.
    std::vector<std::size_t> open;   // chain of sections that are still "open"
    for (std::size_t i = 0; i < sections.size(); ++i) {
        while (!open.empty() && sections[open.back()].level >= sections[i].level) open.pop_back();
        if (!open.empty()) {
            sections[i].parent = open.back();
            sections[open.back()].children.push_back(i);
        }
        open.push_back(i);
    }
    // A section spans the pages of all its subsections. Children always come
    // after their parent, so one backwards pass is enough.
    for (std::size_t i = sections.size(); i-- > 0;) {
        if (sections[i].parent) {
            Section& parent = sections[*sections[i].parent];
            parent.span_page_end = std::max(parent.span_page_end, sections[i].span_page_end);
        }
    }
    return sections;
}

}  // namespace htpd
