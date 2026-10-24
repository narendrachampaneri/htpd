#include "htpd/order.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <string_view>

#include "htpd/text.hpp"

namespace htpd {

namespace {

constexpr std::size_t kMinLines = 6;       // a page with fewer lines is left as it is
constexpr std::size_t kMaxLines = 2500;    // ... and so is a page with more (a drawing made of text)

Box unite(const Box& a, const Box& b) {
    return {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1)};
}

// The box of a line as the line is set: the glyphs of "nine" and of "Eggs" make boxes of different
// height, and the strips between lines must not depend on the letters.
Box box_of(const Line& l) {
    if (l.figure > 0 || l.size <= 0) return {l.x0, l.y0, l.x1, l.y1};
    return {l.x0, std::min(l.y0, l.baseline - 0.8f * l.size), l.x1, std::max(l.y1, l.baseline + 0.25f * l.size)};
}

float median(std::vector<float> v) {
    if (v.empty()) return 0;
    const auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
    std::ranges::nth_element(v, mid);
    return *mid;
}

// Hebrew, Arabic and the scripts that read from right to left: the left column is not the first there.
// Such a page is left as it is stored.
bool has_right_to_left(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        if (static_cast<unsigned char>(s[i]) < 0x80) {
            ++i;
            continue;
        }
        const char32_t cp = text::decode_utf8(s, i);
        if ((cp >= 0x0590 && cp <= 0x08FF) || (cp >= 0xFB1D && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF)) return true;
    }
    return false;
}

// A line that holds only signs - an icon from a symbol font, an arrow - and no letter or digit. It places
// nothing on the page: it stays at the line before it in the PDF, like the place of a figure.
bool only_signs(std::string_view s) {
    bool any = false;
    for (std::size_t i = 0; i < s.size();) {
        if (static_cast<unsigned char>(s[i]) < 0x80) {
            const char ch = s[i++];
            if (std::isalnum(static_cast<unsigned char>(ch))) return false;
            any = any || ch != ' ';
            continue;
        }
        const char32_t cp = text::decode_utf8(s, i);
        if (!((cp >= 0xE000 && cp <= 0xF8FF) || (cp >= 0x2190 && cp <= 0x2BFF) || (cp >= 0x1F000 && cp <= 0x1FAFF))) return false;
        any = true;
    }
    return any;
}

bool starts_lower(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const char32_t cp = text::decode_utf8(s, i);
        if (cp >= U'a' && cp <= U'z') return true;
        if (cp >= 0x00DF && cp <= 0x00FF && cp != 0x00F7) return true;   // lower-case Latin-1 letters
        if ((cp >= U'A' && cp <= U'Z') || cp > 0x7F || (cp >= U'0' && cp <= U'9')) return false;
    }
    return false;
}

// ---- a line of two columns stored as one ---------------------------------------------------------

// Cuts a line at its wide gaps into one line per piece. False when the line does not carry its gaps
// in the form this needs.
bool cut_line(const Line& line, std::vector<Line>& out) {
    const std::size_t g = line.gaps.size();
    if (g == 0 || line.gap_begin.size() != g || line.gap_end.size() != g) return false;
    std::size_t spaces = 0;
    for (const char c : line.text) spaces += c == ' ';
    if (line.space_spans.size() != spaces || line.space_begin.size() != spaces || line.space_end.size() != spaces) return false;
    std::vector<std::size_t> before(g);   // spaces before the gap's own space
    std::size_t count = 0, at = 0;
    for (std::size_t k = 0; k < g; ++k) {
        if (line.gaps[k] >= line.text.size() || line.text[line.gaps[k]] != ' ' || (k > 0 && line.gaps[k] <= line.gaps[k - 1])) return false;
        for (; at < line.gaps[k]; ++at) count += line.text[at] == ' ';
        before[k] = count;
    }
    out.clear();
    for (std::size_t k = 0; k <= g; ++k) {
        Line piece = line;
        const std::size_t from = k == 0 ? 0 : line.gaps[k - 1] + 1;
        const std::size_t to = k == g ? line.text.size() : line.gaps[k];
        const std::size_t s_from = k == 0 ? 0 : before[k - 1] + 1;
        const std::size_t s_to = k == g ? spaces : before[k];
        piece.text = line.text.substr(from, to - from);
        piece.gaps.clear();
        piece.gap_begin.clear();
        piece.gap_end.clear();
        const auto slice = [&](const std::vector<float>& v) {
            return std::vector<float>(v.begin() + static_cast<std::ptrdiff_t>(s_from), v.begin() + static_cast<std::ptrdiff_t>(s_to));
        };
        piece.space_spans = slice(line.space_spans);
        piece.space_begin = slice(line.space_begin);
        piece.space_end = slice(line.space_end);
        if (k > 0) {
            piece.x0 = line.gap_end[k - 1];
            piece.origin_x = line.gap_end[k - 1];
        }
        if (k < g) piece.x1 = line.gap_begin[k];
        piece.table = false;
        piece.continues = 0;
        if (piece.text.empty() || piece.x1 <= piece.x0) return false;
        out.push_back(std::move(piece));
    }
    return true;
}

