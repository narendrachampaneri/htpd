#include "htpd/tags.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "htpd/layout.hpp"
#include "htpd/sections.hpp"
#include "htpd/text.hpp"

namespace htpd {

namespace {

// A heading longer than this many characters is a paragraph that was tagged wrongly.
constexpr std::size_t kMaxHeadingGlyphs = 200;
constexpr std::size_t kMaxColumns = 64;
// Page furniture is only taken from this share of the page height at the top and bottom.
constexpr float kArtifactBand = 0.2f;

struct Tags {
    const PageText& page;
    const PageTags& t;
    std::unordered_map<int, int> owner;   // marked-content ID -> element
    bool consistent = true;               // an ID that two elements claim makes the page's tags unreliable

    explicit Tags(const PageText& p) : page(p), t(p.tags) {
        for (std::size_t n = 0; n < t.nodes.size(); ++n) {
            for (const TagKid& kid : t.nodes[n].kids) {
                if (kid.node >= 0) continue;
                if (!owner.emplace(kid.mcid, static_cast<int>(n)).second) consistent = false;
            }
        }
    }

    const TagNode& node(int i) const { return t.nodes[static_cast<std::size_t>(i)]; }

    // The nearest element above (or at) `n` for which `pick` holds, or -1.
    template <class Pick>
    int up(int n, Pick pick) const {
        for (int guard = 0; n >= 0 && guard < 100; ++guard) {
            if (pick(node(n))) return n;
            n = node(n).parent;
        }
        return -1;
    }

    bool inside(int n, std::string_view type) const {
        return up(n, [&](const TagNode& e) { return e.type == type; }) >= 0;
    }

