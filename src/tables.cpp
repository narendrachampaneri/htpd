#include "htpd/tables.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>

namespace htpd {

namespace {

constexpr bool is_digit(char c) { return c >= '0' && c <= '9'; }
constexpr bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
constexpr bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
constexpr bool is_alpha(char c) { return is_lower(c) || is_upper(c); }
constexpr bool is_alnum(char c) { return is_alpha(c) || is_digit(c); }

// Appends the next line to a cell: a space, except after a hyphen that breaks a word.
void join_plain(std::string& cell, std::string_view line) {
    if (cell.empty() || line.empty()) {
        cell += line;
        return;
    }
    const std::size_t n = cell.size();
    const bool broken_at_hyphen = n > 1 && cell[n - 1] == '-' && is_alnum(cell[n - 2]) && is_alnum(line.front());
    if (!broken_at_hyphen) cell.push_back(' ');
    cell += line;
}

// A money amount or a number as the text of one word: "($1,433,600.00)", "$788,400.0", "-12,5".
// Returns the number of decimals after the last '.', or -1 when the word is not such an amount.
int amount_decimals(std::string_view w) {
    std::size_t digits = 0;
    int decimals = -1;
    for (const char c : w) {
        if (is_digit(c)) {
            ++digits;
            if (decimals >= 0) ++decimals;
        } else if (c == '.') {
            if (decimals >= 0) return -1;
            decimals = 0;
        } else if (c != ',' && c != '$' && c != '(' && c != ')' && c != '-' && c != '%') {
            return -1;
        }
    }
    return digits >= 3 ? std::max(decimals, 0) : -1;
}

}  // namespace

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
    // "US-GAAP" + "-1010": a code in capitals and digits that breaks at its own hyphen. (A minus sign
    // after a word - "range" + "-0.3" - is not one: the code must be capitals and digits only.)
    if (b.size() >= 2 && b.front() == '-' && a.size() >= 3 && has(a, is_upper) &&
        std::ranges::all_of(a, [](char c) { return is_upper(c) || is_digit(c) || c == '-'; }) &&
        std::ranges::all_of(b.substr(1), [](char c) { return is_upper(c) || is_digit(c) || c == '-'; })) {
        return true;
    }
    // An amount broken inside itself, in a column too narrow for it. "($1,433,600." + "00)" and
    // "($788,400.0" + "0)": the cents are completed; "$1,433," + "600.00": a group of three digits.
    if (a.front() == '(' || a.front() == '$') {
        const int decimals = amount_decimals(a);
        const bool tail = std::ranges::all_of(b, [](char c) { return is_digit(c) || c == ')'; }) && is_digit(b.front());
        if (decimals >= 0 && a.find('.') != std::string_view::npos && tail) {
            const auto digits = static_cast<int>(std::ranges::count_if(b, is_digit));
            if (decimals + digits == 2) return true;
        }
        if (a.back() == ',' && is_digit(a[a.size() - 2]) && amount_decimals(a) >= 0 && b.size() >= 3 &&
            std::ranges::all_of(b.substr(0, 3), is_digit)) {
            return true;
        }
    }
    if (!word_char(a.back()) || !std::ranges::all_of(b, word_char)) return false;
    // "LCD_P2" + "4", "LLWU_P" + "15", "US-GAAP-101" + "0": digits after a name with digits or "_"
    // (Numbers one under the other - "10" and "11" - are no name.)
    const bool name = has(a, [](char c) { return c == '_'; }) || (has(a, is_alpha) && has(a, is_digit));
    if (name && b.size() <= 4 && is_digit(b.front()) && std::ranges::all_of(b, is_digit)) return true;
    // "notification_payload_f" + "ield_23": both halves of one name with "_" (in lower case, or in
    // capitals: "CAN_FD_CONT" + "ROL_STATUS"). Two names of one kind ("SPI_CLK" and "SPI_MISO", one
    // word each) start the same way and are not one name.
    if (has(a, [](char c) { return c == '_'; }) && has(b, [](char c) { return c == '_'; })) {
        if (is_lower(b.front()) && is_lower(a.back())) return true;
        if (is_upper(b.front()) && is_upper(a.back()) && a.substr(0, a.find('_')) != b.substr(0, b.find('_'))) {
            return true;
        }
    }
    // "DEFAU" + "LT", "SPI2_MI" + "SO": a very short rest of a word that fills its column
    return fills && space == std::string_view::npos && a.size() >= 4 && b.size() <= 3 && next.size() == b.size() &&
           (std::ranges::all_of(b, is_lower) == std::ranges::all_of(a.substr(a.size() - 1), is_lower));
}

void join_cell_line(std::string& cell, std::string_view next, bool fills) {
    if (continues_word(cell, next, fills)) {
        cell += next;
    } else {
        join_plain(cell, next);
    }
}