// ---- items: what the page is cut into -------------------------------------------------------------

struct Item {
    Box b;
    std::vector<int> lines;       // the lines of the page it holds, ascending
    int seg = -1;                 // >= 0: piece `seg` of the one line in `lines`
    float size = 10;
    std::size_t words = 0;
    bool group = false;           // a table, a listing or a framed area: never cut
    bool container = false;       // a framed or shaded area
    bool table = false;           // a table or a listing
    bool cand = false;            // a line that may hold the text of two columns: cut where the columns are sure
    const Line* head = nullptr;   // its first and last line
    const Line* tail = nullptr;
};

struct Ctx {
    bool explained = false;        // a rule above has a reason to change the order of the page
    std::vector<Line>& lines;
    const OrderOptions& options;
    std::vector<std::vector<Line>> pieces;   // per line: its pieces, when it is cut
    std::vector<Item> items;
    // Lines that stay at the line before them in the PDF (the place of a figure, the words inside one).
    std::vector<std::vector<int>> after;
    std::vector<int> leading;   // ... and those before the first line of the page
    bool columns = false;       // the page has columns of running text side by side
};

bool is_listing(const Ctx& c, const Line& l) {
    return l.ocr == Line::Ocr::None && l.font < c.options.fonts.size() && c.options.fonts[l.font].monospace;
}

// A row of a table: from the tags or the rules, or set in columns that the rows around it share.
bool is_table_line(const Line& l) { return !l.cells.empty() || (l.table && !l.gaps.empty()) || l.continues != 0; }

// A line that may hold a piece of text in each of two columns (or three or four). Whether it does is
// decided by the page: the columns it would make must be running text (cut_region).
bool few_pieces(const std::vector<Line>& pieces) { return pieces.size() >= 2 && pieces.size() <= 4; }

Item text_item(int index, const Line& line, int seg) {
    Item it;
    it.b = box_of(line);
    it.lines = {index};
    it.seg = seg;
    it.size = std::max(line.size, 1.0f);
    it.words = text::word_count(line.text);
    it.head = it.tail = &line;
    return it;
}

void add_group(Ctx& c, std::vector<int> idx, Box box, std::vector<int>& owner, bool container) {
    Item it;
    it.group = true;
    it.table = !container;
    it.container = container;
    std::ranges::sort(idx);
    std::vector<float> sizes;
    for (const int i : idx) {
        box = unite(box, box_of(c.lines[static_cast<std::size_t>(i)]));
        sizes.push_back(std::max(c.lines[static_cast<std::size_t>(i)].size, 1.0f));
        it.words += text::word_count(c.lines[static_cast<std::size_t>(i)].text);
        owner[static_cast<std::size_t>(i)] = static_cast<int>(c.items.size());
    }
    it.b = box;
    it.size = std::max(median(sizes), 1.0f);
    it.head = &c.lines[static_cast<std::size_t>(idx.front())];
    it.tail = &c.lines[static_cast<std::size_t>(idx.back())];
    it.lines = std::move(idx);
    c.items.push_back(std::move(it));
}

