#include "htpd/sections.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>

#include "htpd/text.hpp"

namespace htpd {

namespace {

constexpr bool is_digit(char c) { return c >= '0' && c <= '9'; }
constexpr bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
constexpr bool is_alpha(char c) { return is_lower(c) || (c >= 'A' && c <= 'Z'); }
constexpr bool is_alnum(char c) { return is_alpha(c) || is_digit(c); }

// A line with at least two wide gaps is treated as a table row - unless it is
// a line of justified text: every space of it is wide, all about equally wide,
// and most of its words are ordinary words. (In a table the cells sit at the
// columns, so the gaps differ; a row of bit numbers in a register drawing has
// no lower-case words.)
bool is_row(const Line& line) {
    const std::size_t gaps = line.gaps.size();
    if (gaps < 2) return false;
    if (gaps < 3 || line.gap_begin.size() != gaps || line.gap_end.size() != gaps) return true;
    if (static_cast<std::size_t>(std::ranges::count(line.text, ' ')) != gaps) return true;
    float narrowest = 1e30f, widest = 0;
    for (std::size_t k = 0; k < gaps; ++k) {
        const float width = line.gap_end[k] - line.gap_begin[k];
        narrowest = std::min(narrowest, width);
        widest = std::max(widest, width);
    }
    if (narrowest <= 0 || widest > 1.25f * narrowest + 0.5f) return true;
    // An ordinary word: letters only (punctuation around it allowed), at least one lower case.
    const auto ordinary = [](std::string_view w) {
        while (!w.empty() && std::string_view("([\"'").find(w.front()) != std::string_view::npos) w.remove_prefix(1);
        while (!w.empty() && std::string_view(")],.;:\"'").find(w.back()) != std::string_view::npos) w.remove_suffix(1);
        return !w.empty() && std::ranges::all_of(w, is_alpha) && std::ranges::any_of(w, is_lower);
    };
    std::size_t words = 0, plain = 0;
    for (std::size_t start = 0; start <= line.text.size();) {
        const std::size_t end = std::min(line.text.find(' ', start), line.text.size());
        ++words;
        if (ordinary(std::string_view(line.text).substr(start, end - start))) ++plain;
        start = end + 1;
    }
    return 2 * plain < words;
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
    return rows_text(rows, {});
}

std::string rows_text(std::span<const Line* const> rows, std::span<const std::uint8_t> rest) {
    const auto is_rest = [&](std::size_t i) { return i < rest.size() && rest[i] != 0; };
    // When anything is not certain: every line as it stands, one per line.
    const auto plain = [&] {
        std::string out;
        for (const Line* line : rows) {
            if (!out.empty()) out.push_back('\n');
            out += row_text(*line);
        }
        return out;
    };

    std::vector<std::vector<Cell>> table;   // the rows, without the lines that hold the rest of a cell
    std::size_t columns = 0, complete = 0;
    bool any_rest = false;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (is_rest(i)) {
            any_rest = true;
            continue;
        }
        table.push_back(cells_of(*rows[i]));
        if (table.back().empty()) return plain();
        columns = std::max(columns, table.back().size());
    }
    if (table.empty()) return plain();
    for (const auto& row : table) {
        if (row.size() == columns) ++complete;
    }
    if (complete == table.size() && !any_rest) return plain();   // every row has every cell

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
    std::vector<std::vector<std::string>> placed_rows;
    placed_rows.reserve(table.size());
    for (const auto& row : table) {
        std::vector<std::string> placed(columns);
        if (row.size() == columns) {
            for (std::size_t c = 0; c < columns; ++c) placed[c] = row[c].text;
            placed_rows.push_back(std::move(placed));
            continue;
        }
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
        placed_rows.push_back(std::move(placed));
    }