// ---- lattice -------------------------------------------------------------------------------

namespace {

// A horizontal or vertical line: its extent [a, b] along its direction at position `pos`.
struct Seg {
    float pos = 0, a = 0, b = 0;
};

// Rules that lie on one line and overlap or touch become one.
std::vector<Seg> merge_segments(std::vector<Seg> segs, const LatticeOptions& o) {
    std::ranges::sort(segs, {}, &Seg::pos);
    std::vector<Seg> out;
    for (std::size_t i = 0; i < segs.size();) {
        std::size_t j = i;
        while (j + 1 < segs.size() && segs[j + 1].pos - segs[i].pos <= o.snap) ++j;
        std::vector<Seg> line(segs.begin() + static_cast<std::ptrdiff_t>(i), segs.begin() + static_cast<std::ptrdiff_t>(j) + 1);
        std::ranges::sort(line, {}, &Seg::a);
        float pos_sum = 0;
        for (const Seg& s : line) pos_sum += s.pos;
        const float pos = pos_sum / static_cast<float>(line.size());
        Seg cur{pos, line.front().a, line.front().b};
        for (std::size_t k = 1; k < line.size(); ++k) {
            if (line[k].a <= cur.b + o.reach) {
                cur.b = std::max(cur.b, line[k].b);
            } else {
                out.push_back(cur);
                cur = {pos, line[k].a, line[k].b};
            }
        }
        out.push_back(cur);
        i = j + 1;
    }
    return out;
}

// Sorted positions, those closer than `snap` made one (their mean).
std::vector<float> cluster(std::vector<float> v, float snap) {
    std::ranges::sort(v);
    std::vector<float> out;
    for (std::size_t i = 0; i < v.size();) {
        std::size_t j = i;
        float sum = v[i];
        while (j + 1 < v.size() && v[j + 1] - v[i] <= snap) sum += v[++j];
        out.push_back(sum / static_cast<float>(j - i + 1));
        i = j + 1;
    }
    return out;
}

struct Word {
    float x0, x1;
    std::size_t begin, end;   // byte range in the line's text
    std::size_t line;         // index in the page's lines
};

// The words of a line with their extent; nothing when the line does not carry the place of each
// space (then it is read only when it lies in one cell as a whole).
std::vector<Word> words_of(const Line& line, std::size_t index) {
    const std::size_t spaces = static_cast<std::size_t>(std::ranges::count(line.text, ' '));
    if (line.space_begin.size() != spaces || line.space_end.size() != spaces) return {};
    std::vector<Word> out;
    out.reserve(spaces + 1);
    float x0 = line.x0;
    std::size_t begin = 0;
    std::size_t k = 0;
    for (std::size_t i = 0; i <= line.text.size(); ++i) {
        if (i < line.text.size() && line.text[i] != ' ') continue;
        const float x1 = k < spaces ? line.space_begin[k] : line.x1;
        out.push_back({x0, x1, begin, i, index});
        if (k < spaces) x0 = line.space_end[k];
        ++k;
        begin = i + 1;
    }
    return out;
}

struct Cell {
    std::size_t col0, col1, row0, row1;   // atoms, inclusive
    std::vector<Word> words;
};

// The table one connected set of rules draws, or nothing when its grid is not consistent.
struct Grid {
    std::vector<float> xs, ys;           // grid lines, ascending
    std::vector<Cell> cells;
    Box box;
};

std::optional<Grid> make_grid(const std::vector<Seg>& hs, const std::vector<Seg>& vs, const LatticeOptions& o) {
    Grid g;
    std::vector<float> xs, ys;
    float x_min = 1e30f, x_max = -1e30f, y_min = 1e30f, y_max = -1e30f;
    for (const Seg& h : hs) {
        ys.push_back(h.pos);
        x_min = std::min(x_min, h.a);
        x_max = std::max(x_max, h.b);
        y_min = std::min(y_min, h.pos);
        y_max = std::max(y_max, h.pos);
    }
    for (const Seg& v : vs) {
        xs.push_back(v.pos);
        y_min = std::min(y_min, v.a);
        y_max = std::max(y_max, v.b);
        x_min = std::min(x_min, v.pos);
        x_max = std::max(x_max, v.pos);
    }
    xs.push_back(x_min);
    xs.push_back(x_max);
    ys.push_back(y_min);
    ys.push_back(y_max);
    g.xs = cluster(std::move(xs), o.snap);
    g.ys = cluster(std::move(ys), o.snap);
    const std::size_t cols = g.xs.size() - 1, rows = g.ys.size() - 1;
    if (cols < 2 || rows < 2 || cols * rows > o.max_cells) return std::nullopt;
    g.box = {g.xs.front(), g.ys.front(), g.xs.back(), g.ys.back()};

    // Which edges between atoms are drawn. The outer border counts as drawn.
    const auto near = [&](float a, float b) { return std::fabs(a - b) <= o.snap + 0.01f; };
    std::vector<std::vector<char>> edge_v(g.xs.size(), std::vector<char>(rows, 0));    // at xs[i], for atom row j
    std::vector<std::vector<char>> edge_h(g.ys.size(), std::vector<char>(cols, 0));    // at ys[j], for atom column i
    for (std::size_t i = 0; i < g.xs.size(); ++i) {
        for (std::size_t j = 0; j < rows; ++j) {
            if (i == 0 || i + 1 == g.xs.size()) {
                edge_v[i][j] = 1;
                continue;
            }
            for (const Seg& v : vs) {
                if (near(v.pos, g.xs[i]) && v.a <= g.ys[j] + o.reach && v.b >= g.ys[j + 1] - o.reach) {
                    edge_v[i][j] = 1;
                    break;
                }
            }
        }
    }
    for (std::size_t j = 0; j < g.ys.size(); ++j) {
        for (std::size_t i = 0; i < cols; ++i) {
            if (j == 0 || j + 1 == g.ys.size()) {
                edge_h[j][i] = 1;
                continue;
            }
            for (const Seg& h : hs) {
                if (near(h.pos, g.ys[j]) && h.a <= g.xs[i] + o.reach && h.b >= g.xs[i + 1] - o.reach) {
                    edge_h[j][i] = 1;
                    break;
                }
            }
        }
    }
    // Atoms without an edge between them are one cell.
    std::vector<std::size_t> parent(cols * rows);
    std::iota(parent.begin(), parent.end(), std::size_t{0});
    const auto find = [&](std::size_t a) {
        while (parent[a] != a) a = parent[a] = parent[parent[a]];
        return a;
    };
    const auto atom = [&](std::size_t i, std::size_t j) { return j * cols + i; };
    for (std::size_t j = 0; j < rows; ++j) {
        for (std::size_t i = 0; i < cols; ++i) {
            if (i + 1 < cols && !edge_v[i + 1][j]) parent[find(atom(i, j))] = find(atom(i + 1, j));
            if (j + 1 < rows && !edge_h[j + 1][i]) parent[find(atom(i, j))] = find(atom(i, j + 1));
        }
    }
    std::vector<long> cell_of_root(cols * rows, -1);
    for (std::size_t j = 0; j < rows; ++j) {
        for (std::size_t i = 0; i < cols; ++i) {
            const std::size_t r = find(atom(i, j));
            if (cell_of_root[r] < 0) {
                cell_of_root[r] = static_cast<long>(g.cells.size());
                g.cells.push_back({i, i, j, j, {}});
            }
            Cell& c = g.cells[static_cast<std::size_t>(cell_of_root[r])];
            c.col0 = std::min(c.col0, i);
            c.col1 = std::max(c.col1, i);
            c.row0 = std::min(c.row0, j);
            c.row1 = std::max(c.row1, j);
        }
    }
    // Every cell is a rectangle of atoms.
    std::vector<std::size_t> atoms(g.cells.size(), 0);
    for (std::size_t j = 0; j < rows; ++j) {
        for (std::size_t i = 0; i < cols; ++i) ++atoms[static_cast<std::size_t>(cell_of_root[find(atom(i, j))])];
    }
    for (std::size_t k = 0; k < g.cells.size(); ++k) {
        const Cell& c = g.cells[k];
        if (atoms[k] != (c.col1 - c.col0 + 1) * (c.row1 - c.row0 + 1)) return std::nullopt;
    }
    return g;
}

// The atom (index into xs / ys) a coordinate lies in, or none.
std::optional<std::size_t> atom_at(const std::vector<float>& lines, float v, float slack) {
    if (v < lines.front() - slack || v > lines.back() + slack) return std::nullopt;
    const auto it = std::upper_bound(lines.begin(), lines.end(), v);
    std::size_t i = static_cast<std::size_t>(it - lines.begin());
    i = i == 0 ? 0 : i - 1;
    return std::min(i, lines.size() - 2);
}

// One line of a cell: its text and where it stands.
struct CellLine {
    std::string text;
    float baseline = 0, x0 = 0, x1 = 0;
    std::vector<Word> words;
};

// The lines of a cell from top to bottom: the words of one visual line from left to right.
std::vector<CellLine> cell_lines(const Cell& c, const Grid& g, const std::vector<Line>& lines) {
    std::vector<Word> ws = c.words;
    std::ranges::stable_sort(ws, [&](const Word& x, const Word& y) { return lines[x.line].baseline < lines[y.line].baseline; });
    std::vector<CellLine> out;
    for (std::size_t first = 0; first < ws.size();) {
        std::size_t last = first + 1;
        const float size = std::max(lines[ws[first].line].size, 1.0f);
        while (last < ws.size() && lines[ws[last].line].baseline - lines[ws[first].line].baseline <= 0.5f * size) ++last;
        std::stable_sort(ws.begin() + static_cast<std::ptrdiff_t>(first), ws.begin() + static_cast<std::ptrdiff_t>(last),
                         [](const Word& x, const Word& y) { return x.x0 < y.x0; });
        CellLine cl;
        cl.baseline = lines[ws[first].line].baseline;
        cl.x0 = ws[first].x0;
        cl.x1 = ws[last - 1].x1;
        // One piece of text per run of neighbouring words of one line.
        for (std::size_t k = first; k < last; ++k) {
            const Word& w = ws[k];
            if (k > first) cl.text += ' ';
            cl.text += std::string_view(lines[w.line].text).substr(w.begin, w.end - w.begin);
            cl.words.push_back(w);
        }
        out.push_back(std::move(cl));
        first = last;
    }
    (void)g;
    return out;
}

// The text of a cell from its lines. A word broken inside itself because the cell is too narrow
// for it is joined: the line before is one word that reaches the right limit of the cell (the
// padding on the right is as wide as on the left), so the next letter did not fit.
std::string cell_text(const Cell& c, const Grid& g, std::span<const CellLine> cl, const std::vector<Line>& lines) {
    (void)lines;
    std::string text;
    const float cell_left = g.xs[c.col0], cell_right = g.xs[c.col1 + 1];
    for (std::size_t k = 0; k < cl.size(); ++k) {
        bool fills = false;
        if (k > 0 && cl[k - 1].text.find(' ') == std::string::npos) {
            const Word& head = cl[k].words.front();
            const float glyph = (head.x1 - head.x0) / static_cast<float>(std::max<std::size_t>(head.end - head.begin, 1));
            fills = cell_right - (cl[k - 1].x0 - cell_left) - cl[k - 1].x1 < glyph + 0.5f;
        }
        join_cell_line(text, cl[k].text, fills);
    }
    return text;
}

// A short number or amount: has a digit, few letters ("2.62", "-40", "100 mA", "12 %").
bool numeric_item(std::string_view t) {
    std::size_t digits = 0, letters = 0, tokens = 1;
    for (const char ch : t) {
        if (is_digit(ch)) ++digits;
        else if (is_alpha(ch)) ++letters;
        else if (ch == ' ') ++tokens;
    }
    return digits > 0 && letters <= 3 && tokens <= 3;
}

// Values stacked in the cells of one row: two or more cells with a number on each of their lines,
// all the multi-line cells of the row with the same number of lines at the same heights ("Level 1..4
// falling | 2.62 | 2.70 | 2.78"). Each line then is a row of its own. Wrapped text is never that: its
// lines are not numbers.
bool is_stack(const std::vector<const Cell*>& mine, const std::vector<std::vector<CellLine>>& lines_of) {
    std::size_t numeric_cells = 0, count = 0;
    for (std::size_t k = 0; k < mine.size(); ++k) {
        const auto& cl = lines_of[k];
        if (cl.size() < 2) continue;
        if (count != 0 && cl.size() != count) return false;
        count = cl.size();
        // A line that goes on in the next ("($1,433,600." over "00)") is a cell that wraps.
        for (std::size_t i = 1; i < cl.size(); ++i) {
            if (continues_word(cl[i - 1].text, cl[i].text, true)) return false;
        }
        if (std::ranges::all_of(cl, [](const CellLine& l) { return numeric_item(l.text); })) ++numeric_cells;
    }
    if (count < 2 || numeric_cells < 2) return false;
    // The same heights, line by line.
    for (std::size_t i = 0; i < count; ++i) {
        float lo = 1e30f, hi = -1e30f;
        for (std::size_t k = 0; k < mine.size(); ++k) {
            if (lines_of[k].size() != count) continue;
            lo = std::min(lo, lines_of[k][i].baseline);
            hi = std::max(hi, lines_of[k][i].baseline);
        }
        if (hi - lo > 2.0f) return false;
    }
    return true;
}

// The lines of the rows of a grid: one per row that holds text, with the mark of a span after the
// text of the cell that spans.
std::vector<Line> build_rows(const Grid& g, const std::vector<Line>& lines, std::int32_t id, int page,
                             std::size_t& spans, bool& unsure) {
    const std::size_t atom_rows = g.ys.size() - 1;
    std::vector<std::vector<const Cell*>> starts(atom_rows);
    for (const Cell& c : g.cells) starts[c.row0].push_back(&c);
    std::vector<Line> rows;

    // Makes the line of a row from the chosen cells: (cell, text, words of the row in it).
    struct Piece {
        const Cell* cell;
        std::string text;
        std::vector<Word> words;
    };
    const auto emit = [&](std::vector<Piece>& pieces, std::vector<std::string> cells) {
        Line line;
        line.page = page;
        line.tag_table = id;
        line.table = true;
        line.cells = std::move(cells);
        float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f, previous_right = 0;
        const Line* lead = nullptr;
        std::size_t lead_words = 0;
        bool first = true;
        for (const Piece& p : pieces) {
            if (p.words.empty()) continue;
            float cx0 = 1e30f, cx1 = -1e30f;
            for (const Word& w : p.words) {
                cx0 = std::min(cx0, w.x0);
                cx1 = std::max(cx1, w.x1);
                x0 = std::min(x0, w.x0);
                x1 = std::max(x1, w.x1);
                y0 = std::min(y0, lines[w.line].y0);
                y1 = std::max(y1, lines[w.line].y1);
            }
            const Line& source = lines[p.words.front().line];
            if (p.words.size() > lead_words) {
                lead = &source;
                lead_words = p.words.size();
            }
            if (!line.text.empty()) {
                line.gaps.push_back(static_cast<std::uint32_t>(line.text.size()));
                line.gap_begin.push_back(previous_right);
                line.gap_end.push_back(cx0);
                line.text.push_back(' ');
            }
            line.text += p.text;
            previous_right = cx1;
            if (first) {
                line.baseline = source.baseline;
                line.origin_x = source.origin_x;
                first = false;
            }
        }
        if (lead == nullptr || line.text.empty()) return;
        line.x0 = x0;
        line.x1 = x1;
        line.y0 = y0;
        line.y1 = y1;
        line.size = lead->size;
        line.font = lead->font;
        line.color = lead->color;
        line.bold = lead->bold;
        rows.push_back(std::move(line));
    };

    const std::size_t columns = g.xs.size() - 1;
    for (std::size_t r = 0; r < atom_rows; ++r) {
        std::vector<const Cell*> mine = starts[r];
        std::ranges::sort(mine, {}, &Cell::col0);
        std::vector<std::vector<CellLine>> lines_of;
        for (const Cell* c : mine) lines_of.push_back(cell_lines(*c, g, lines));

        // Two cells of four lines or more in one row, which are no stack of numbers: the lines are most likely rows whose rules the page
        // does not draw (rows drawn in blocks of four). Not this method's table.
        {
            std::size_t aligned = 0;
            for (const auto& cl : lines_of) aligned += cl.size() >= 4;
            bool tall_span_here = false;
            for (const Cell* c : mine) tall_span_here = tall_span_here || c->row1 > c->row0;
            if (aligned >= 2 && !tall_span_here && !is_stack(mine, lines_of)) {
                unsure = true;
                return rows;
            }
        }
        std::vector<Piece> pieces;
        std::vector<std::string> cells(columns);
        bool tall_span = false;
        for (const Cell* c : mine) tall_span = tall_span || c->row1 > c->row0;
        if (!tall_span && is_stack(mine, lines_of)) {
            // Each line of the stack is a row. A cell of one line goes to the row at its height.
            std::size_t count = 0;
            for (const auto& cl : lines_of) count = std::max(count, cl.size());
            std::vector<float> heights(count, 0);
            for (const auto& cl : lines_of) {
                if (cl.size() == count) {
                    for (std::size_t i = 0; i < count; ++i) heights[i] = cl[i].baseline;
                    break;
                }
            }
            std::vector<std::vector<Piece>> sub(count);
            std::vector<std::vector<std::string>> sub_cells(count, std::vector<std::string>(columns));
            for (std::size_t k = 0; k < mine.size(); ++k) {
                const auto& cl = lines_of[k];
                for (std::size_t i = 0; i < cl.size(); ++i) {
                    std::size_t to = i;
                    if (cl.size() != count) {
                        to = 0;
                        for (std::size_t j = 1; j < count; ++j) {
                            if (std::fabs(heights[j] - cl[i].baseline) < std::fabs(heights[to] - cl[i].baseline)) to = j;
                        }
                    }
                    sub_cells[to][mine[k]->col0] = cl[i].text;
                    sub[to].push_back({mine[k], cl[i].text, cl[i].words});
                }
            }
            for (std::size_t i = 0; i < count; ++i) emit(sub[i], std::move(sub_cells[i]));
            continue;
        }
        for (std::size_t k = 0; k < mine.size(); ++k) {
            const Cell& c = *mine[k];
            if (c.words.empty() && c.row0 == c.row1 && c.col0 == c.col1) continue;
            std::string text = cell_text(c, g, lines_of[k], lines);
            const std::string plain = text;
            const bool wide = c.col1 > c.col0, tall = c.row1 > c.row0;
            if (wide) text += (text.empty() ? "[" : " [") + std::string(c.col1 - c.col0, '>') + "]";
            if (tall) text += (text.empty() ? "[" : " [") + std::string(c.row1 - c.row0, '=') + "]";
            spans += wide || tall;
            cells[c.col0] = std::move(text);
            pieces.push_back({&c, plain, c.words});
        }
        if (std::ranges::all_of(cells, [](const std::string& s) { return s.empty(); })) continue;
        emit(pieces, std::move(cells));
    }
    return rows;
}

}  // namespace