// Builds the items of the page. False when the page is not for this step.
bool build_items(Ctx& c) {
    const std::size_t n = c.lines.size();
    for (const Line& l : c.lines) {
        // Text read by OCR has the order of the recogniser; tagged text the order of the tags; text
        // from right to left a reading order of its own.
        if (l.ocr != Line::Ocr::None || l.tag != 0 || l.artifact || has_right_to_left(l.text)) return false;
        if (l.text.empty() && l.figure == 0) return false;
    }
    std::vector<int> owner(n, -1);
    const auto center_in = [](const Line& l, const Box& b) {
        const float cx = (l.x0 + l.x1) / 2, cy = (l.y0 + l.y1) / 2;
        return cx >= b.x0 && cx <= b.x1 && cy >= b.y0 && cy <= b.y1;
    };

    // Lines that stay where the PDF has them: the place of a figure (no text), the words drawn inside a figure
    // (the labels of a diagram), a sign from a symbol font. They are no part of the page that is cut: each stays
    // at the line before it - which keeps the labels of a diagram in the order the PDF stores them.
    std::vector<Box> figures;
    for (const Line& l : c.lines) {
        if (l.figure > 0) figures.push_back(box_of(l));
    }
    c.after.assign(n, {});
    int previous_text = -1;
    for (std::size_t i = 0; i < n; ++i) {
        const Line& l = c.lines[i];
        const bool stay = l.figure > 0 || only_signs(l.text) ||
                          (!figures.empty() && !is_table_line(l) && !is_listing(c, l) &&
                           std::ranges::any_of(figures, [&](const Box& f) { return center_in(l, f); }));
        if (!stay) {
            previous_text = static_cast<int>(i);
            continue;
        }
        owner[i] = -2;
        if (previous_text >= 0) {
            c.after[static_cast<std::size_t>(previous_text)].push_back(static_cast<int>(i));
        } else {
            c.leading.push_back(static_cast<int>(i));
        }
    }
    // Lines that may hold the text of two columns (a wide gap in them): they are cut only where the page shows
    // columns of running text (cut_region), and are no table rows before that.
    std::vector<char> cand(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const Line& l = c.lines[i];
        if (owner[i] != -1 || l.gaps.empty() || l.figure != 0 || !l.cells.empty() || l.continues != 0 || is_listing(c, l)) continue;
        std::vector<Line> pieces;
        if (!cut_line(l, pieces) || !few_pieces(pieces)) continue;
        c.pieces[i] = std::move(pieces);
        cand[i] = 1;
    }
    // Tables and listings: runs of lines.
    for (std::size_t i = 0; i < n;) {
        const Line& l = c.lines[i];
        const auto protect = [&](std::size_t j) {
            return c.lines[j].figure == 0 && owner[j] == -1 && ((is_table_line(c.lines[j]) && !cand[j]) || is_listing(c, c.lines[j]));
        };
        if (!protect(i)) {
            ++i;
            continue;
        }
        std::size_t j = i;
        std::vector<int> idx;
        while (j < n && protect(j)) {
            idx.push_back(static_cast<int>(j));
            ++j;
        }
        add_group(c, std::move(idx), box_of(l), owner, false);
        i = j;
    }
    // Framed and shaded areas that hold two lines or more: the lines inside it that follow each other in
    // the PDF (lines of the area stored far apart are not moved).
    std::vector<const Box*> frames;
    const float page_area = c.options.page_width * c.options.page_height;
    for (const Box& f : c.options.frames) {
        if (f.width() < 40 || f.height() < 18 || (page_area > 0 && f.width() * f.height() > 0.6f * page_area)) continue;
        frames.push_back(&f);
    }
    std::ranges::sort(frames, [](const Box* a, const Box* b) { return a->width() * a->height() < b->width() * b->height(); });
    for (const Box* f : frames) {
        const auto inside = [&](std::size_t j) {
            const Line& l = c.lines[j];
            return owner[j] == -1 && l.figure == 0 && l.x0 >= f->x0 - 2 && l.x1 <= f->x1 + 2 && l.y0 >= f->y0 - 2 && l.y1 <= f->y1 + 2;
        };
        for (std::size_t j = 0; j < n;) {
            if (!inside(j)) {
                ++j;
                continue;
            }
            std::vector<int> idx;
            for (; j < n && inside(j); ++j) idx.push_back(static_cast<int>(j));
            if (idx.size() >= 2) add_group(c, std::move(idx), *f, owner, true);
        }
    }

    // Everything else is text; the items come in the order of the lines.
    std::vector<Item> items;
    std::vector<char> placed(c.items.size(), 0);
    for (std::size_t i = 0; i < n; ++i) {
        if (owner[i] == -2) continue;
        if (owner[i] >= 0) {
            const auto g = static_cast<std::size_t>(owner[i]);
            if (!placed[g]) {
                placed[g] = 1;
                items.push_back(std::move(c.items[g]));
            }
            continue;
        }
        items.push_back(text_item(static_cast<int>(i), c.lines[i], -1));
        items.back().cand = cand[i] != 0;
    }
    c.items = std::move(items);
    return c.items.size() >= 2;
}

// ---- the XY-cut ---------------------------------------------------------------------------------

struct Node {
    enum class Kind { Leaf, Stack, Side } kind = Kind::Leaf;
    std::vector<int> items;      // a leaf: its items
    std::vector<Node> kids;      // a stack: top to bottom; a side: left to right
};

struct Strip {
    float from, to;
};

// Running text in a column: at least three items, most of them nearly as wide as the column, four words
// or more to a line. A column of cells, numbers or names is not. `fill` and `share`: how wide, how many.
bool text_column(const Ctx& c, const std::vector<int>& ids, float fill, float share, bool groups_ok) {
    float widest = 0;
    std::vector<float> words, widths;
    for (const int id : ids) {
        const Item& it = c.items[static_cast<std::size_t>(id)];
        if (it.group) {
            if (!groups_ok) return false;
            continue;
        }
        widest = std::max(widest, it.b.width());
        widths.push_back(it.b.width());
        words.push_back(static_cast<float>(it.words));
    }
    if (words.size() < 3 || median(words) < 4.0f) return false;
    // Against the widest line of a column; against the usual line where the part holds columns side by
    // side or a title over them (groups_ok).
    const float reference = groups_ok ? median(widths) : widest;
    std::size_t filled = 0;
    for (const int id : ids) {
        const Item& it = c.items[static_cast<std::size_t>(id)];
        filled += !it.group && it.b.width() >= fill * reference;
    }
    return static_cast<float>(filled) >= share * static_cast<float>(words.size());
}

