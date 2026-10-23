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
    if (line.table && gaps >= 1) return true;   // its columns line up with the rows around it
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

// Whether the next line of a table cell continues the last word of the cell
// without a space: a name broken inside itself because the column is too
// narrow for it ("UART2_" / "RX", "LCD_P2" / "4", "field" / "_2",
// "($1,433,600.00" / ")"). Only when that is certain; otherwise the line
// break is a space, as everywhere. `fills` tells that the line before is one
// word as wide as its column: then a very short rest ("DEFAU" / "LT",
// "Length-Delimit" / "ed") is the end of that word.
bool continues_word(std::string_view cell, std::string_view next, bool fills) {
    const std::size_t space = cell.rfind(' ');
    const std::string_view a = space == std::string_view::npos ? cell : cell.substr(space + 1);
    const std::string_view b = next.substr(0, next.find(' '));
    if (a.empty() || b.empty()) return false;
    const auto word_char = [](char c) { return is_alnum(c) || c == '_'; };
    const auto has = [](std::string_view w, auto pred) { return std::ranges::any_of(w, pred); };
    // "UART2_" + "RX", "PTA4/" + "LLWU_P"
    if ((a.back() == '_' || a.back() == '/') && a.size() >= 2 && is_alnum(b.front())) return true;
    // "auth_srv_payload_field" + "_2", "($1,433,600.00" + ")"
    if ((b.front() == '_' && word_char(a.back())) || (b == ")" && is_digit(a.back()))) return true;
    if (!word_char(a.back()) || !std::ranges::all_of(b, word_char)) return false;
    // "LCD_P2" + "4", "LLWU_P" + "15", "US-GAAP-101" + "0": digits after a name with digits or "_"
    const bool name = has(a, [](char c) { return c == '_' || is_digit(c); });
    if (name && b.size() <= 4 && is_digit(b.front()) && std::ranges::all_of(b, is_digit)) return true;
    // "notification_payload_f" + "ield_23": both halves of one name with "_"
    if (has(a, [](char c) { return c == '_'; }) && has(b, [](char c) { return c == '_'; }) && is_lower(b.front()) &&
        is_lower(a.back())) {
        return true;
    }
    // "DEFAU" + "LT", "SPI2_MI" + "SO": a very short rest of a word that fills its column
    return fills && space == std::string_view::npos && a.size() >= 4 && b.size() <= 3 && next.size() == b.size() &&
           (std::ranges::all_of(b, is_lower) == std::ranges::all_of(a.substr(a.size() - 1), is_lower));
}

// Appends the next line of a table cell (see continues_word).
void join_cell_line(std::string& cell, std::string_view next, bool fills) {
    if (continues_word(cell, next, fills)) {
        cell += next;
    } else {
        join_line(cell, next, false);
    }
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
            join_cell_line(placed_rows[row_index][c], piece.text, false);
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

// A space between two cells is at least this many font sizes wide (columns.hpp).
constexpr float kMinGutter = 0.8f;

// A cell that runs across the start of the next column, set so tight that
// the line builder saw no column gap ("Requests votes, increments term
// counter Election timeout elapses"): it is split at the space in which that
// column starts, when the space is wider than one between words. Nothing
// when there is no such space (then it is not certain where the cell ends).
std::vector<Cell> split_at_columns(const Line& line, const Cell& cell, std::span<const float> left, float slack) {
    const std::size_t spaces = static_cast<std::size_t>(std::ranges::count(line.text, ' '));
    if (line.space_begin.size() != spaces || line.space_end.size() != spaces) return {};
    const std::string_view text = line.text;
    const std::size_t offset = static_cast<std::size_t>(cell.text.data() - text.data());
    if (offset > text.size()) return {};
    std::size_t k = static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), ' '));
    std::vector<Cell> out;
    Cell rest = cell;
    std::size_t c = 0;
    while (c + 1 < left.size() && rest.x0 >= left[c + 1] - slack) ++c;
    for (std::size_t pos = offset; pos < offset + cell.text.size(); ++pos) {
        if (text[pos] != ' ') continue;
        const std::size_t space = k++;
        if (c + 1 >= left.size() || rest.x1 <= left[c + 1] + slack) break;   // the rest fits its column
        // The column starts inside this space, and the space is wider than one between words.
        const float next_word = line.space_end[space];
        if (line.space_begin[space] > left[c + 1] + slack || next_word < left[c + 1] - slack) continue;
        if (next_word - line.space_begin[space] < kMinGutter * std::max(line.size, 1.0f)) continue;
        const std::size_t begin = static_cast<std::size_t>(rest.text.data() - text.data());
        out.push_back({rest.x0, line.space_begin[space], text.substr(begin, pos - begin)});
        rest = {next_word, cell.x1, text.substr(pos + 1, offset + cell.text.size() - pos - 1)};
        ++c;
    }
    if (out.empty()) return {};
    out.push_back(rest);
    return out;
}