LatticeReport find_lattice_tables(std::vector<Line>& lines, std::span<const Rule> rules, std::int32_t& next_id,
                                  const LatticeOptions& o) {
    LatticeReport report;
    if (rules.size() < 4 || lines.empty()) return report;

    // 1. The rules as segments, merged where they lie on one line.
    std::vector<Seg> hs, vs;
    for (const Rule& r : rules) {
        if (r.width > 6.0f) continue;   // a block of colour, not a line
        if (r.horizontal()) hs.push_back({r.y0, r.x0, r.x1});
        else vs.push_back({r.x0, r.y0, r.y1});
    }
    if (hs.size() < 2 || vs.size() < 2) return report;   // a grid needs both
    hs = merge_segments(std::move(hs), o);
    vs = merge_segments(std::move(vs), o);

    // 2. Connected sets of rules: a horizontal and a vertical rule that touch are one set.
    const std::size_t nh = hs.size(), n = hs.size() + vs.size();
    std::vector<std::size_t> parent(n);
    std::iota(parent.begin(), parent.end(), std::size_t{0});
    const auto find = [&](std::size_t a) {
        while (parent[a] != a) a = parent[a] = parent[parent[a]];
        return a;
    };
    for (std::size_t h = 0; h < nh; ++h) {
        for (std::size_t v = 0; v < vs.size(); ++v) {
            if (vs[v].pos >= hs[h].a - o.reach && vs[v].pos <= hs[h].b + o.reach && hs[h].pos >= vs[v].a - o.reach &&
                hs[h].pos <= vs[v].b + o.reach) {
                parent[find(h)] = find(nh + v);
            }
        }
    }
    std::vector<std::vector<std::size_t>> sets(n);
    for (std::size_t i = 0; i < n; ++i) sets[find(i)].push_back(i);

    // The words of every usable line.
    struct LineWords {
        std::vector<Word> words;
        bool usable = false;
    };
    std::vector<LineWords> line_words(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const Line& l = lines[i];
        if (l.figure > 0 || l.text.empty() || !l.cells.empty() ||
            l.ocr == Line::Ocr::Picture) {
            continue;
        }
        line_words[i].words = words_of(l, i);
        line_words[i].usable = true;
    }

    struct Made {
        std::size_t first_line;
        std::vector<std::size_t> covered;
        std::vector<Line> rows;
    };
    std::vector<Made> made;
    std::vector<char> taken(lines.size(), 0);

    for (const auto& set : sets) {
        std::vector<Seg> sh, sv;
        for (const std::size_t i : set) (i < nh ? sh : sv).push_back(i < nh ? hs[i] : vs[i - nh]);
        if (sh.size() < 2 || sv.size() < 2) continue;
        auto grid = make_grid(sh, sv, o);
        if (!grid) {
            ++report.left;
            continue;
        }
        Grid& g = *grid;
        const float slack = o.snap;

        // 3. The words: each into the cell its middle lies in. Every word inside the grid must go
        //    into a cell, none may cross a line of the grid, and a line is in the table as a whole.
        bool consistent = true;
        std::vector<std::size_t> covered;
        std::vector<long> atom_cell((g.xs.size() - 1) * (g.ys.size() - 1), -1);
        const std::size_t cols = g.xs.size() - 1;
        for (std::size_t k = 0; k < g.cells.size(); ++k) {
            const Cell& c = g.cells[k];
            for (std::size_t j = c.row0; j <= c.row1; ++j) {
                for (std::size_t i = c.col0; i <= c.col1; ++i) atom_cell[j * cols + i] = static_cast<long>(k);
            }
        }
        for (std::size_t li = 0; li < lines.size() && consistent; ++li) {
            const Line& l = lines[li];
            if (l.text.empty() && l.figure == 0) continue;
            const float cy = (l.y0 + l.y1) / 2;
            const bool in_y = cy >= g.box.y0 - slack && cy <= g.box.y1 + slack;
            if (!in_y) continue;
            // A line whose box overlaps the grid sideways.
            if (l.x1 < g.box.x0 - slack || l.x0 > g.box.x1 + slack) continue;
            if (taken[li]) {
                consistent = false;
                break;
            }
            if (l.figure > 0 || l.ocr == Line::Ocr::Picture || !l.cells.empty()) {
                consistent = false;
                break;
            }
            const LineWords& lw = line_words[li];
            if (!lw.usable) {
                consistent = false;
                break;
            }
            std::size_t inside = 0;
            std::vector<Word> mine;
            if (lw.words.empty()) {
                // No place for each word: the line must lie in one cell as a whole.
                const auto cx = atom_at(g.xs, (l.x0 + l.x1) / 2, slack);
                const auto cyi = atom_at(g.ys, cy, slack);
                if (!cx || !cyi || l.x0 < g.xs[*cx] - slack || l.x1 > g.xs[*cx + 1] + slack) {
                    consistent = false;
                    break;
                }
                g.cells[static_cast<std::size_t>(atom_cell[*cyi * cols + *cx])].words.push_back(
                    {l.x0, l.x1, 0, l.text.size(), li});
                covered.push_back(li);
                continue;
            }
            for (const Word& w : lw.words) {
                const auto ax = atom_at(g.xs, (w.x0 + w.x1) / 2, slack);
                const auto ay = atom_at(g.ys, cy, slack);
                if (!ax || !ay) continue;   // outside the grid
                ++inside;
                // The word must not cross a line of the grid.
                for (std::size_t i = 1; i + 1 < g.xs.size(); ++i) {
                    if (w.x0 < g.xs[i] - 1.5f && w.x1 > g.xs[i] + 1.5f) {
                        // crossing an inner line that is not drawn here is fine (inside a cell)
                        const long left = atom_cell[*ay * cols + (i - 1)], right = atom_cell[*ay * cols + i];
                        if (left != right) consistent = false;
                    }
                }
                g.cells[static_cast<std::size_t>(atom_cell[*ay * cols + *ax])].words.push_back(w);
            }
            if (inside == 0) continue;
            if (inside != lw.words.size()) {
                consistent = false;   // a line that is partly inside the grid
                break;
            }
            covered.push_back(li);
        }
        if (!consistent || covered.empty()) {
            ++report.left;
            continue;
        }
        // Most cells hold text, and at least two rows have two cells with text.
        std::size_t filled = 0;
        for (const Cell& c : g.cells) filled += !c.words.empty();
        if (static_cast<double>(filled) < o.min_filled * static_cast<double>(g.cells.size())) {
            ++report.left;
            continue;
        }
        std::size_t rich_rows = 0;
        {
            std::vector<std::size_t> per_row(g.ys.size() - 1, 0);
            for (const Cell& c : g.cells) per_row[c.row0] += !c.words.empty();
            for (const std::size_t r : per_row) rich_rows += r >= 2;
        }
        if (rich_rows < 2) {
            ++report.left;
            continue;
        }

        // 4. The rows: the text of every cell, with a mark where it spans.
        Made m;
        m.covered = covered;
        m.first_line = *std::ranges::min_element(covered);
        std::size_t spans = 0;
        bool unsure = false;
        m.rows = build_rows(g, lines, next_id, lines[m.first_line].page, spans, unsure);
        if (unsure || m.rows.size() < 2) {
            ++report.left;
            continue;
        }
        for (const std::size_t li : covered) taken[li] = 1;
        ++next_id;
        ++report.tables;
        report.rows += m.rows.size();
        report.spans += spans;
        made.push_back(std::move(m));
    }
    if (made.empty()) return report;

    // 5. The lines of the page, with the rows of each table where its first line stood.
    std::ranges::sort(made, {}, &Made::first_line);
    std::vector<Line> out;
    out.reserve(lines.size());
    std::size_t next_table = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        while (next_table < made.size() && made[next_table].first_line == i) {
            for (Line& row : made[next_table].rows) out.push_back(std::move(row));
            ++next_table;
        }
        if (!taken[i]) out.push_back(std::move(lines[i]));
    }
    lines = std::move(out);
    return report;
}