// The stretches between the items, along one axis, that are at least `least` wide. Next to a box, a table
// or a figure a stretch of a tenth of a font size is enough: the box sets itself apart.
std::vector<Strip> free_strips(const Ctx& c, const std::vector<int>& ids, bool vertical, float least, float unit) {
    struct Span {
        float lo, hi;
        bool group;
    };
    std::vector<Span> spans;
    spans.reserve(ids.size());
    for (const int id : ids) {
        const Item& it = c.items[static_cast<std::size_t>(id)];
        spans.push_back({vertical ? it.b.x0 : it.b.y0, vertical ? it.b.x1 : it.b.y1, it.group});
    }
    std::ranges::sort(spans, [](const Span& a, const Span& b) { return a.lo < b.lo; });
    std::vector<Strip> out;
    float reach = spans.front().hi;
    bool reach_group = spans.front().group;
    for (const Span& sp : spans) {
        const float gap = sp.lo - reach;
        if (gap >= least || (gap >= 0.1f * unit && (sp.group || reach_group))) out.push_back({reach, sp.lo});
        if (sp.hi > reach) {
            reach = sp.hi;
            reach_group = sp.group;
        }
    }
    return out;
}

// The pieces of a candidate line, as new items (ids).
std::vector<int> piece_items(Ctx& c, int id) {
    const int line = c.items[static_cast<std::size_t>(id)].lines.front();
    std::vector<int> out;
    const std::vector<Line>& pieces = c.pieces[static_cast<std::size_t>(line)];
    for (std::size_t k = 0; k < pieces.size(); ++k) {
        out.push_back(static_cast<int>(c.items.size()));
        c.items.push_back(text_item(line, pieces[k], static_cast<int>(k)));
    }
    return out;
}

std::vector<Strip> widest_strips(const std::vector<Strip>& strips, float& widest) {
    widest = 0;
    for (const Strip& s : strips) widest = std::max(widest, s.to - s.from);
    std::vector<Strip> out;
    for (const Strip& s : strips) {
        if (s.to - s.from >= 0.5f * widest) out.push_back(s);
    }
    return out;
}

std::vector<std::vector<int>> split_at(const Ctx& c, const std::vector<int>& ids, const std::vector<Strip>& strips, bool vertical) {
    std::vector<std::vector<int>> parts(strips.size() + 1);
    for (const int id : ids) {
        const Box& b = c.items[static_cast<std::size_t>(id)].b;
        const float at = vertical ? b.x0 : b.y0;
        std::size_t part = 0;
        while (part < strips.size() && at >= strips[part].to - 0.01f) ++part;
        parts[part].push_back(id);
    }
    return parts;
}

Node cut_region(Ctx& c, std::vector<int> ids) {
    Node node;
    if (ids.size() < 2) {
        node.items = std::move(ids);
        return node;
    }
    std::vector<float> sizes;
    for (const int id : ids) sizes.push_back(c.items[static_cast<std::size_t>(id)].size);
    const float unit = std::max(median(sizes), 6.0f);
    const float gutter = std::max(0.9f * unit, 6.0f);
    std::vector<Strip> down = free_strips(c, ids, true, gutter, unit);
    const std::vector<Strip> across = free_strips(c, ids, false, 0.5f * unit, unit);
    float widest = 0, tallest = 0;
    for (const Strip& s : across) tallest = std::max(tallest, s.to - s.from);

    // Lines that may hold the text of two columns: with every one of them cut, is there a gutter that is
    // free of text, and are the columns on both sides running text? Then the lines that stand across it
    // are two lines (the page stores its columns row by row), and the gutter is there.
    const std::size_t mark = c.items.size();
    std::vector<int> cut_ids;
    bool use_pieces = false;
    if (std::ranges::any_of(ids, [&](int id) { return c.items[static_cast<std::size_t>(id)].cand; })) {
        std::vector<std::pair<int, std::vector<int>>> made;   // candidate -> its pieces
        std::vector<int> virt;
        for (const int id : ids) {
            if (!c.items[static_cast<std::size_t>(id)].cand) {
                virt.push_back(id);
                continue;
            }
            std::vector<int> ps = piece_items(c, id);
            virt.insert(virt.end(), ps.begin(), ps.end());
            made.emplace_back(id, std::move(ps));
        }
        float unused = 0;
        const std::vector<Strip> vstrips = widest_strips(free_strips(c, virt, true, gutter, unit), unused);
        const auto crosses = [&](const Item& it) {
            return std::ranges::any_of(vstrips, [&](const Strip& s) { return it.b.x0 < s.to && it.b.x1 > s.from; });
        };
        const bool any_crossing = std::ranges::any_of(made, [&](const auto& m) { return crosses(c.items[static_cast<std::size_t>(m.first)]); });
        if (!vstrips.empty() && any_crossing) {
            std::vector<int> final_ids;
            for (const int id : ids) {
                const auto it = std::ranges::find_if(made, [&](const auto& m) { return m.first == id; });
                if (it != made.end() && crosses(c.items[static_cast<std::size_t>(id)])) {
                    final_ids.insert(final_ids.end(), it->second.begin(), it->second.end());
                } else {
                    final_ids.push_back(id);
                }
            }
            const auto parts = split_at(c, final_ids, vstrips, true);
            const bool columns = std::ranges::all_of(parts, [&](const std::vector<int>& part) { return text_column(c, part, 0.8f, 0.7f, false); });
            if (columns) {
                use_pieces = true;
                cut_ids = std::move(final_ids);
                down = vstrips;
            }
        }
    }
    if (!use_pieces) c.items.resize(mark);
    const std::vector<int>& use = use_pieces ? cut_ids : ids;

    const std::vector<Strip> down_wide = widest_strips(down, widest);
    // Columns win unless a band across is far wider than the gutter: two paragraphs that happen to end
    // at the same height are no reason to read the page in bands.
    const bool vertical = !down_wide.empty() && (across.empty() || widest * 1.5f >= tallest);
    std::vector<Strip> strips;
    if (vertical) {
        strips = down_wide;
    } else {
        // The widest bands first; the smaller ones are looked at in the parts (two columns of which one is
        // shorter would otherwise be cut where only one of them has a paragraph gap).
        for (const Strip& s : across) {
            if (s.to - s.from >= 0.6f * tallest) strips.push_back(s);
        }
        if (use_pieces) c.items.resize(mark);
    }
    if (strips.empty()) {
        node.items = std::move(ids);
        return node;
    }
    node.kind = vertical ? Node::Kind::Side : Node::Kind::Stack;
    for (auto& part : split_at(c, vertical ? use : ids, strips, vertical)) {
        if (part.empty()) continue;
        Node kid = cut_region(c, std::move(part));
        if (kid.kind == node.kind) {
            for (Node& k : kid.kids) node.kids.push_back(std::move(k));
        } else {
            node.kids.push_back(std::move(kid));
        }
    }
    if (node.kids.size() == 1) return std::move(node.kids.front());
    return node;
}