    // The rest of a cell that wraps onto the next lines joins that cell. Each
    // piece must start in a column after the first and end before the next
    // column begins; otherwise the lines stay as they stand.
    std::size_t row_index = 0;
    bool seen_row = false;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!is_rest(i)) {
            if (seen_row) ++row_index;
            seen_row = true;
            continue;
        }
        if (!seen_row) return plain();
        const std::vector<Cell> pieces = cells_of(*rows[i]);
        if (pieces.empty()) return plain();
        for (const Cell& piece : pieces) {
            std::size_t c = 0;
            while (c + 1 < columns && piece.x0 >= left[c + 1] - kSlack) ++c;
            const bool clear_right = c + 1 == columns || piece.x1 <= left[c + 1] + kSlack;
            if (c == 0 || !clear_right) return plain();
            join_line(placed_rows[row_index][c], piece.text, false);
        }
    }

    std::string out;
    for (const auto& placed : placed_rows) {
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

namespace {

// A table that the PDF stores cell by cell: after its header row come the
// lines of one cell after the other (the name, every line of a long
// description, then the short cells to the right, which stand back up beside
// the first line). Read in that order, the rows come apart. Starting at a
// header row with at least three cells, this takes the following lines as
// long as every piece of each line stays inside one column, and puts the
// rows together again by position: each piece in the first column starts a
// row, every other piece joins the row it stands beside. Text that crosses
// the columns ends the table. Returns the end of the table (`start` when
// there is nothing to rebuild: the lines already read row by row), and the
// rows in `out`. Every piece of text is kept.
std::size_t rebuild_table(std::span<const Line> lines, std::size_t start, std::span<const ParagraphKind> kinds,
                          float line_pitch, std::string& out) {
    const Line& header = lines[start];
    const std::vector<Cell> head = cells_of(header);
    const std::size_t columns = head.size();
    if (columns < 3) return start;
    const float size = std::max(header.size, 1.0f);
    const float slack = 0.5f * size;
    std::vector<float> left(columns);
    for (std::size_t c = 0; c < columns; ++c) left[c] = head[c].x0;
    for (std::size_t c = 0; c + 1 < columns; ++c) {
        if (head[c].x1 >= left[c + 1]) return start;
    }
    const float right_edge = std::max(header.x1, left.back()) + 15.0f * size;

    struct Piece {
        float baseline, x0, x1;
        std::size_t column;
        std::string_view text;
        std::size_t line;   // index in `lines`
    };
    std::vector<Piece> pieces;
    const auto column_of = [&](const Cell& cell) -> std::optional<std::size_t> {
        if (cell.x0 < left[0] - slack) return std::nullopt;
        std::size_t c = 0;
        while (c + 1 < columns && cell.x0 >= left[c + 1] - slack) ++c;
        const float limit = c + 1 < columns ? left[c + 1] + slack : right_edge;
        if (cell.x1 > limit) return std::nullopt;
        return c;
    };
    for (const Cell& cell : head) pieces.push_back({header.baseline, cell.x0, cell.x1, *column_of(cell), cell.text, start});

    std::vector<std::size_t> out_of_order, wraps;   // lines that go back up, lines that are not rows
    float lowest = header.baseline;
    float previous_baseline = header.baseline;
    std::size_t end = start + 1;
    for (; end < lines.size(); ++end) {
        const Line& line = lines[end];
        if (line.text.empty()) continue;
        if (line.page != header.page || kinds[end] == ParagraphKind::Code) break;
        if (std::fabs(line.size - header.size) > 0.15f * size) break;
        if (line.baseline < header.baseline - slack || line.baseline > lowest + 4.0f * line_pitch * size) break;
        const std::vector<Cell> cells = cells_of(line);
        if (cells.empty()) break;
        std::vector<Piece> found;
        for (const Cell& cell : cells) {
            const auto c = column_of(cell);
            if (!c) break;
            found.push_back({line.baseline, cell.x0, cell.x1, *c, cell.text, end});
        }
        if (found.size() != cells.size()) break;
        if (line.baseline < previous_baseline - slack) out_of_order.push_back(end);
        if (kinds[end] != ParagraphKind::Rows) wraps.push_back(end);
        previous_baseline = line.baseline;
        lowest = std::max(lowest, line.baseline);
        pieces.insert(pieces.end(), found.begin(), found.end());
    }
    // The rows: one for each piece in the first column, by height on the page.
    const auto row_starts = [&] {
        std::vector<float> starts;
        for (const Piece& p : pieces) {
            if (p.column == 0) starts.push_back(p.baseline);
        }
        std::ranges::sort(starts);
        std::vector<float> at;
        for (const float y : starts) {
            if (at.empty() || y > at.back() + 0.3f * size) at.push_back(y);
        }
        return at;
    };
    // A real row has cells beside its first one. Lines at the end that only
    // hold first-column text in the last row are the text after the table.
    for (std::vector<float> at = row_starts(); at.size() >= 2 && end > start + 1;) {
        const float last = at.back();
        const bool alone = std::ranges::none_of(pieces, [&](const Piece& p) {
            return p.column != 0 && p.baseline >= last - slack;
        });
        if (!alone) break;
        // drop the lines from the end while they belong to that last row
        const auto first_of_last = std::ranges::find_if(pieces, [&](const Piece& p) {
            return p.baseline >= last - slack;
        });
        if (first_of_last == pieces.begin()) break;
        const std::size_t keep = static_cast<std::size_t>(first_of_last - pieces.begin());
        if (std::any_of(pieces.begin() + static_cast<std::ptrdiff_t>(keep), pieces.end(),
                        [&](const Piece& p) { return p.baseline < last - slack; })) break;
        while (end > start + 1 && lines[end - 1].baseline >= last - slack) --end;
        pieces.resize(keep);
        at = row_starts();
    }
    // Lines read row by row are handled as before (rows_text).
    const auto within = [&](const std::vector<std::size_t>& at) {
        return std::ranges::any_of(at, [&](std::size_t i) { return i < end; });
    };
    if (!within(out_of_order) && !within(wraps)) return start;
    const std::vector<float> rows_at = row_starts();
    if (rows_at.size() < 2) return start;   // the header and at least one row

    // Columns the header does not show: when two pieces of one line stand in
    // the same column (a header "Access Reset" printed as one block above the
    // cells "WARZ" and "-"), the later piece starts a column of its own.
    std::vector<float> lefts = left;
    {
        std::vector<float> found;
        // The pieces of one line stand next to each other in `pieces`.
        for (std::size_t first = 0; first < pieces.size();) {
            std::size_t last = first;
            while (last + 1 < pieces.size() && pieces[last + 1].line == pieces[first].line) ++last;
            for (std::size_t i = first + 1; i <= last; ++i) {
                for (std::size_t j = first; j < i; ++j) {
                    if (pieces[j].column == pieces[i].column && pieces[i].x0 > pieces[j].x1) {
                        found.push_back(pieces[i].x0);
                        break;
                    }
                }
            }
            first = last + 1;
        }
        std::ranges::sort(found);
        for (std::size_t k = 0; k < found.size();) {
            std::size_t m = k;
            while (m + 1 < found.size() && found[m + 1] - found[k] < size) ++m;
            const float x = found[k];
            if (std::ranges::none_of(lefts, [&](float l) { return std::fabs(l - x) < size; })) lefts.push_back(x);
            k = m + 1;
        }
        std::ranges::sort(lefts);
    }
    const std::size_t all_columns = lefts.size();
    for (Piece& p : pieces) {
        std::size_t c = 0;
        while (c + 1 < all_columns && p.x0 >= lefts[c + 1] - slack) ++c;
        p.column = c;
    }

    std::vector<std::vector<std::vector<const Piece*>>> table(rows_at.size(),
                                                              std::vector<std::vector<const Piece*>>(all_columns));
    for (const Piece& p : pieces) {
        std::size_t r = 0;
        while (r + 1 < rows_at.size() && p.baseline >= rows_at[r + 1] - slack) ++r;
        table[r][p.column].push_back(&p);
    }
    out.clear();
    for (auto& row : table) {
        std::string line;
        for (std::size_t c = 0; c < all_columns; ++c) {
            std::ranges::sort(row[c], [](const Piece* a, const Piece* b) {
                return a->baseline != b->baseline ? a->baseline < b->baseline : a->x0 < b->x0;
            });
            std::string cell;
            for (const Piece* p : row[c]) join_line(cell, p->text, false);
            if (c > 0) line += " | ";
            line += cell;
        }
        if (!out.empty()) out.push_back('\n');
        if (const std::size_t first = line.find_first_not_of(' '); first != std::string::npos) {
            out += line.substr(first, line.find_last_not_of(' ') - first + 1);
        }
    }
    return end;
}

}  // namespace

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
    std::vector<std::uint8_t> rest;  // for each of `rows`: 1 when the line holds the rest of a cell
    const Line* last_row = nullptr;  // the last line of `rows` that is a row of its own
    float table_right = 0;           // right edge of the rows so far
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
        if (!rows.empty()) current.text = rows_text(rows, rest);
        rows.clear();
        rest.clear();
        last_row = nullptr;
        if (!current.text.empty()) paragraphs.push_back(std::move(current));
        current = Paragraph{};
    };
    const auto is_empty = [&] { return current.text.empty() && code.empty() && rows.empty(); };
    // A line right under a table row that starts in a column after the first
    // (the first column is empty there) and is not a row of its own holds the
    // rest of a cell that wraps. rows_text joins it to its cell.
    const auto rest_of_cell = [&](const Line& line, ParagraphKind kind) {
        if (current.kind != ParagraphKind::Rows || last_row == nullptr || previous == nullptr) return false;
        if (kind == ParagraphKind::Code || line.gaps.size() >= 2 || last_row->gap_end.empty()) return false;
        if (line.page != previous->page) return false;
        const float size = std::max(last_row->size, 1.0f);
        if (std::fabs(line.size - last_row->size) > 0.15f * size) return false;
        const float step = line.baseline - previous->baseline;
        if (step <= 0 || step > line_pitch * 1.35f * size) return false;
        if (line.x0 < last_row->gap_end.front() - 0.5f * size) return false;
        return line.x1 <= table_right + 6.0f * size;
    };

    for (std::size_t index = 0; index < lines.size(); ++index) {
        const Line& line = lines[index];
        if (line.text.empty()) continue;
        const ParagraphKind kind = kinds[index];
        const bool table_starts = current.kind != ParagraphKind::Rows || rows.empty() ||
                                  (previous != nullptr && line.page != previous->page);
        if (kind == ParagraphKind::Rows && table_starts) {
            std::string rebuilt;
            if (const std::size_t end = rebuild_table(lines, index, kinds, line_pitch, rebuilt); end > index) {
                flush();
                paragraphs.push_back(Paragraph{line.page, ParagraphKind::Rows, std::move(rebuilt)});
                index = end - 1;
                previous = nullptr;
                continue;
            }
        }
        if (rest_of_cell(line, kind)) {
            rows.push_back(&line);
            rest.push_back(1);
            previous = &line;
            continue;
        }

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
            rest.push_back(0);
            table_right = last_row == nullptr ? line.x1 : std::max(table_right, line.x1);
            last_row = &line;
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
                         int heading_page, std::size_t begin, std::size_t end, std::size_t first_line) {
        Section s;
        s.order = sections.size();
        s.first_line = std::min(first_line, lines.size());
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
                lines[begin].page, begin, end, begin);
            begin = end;
        }
        return sections;
    }

    const std::size_t first = std::min(headings.front().line_index, lines.size());
    if (first > 0) add("Front matter", {}, 1, HeadingSource::None, lines.front().page, 0, first, 0);

    std::size_t title_end = 0;   // end of the previous heading's own title lines
    for (std::size_t i = 0; i < headings.size(); ++i) {
        const Heading& h = headings[i];
        // A heading never starts inside the title lines of the one before it.
        const std::size_t begin = std::max(h.line_index + h.line_count, title_end);
        const std::size_t end = i + 1 < headings.size() ? headings[i + 1].line_index : lines.size();
        add(h.title, h.number, h.level, h.source, h.page, begin, end, std::min(h.line_index, begin));
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