// ---- stream --------------------------------------------------------------------------------

std::vector<char> monospace_table_rows(const std::vector<Line>& lines, std::span<const char> mono) {
    std::vector<char> out(lines.size(), 0);
    const auto candidate = [&](std::size_t i) {
        const Line& l = lines[i];
        return i < mono.size() && mono[i] && l.figure == 0 && l.gaps.size() >= 2 && l.gap_begin.size() == l.gaps.size() &&
               l.gap_end.size() == l.gaps.size();
    };
    for (std::size_t i = 0; i < lines.size();) {
        if (!candidate(i)) {
            ++i;
            continue;
        }
        std::size_t j = i + 1;
        while (j < lines.size() && candidate(j) && lines[j].page == lines[i].page) {
            const float size = std::max(lines[j].size, 1.0f);
            const float step = lines[j].baseline - lines[j - 1].baseline;
            if (step <= 0.3f * size || step > 2.6f * size) break;
            ++j;
        }
        if (j - i >= 4) {
            // The first two gaps of every line have a stretch in common.
            bool aligned = true;
            for (std::size_t k = 0; k < 2 && aligned; ++k) {
                float lo = -1e30f, hi = 1e30f;
                for (std::size_t r = i; r < j; ++r) {
                    lo = std::max(lo, lines[r].gap_begin[k]);
                    hi = std::min(hi, lines[r].gap_end[k]);
                }
                aligned = lo < hi;
            }
            if (aligned) std::fill(out.begin() + static_cast<std::ptrdiff_t>(i), out.begin() + static_cast<std::ptrdiff_t>(j), char{1});
        }
        i = j;
    }
    return out;
}