void collect(const Node& n, std::vector<int>& out) {
    if (n.kind == Node::Kind::Leaf) {
        out.insert(out.end(), n.items.begin(), n.items.end());
        return;
    }
    for (const Node& k : n.kids) collect(k, out);
}

// ---- blocks: what the page is read in ---------------------------------------------------------------

struct Block {
    std::vector<int> items;    // in reading order
    Box b;
    float size = 10;
    std::size_t lines = 0;
    bool plain = true;         // text only: no table, listing or box
    bool container = false;
    bool listing = false;      // a table or a listing
    bool footnote = false;
    const Line* head = nullptr;
    const Line* tail = nullptr;
};

Block make_block(const Ctx& c, std::vector<int> items) {
    Block b;
    std::vector<float> sizes;
    bool footnote = c.options.styles != nullptr && !items.empty();
    for (std::size_t k = 0; k < items.size(); ++k) {
        const Item& it = c.items[static_cast<std::size_t>(items[k])];
        b.b = k == 0 ? it.b : unite(b.b, it.b);
        sizes.push_back(it.size);
        b.lines += it.lines.size();
        b.plain = b.plain && !it.group;
        b.container = b.container || it.container;
        b.listing = b.listing || it.table;
        if (k == 0) b.head = it.head;
        b.tail = it.tail;
        if (footnote) {
            for (const int i : it.lines) {
                const Line& l = c.lines[static_cast<std::size_t>(i)];
                footnote = footnote && l.figure == 0 && c.options.styles->role_of(l) == Role::Footnote;
            }
        }
    }
    b.size = median(sizes);
    b.footnote = footnote && c.options.page_height > 0 && b.b.y0 > 0.5f * c.options.page_height;
    b.items = std::move(items);
    return b;
}

// The items of a subtree in the order the PDF stores them.
std::vector<int> stored_order(const Ctx& c, const Node& n) {
    std::vector<int> ids;
    collect(n, ids);
    std::ranges::sort(ids, [&](int a, int b) {
        const Item& x = c.items[static_cast<std::size_t>(a)];
        const Item& y = c.items[static_cast<std::size_t>(b)];
        return x.lines.front() != y.lines.front() ? x.lines.front() < y.lines.front() : x.seg < y.seg;
    });
    return ids;
}

struct Stat {
    Box b;
    float size = 0;
    std::size_t lines = 0;
    bool plain = true;
};

Stat stat_of(const Ctx& c, const std::vector<int>& ids) {
    Stat s;
    std::vector<float> sizes;
    for (std::size_t k = 0; k < ids.size(); ++k) {
        const Item& it = c.items[static_cast<std::size_t>(ids[k])];
        s.b = k == 0 ? it.b : unite(s.b, it.b);
        sizes.push_back(it.size);
        s.lines += it.lines.size();
        s.plain = s.plain && !it.group;
    }
    s.size = median(sizes);
    return s;
}

// Notes in the margin: a narrow column of smaller text beside a wider one. Which of the two (0, 1), or -1.
int margin_kid(const Ctx& c, const std::vector<int>& left, const std::vector<int>& right) {
    const Stat a = stat_of(c, left), b = stat_of(c, right);
    if (!a.plain || !b.plain) return -1;
    const auto margin = [](const Stat& m, const Stat& main) {
        return m.lines >= 1 && main.lines >= 4 && m.lines <= main.lines && m.b.width() <= 0.5f * main.b.width() &&
               m.size <= 0.95f * main.size;
    };
    if (margin(a, b)) return 0;
    if (margin(b, a)) return 1;
    return -1;
}