    // Every marked-content ID below `n`.
    void collect_mcids(int n, std::vector<int>& out, int depth = 0) const {
        if (depth > 64) return;
        for (const TagKid& kid : node(n).kids) {
            if (kid.node >= 0) collect_mcids(kid.node, out, depth + 1);
            else out.push_back(kid.mcid);
        }
    }
};

// The heading level of an element type: H1-H6 and H (0: by nesting). -1: not a heading.
int heading_type(std::string_view type) {
    if (type == "H") return 0;
    if (type.size() == 2 && type[0] == 'H' && type[1] >= '1' && type[1] <= '9') return std::min(type[1] - '0', 6);
    return -1;
}

int heading_level(const Tags& tags, int n) {
    const int declared = heading_type(tags.node(n).type);
    if (declared > 0) return declared;
    int level = 1;   // H: one level below each section (Sect) around it
    for (int p = tags.node(n).parent, guard = 0; p >= 0 && guard < 100; p = tags.node(p).parent, ++guard) {
        if (tags.node(p).type == "Sect") ++level;
    }
    return std::min(level, 6);
}

bool has_alnum(char32_t c) {
    return (c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || c > 0x7F;
}

// ---- tables -----------------------------------------------------------------

struct GridCell {
    int row = 0, col = 0, row_span = 1, col_span = 1;
    std::vector<int> mcids;
    std::vector<std::size_t> glyphs;   // indices in page.glyphs, in stored order
};

struct TableData {
    int node = -1;
    int rows = 0, columns = 0;
    std::vector<GridCell> cells;
    std::vector<char> complete;        // per row: every column is covered by a cell that starts there or above
    bool row_spans = false;            // a cell spans rows
    std::size_t first = 0, last = 0;   // the glyphs of the table lie in [first, last]
};

// The rows of one Table element as a grid; nothing when the table is malformed
// (a nested table, a row with something other than cells, absurd sizes).
std::optional<TableData> read_grid(const Tags& tags, int table) {
    std::vector<int> rows;
    bool malformed = false;
    const auto walk = [&](auto&& self, int n, int depth) -> void {
        if (depth > 16 || malformed) return;
        for (const TagKid& kid : tags.node(n).kids) {
            if (kid.node < 0) continue;
            const TagNode& e = tags.node(kid.node);
            if (e.type == "TR") rows.push_back(kid.node);
            else if (e.type == "Table") malformed = true;   // a table inside a table
            else if (e.type == "THead" || e.type == "TBody" || e.type == "TFoot" || e.type == "Sect" ||
                     e.type == "Div" || e.type == "Part" || e.type == "Art") {
                self(self, kid.node, depth + 1);
            }
        }
    };
    walk(walk, table, 0);
    if (malformed || rows.empty() || rows.size() > 5000) return std::nullopt;

    TableData data;
    data.node = table;
    data.rows = static_cast<int>(rows.size());
    std::vector<std::vector<char>> taken(rows.size());
    for (std::size_t r = 0; r < rows.size(); ++r) {
        int col = 0;
        for (const TagKid& kid : tags.node(rows[r]).kids) {
            if (kid.node < 0) return std::nullopt;                       // content straight in a row
            const TagNode& e = tags.node(kid.node);
            if (e.type != "TH" && e.type != "TD") return std::nullopt;   // something else in a row
            while (static_cast<std::size_t>(col) < taken[r].size() && taken[r][static_cast<std::size_t>(col)]) ++col;
            GridCell cell;
            cell.row = static_cast<int>(r);
            cell.col = col;
            cell.row_span = std::clamp(e.row_span, 1, data.rows - cell.row);
            cell.col_span = std::clamp(e.col_span, 1, static_cast<int>(kMaxColumns));
            if (col + cell.col_span > static_cast<int>(kMaxColumns)) return std::nullopt;
            tags.collect_mcids(kid.node, cell.mcids);
            for (int dr = 0; dr < cell.row_span; ++dr) {
                auto& row = taken[r + static_cast<std::size_t>(dr)];
                if (row.size() < static_cast<std::size_t>(col + cell.col_span)) row.resize(static_cast<std::size_t>(col + cell.col_span), 0);
                for (int dc = 0; dc < cell.col_span; ++dc) row[static_cast<std::size_t>(col + dc)] = 1;
            }
            data.columns = std::max(data.columns, col + cell.col_span);
            col += cell.col_span;
            data.cells.push_back(std::move(cell));
        }
    }
    if (data.columns < 2 || data.columns > static_cast<int>(kMaxColumns) || data.cells.empty()) return std::nullopt;
    data.complete.assign(rows.size(), 1);
    for (std::size_t r = 0; r < rows.size(); ++r) {
        taken[r].resize(static_cast<std::size_t>(data.columns), 0);
        data.complete[r] = std::ranges::all_of(taken[r], [](char c) { return c != 0; });
    }
    data.row_spans = std::ranges::any_of(data.cells, [](const GridCell& c) { return c.row_span > 1; });
    return data;
}

// The words of some glyphs of a page, laid out as lines and joined with spaces.
std::string text_of(const PageText& page, const std::vector<std::size_t>& indices) {
    PageText sub;
    sub.index = page.index;
    sub.glyphs.reserve(indices.size() * 2);
    for (std::size_t k = 0; k < indices.size(); ++k) {
        if (k > 0) {
            for (std::size_t between = indices[k - 1] + 1; between < indices[k]; ++between) {
                if (page.glyphs[between].is_space) {
                    Glyph space;
                    space.cp = U' ';
                    space.is_space = true;
                    sub.glyphs.push_back(space);
                    break;
                }
            }
        }
        Glyph g = page.glyphs[indices[k]];
        g.tag = 0;
        g.artifact = false;
        sub.glyphs.push_back(g);
    }
    std::string out;
    for (const Line& line : build_lines(sub)) {
        if (!line.text.empty()) append_wrapped_line(out, line.text, false);
    }
    return out;
}

struct Rect {
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    bool empty() const { return x1 < x0; }
    void add(const Glyph& g) {
        x0 = std::min(x0, g.x0);
        y0 = std::min(y0, g.y0);
        x1 = std::max(x1, g.x1);
        y1 = std::max(y1, g.y1);
    }
};

// Placed by the order of the tags alone, a row with fewer cells than the table
// has columns would put its values under the wrong headings: the tags often
// leave out an empty cell. Such rows are placed by their positions on the page,
// against the columns that the complete rows show; where that is not certain
// the table is left to the ordinary rules.
bool place_short_rows(TableData& table, const std::vector<Rect>& cell_box) {
    if (std::ranges::all_of(table.complete, [](char c) { return c != 0; })) return true;
    // A cell that spans rows and starts in a row that is placed by position would carry any mistake into the rows below.
    for (const GridCell& cell : table.cells) {
        if (cell.row_span > 1 && !table.complete[static_cast<std::size_t>(cell.row)]) return false;
    }
    const auto columns = static_cast<std::size_t>(table.columns);
    std::vector<Rect> extent(columns);
    for (std::size_t c = 0; c < table.cells.size(); ++c) {
        const GridCell& cell = table.cells[c];
        if (!table.complete[static_cast<std::size_t>(cell.row)] || cell_box[c].empty() || cell.col_span != 1) continue;
        Rect& e = extent[static_cast<std::size_t>(cell.col)];
        e.x0 = std::min(e.x0, cell_box[c].x0);
        e.x1 = std::max(e.x1, cell_box[c].x1);
    }
    if (std::ranges::all_of(extent, [](const Rect& e) { return e.empty(); })) return false;
    for (std::size_t c = 1; c < columns; ++c) {
        if (!extent[c].empty() && !extent[c - 1].empty() && extent[c].x0 < extent[c - 1].x1) return false;   // columns that overlap
    }
    for (std::size_t r = 0; r < table.complete.size(); ++r) {
        if (table.complete[r]) continue;
        int last_column = -1;
        for (std::size_t c = 0; c < table.cells.size(); ++c) {
            GridCell& cell = table.cells[c];
            if (cell.row != static_cast<int>(r)) continue;
            if (cell_box[c].empty()) {
                if (cell.col_span != 1) return false;
                continue;   // an empty cell stays empty wherever it is
            }
            if (cell.col_span != 1) return false;
            int best = -1;
            float best_overlap = 0, second = 0;
            for (std::size_t k = 0; k < columns; ++k) {
                if (extent[k].empty()) continue;
                const float overlap = std::min(cell_box[c].x1, extent[k].x1) - std::max(cell_box[c].x0, extent[k].x0);
                if (overlap > best_overlap) {
                    second = best_overlap;
                    best_overlap = overlap;
                    best = static_cast<int>(k);
                } else if (overlap > second) {
                    second = overlap;
                }
            }
            if (best < 0 || second * 2 > best_overlap || best <= last_column) return false;
            cell.col = best;
            last_column = best;
        }
    }
    // No two cells may cover the same place.
    std::vector<char> covered(static_cast<std::size_t>(table.rows) * columns, 0);
    for (const GridCell& cell : table.cells) {
        for (int dr = 0; dr < cell.row_span; ++dr) {
            for (int dc = 0; dc < cell.col_span; ++dc) {
                const auto at = static_cast<std::size_t>(cell.row + dr) * columns + static_cast<std::size_t>(cell.col + dc);
                if (at >= covered.size() || covered[at]++) return false;
            }
        }
    }
    return true;
}

// Checks a table against the page and, when the tags and the page agree,
// gives its rows as lines. Nothing when they do not.
std::optional<std::vector<Line>> table_rows(const Tags& tags, TableData& table, int table_id, std::size_t& spans_out) {
    const PageText& page = tags.page;
    std::unordered_map<int, std::size_t> cell_of;   // marked-content ID -> cell
    for (std::size_t c = 0; c < table.cells.size(); ++c) {
        for (const int mcid : table.cells[c].mcids) {
            if (!cell_of.emplace(mcid, c).second) return std::nullopt;   // one piece of content in two cells
        }
    }
    constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    std::vector<std::size_t> glyph_cell(page.glyphs.size(), kNone);
    table.first = kNone;
    Rect box;
    for (std::size_t i = 0; i < page.glyphs.size(); ++i) {
        const Glyph& g = page.glyphs[i];
        if (g.is_space || g.mcid < 0) continue;
        const auto it = cell_of.find(g.mcid);
        if (it == cell_of.end()) continue;
        glyph_cell[i] = it->second;
        table.cells[it->second].glyphs.push_back(i);
        if (table.first == kNone) table.first = i;
        table.last = i;
        box.add(g);
    }
    if (table.first == kNone) return std::nullopt;

    // The glyphs of the table stand together in the stored order ...
    for (std::size_t i = table.first; i <= table.last; ++i) {
        if (!page.glyphs[i].is_space && glyph_cell[i] == kNone) return std::nullopt;
    }
    // ... and nothing else lies on the table.
    for (std::size_t i = 0; i < page.glyphs.size(); ++i) {
        const Glyph& g = page.glyphs[i];
        if (g.is_space || glyph_cell[i] != kNone) continue;
        const float cx = (g.x0 + g.x1) / 2, cy = (g.y0 + g.y1) / 2;
        if (cx > box.x0 + 1 && cx < box.x1 - 1 && cy > box.y0 + 1 && cy < box.y1 - 1) return std::nullopt;
    }

    // The cells come in the order of the page: rows from top to bottom, the cells of a row from left to right.
    std::vector<Rect> cell_box(table.cells.size());
    for (std::size_t c = 0; c < table.cells.size(); ++c) {
        for (const std::size_t i : table.cells[c].glyphs) cell_box[c].add(page.glyphs[i]);
    }
    {
        float previous_top = -1e30f, previous_size = 0;
        std::size_t c = 0;
        while (c < table.cells.size()) {
            const int row = table.cells[c].row;
            float top = 1e30f, size = 0, left_edge = -1e30f;
            for (; c < table.cells.size() && table.cells[c].row == row; ++c) {
                if (cell_box[c].empty()) continue;
                const float cell_size = page.glyphs[table.cells[c].glyphs.front()].size;
                if (cell_box[c].x0 < left_edge - 0.5f * cell_size) return std::nullopt;
                left_edge = std::max(left_edge, cell_box[c].x0);
                top = std::min(top, cell_box[c].y0);
                size = std::max(size, cell_size);
            }
            if (top > 1e29f) continue;
            if (top < previous_top - 0.5f * previous_size) return std::nullopt;
            previous_top = top;
            previous_size = size;
        }
    }

    if (!place_short_rows(table, cell_box)) return std::nullopt;

    // The text of every cell, with a mark where it spans.
    std::vector<std::vector<std::string>> grid(static_cast<std::size_t>(table.rows),
                                               std::vector<std::string>(static_cast<std::size_t>(table.columns)));
    std::vector<std::string> plain(table.cells.size());
    std::size_t spans = 0;
    for (std::size_t c = 0; c < table.cells.size(); ++c) {
        const GridCell& cell = table.cells[c];
        plain[c] = text_of(page, cell.glyphs);
        std::string text = plain[c];
        if (cell.col_span > 1) text += (text.empty() ? "[" : " [") + std::string(static_cast<std::size_t>(cell.col_span - 1), '>') + "]";
        if (cell.row_span > 1) text += (text.empty() ? "[" : " [") + std::string(static_cast<std::size_t>(cell.row_span - 1), '=') + "]";
        if (cell.col_span > 1 || cell.row_span > 1) ++spans;
        grid[static_cast<std::size_t>(cell.row)][static_cast<std::size_t>(cell.col)] = std::move(text);
    }

    std::vector<Line> lines;
    for (int r = 0; r < table.rows; ++r) {
        Line line;
        line.page = page.index;
        line.tag_table = table_id;
        line.table = true;
        line.cells = std::move(grid[static_cast<std::size_t>(r)]);
        Rect row_box;
        std::vector<std::pair<int, std::size_t>> sizes;
        std::size_t bold = 0, glyphs = 0;
        std::vector<std::pair<std::uint16_t, std::size_t>> fonts;
        float previous_right = 0;
        bool first_cell = true;
        float baseline = 0;
        for (const GridCell& cell : table.cells) {
            if (cell.row != r) continue;
            const std::size_t c = static_cast<std::size_t>(&cell - table.cells.data());
            const std::string& words = plain[c];
            if (words.empty()) continue;
            if (!line.text.empty()) {
                line.gaps.push_back(static_cast<std::uint32_t>(line.text.size()));
                line.gap_begin.push_back(previous_right);
                line.gap_end.push_back(cell_box[c].x0);
                line.text.push_back(' ');
            }
            line.text += words;
            previous_right = cell_box[c].x1;
            for (const std::size_t i : cell.glyphs) {
                const Glyph& g = page.glyphs[i];
                row_box.add(g);
                if (first_cell) {
                    baseline = g.baseline;
                    line.origin_x = g.origin_x;
                    first_cell = false;
                }
                ++glyphs;
                if (g.bold) ++bold;
                const int key = static_cast<int>(std::lround(g.size * 4.0f));
                const auto s = std::ranges::find(sizes, key, &std::pair<int, std::size_t>::first);
                if (s == sizes.end()) sizes.emplace_back(key, 1); else ++s->second;
                const auto f = std::ranges::find(fonts, g.font, &std::pair<std::uint16_t, std::size_t>::first);
                if (f == fonts.end()) fonts.emplace_back(g.font, 1); else ++f->second;
            }
        }
        if (line.text.empty()) {
            // A row with no text at all: nothing to show.
            continue;
        }
        line.x0 = row_box.x0;
        line.y0 = row_box.y0;
        line.x1 = row_box.x1;
        line.y1 = row_box.y1;
        line.baseline = baseline;
        const auto best_size = std::ranges::max_element(sizes, {}, &std::pair<int, std::size_t>::second);
        line.size = static_cast<float>(best_size->first) / 4.0f;
        line.font = std::ranges::max_element(fonts, {}, &std::pair<std::uint16_t, std::size_t>::second)->first;
        line.bold = bold * 10 >= glyphs * 6;
        lines.push_back(std::move(line));
    }
    if (lines.empty()) return std::nullopt;
    spans_out = spans;
    return lines;
}

}  // namespace

TaggedPage build_tagged_lines(PageText& page, TagReport& report) {
    TaggedPage out;
    if (!page.tags.present()) {
        out.lines = build_lines(page);
        return out;
    }
    const Tags tags(page);
    ++report.pages;
    if (!tags.consistent) {
        // Two elements claim the same content: the tags of this page cannot be told apart.
        for (Glyph& g : page.glyphs) g.artifact = false;
        out.lines = build_lines(page);
        return out;
    }

    // ---- tables
    struct Found {
        std::size_t first = 0, last = 0;
        std::vector<Line> rows;
        std::size_t spans = 0;
    };
    std::vector<Found> found;
    {
        int ordinal = 0;
        for (std::size_t n = 0; n < page.tags.nodes.size(); ++n) {
            const TagNode& e = page.tags.nodes[n];
            if (e.type != "Table") continue;
            if (e.parent >= 0 && tags.inside(e.parent, "Table")) continue;   // a table inside a table
            auto grid = read_grid(tags, static_cast<int>(n));
            if (!grid) {
                ++report.tables_left;
                continue;
            }
            const int id = (page.index + 1) * 1024 + ++ordinal;
            std::size_t spans = 0;
            auto rows = table_rows(tags, *grid, id, spans);
            if (!rows) {
                ++report.tables_left;
                continue;
            }
            found.push_back({grid->first, grid->last, std::move(*rows), spans});
        }
        std::ranges::sort(found, {}, &Found::first);
        // Tables that overlap cannot both be cut out of the text.
        for (std::size_t k = 1; k < found.size();) {
            if (found[k].first <= found[k - 1].last) {
                ++report.tables_left;
                found.erase(found.begin() + static_cast<std::ptrdiff_t>(k));
            } else {
                ++k;
            }
        }
    }
    for (const Found& f : found) {
        ++report.tables;
        report.table_rows += f.rows.size();
        report.spans += f.spans;
    }
    std::vector<char> in_table(page.glyphs.size(), 0);
    for (const Found& f : found) std::fill(in_table.begin() + static_cast<std::ptrdiff_t>(f.first),
                                           in_table.begin() + static_cast<std::ptrdiff_t>(f.last) + 1, char{1});

    // ---- headings and list items: the glyphs they own get a tag
    {
        // The element each glyph belongs to as a heading or a list item, or -1.
        std::vector<int> member(page.glyphs.size(), -1);
        std::unordered_map<int, std::size_t> size_of;
        std::unordered_set<int> has_text;   // elements with a letter or a digit in them
        for (std::size_t i = 0; i < page.glyphs.size(); ++i) {
            const Glyph& g = page.glyphs[i];
            if (g.is_space || g.mcid < 0 || in_table[i] || g.artifact) continue;
            const auto owner = tags.owner.find(g.mcid);
            if (owner == tags.owner.end()) continue;
            if (tags.inside(owner->second, "Table")) continue;   // cells of a table the tags do not describe: ordinary rules
            const int e = tags.up(owner->second, [](const TagNode& n) { return heading_type(n.type) >= 0 || n.type == "LI"; });
            if (e < 0) continue;
            member[i] = e;
            ++size_of[e];
            if (has_alnum(g.cp)) has_text.insert(e);
        }
        // An element's glyphs must make one run: nothing else between its first and its last.
        std::unordered_map<int, int> runs;
        int previous = -2;
        for (std::size_t i = 0; i < page.glyphs.size(); ++i) {
            if (page.glyphs[i].is_space) continue;
            if (member[i] != previous && member[i] >= 0) ++runs[member[i]];
            previous = member[i];
        }
        std::unordered_set<int> accepted, lists;
        for (const auto& [e, glyphs] : size_of) {
            const bool heading = heading_type(tags.node(e).type) >= 0;
            const bool one_run = runs[e] == 1;
            if (!one_run || !has_text.contains(e) || (heading && glyphs > kMaxHeadingGlyphs)) {
                (heading ? report.headings_left : report.items_left) += 1;
                continue;
            }
            accepted.insert(e);
        }
        for (std::size_t i = 0; i < page.glyphs.size(); ++i) {
            if (member[i] >= 0 && accepted.contains(member[i])) {
                page.glyphs[i].tag = member[i] + 1;
            }
        }
        for (const int e : accepted) {
            if (tags.node(e).type != "LI") continue;
            ++report.list_items;
            const int list = tags.up(tags.node(e).parent, [](const TagNode& n) { return n.type == "L"; });
            if (list >= 0) lists.insert(list);
        }
        report.lists += lists.size();
    }

    // ---- lines: the text around the tables, and the tables
    const auto build = [&](std::size_t begin, std::size_t end) {
        PageText part;
        part.index = page.index;
        part.turned = page.turned;
        part.glyphs.assign(page.glyphs.begin() + static_cast<std::ptrdiff_t>(begin),
                           page.glyphs.begin() + static_cast<std::ptrdiff_t>(end));
        return build_lines(part);
    };
    if (found.empty()) {
        out.lines = build_lines(page);
    } else {
        std::size_t at = 0;
        for (Found& f : found) {
            for (Line& l : build(at, f.first)) out.lines.push_back(std::move(l));
            for (Line& l : f.rows) out.lines.push_back(std::move(l));
            at = f.last + 1;
        }
        for (Line& l : build(at, page.glyphs.size())) out.lines.push_back(std::move(l));
    }

    // ---- what the lines are
    {
        std::unordered_set<int> seen;
        std::int32_t before = 0;
        for (Line& line : out.lines) {
            if (line.tag <= 0) {
                before = 0;
                continue;
            }
            const int e = line.tag - 1;
            const int level = heading_type(tags.node(e).type);
            if (level >= 0) {
                line.tag_heading = static_cast<std::uint8_t>(heading_level(tags, e));
                if (seen.insert(e).second) ++report.headings;
            } else {
                line.tag_item = line.tag != before;
            }
            before = line.tag;
        }
    }

    return out;
}

namespace {

bool is_edge_artifact(const Line& line, float page_height) {
    if (!line.artifact || line.text.empty() || !line.cells.empty()) return false;
    const float middle = (line.y0 + line.y1) / 2;
    return middle < kArtifactBand * page_height || middle > (1 - kArtifactBand) * page_height;
}

}  // namespace

std::size_t count_artifacts(const std::vector<std::vector<Line>>& pages, std::span<const float> heights) {
    std::size_t n = 0;
    for (std::size_t p = 0; p < pages.size(); ++p) {
        for (const Line& line : pages[p]) n += is_edge_artifact(line, p < heights.size() ? heights[p] : 0.0f);
    }
    return n;
}

bool should_read_tags(TagMode mode, bool tagged, std::size_t bookmarks, int pages, std::string& why) {
    why.clear();
    if (mode == TagMode::On) return true;
    if (mode == TagMode::Off || !tagged) return false;
    if (bookmarks >= 3) why = "it has bookmarks, which give the headings";
    else if (pages > kAutoTagPages) why = std::format("it has more than {} pages", kAutoTagPages);
    return why.empty();
}

std::vector<ArtifactLine> take_artifacts(std::vector<std::vector<Line>>& pages, std::span<const float> heights) {
    std::vector<ArtifactLine> out;
    for (std::size_t p = 0; p < pages.size(); ++p) {
        const float height = p < heights.size() ? heights[p] : 0.0f;
        std::erase_if(pages[p], [&](const Line& line) {
            if (!is_edge_artifact(line, height)) return false;
            out.push_back({line.page, (line.y0 + line.y1) / 2 < height / 2, line.text});
            return true;
        });
    }
    return out;
}

void add_artifacts(FurnitureReport& report, std::span<const ArtifactLine> artifacts, std::size_t page_count) {
    if (artifacts.empty()) return;
    std::vector<std::pair<bool, std::string>> keys;   // (header, key) of each pattern added here
    std::vector<std::vector<std::string>> texts;   // the keys of the texts of each pattern
    const std::size_t first_new = report.patterns.size();
    for (const ArtifactLine& line : artifacts) {
        const std::string key = furniture_key(line.text);
        std::size_t k = 0;
        while (k < keys.size() && !(keys[k].first == line.top && keys[k].second == key)) ++k;
        if (k == keys.size()) {
            keys.emplace_back(line.top, key);
            texts.emplace_back();
            FurniturePattern pattern;
            pattern.top = line.top;
            pattern.sample = line.text;
            pattern.page_text.resize(page_count);
            report.patterns.push_back(std::move(pattern));
        }
        FurniturePattern& pattern = report.patterns[first_new + k];
        const auto page = static_cast<std::size_t>(line.page);
        if (page < pattern.page_text.size() && pattern.page_text[page].empty()) {
            pattern.page_text[page] = line.text;
            ++pattern.pages;
        }
        texts[k].push_back(key);
        ++report.removed_lines;
    }
    for (std::size_t k = 0; k < keys.size(); ++k) {
        std::ranges::sort(texts[k]);
        texts[k].erase(std::unique(texts[k].begin(), texts[k].end()), texts[k].end());
        report.patterns[first_new + k].variants = texts[k].size();
    }
    std::ranges::sort(report.patterns, [](const FurniturePattern& a, const FurniturePattern& b) {
        if (a.top != b.top) return a.top;
        if (a.pages != b.pages) return a.pages > b.pages;
        return a.sample < b.sample;
    });
}

std::vector<TagFigure> tagged_figures(const PageText& page) {
    std::vector<TagFigure> out;
    if (!page.tags.present() || page.tags.objects.empty()) return out;
    const Tags tags(page);
    if (!tags.consistent) return out;

    // The words of an element: its glyphs in the stored order.
    const auto words_of = [&](int node) {
        std::vector<int> mcids;
        tags.collect_mcids(node, mcids);
        const std::unordered_set<int> wanted(mcids.begin(), mcids.end());
        std::vector<std::size_t> glyphs;
        for (std::size_t i = 0; i < page.glyphs.size(); ++i) {
            if (!page.glyphs[i].is_space && page.glyphs[i].mcid >= 0 && wanted.contains(page.glyphs[i].mcid)) glyphs.push_back(i);
        }
        return text_of(page, glyphs);
    };

    for (std::size_t n = 0; n < page.tags.nodes.size(); ++n) {
        const TagNode& figure = page.tags.nodes[n];
        if (figure.type != "Figure" || tags.inside(figure.parent, "Table") || tags.inside(figure.parent, "Figure")) continue;
        std::vector<int> mcids;
        tags.collect_mcids(static_cast<int>(n), mcids);
        const std::unordered_set<int> wanted(mcids.begin(), mcids.end());
        Rect box;
        bool picture = false, drawn = false;
        for (const TagObject& object : page.tags.objects) {
            if (!wanted.contains(object.mcid)) continue;
            box.x0 = std::min(box.x0, object.box.x0);
            box.y0 = std::min(box.y0, object.box.y0);
            box.x1 = std::max(box.x1, object.box.x1);
            box.y1 = std::max(box.y1, object.box.y1);
            (object.kind == TagObject::Kind::Image ? picture : drawn) = true;
        }
        if (box.empty()) continue;   // nothing drawn: no figure
        for (const Glyph& g : page.glyphs) {
            if (!g.is_space && g.mcid >= 0 && wanted.contains(g.mcid)) box.add(g);
        }
        TagFigure f;
        f.box = {box.x0, box.y0, box.x1, box.y1};
        f.picture = picture && !drawn;
        // The Caption: inside the figure, or the element right before or after it.
        int caption = -1;
        for (const TagKid& kid : figure.kids) {
            if (kid.node >= 0 && tags.node(kid.node).type == "Caption") caption = kid.node;
        }
        if (caption < 0 && figure.parent >= 0) {
            const auto& siblings = tags.node(figure.parent).kids;
            for (std::size_t k = 0; k < siblings.size(); ++k) {
                if (siblings[k].node != static_cast<int>(n)) continue;
                for (const std::size_t other : {k + 1, k - 1}) {
                    if (other < siblings.size() && siblings[other].node >= 0 && tags.node(siblings[other].node).type == "Caption") {
                        caption = siblings[other].node;
                        break;
                    }
                }
            }
        }
        if (caption >= 0) {
            f.caption = words_of(caption);
            f.has_caption = !f.caption.empty();
        }
        if (!f.has_caption) f.caption = figure.alt;
        out.push_back(std::move(f));
    }
    return out;
}

std::size_t merge_tag_figures(std::vector<Figure>& found, std::span<const TagFigure> tagged, int page, float width,
                              float height, double margin_band) {
    std::size_t used = 0;
    const auto area = [](const Box& b) { return std::max(0.0f, b.width()) * std::max(0.0f, b.height()); };
    for (const TagFigure& t : tagged) {
        Figure* same = nullptr;
        for (Figure& f : found) {
            const float x = std::min(f.box.x1, t.box.x1) - std::max(f.box.x0, t.box.x0);
            const float y = std::min(f.box.y1, t.box.y1) - std::max(f.box.y0, t.box.y0);
            if (x > 0 && y > 0 && x * y >= 0.5f * std::min(area(f.box), area(t.box))) {
                same = &f;
                break;
            }
        }
        if (same != nullptr) {
            // The author's caption stands in for a made-up one ("drawing on page 3").
            if (same->caption.empty() && !t.caption.empty()) {
                same->caption = t.caption;
                ++used;
            }
            continue;
        }
        const float band = static_cast<float>(margin_band) * height;
        if (t.box.width() < 24 || t.box.height() < 14 || area(t.box) > 0.85f * width * height ||
            t.box.y1 <= band || t.box.y0 >= height - band) {
            continue;   // an icon, a frame around the page, a logo in the margin
        }
        found.push_back(Figure{page, t.box, t.picture ? Figure::Kind::Picture : Figure::Kind::Drawing, t.caption, 0});
        ++used;
    }
    std::ranges::sort(found, [](const Figure& a, const Figure& b) { return a.box.y0 < b.box.y0; });
    return used;
}

}  // namespace htpd