// A table that the PDF stores cell by cell: after its header row come the
// lines of one cell after the other (the name, every line of a long
// description, then the short cells to the right, which stand back up beside
// the first line). Read in that order, the rows come apart. Starting at a
// header row with at least three cells, this takes the following lines as
// long as every piece of each line stays inside one column, and puts the
// rows together again by position: the rows are the bands the pieces share
// across the page (see below). Text that crosses the columns ends the
// table. Returns the end of the table (nothing when there is nothing to
// rebuild: the lines already read row by row), and the rows in `out`. Every
// piece of text is kept.
std::optional<std::size_t> rebuild_table(std::span<const Line> lines, std::size_t header_begin, std::size_t header_end,
                                         std::span<const ParagraphKind> kinds, float line_pitch, std::string& out) {
    // The header: the lines [header_begin, header_end) - one row, or a header
    // printed in pieces ("Gross" over "Revenue"). The rows follow.
    const bool band_header = header_end > header_begin + 1;
    const auto is_head = [&](std::size_t line) { return line >= header_begin && line < header_end; };
    const Line& header = lines[header_begin];
    std::vector<Cell> head;           // the columns of the header, left to right
    std::vector<std::pair<std::size_t, Cell>> head_cells;   // every cell of the header lines, with its line
    float header_top = header.baseline, header_bottom = header.baseline, header_right = header.x1;
    for (std::size_t i = header_begin; i < header_end; ++i) {
        if (lines[i].text.empty()) continue;
        const std::vector<Cell> cells = cells_of(lines[i]);
        if (cells.empty()) return std::nullopt;
        for (const Cell& cell : cells) head_cells.emplace_back(i, cell);
        header_top = std::min(header_top, lines[i].baseline);
        header_bottom = std::max(header_bottom, lines[i].baseline);
        header_right = std::max(header_right, lines[i].x1);
    }
    {
        std::vector<Cell> sorted;
        for (const auto& [i, cell] : head_cells) sorted.push_back(cell);
        std::ranges::stable_sort(sorted, {}, &Cell::x0);
        for (const Cell& cell : sorted) {
            if (band_header && !head.empty() && cell.x0 < head.back().x1) {
                head.back().x1 = std::max(head.back().x1, cell.x1);   // stacked pieces of one column
            } else {
                head.push_back(cell);
            }
        }
    }
    const std::size_t columns = head.size();
    if (columns < 3) return std::nullopt;
    const float size = std::max(header.size, 1.0f);
    const float slack = 0.5f * size;
    std::vector<float> left(columns);
    for (std::size_t c = 0; c < columns; ++c) left[c] = head[c].x0;
    for (std::size_t c = 0; c + 1 < columns; ++c) {
        if (head[c].x1 >= left[c + 1]) return std::nullopt;
    }
    // Text further right than this is not part of the table; a last column
    // wider than its heading moves it along with the rows (as rest_of_cell).
    float right_edge = std::max(header_right, left.back()) + 15.0f * size;

    struct Piece {
        float baseline, x0, x1;
        std::size_t column;
        std::string_view text;
        std::size_t line;   // index in `lines`
        bool centred = false;   // alone on its line, placed by its centre
    };
    std::vector<Piece> pieces;
    const auto column_of = [&](const Cell& cell, bool centred = false) -> std::optional<std::size_t> {
        if (cell.x0 < left[0] - slack) return std::nullopt;
        std::size_t c = 0;
        while (c + 1 < columns && cell.x0 >= left[c + 1] - slack) ++c;
        const float limit = c + 1 < columns ? left[c + 1] + slack : right_edge;
        if (cell.x1 <= limit) return c;
        // With `centred`: a cell set in the middle of its column ("See note"
        // under "Max."): its centre in the column, and clear of the header
        // text of the columns beside it.
        if (!centred) return std::nullopt;
        const float centre = (cell.x0 + cell.x1) / 2;
        std::size_t m = 0;
        while (m + 1 < columns && centre >= left[m + 1]) ++m;
        if (m == 0 || cell.x0 < head[m - 1].x1) return std::nullopt;
        if (m + 1 < columns ? cell.x1 > left[m + 1] - slack : cell.x1 > right_edge) return std::nullopt;
        return m;
    };
    for (const auto& [i, cell] : head_cells) {
        const auto c = column_of(cell);
        if (!c) return std::nullopt;
        pieces.push_back({lines[i].baseline, cell.x0, cell.x1, *c, cell.text, i});
    }

    std::vector<std::size_t> out_of_order, wraps;   // lines that go back up, lines that are not rows
    float lowest = header_bottom;
    float previous_baseline = header_bottom;
    std::size_t end = header_end;
    for (; end < lines.size(); ++end) {
        const Line& line = lines[end];
        if (line.figure > 0) break;   // a figure stands here: the table ends before it
        if (line.text.empty()) continue;
        // A monospace line stays in the table when it fits the columns (a
        // name in a cell, "CAN_FD_REG_" over "0"), like any other line.
        if (line.page != header.page) break;
        // Text larger than the header, or much smaller, is not part of the table
        // (a symbol with a subscript, "VLVW1H", is a little smaller).
        if (line.size > header.size + 0.15f * size || line.size < 0.7f * header.size) break;
        if (line.baseline < header_top - slack || line.baseline > lowest + 4.0f * line_pitch * size) break;
        const std::vector<Cell> cells = cells_of(line);
        if (cells.empty()) break;
        // Places a cell: in its column; split where the next column starts;
        // set in the middle of its column; or split at a space wider than one
        // between words into a first part placed so and the rest placed the
        // same way ("PTI6 UART2_" over "ALT1" and "ALT2"), always from left
        // to right.
        const std::size_t spaces = static_cast<std::size_t>(std::ranges::count(line.text, ' '));
        const bool spaced = line.space_begin.size() == spaces && line.space_end.size() == spaces;
        const auto place = [&](const auto& self, const Cell& cell, std::size_t from, bool alone, int depth,
                               std::vector<Piece>& into) -> bool {
            if (const auto c = column_of(cell); c && *c >= from) {
                into.push_back({line.baseline, cell.x0, cell.x1, *c, cell.text, end});
                return true;
            }
            const std::vector<Cell> parts = split_at_columns(line, cell, left, slack);
            if (!parts.empty()) {
                std::vector<Piece> got;
                std::size_t next = from;
                for (const Cell& part : parts) {
                    const auto c = column_of(part);
                    if (!c || *c < next) break;
                    got.push_back({line.baseline, part.x0, part.x1, *c, part.text, end});
                    next = *c + 1;
                }
                if (got.size() == parts.size()) {
                    into.insert(into.end(), got.begin(), got.end());
                    return true;
                }
            }
            // (A line alone placed by its centre, with nothing beside it, is a
            // heading between the rows: see below.)
            if (const auto c = column_of(cell, true); c && *c >= from) {
                into.push_back({line.baseline, cell.x0, cell.x1, *c, cell.text, end, alone});
                return true;
            }
            if (depth == 0 || !spaced) return false;
            const std::string_view text = line.text;
            const std::size_t offset = static_cast<std::size_t>(cell.text.data() - text.data());
            std::size_t k = static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), ' '));
            for (std::size_t pos = offset; pos < offset + cell.text.size(); ++pos) {
                if (text[pos] != ' ') continue;
                const std::size_t space = k++;
                if (line.space_end[space] - line.space_begin[space] < kMinGutter * size) continue;
                const Cell first{cell.x0, line.space_begin[space], text.substr(offset, pos - offset)};
                const Cell rest{line.space_end[space], cell.x1, text.substr(pos + 1, offset + cell.text.size() - pos - 1)};
                std::vector<Piece> got;
                if (self(self, first, from, false, 0, got) && self(self, rest, got.back().column + 1, false, depth - 1, got)) {
                    into.insert(into.end(), got.begin(), got.end());
                    return true;
                }
            }
            return false;
        };
        std::vector<Piece> found;
        bool fits = true;
        for (const Cell& cell : cells) {
            if (!place(place, cell, 0, cells.size() == 1, 3, found)) {
                fits = false;
                break;
            }
        }
        if (!fits) break;
        if (line.baseline < previous_baseline - slack) out_of_order.push_back(end);
        if (kinds[end] != ParagraphKind::Rows) wraps.push_back(end);
        previous_baseline = line.baseline;
        lowest = std::max(lowest, line.baseline);
        right_edge = std::max(right_edge, line.x1 + 6.0f * size);
        pieces.insert(pieces.end(), found.begin(), found.end());
    }
    // The rows, as bands across the page. Pieces in different columns whose
    // baselines lie closer than a line (a cell set in the middle of a row
    // whose other cells wrap: "US-GAAP-101" above and "0" below the line of
    // the values) belong to one row. A band with text in one column only, or
    // nothing in the first column, holds the rest of a cell that wraps: it
    // joins the band above, or the one below when that one is clearly nearer.
    // Returns for each piece the number of its row (0 = the header).
    const float link = 0.9f * size;
    struct Band {
        float top = 1e30f, bottom = -1e30f;
        std::vector<bool> columns;
        bool header = false;
        bool cells = false;   // holds a line with two or more cells: a row of its own
    };
    struct Bands {
        std::vector<Band> rows;            // from top to bottom
        std::vector<std::size_t> of;       // for each piece: its band
    };
    const auto find_bands = [&]() {
        const std::size_t n = pieces.size();
        std::vector<std::size_t> parent(n);
        for (std::size_t i = 0; i < n; ++i) parent[i] = i;
        const auto find = [&](std::size_t i) {
            while (parent[i] != i) i = parent[i] = parent[parent[i]];
            return i;
        };
        std::vector<std::size_t> order(n);
        for (std::size_t i = 0; i < n; ++i) order[i] = i;
        std::ranges::stable_sort(order, [&](std::size_t x, std::size_t y) { return pieces[x].baseline < pieces[y].baseline; });
        // The pieces of the header are one row.
        for (std::size_t i = 0; i < n; ++i) {
            if (is_head(pieces[i].line) && is_head(pieces[0].line)) parent[find(i)] = find(0);
        }
        for (std::size_t a = 0; a < n; ++a) {
            for (std::size_t b = a + 1; b < n; ++b) {
                const Piece& p = pieces[order[a]];
                const Piece& q = pieces[order[b]];
                const float d = q.baseline - p.baseline;
                if (d >= link) break;
                if (p.column != q.column || d < 0.3f * size) parent[find(order[a])] = find(order[b]);
            }
        }
        // The linked groups from top to bottom; groups whose extents overlap are one band.
        std::vector<Band> groups;
        std::vector<std::size_t> group_of(n), root_group(n, n);
        for (const std::size_t i : order) {
            const std::size_t r = find(i);
            if (root_group[r] == n) {
                root_group[r] = groups.size();
                groups.push_back({});
                groups.back().columns.assign(columns, false);
            }
            group_of[i] = root_group[r];
            Band& g = groups[group_of[i]];
            g.top = std::min(g.top, pieces[i].baseline);
            g.bottom = std::max(g.bottom, pieces[i].baseline);
            g.columns[std::min(pieces[i].column, columns - 1)] = true;
            g.header = g.header || is_head(pieces[i].line);
            g.cells = g.cells || (i > 0 && pieces[i - 1].line == pieces[i].line) ||
                      (i + 1 < n && pieces[i + 1].line == pieces[i].line);
        }
        Bands bands;
        std::vector<std::size_t> band_of_group(groups.size());
        for (std::size_t k = 0; k < groups.size(); ++k) {
            if (bands.rows.empty() || groups[k].top > bands.rows.back().bottom + 0.3f * size) {
                bands.rows.push_back(groups[k]);
            } else {
                Band& row = bands.rows.back();
                row.bottom = std::max(row.bottom, groups[k].bottom);
                for (std::size_t c = 0; c < columns; ++c) row.columns[c] = row.columns[c] || groups[k].columns[c];
                row.header = row.header || groups[k].header;
                row.cells = row.cells || groups[k].cells;
            }
            band_of_group[k] = bands.rows.size() - 1;
        }
        bands.of.resize(n);
        for (std::size_t i = 0; i < n; ++i) bands.of[i] = band_of_group[group_of[i]];
        return bands;
    };
    // A real row has cells beside its first one. Lines at the end that only
    // hold first-column text in the last band are the text after the table.
    Bands bands = find_bands();
    while (bands.rows.size() >= 2 && end > header_end) {
        const std::size_t last = bands.rows.size() - 1;
        const Band& band = bands.rows[last];
        if (band.header || std::ranges::count(band.columns, true) != 1 || !band.columns[0]) break;
        std::size_t first_line = end;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            if (bands.of[i] == last) first_line = std::min(first_line, pieces[i].line);
        }
        // only when the lines of that band are the last lines read
        bool tail = first_line >= header_end;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            if (pieces[i].line >= first_line && bands.of[i] != last) tail = false;
        }
        if (!tail) break;
        end = first_line;
        while (end > header_end && lines[end - 1].text.empty()) --end;
        std::erase_if(pieces, [&](const Piece& p) { return p.line >= first_line; });
        bands = find_bands();
    }
    // A line alone, set in the middle of a column, with nothing beside it in
    // its band and not where other text of that column starts, is a heading
    // between the rows ("Debug/Trace Registers"): the table ends before it.
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        if (!pieces[i].centred) continue;
        const std::size_t band = bands.of[i];
        if (std::ranges::count(bands.of, band) != 1) continue;
        // the next line of a cell starts where that cell starts
        const bool aligned = std::ranges::any_of(pieces, [&](const Piece& p) {
            return &p != &pieces[i] && p.column == pieces[i].column && std::fabs(p.x0 - pieces[i].x0) <= slack;
        });
        if (aligned) continue;
        const std::size_t cut = pieces[i].line;
        end = cut;
        std::erase_if(pieces, [&](const Piece& p) { return p.line >= cut; });
        bands = find_bands();
        i = static_cast<std::size_t>(-1);   // look again from the start
    }
    // A band with text in one column only, or nothing in the first column,
    // and no line with two cells, holds the rest of a cell that wraps: it
    // joins the band above, or the one below when that one is clearly nearer -
    // never one further away than two lines.
    std::vector<std::size_t> rows(pieces.size(), 0);   // for each piece the number of its row (0 = the header)
    std::size_t row_total = 0;
    {
        const std::vector<Band>& b = bands.rows;
        const auto rest = [&](const Band& band) {
            const auto filled = std::ranges::count(band.columns, true);
            return !band.header && !band.cells && (filled == 1 || !band.columns[0]);
        };
        const float reach = 2.0f * line_pitch * size;   // the next line or list item of a wrapped cell
        std::vector<std::size_t> target(b.size());
        for (std::size_t r = 0; r < b.size(); ++r) target[r] = r;
        for (std::size_t r = 1; r < b.size(); ++r) {
            if (!rest(b[r])) continue;
            const float above = b[r].top - b[r - 1].bottom;
            const bool below_exists = r + 1 < b.size() && !rest(b[r + 1]);
            const float below = below_exists ? b[r + 1].top - b[r].bottom : 1e30f;
            if (below < 0.75f * above && below <= reach) {
                target[r] = r + 1;
            } else if (above <= reach) {
                target[r] = target[r - 1];
            }
        }
        // A rest that joins the row below takes the number of that row.
        for (std::size_t r = b.size(); r-- > 0;) {
            if (target[r] == r + 1) target[r] = target[r + 1];
        }
        std::vector<std::size_t> number(b.size(), 0);
        for (std::size_t r = 0; r < b.size(); ++r) {
            if (target[r] == r) number[r] = row_total++;
        }
        for (std::size_t i = 0; i < pieces.size(); ++i) rows[i] = number[target[bands.of[i]]];
    }
    // Lines read row by row are handled as before (rows_text).
    const auto within = [&](const std::vector<std::size_t>& at) {
        return std::ranges::any_of(at, [&](std::size_t i) { return i < end; });
    };
    if (!band_header && !within(out_of_order) && !within(wraps)) return std::nullopt;
    if (row_total < 2) return std::nullopt;   // the header and at least one row

    // A header cell printed as one block over two columns ("GAAP Code Q1 ($k)"
    // over the codes and the values): the cells below it fall into two groups
    // with a free strip between them. The header is split at its space on
    // that strip, and the second part starts a column of its own.
    std::vector<float> split_lefts;
    for (std::size_t h = 0; h < pieces.size() && is_head(pieces[h].line); ++h) {
        std::vector<std::pair<float, float>> below;
        for (const Piece& p : pieces) {
            if (!is_head(p.line) && p.column == pieces[h].column) below.emplace_back(p.x0, p.x1);
        }
        std::ranges::sort(below);
        std::vector<std::pair<float, float>> groups;
        for (const auto& [x0, x1] : below) {
            if (!groups.empty() && x0 < groups.back().second + kMinGutter * size) {
                groups.back().second = std::max(groups.back().second, x1);
            } else {
                groups.emplace_back(x0, x1);
            }
        }
        if (groups.size() != 2) continue;
        const Line& line = lines[pieces[h].line];
        const std::size_t spaces = static_cast<std::size_t>(std::ranges::count(line.text, ' '));
        if (line.space_begin.size() != spaces || line.space_end.size() != spaces) continue;
        const std::string_view text = line.text;
        const std::size_t offset = static_cast<std::size_t>(pieces[h].text.data() - text.data());
        std::size_t k = static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), ' '));
        for (std::size_t pos = offset; pos < offset + pieces[h].text.size(); ++pos) {
            if (text[pos] != ' ') continue;
            const std::size_t space = k++;
            if (line.space_end[space] < groups[0].second || line.space_begin[space] > groups[1].first) continue;
            Piece second = pieces[h];
            second.x0 = line.space_end[space];
            second.text = text.substr(pos + 1, offset + pieces[h].text.size() - pos - 1);
            pieces[h].x1 = line.space_begin[space];
            pieces[h].text = text.substr(offset, pos - offset);
            split_lefts.push_back(std::min(second.x0, groups[1].first));
            pieces.insert(pieces.begin() + static_cast<std::ptrdiff_t>(h) + 1, second);
            rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(h) + 1, rows[h]);
            ++h;
            break;
        }
    }

    // Columns the header does not show: when two pieces of one line stand in
    // the same column (a header "Access Reset" printed as one block above the
    // cells "WARZ" and "-"), the later piece starts a column of its own.
    std::vector<float> lefts = left;
    {
        std::vector<float> found = split_lefts;
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
    // A piece that runs across the start of a column found here is split
    // there, as above ("VLVDH Falling low-voltage ..." under "Symbol" and
    // "Description").
    if (all_columns > columns) {
        std::vector<Piece> split;
        std::vector<std::size_t> split_rows;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            const Piece& p = pieces[i];
            std::size_t c = 0;
            while (c + 1 < all_columns && p.x0 >= lefts[c + 1] - slack) ++c;
            std::vector<Cell> parts;
            if (c + 1 < all_columns && p.x1 > lefts[c + 1] + slack) {
                parts = split_at_columns(lines[p.line], Cell{p.x0, p.x1, p.text}, lefts, slack);
            }
            if (parts.empty()) {
                split.push_back(p);
                split_rows.push_back(rows[i]);
                continue;
            }
            for (const Cell& part : parts) {
                split.push_back({p.baseline, part.x0, part.x1, p.column, part.text, p.line});
                split_rows.push_back(rows[i]);
            }
        }
        pieces = std::move(split);
        rows = std::move(split_rows);
    }
    for (Piece& p : pieces) {
        std::size_t c = 0;
        while (c + 1 < all_columns && p.x0 >= lefts[c + 1] - slack) ++c;
        if (c + 1 < all_columns && p.x1 > lefts[c + 1] + slack) {
            // set in the middle of the next column (as in column_of above)
            const float centre = (p.x0 + p.x1) / 2;
            std::size_t m = c;
            while (m + 1 < all_columns && centre >= lefts[m + 1]) ++m;
            if (m + 1 == all_columns || p.x1 <= lefts[m + 1] - slack) c = m;
        }
        p.column = c;
    }

    std::vector<std::vector<std::vector<const Piece*>>> table(row_total,
                                                              std::vector<std::vector<const Piece*>>(all_columns));
    for (std::size_t i = 0; i < pieces.size(); ++i) table[rows[i]][pieces[i].column].push_back(&pieces[i]);
    // The lines of one cell: its pieces by height (baselines within a third
    // of the size are one line), each line from left to right.
    using CellLines = std::vector<std::vector<const Piece*>>;
    const auto lines_of = [&](std::vector<const Piece*> cell) {
        std::ranges::stable_sort(cell, [](const Piece* a, const Piece* b) { return a->baseline < b->baseline; });
        CellLines out_lines;
        for (std::size_t k = 0; k < cell.size(); ++k) {
            if (k == 0 || cell[k]->baseline - cell[k - 1]->baseline >= 0.3f * size) out_lines.emplace_back();
            out_lines.back().push_back(cell[k]);
        }
        for (auto& l : out_lines) std::ranges::stable_sort(l, {}, &Piece::x0);
        return out_lines;
    };
    // A value: a number ("386", "($1,433.60)", "-0.5"), or a dash for none.
    const auto is_value = [](const std::vector<const Piece*>& line) {
        bool digit = false, dashes_only = true;
        for (const Piece* p : line) {
            std::string_view t = p->text;
            if (t == "\xE2\x80\x94" || t == "\xE2\x80\x93" || t == "-") continue;   // em dash, en dash, minus
            dashes_only = false;
            for (std::size_t i = 0; i < t.size(); ++i) {
                const char ch = t[i];
                if (is_digit(ch)) {
                    digit = true;
                } else if (std::string_view("$.,()+-%").find(ch) == std::string_view::npos) {
                    // U+2212 MINUS SIGN and U+2013 EN DASH before a number
                    if (t.substr(i).starts_with("\xE2\x88\x92") || t.substr(i).starts_with("\xE2\x80\x93")) {
                        i += 2;
                        continue;
                    }
                    return false;
                }
            }
        }
        return dashes_only ? !line.empty() : digit;
    };
    // The widest piece of each column: a word as wide as that fills its column.
    std::vector<float> widest(all_columns, 0);
    for (const Piece& p : pieces) widest[p.column] = std::max(widest[p.column], p.x1 - p.x0);
    const auto write_row = [&](const std::vector<CellLines>& cells) {
        std::string line;
        for (std::size_t c = 0; c < all_columns; ++c) {
            std::string cell;
            const std::vector<const Piece*>* before = nullptr;
            for (const auto& l : cells[c]) {
                for (std::size_t k = 0; k < l.size(); ++k) {
                    if (k == 0 && before != nullptr) {
                        const bool fills = before->size() == 1 &&
                                           before->front()->x1 - before->front()->x0 >= 0.85f * widest[c];
                        join_cell_line(cell, l[k]->text, fills);
                    } else {
                        join_line(cell, l[k]->text, false);
                    }
                }
                before = &l;
            }
            if (c > 0) line += " | ";
            line += cell;
        }
        if (!out.empty()) out.push_back('\n');
        if (const std::size_t first = line.find_first_not_of(' '); first != std::string::npos) {
            out += line.substr(first, line.find_last_not_of(' ') - first + 1);
        }
    };
    // Sub-rows: values stacked in a cell ("406" over "386" over "792") never
    // wrap; they belong to the stacked labels beside them ("• 25 °C",
    // "• -40 °C", "• 105 °C"), one by one. Only when every stacked column has
    // the same number of lines and a label column names them (above them it
    // may hold the title of the row). Nothing when that is not so.
    const auto sub_rows = [&](const std::vector<CellLines>& cells) -> std::optional<std::vector<std::vector<CellLines>>> {
        std::size_t stacked = 0, count = 0;
        bool even = true;
        for (std::size_t c = 0; c < all_columns; ++c) {
            if (cells[c].size() < 2 || !std::ranges::all_of(cells[c], is_value)) continue;
            if (count == 0) count = cells[c].size();
            even = even && cells[c].size() == count;
            ++stacked;
        }
        if (stacked == 0 || !even) return std::nullopt;
        // A label column with more lines: the lines above the last `count`
        // are the title of the row; the last ones must be list items set at
        // the same place ("• 25 °C", "• -40 °C", "• 105 °C").
        std::size_t title = 0;
        bool named = false;
        for (std::size_t c = 0; c < all_columns; ++c) {
            const CellLines& l = cells[c];
            if (l.size() <= 1 || std::ranges::all_of(l, is_value)) continue;
            if (l.size() < count) return std::nullopt;
            const std::size_t extra = l.size() - count;
            if (extra > 0) {
                if (title != 0 && title != extra) return std::nullopt;
                title = extra;
                for (std::size_t k = extra; k < l.size(); ++k) {
                    if (!starts_list_item(l[k].front()->text) ||
                        std::fabs(l[k].front()->x0 - l[extra].front()->x0) > slack) {
                        return std::nullopt;
                    }
                }
            }
            named = true;
        }
        if (!named) return std::nullopt;
        for (std::size_t c = 0; c < all_columns; ++c) {
            const CellLines& l = cells[c];
            if (std::ranges::all_of(l, is_value) && l.size() > 1 && l.size() != count) return std::nullopt;
        }
        const CellLines* values = nullptr;   // a stacked value column
        for (std::size_t c = 0; c < all_columns && values == nullptr; ++c) {
            if (cells[c].size() == count && std::ranges::all_of(cells[c], is_value)) values = &cells[c];
        }
        std::vector<std::vector<CellLines>> subs(count + (title > 0 ? 1 : 0), std::vector<CellLines>(all_columns));
        const std::size_t first_sub = title > 0 ? 1 : 0;
        for (std::size_t c = 0; c < all_columns; ++c) {
            const CellLines& l = cells[c];
            if (l.size() >= count && l.size() > 1) {
                const std::size_t extra = l.size() - count;
                for (std::size_t k = 0; k < l.size(); ++k) {
                    if (k < extra) subs[0][c].push_back(l[k]);
                    else subs[first_sub + k - extra][c].push_back(l[k]);
                }
            } else if (l.size() == 1) {
                std::size_t at = 0;   // the title, when there is one; else the nearest value
                if (title == 0) {
                    float best = 1e30f;
                    for (std::size_t k = 0; k < count; ++k) {
                        const float d = std::fabs((*values)[k].front()->baseline - l[0].front()->baseline);
                        if (d < best) {
                            best = d;
                            at = k;
                        }
                    }
                }
                subs[at][c].push_back(l[0]);
            }
        }
        return subs;
    };
    // The rows after a row, with nothing in the first column or only list
    // items as labels ("• 105 °C"), may hold its sub-rows, when the labels
    // and the values are set at different heights: tried as one group.
    const auto list_items_only = [&](const std::vector<std::vector<const Piece*>>& row) {
        for (const auto& cell : row) {
            if (cell.empty()) continue;
            // the labels: the first column with text
            return std::ranges::all_of(cell, [](const Piece* p) { return starts_list_item(p->text); });
        }
        return false;
    };
    out.clear();
    for (std::size_t r = 0; r < table.size(); ++r) {
        const auto cells_from = [&](std::size_t from, std::size_t to) {
            std::vector<CellLines> cells(all_columns);
            for (std::size_t c = 0; c < all_columns; ++c) {
                std::vector<const Piece*> all;
                for (std::size_t k = from; k < to; ++k) all.insert(all.end(), table[k][c].begin(), table[k][c].end());
                cells[c] = lines_of(all);
            }
            return cells;
        };
        std::size_t to = r + 1;
        while (to < table.size() && (table[to][0].empty() || list_items_only(table[to]))) ++to;
        if (to > r + 1) {
            if (const auto subs = sub_rows(cells_from(r, to))) {
                for (const auto& sub : *subs) write_row(sub);
                r = to - 1;
                continue;
            }
        }
        const std::vector<CellLines> cells = cells_from(r, r + 1);
        if (const auto subs = sub_rows(cells)) {
            for (const auto& sub : *subs) write_row(sub);
        } else {
            write_row(cells);
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

namespace {

// The header of a table printed in pieces (each column heading on one or
// two lines of its own: "Gross" over "Revenue", "Pin" over "Name"), read as
// the last lines of the paragraph before the first row of the table. They
// are the header when they lie close together right above that row - or
// beside it, on its line and the line below, when the row line is the rest
// of the header ("ALT0 ALT1 ... ALT7" beside "100"/"LQFP", "Pin"/"Name") -
// and stand in at least three separate blocks side by side, none running
// across another (a caption or a sentence does). Returns the lines of the
// header [first, end), or nothing.
std::optional<std::pair<std::size_t, std::size_t>> header_above(std::span<const Line> lines,
                                                                std::span<const std::size_t> text_lines,
                                                                std::size_t row, float line_pitch) {
    const Line& first_row = lines[row];
    const float size = std::max(first_row.size, 1.0f);
    std::size_t taken = 0;
    float top = 1e30f, bottom = -1e30f;
    for (std::size_t k = text_lines.size(); k-- > 0;) {
        const Line& line = lines[text_lines[k]];
        if (line.page != first_row.page) break;
        if (line.baseline < first_row.baseline - 4.0f * line_pitch * size || line.baseline > first_row.baseline + 2.0f * size) break;
        if (line.size > 1.3f * first_row.size || line.size < 0.7f * first_row.size) break;
        // one band: the pieces of the header lie close together
        if (std::max(bottom, line.baseline) - std::min(top, line.baseline) > 2.0f * size) break;
        top = std::min(top, line.baseline);
        bottom = std::max(bottom, line.baseline);
        ++taken;
    }
    if (taken < 2) return std::nullopt;
    std::size_t end = row;
    if (bottom >= first_row.baseline - 0.3f * size) {
        // beside the row: on its line and the line below, two lines of header
        const bool on_its_line = top <= first_row.baseline + 0.3f * size;
        if (!on_its_line || bottom - top < 0.8f * size) return std::nullopt;
        end = row + 1;
    }
    const std::size_t first = text_lines[text_lines.size() - taken];
    // nothing else between them and the row
    for (std::size_t i = first; i < row; ++i) {
        if (std::ranges::find(text_lines.subspan(text_lines.size() - taken), i) == text_lines.end() &&
            (lines[i].figure > 0 || !lines[i].text.empty())) {
            return std::nullopt;
        }
    }
    // blocks side by side; a cell that overlaps two others joins them, which a caption does
    std::vector<Cell> cells;
    std::vector<std::size_t> head_lines(text_lines.end() - static_cast<std::ptrdiff_t>(taken), text_lines.end());
    if (end > row) head_lines.push_back(row);
    for (const std::size_t i : head_lines) {
        const std::vector<Cell> found = cells_of(lines[i]);
        if (found.empty()) return std::nullopt;
        cells.insert(cells.end(), found.begin(), found.end());
    }
    std::ranges::stable_sort(cells, {}, &Cell::x0);
    std::size_t blocks = 0;
    float reach = -1e30f;
    for (const Cell& cell : cells) {
        if (cell.x0 >= reach) ++blocks;
        reach = std::max(reach, cell.x1);
    }
    for (const Cell& cell : cells) {
        std::size_t overlapped = 0;
        for (const Cell& other : cells) {
            if (&other != &cell && other.x0 < cell.x1 && other.x1 > cell.x0) ++overlapped;
        }
        if (overlapped >= 2) return std::nullopt;
    }
    if (blocks < 3) return std::nullopt;
    return std::pair{first, end};
}

}  // namespace

std::vector<Paragraph> build_paragraphs(std::span<const Line> lines, float line_pitch,
                                        const SectionOptions& options) {
    std::vector<Paragraph> paragraphs;
    Paragraph current;
    std::vector<const Line*> code;   // the lines of `current` when it is a code block
    std::vector<const Line*> rows;   // the lines of `current` when it holds table rows
    std::vector<std::size_t> text_lines;   // the lines of `current` when it is text (their index)
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
        text_lines.clear();
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
        if (line.figure > 0) {   // a figure: a block of its own between the paragraphs
            flush();
            paragraphs.push_back(Paragraph{line.page, ParagraphKind::Figure, std::to_string(line.figure)});
            previous = nullptr;
            continue;
        }
        if (line.text.empty()) continue;
        const ParagraphKind kind = kinds[index];
        const bool table_starts = current.kind != ParagraphKind::Rows || rows.empty() ||
                                  (previous != nullptr && line.page != previous->page);
        if (kind == ParagraphKind::Rows && table_starts) {
            std::string rebuilt;
            // The header printed in pieces above the first row: it leaves the paragraph before.
            const auto header = current.kind == ParagraphKind::Text
                                    ? header_above(lines, text_lines, index, line_pitch)
                                    : std::nullopt;
            if (header) {
                if (const auto end = rebuild_table(lines, header->first, header->second, kinds, line_pitch, rebuilt)) {
                    std::vector<std::size_t> kept(text_lines.begin(), text_lines.end());
                    std::erase_if(kept, [&](std::size_t i) { return i >= header->first; });
                    current.text.clear();
                    for (const std::size_t i : kept) join_line(current.text, lines[i].text, options.dehyphenate);
                    flush();
                    paragraphs.push_back(Paragraph{lines[header->first].page, ParagraphKind::Rows, std::move(rebuilt)});
                    index = *end - 1;
                    previous = nullptr;
                    continue;
                }
            }
            if (const auto end = rebuild_table(lines, index, index + 1, kinds, line_pitch, rebuilt)) {
                flush();
                paragraphs.push_back(Paragraph{line.page, ParagraphKind::Rows, std::move(rebuilt)});
                index = *end - 1;
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
            // The height of a line read by OCR depends on its letters ("enclave." has no
            // tall ones), so only a clear change of size starts a new paragraph there.
            const float same_size = line.ocr == Line::Ocr::Page && previous->ocr == Line::Ocr::Page ? 0.3f : 0.1f;
            if (starts_list_item(line.text) || std::fabs(line.size - previous->size) > same_size * size) {
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
            text_lines.push_back(index);
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
        for (std::size_t i = begin; i < end; ++i) {
            if (lines[i].ocr != Line::Ocr::None && (s.ocr_pages.empty() || s.ocr_pages.back() != lines[i].page)) {
                s.ocr_pages.push_back(lines[i].page);
            }
        }
        std::ranges::sort(s.ocr_pages);
        s.ocr_pages.erase(std::unique(s.ocr_pages.begin(), s.ocr_pages.end()), s.ocr_pages.end());
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
    // Only figures before the first heading (a logo on the cover): they join
    // the first section rather than make a front matter of their own.
    const bool only_figures = std::all_of(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(first),
                                          [](const Line& l) { return l.figure > 0; });
    if (first > 0 && !only_figures) add("Front matter", {}, 1, HeadingSource::None, lines.front().page, 0, first, 0);

    std::size_t title_end = 0;   // end of the previous heading's own title lines
    for (std::size_t i = 0; i < headings.size(); ++i) {
        const Heading& h = headings[i];
        // A heading never starts inside the title lines of the one before it.
        const std::size_t begin = std::max(h.line_index + h.line_count, title_end);
        const std::size_t end = i + 1 < headings.size() ? headings[i + 1].line_index : lines.size();
        add(h.title, h.number, h.level, h.source, h.page, begin, end, std::min(h.line_index, begin));
        title_end = begin;
    }

    if (first > 0 && only_figures && !sections.empty()) {
        Section& s0 = sections.front();
        std::vector<Paragraph> lead = build_paragraphs(lines.subspan(0, first), pitch, options);
        s0.page_start = std::min(s0.page_start, lines.front().page);
        s0.paragraphs.insert(s0.paragraphs.begin(), std::make_move_iterator(lead.begin()),
                             std::make_move_iterator(lead.end()));
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