std::vector<Block> arrange(Ctx& c, const Node& n);

// The runs of a sequence of labels: (label, length).
using Runs = std::vector<std::pair<int, std::size_t>>;

// Whether every label forms one run, after the runs in `skip` are taken out.
bool one_run_each(const Runs& runs, const std::vector<std::size_t>& skip, std::size_t kids) {
    std::vector<char> seen(kids, 0);
    int last = -1;
    for (std::size_t r = 0; r < runs.size(); ++r) {
        if (std::ranges::find(skip, r) != skip.end()) continue;
        if (runs[r].first == last) continue;
        last = runs[r].first;
        if (seen[static_cast<std::size_t>(last)]) return false;
        seen[static_cast<std::size_t>(last)] = 1;
    }
    return true;
}

std::vector<Block> arrange_side(Ctx& c, const Node& n) {
    const std::size_t k = n.kids.size();
    std::vector<std::vector<int>> ids(k);
    for (std::size_t i = 0; i < k; ++i) collect(n.kids[i], ids[i]);
    if (std::ranges::count_if(ids, [&](const std::vector<int>& col) { return text_column(c, col, 0.7f, 0.6f, true); }) >= 2) c.columns = true;

    // Notes in the margin are read after the paragraph beside them, whatever the order of storage.
    if (k == 2) {
        const int m = margin_kid(c, ids[0], ids[1]);
        // ... beside running text: notes put between the rows of a table would cut it
        if (m >= 0 && text_column(c, ids[static_cast<std::size_t>(1 - m)], 0.7f, 0.6f, true)) {
            c.explained = true;
            std::vector<Block> main = arrange(c, n.kids[static_cast<std::size_t>(1 - m)]);
            std::vector<Block> notes = arrange(c, n.kids[static_cast<std::size_t>(m)]);
            std::vector<std::vector<Block>> after(main.size());
            for (Block& note : notes) {
                std::size_t at = 0;
                float best = -1e30f;
                for (std::size_t j = 0; j < main.size(); ++j) {
                    if (main[j].b.y0 <= note.b.y0 + 0.5f * note.size && main[j].b.y0 > best) {
                        best = main[j].b.y0;
                        at = j;
                    }
                }
                after[at].push_back(std::move(note));
            }
            std::vector<Block> out;
            for (std::size_t j = 0; j < main.size(); ++j) {
                out.push_back(std::move(main[j]));
                for (Block& note : after[j]) out.push_back(std::move(note));
            }
            return out;
        }
    }

    // A table or a listing among the columns: the cells of a table stored column by column, or cell by cell,
    // are put together by position later (sections.cpp), which needs the PDF's order.
    for (const auto& col : ids) {
        if (std::ranges::any_of(col, [&](int id) { return c.items[static_cast<std::size_t>(id)].table; })) {
            std::vector<Block> one;
            one.push_back(make_block(c, stored_order(c, n)));
            return one;
        }
    }

    // How the lines of the columns follow each other in the PDF.
    std::vector<std::pair<int, int>> entries;   // (line, column)
    for (std::size_t i = 0; i < k; ++i) {
        for (const int id : ids[i]) {
            for (const int line : c.items[static_cast<std::size_t>(id)].lines) entries.emplace_back(line, static_cast<int>(i));
        }
    }
    std::ranges::sort(entries);
    Runs runs;
    bool shared = false;   // one line holds text of two columns: stored row by row
    for (std::size_t e = 0; e < entries.size(); ++e) {
        if (e > 0 && entries[e].first == entries[e - 1].first) {
            shared = shared || entries[e].second != entries[e - 1].second;
            continue;
        }
        if (!runs.empty() && runs.back().first == entries[e].second) {
            ++runs.back().second;
        } else {
            runs.emplace_back(entries[e].second, 1);
        }
    }
    const auto in_order = [&](std::vector<std::size_t> order) {
        std::vector<Block> out;
        for (const std::size_t i : order) {
            for (Block& b : arrange(c, n.kids[i])) out.push_back(std::move(b));
        }
        return out;
    };
    std::vector<std::size_t> by_position(k);
    for (std::size_t i = 0; i < k; ++i) by_position[i] = i;

    if (!shared && runs.size() == k) {
        // Every column is stored in one piece: the order of the columns is the PDF's.
        std::vector<std::size_t> stored;
        for (const auto& [kid, len] : runs) {
            (void)len;
            stored.push_back(static_cast<std::size_t>(kid));
        }
        return in_order(stored);
    }
    bool go = shared;
    if (!go) {
        // A box, a caption or a figure stored away from its place: one or two small stretches that, taken
        // out, leave every column in one piece.
        std::size_t total = 0;
        for (const auto& r : runs) total += r.second;
        if (runs.size() <= 8) {
            for (std::size_t a = 0; a < runs.size() && !go; ++a) {
                for (std::size_t b = a; b < runs.size() && !go; ++b) {
                    const std::vector<std::size_t> skip = a == b ? std::vector<std::size_t>{a} : std::vector<std::size_t>{a, b};
                    std::size_t gone = 0;
                    for (const std::size_t r : skip) gone += runs[r].second;
                    if (gone * 100 <= total * 30 && one_run_each(runs, skip, k)) go = true;
                }
            }
        }
        // Lines of the columns one after the other, in turn: stored row by row, or in no order.
        if (!go) go = total >= 6 && total <= 2 * runs.size();
    }
    if (go && !shared && !std::ranges::all_of(ids, [&](const std::vector<int>& col) { return text_column(c, col, 0.7f, 0.6f, true); })) go = false;   // columns of text only
    if (go) {
        c.explained = true;
        return in_order(by_position);
    }
    // Not clear: a table stored cell by cell, blocks of the columns in turn. The PDF's order stays.
    std::vector<Block> one;
    one.push_back(make_block(c, stored_order(c, n)));
    return one;
}