std::size_t mark_row_continuations(std::vector<Line>& lines, std::size_t begin, std::size_t end,
                                   std::span<const float> cuts) {
    if (end > lines.size() || end < begin + 4 || cuts.empty() || cuts.size() > 30) return 0;
    const std::size_t columns = cuts.size() + 1;

    struct Info {
        std::vector<std::string> piece;   // the text of the line in each column
        std::size_t filled = 0;
        bool numeric = false;
    };
    std::vector<Info> info(end - begin);
    for (std::size_t i = begin; i < end; ++i) {
        Info& in = info[i - begin];
        in.piece.assign(columns, std::string());
        const Line& l = lines[i];
        std::vector<Word> ws = words_of(l, i);
        if (ws.empty() && !l.text.empty() && l.text.find(' ') == std::string::npos) ws.push_back({l.x0, l.x1, 0, l.text.size(), i});
        if (ws.empty()) {
            info[i - begin].filled = 0;
            in.numeric = true;   // not known: never a continuation
            continue;
        }
        for (const Word& w : ws) {
            const float centre = (w.x0 + w.x1) / 2;
            std::size_t c = 0;
            while (c < cuts.size() && centre > cuts[c]) ++c;
            std::string& cell = in.piece[c];
            if (!cell.empty()) cell += ' ';
            cell += std::string_view(l.text).substr(w.begin, w.end - w.begin);
        }
        for (const std::string& cell : in.piece) {
            in.filled += !cell.empty();
            in.numeric = in.numeric || (!cell.empty() && numeric_item(cell));
        }
    }

    // The distance between the lines of one cell: the smallest step between two lines of the block.
    float min_step = 1e30f;
    for (std::size_t i = begin + 1; i < end; ++i) {
        const float step = lines[i].baseline - lines[i - 1].baseline;
        if (step > 0.3f * std::max(lines[i].size, 1.0f)) min_step = std::min(min_step, step);
    }
    if (min_step > 1e29f) return 0;

    // The first column is filled in at least half of the lines that have cells, and the rows are
    // set apart: more than one step is wider than the step between two lines of one cell. (With rows
    // as close as the lines of a cell, a line without a first cell cannot be told from a row.)
    std::size_t with_cells = 0, with_first = 0, apart = 0;
    for (std::size_t i = begin; i < end; ++i) {
        const Info& in = info[i - begin];
        if (in.filled >= 2) {
            ++with_cells;
            with_first += !in.piece[0].empty();
        }
        if (i > begin && lines[i].baseline - lines[i - 1].baseline > 1.3f * min_step) ++apart;
    }
    if (with_cells < 4 || with_first * 2 < with_cells || with_first < 3 || apart < 2) return 0;

    const float tolerance = 1.15f * min_step + 0.3f;
    std::size_t marked = 0;
    for (std::size_t i = begin + 1; i < end; ++i) {
        const Info& in = info[i - begin];
        if (!in.piece[0].empty() || in.filled == 0 || in.numeric) continue;
        const float before = lines[i].baseline - lines[i - 1].baseline;
        const float after = i + 1 < end ? lines[i + 1].baseline - lines[i].baseline : 1e30f;
        if (before <= tolerance && before <= after + 0.3f) {
            // The rest of the row above. Under the header row (the first line) it must stay in the
            // columns the header fills: the second line of a header over two levels does not.
            if (i == begin + 1) {
                const Info& head = info[0];
                bool inside = true;
                for (std::size_t c = 0; c < columns; ++c) inside = inside && (in.piece[c].empty() || !head.piece[c].empty());
                if (!inside) continue;
            }
            lines[i].continues = 1;
            ++marked;
        } else if (after <= tolerance && before > after + 0.3f && i + 1 < end && !info[i + 1 - begin].piece[0].empty()) {
            lines[i].continues = 2;   // the cells begin above the row they belong to
            ++marked;
        }
    }
    return marked;
}

}  // namespace htpd