std::vector<Block> arrange(Ctx& c, const Node& n) {
    switch (n.kind) {
        case Node::Kind::Leaf: {
            std::vector<Block> one;
            one.push_back(make_block(c, stored_order(c, n)));
            return one;
        }
        case Node::Kind::Stack: {
            // The blocks of a stack are read from the top down. The PDF's order is kept unless it clearly
            // says otherwise: two blocks of text (two lines or more, one of them eight words or more) that
            // are stored the other way round. A small thing - a heading, a number - stored a little off stays
            // where the PDF has it.
            struct Kid {
                std::size_t at;
                int first;
                bool big;       // eight words or more
                bool lines2;    // two lines or more
            };
            std::vector<Kid> kids;
            for (std::size_t k = 0; k < n.kids.size(); ++k) {
                std::vector<int> ids;
                collect(n.kids[k], ids);
                int first = 1 << 30, any = 1 << 30;
                std::size_t words = 0, lines = 0;
                for (const int id : ids) {
                    const Item& it = c.items[static_cast<std::size_t>(id)];
                    any = std::min(any, it.lines.front());
                    if (it.words >= 3) first = std::min(first, it.lines.front());   // a number does not place a block
                    words += it.words;
                    lines += it.lines.size();
                }
                kids.push_back({k, first < (1 << 30) ? first : any, words >= 8, lines >= 2});
            }
            bool wrong = false;
            for (std::size_t a = 0; a < kids.size() && !wrong; ++a) {   // already from the top down
                for (std::size_t b = a + 1; b < kids.size() && !wrong; ++b) {
                    wrong = kids[a].first > kids[b].first && kids[a].lines2 && kids[b].lines2 && (kids[a].big || kids[b].big);
                }
            }
            c.explained = c.explained || wrong;
            if (!wrong) {
                std::ranges::stable_sort(kids, [](const Kid& a, const Kid& b) { return a.first < b.first; });
            }
            std::vector<Block> out;
            for (const Kid& kid : kids) {
                for (Block& b : arrange(c, n.kids[kid.at])) out.push_back(std::move(b));
            }
            return out;
        }
        case Node::Kind::Side:
            return arrange_side(c, n);
    }
    return {};
}

// A box, a pull quote or a margin note that stands in the middle of a paragraph: the paragraph goes first.
// The paragraph goes on below the interruption when the block before it ends without a full stop and the
// block after it starts in lower case: a sentence that is cut in two. (Between two sentences a box may stand
// where it is: it cannot be told from a box between two paragraphs.)
std::size_t join_interrupted(const Ctx& c, std::vector<Block>& blocks, std::vector<int>& joined) {
    std::size_t moved = 0;
    for (std::size_t i = 0; i + 2 < blocks.size(); ++i) {
        const Block& a = blocks[i];
        if (!a.plain || a.lines < 2 || a.tail == nullptr) continue;
        for (std::size_t j = i + 2; j < blocks.size() && j <= i + 4; ++j) {
            const Block& b = blocks[j];
            const float size = std::max(a.size, 1.0f);
            if (!b.plain || b.head == nullptr) continue;
            if (std::fabs(a.size - b.size) > 0.08f * size || std::fabs(a.b.x0 - b.b.x0) > size || b.b.y0 < a.b.y1 - 2.0f) continue;
            if (text::ends_with_sentence_punct(a.tail->text) || !starts_lower(b.head->text)) continue;
            bool between = true;
            for (std::size_t m = i + 1; m < j && between; ++m) {
                const Block& x = blocks[m];
                const bool overlaps = std::min(x.b.x1, a.b.x1) - std::max(x.b.x0, a.b.x0) >= 0.5f * x.b.width();
                const bool lies = x.b.y0 >= a.b.y1 - 2.0f && x.b.y1 <= b.b.y0 + 2.0f;
                const bool inset = x.b.x0 - a.b.x0 >= 0.5f * size && a.b.x1 - x.b.x1 >= 0.5f * size;
                const bool unlike = x.container || !x.plain || std::fabs(x.size - a.size) > 0.08f * size || inset;
                const bool caption = x.head != nullptr && starts_like_caption(x.head->text);
                between = overlaps && lies && unlike && !x.listing && !caption;
            }
            if (!between) continue;
            std::rotate(blocks.begin() + static_cast<std::ptrdiff_t>(i) + 1, blocks.begin() + static_cast<std::ptrdiff_t>(j),
                        blocks.begin() + static_cast<std::ptrdiff_t>(j) + 1);
            ++moved;
            joined.push_back(c.items[static_cast<std::size_t>(blocks[i + 1].items.front())].lines.front());
            break;
        }
    }
    return moved;
}

}  // namespace

bool order_page(std::vector<Line>& lines, const OrderOptions& options, OrderReport& report) {
    if (lines.size() < kMinLines || lines.size() > kMaxLines) return false;
    Ctx c{.explained = false, .lines = lines, .options = options, .pieces = std::vector<std::vector<Line>>(lines.size()),
          .items = {}, .after = {}, .leading = {}, .columns = false};
    if (!build_items(c)) return false;

    std::vector<int> ids(c.items.size());
    for (std::size_t i = 0; i < ids.size(); ++i) ids[i] = static_cast<int>(i);
    const Node root = cut_region(c, std::move(ids));
    if (root.kind == Node::Kind::Leaf) return false;

    std::vector<Block> blocks = arrange(c, root);
    std::vector<int> joined;   // lines that go on the paragraph before a box that was moved behind it
    if (join_interrupted(c, blocks, joined) > 0) c.explained = true;
    // Footnotes are read last, where the page has columns (a footnote under the first column would stand
    // between the two halves of a paragraph that runs over). On a page of one column the order of the PDF stands.
    if (c.columns) {
        std::vector<Block> body, notes;
        for (Block& b : blocks) (b.footnote ? notes : body).push_back(std::move(b));
        c.explained = c.explained || (!notes.empty() && !body.empty());
        blocks = std::move(body);
        for (Block& b : notes) blocks.push_back(std::move(b));
    }

    // The lines in their new order. The pieces of a line that stay together in order are the line again.
    std::vector<const Item*> seq;
    for (const Block& b : blocks) {
        for (const int id : b.items) seq.push_back(&c.items[static_cast<std::size_t>(id)]);
    }
    struct Step {
        int line;
        int seg;   // -1: the line as it is; else the piece
    };
    std::vector<Step> plan;
    plan.reserve(lines.size());
    bool changed = false;
    std::size_t split = 0;
    const auto put = [&](int line, int seg, bool last) {
        const auto at = static_cast<std::size_t>(line);
        changed = changed || seg >= 0 || static_cast<std::size_t>(line) != plan.size();
        plan.push_back({line, seg});
        if (last) {
            for (const int m : c.after[at]) {
                changed = changed || static_cast<std::size_t>(m) != plan.size();
                plan.push_back({m, -1});
            }
        }
    };
    for (const int m : c.leading) {
        changed = changed || static_cast<std::size_t>(m) != plan.size();
        plan.push_back({m, -1});
    }
    for (std::size_t s = 0; s < seq.size(); ++s) {
        const Item& it = *seq[s];
        if (it.seg < 0) {
            for (const int i : it.lines) put(i, -1, true);
            continue;
        }
        const auto line = static_cast<std::size_t>(it.lines.front());
        const std::size_t pieces = c.pieces[line].size();
        bool whole = it.seg == 0 && s + pieces <= seq.size();
        for (std::size_t k = 1; whole && k < pieces; ++k) {
            whole = seq[s + k]->seg == static_cast<int>(k) && seq[s + k]->lines.front() == it.lines.front();
        }
        if (whole) {
            put(it.lines.front(), -1, true);
            s += pieces - 1;
        } else {
            split += it.seg == 0;
            put(it.lines.front(), it.seg, it.seg + 1 == static_cast<int>(pieces));
        }
    }
    // A change that none of the rules above explains - a block whose lines are not one after the other in the PDF - is
    // not made: the order of the PDF stands.
    if (!changed || !c.explained) return false;
    // No line may be lost or written twice, whatever the rules did: every line once, or all its pieces once.
    {
        std::vector<unsigned> seen(lines.size(), 0);
        for (const Step& step : plan) {
            unsigned& mask = seen[static_cast<std::size_t>(step.line)];
            const unsigned bit = step.seg < 0 ? 0x100u : 1u << step.seg;
            if ((mask & bit) != 0 || (mask != 0 && (bit == 0x100u || (mask & 0x100u) != 0))) return false;
            mask |= bit;
        }
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const std::size_t pieces = c.pieces[i].size();
            if (seen[i] != 0x100u && seen[i] != (1u << pieces) - 1) return false;
        }
    }
    std::vector<Line> out;
    out.reserve(plan.size());
    for (const Step& step : plan) {
        const auto line = static_cast<std::size_t>(step.line);
        out.push_back(step.seg < 0 ? std::move(lines[line]) : c.pieces[line][static_cast<std::size_t>(step.seg)]);
        out.back().joined = std::ranges::find(joined, step.line) != joined.end();
    }
    lines = std::move(out);
    ++report.pages;
    report.lines_split += split;
    if (report.page_numbers.size() < 20) report.page_numbers.push_back(lines.front().page);
    return true;
}

}  // namespace htpd
