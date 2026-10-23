#include "htpd/figures.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>
#include <optional>

#include "htpd/text.hpp"

namespace htpd {

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// "<word> <number>" at the start of a line, then the end, a separator or a capital.
bool starts_with_numbered(std::string_view s, std::initializer_list<std::string_view> words) {
    for (const std::string_view w : words) {
        if (!s.starts_with(w)) continue;
        std::string_view rest = s.substr(w.size());
        if (rest.empty() || (rest[0] != ' ' && !is_digit(rest[0]))) continue;
        while (!rest.empty() && rest[0] == ' ') rest.remove_prefix(1);
        std::size_t i = 0;
        if (i < rest.size() && rest[i] >= 'A' && rest[i] <= 'Z' && i + 1 < rest.size() &&
            (rest[i + 1] == '.' || rest[i + 1] == '-' || is_digit(rest[i + 1]))) {
            ++i;   // "A.1", "B-2"
        }
        const std::size_t digits = i;
        while (i < rest.size() && (is_digit(rest[i]) || ((rest[i] == '.' || rest[i] == '-') && i + 1 < rest.size() &&
                                                         is_digit(rest[i + 1])))) {
            ++i;
        }
        if (i == digits && i == 0) continue;
        if (i == rest.size()) return true;
        const char next = rest[i];
        if (next == '.' || next == ':' || next == ' ' || next == '-' || next == ')') {
            // "Figure 3 shows ..." is a sentence; a caption goes on with a title (or a dash/colon)
            std::string_view after = rest.substr(i + 1);
            while (!after.empty() && (after[0] == ' ' || after[0] == '.' || after[0] == ':' || after[0] == '-')) {
                after.remove_prefix(1);
            }
            static constexpr std::string_view kVerbs[] = {"shows ", "show ", "is ", "illustrates ", "gives ",
                                                         "and ", "to ", "depicts ", "describes ", "above", "below"};
            for (const std::string_view v : kVerbs) {
                if (after.starts_with(v)) return false;
            }
            return true;
        }
    }
    return false;
}

struct Region {
    Box box;
    std::size_t shapes = 0;
    bool picture = false;
    std::vector<Box> members;   // the shapes of a drawing
    std::size_t first = 0;      // index of its first box
};

bool near(const Box& a, const Box& b, float gap) {
    return a.x0 - gap <= b.x1 && b.x0 - gap <= a.x1 && a.y0 - gap <= b.y1 && b.y0 - gap <= a.y1;
}

Box unite(const Box& a, const Box& b) {
    return {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1)};
}

// Groups of boxes that touch (closer than `gap`), with the number of boxes in each.
std::vector<Region> clusters(std::span<const Box> boxes, float gap, bool picture) {
    std::vector<std::size_t> parent(boxes.size());
    std::iota(parent.begin(), parent.end(), 0);
    const auto root = [&](std::size_t i) {
        while (parent[i] != i) i = parent[i] = parent[parent[i]];
        return i;
    };
    std::vector<std::size_t> order(boxes.size());
    std::iota(order.begin(), order.end(), 0);
    std::ranges::sort(order, [&](std::size_t a, std::size_t b) { return boxes[a].y0 < boxes[b].y0; });
    for (std::size_t a = 0; a < order.size(); ++a) {
        const Box& A = boxes[order[a]];
        for (std::size_t b = a + 1; b < order.size() && boxes[order[b]].y0 <= A.y1 + gap; ++b) {
            if (near(A, boxes[order[b]], gap)) parent[root(order[a])] = root(order[b]);
        }
    }
    std::vector<Region> out;
    std::vector<long> slot(boxes.size(), -1);
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const std::size_t r = root(i);
        if (slot[r] < 0) {
            slot[r] = static_cast<long>(out.size());
            out.push_back(Region{boxes[i], 0, picture, {}, i});
        }
        Region& g = out[static_cast<std::size_t>(slot[r])];
        g.box = unite(g.box, boxes[i]);
        ++g.shapes;
        if (!picture) g.members.push_back(boxes[i]);
    }
    return out;
}

}  // namespace

bool is_figure_caption(std::string_view line) {
    return starts_with_numbered(line, {"Figure", "FIGURE", "Fig.", "FIG.", "Fig"});
}

bool is_table_caption(std::string_view line) {
    return starts_with_numbered(line, {"Table", "TABLE"});
}

std::vector<Figure> find_figures(int page, float width, float height, std::span<const Box> images,
                                 std::span<const Box> paths, std::span<const Box> forms,
                                 std::span<const Line> lines, bool turned, const FigureOptions& o,
                                 std::span<const char> code, std::span<const std::uint64_t> image_keys) {
    // A frame or a background over the whole page is no figure, and must not
    // join the figures on the page into one region with it.
    const float page_area = std::max(1.0f, width * height);
    const auto whole_page = [&](const Box& b) { return b.width() * b.height() > 0.85f * page_area; };
    std::vector<Region> regions = clusters(images, o.join_gap, true);
    if (!turned) {
        std::vector<Box> shapes;
        std::ranges::copy_if(paths, std::back_inserter(shapes), [&](const Box& b) { return !whole_page(b); });
        // A form is a group of drawn objects: as one shape it counts like many lines.
        std::vector<Region> drawn = clusters(shapes, o.join_gap, false);
        for (const Box& f : forms) drawn.push_back(Region{f, o.min_paths, false, {}, 0});
        regions.insert(regions.end(), drawn.begin(), drawn.end());
    }
    std::erase_if(regions, [&](const Region& r) { return whole_page(r.box); });
    // Pictures and drawings that touch are one figure (a photo with arrows on it).
    for (bool merged = true; merged;) {
        merged = false;
        for (std::size_t i = 0; i < regions.size() && !merged; ++i) {
            for (std::size_t j = i + 1; j < regions.size(); ++j) {
                if (near(regions[i].box, regions[j].box, 0)) {
                    regions[i].box = unite(regions[i].box, regions[j].box);
                    regions[i].shapes += regions[j].shapes;
                    regions[i].picture = regions[i].picture || regions[j].picture;
                    regions[i].members.insert(regions[i].members.end(), regions[j].members.begin(),
                                              regions[j].members.end());
                    regions.erase(regions.begin() + static_cast<std::ptrdiff_t>(j));
                    merged = true;
                    break;
                }
            }
        }
    }

    const float band = static_cast<float>(o.margin_band) * height;
    struct Judged {
        Figure figure;
        std::size_t caption_line = static_cast<std::size_t>(-1);   // the "Figure N" line, if any
        float caption_distance = 0;
        bool caption_below = false;   // the caption stands below the figure
    };
    // Whether region r is a figure, and with what caption; `skip` is a caption
    // line it may not take (it belongs to a nearer figure).
    const auto judge = [&](const Region& r, std::size_t skip) -> std::optional<Judged> {
        Box b = r.box;
        if (b.width() < o.min_width || b.height() < 10) return std::nullopt;
        if (whole_page(b)) return std::nullopt;                         // a frame around the page
        if (b.y1 <= band || b.y0 >= height - band) return std::nullopt; // a logo or a rule in the margin

        // Captions next to it: the nearest "Figure N" below or above. A
        // "Table N" nearer than that makes it a table.
        std::string caption;
        std::size_t caption_line = static_cast<std::size_t>(-1);
        bool caption_below = false;
        bool table = false;
        float best = o.caption_reach + 1, table_at = o.caption_reach + 1;
        const auto take_caption = [&](std::size_t li) {
            const Line& l = lines[li];
            caption = text::collapse_spaces(l.text);
            caption_line = li;
            // "Figure 1." alone on its line: its title is the line right below
            if (text::word_count(caption) <= 2 && li + 1 < lines.size()) {
                const Line& next = lines[li + 1];
                const float step = next.baseline - l.baseline;
                if (!next.text.empty() && next.figure == 0 && step > 0 && step < 2.0f * std::max(l.size, 1.0f) &&
                    std::abs(next.x0 - l.x0) < 4 * std::max(l.size, 1.0f)) {
                    caption += ' ' + text::collapse_spaces(next.text);
                }
            }
        };
        for (std::size_t li = 0; li < lines.size(); ++li) {
            const Line& l = lines[li];
            if (l.x1 < b.x0 || l.x0 > b.x1) continue;
            const float below = l.y0 - b.y1, above = b.y0 - l.y1;
            const float d = below >= -2 ? below : (above >= -2 ? above : -1);
            if (d < -2 || d > o.caption_reach) continue;
            if (li != skip && is_figure_caption(l.text) && d < best) {
                best = d;
                take_caption(li);
                caption_below = below >= -2;
            } else if (is_table_caption(l.text)) {
                table = true;
                table_at = std::min(table_at, d);
            }
        }
        if (!caption.empty() && table && table_at < best) {
            caption.clear();   // the table's own caption stands nearer
            caption_line = static_cast<std::size_t>(-1);
        }
        // A "Figure N" further below, with only short labels between (the
        // legend of a block diagram): its caption; the figure reaches down to it.
        if (caption.empty() && !table) {
            for (std::size_t li = 0; li < lines.size(); ++li) {
                const Line& l = lines[li];
                const float d = l.y0 - b.y1;
                if (li == skip || l.x1 < b.x0 || l.x0 > b.x1 || d <= o.caption_reach || d > 3 * o.caption_reach ||
                    !is_figure_caption(l.text)) {
                    continue;
                }
                const bool labels_only = std::ranges::all_of(lines, [&](const Line& m) {
                    const bool between = m.y0 >= b.y1 - 2 && m.y1 <= l.y0 + 2 && m.x1 >= b.x0 && m.x0 <= b.x1;
                    return &m == &l || !between || m.text.empty() || text::word_count(m.text) <= 4;
                });
                if (!labels_only) continue;
                best = d;
                take_caption(li);
                caption_below = true;
                b.y1 = l.y0 - 2;
                break;
            }
        }

        // A caption "Figure N" lets a thin drawing count (a register layout);
        // the size rule below holds for everything else.
        const bool numbered = !caption.empty();
        if (!numbered && (b.height() < o.min_height || b.width() * b.height() < o.min_area)) return std::nullopt;

        // No "Figure N": a short line of words centred just below it, narrower
        // than the text column, is its title ("Memory layout of big-endian data object").
        if (caption.empty() && !table) {
            const float centre = (b.x0 + b.x1) / 2;
            float nearest = 15;
            for (const Line& l : lines) {
                const float d = l.y0 - b.y1;
                if (d < -2 || d > nearest || l.text.empty() || text::word_count(l.text) > 15) continue;
                if (std::abs((l.x0 + l.x1) / 2 - centre) > 0.1f * b.width() + 10) continue;
                if (l.x1 - l.x0 > 0.6f * width) continue;   // a line across the text column is a sentence
                const std::size_t letters = text::letter_count(l.text);
                if (letters < 3 || 2 * letters < l.text.size()) continue;
                if (text::ends_with_sentence_punct(l.text) && text::word_count(l.text) > 8) continue;
                nearest = d;
                caption = text::collapse_spaces(l.text);
            }
        }

        bool keep = r.picture;
        if (!keep) {
            // The text inside: how much of the area it covers, and how much of it is table rows.
            float cover = 0;
            std::size_t inside = 0, rows = 0, listing = 0;
            for (std::size_t li = 0; li < lines.size(); ++li) {
                const Line& l = lines[li];
                const float cx = (l.x0 + l.x1) / 2, cy = (l.y0 + l.y1) / 2;
                if (cx < b.x0 || cx > b.x1 || cy < b.y0 || cy > b.y1) continue;
                ++inside;
                if (l.gaps.size() >= 2) ++rows;
                if (li < code.size() && code[li]) ++listing;
                cover += (l.x1 - l.x0) * std::max(l.y1 - l.y0, 1.0f);
            }
            // A table drawn with rules: three or more heights where thin lines
            // (one line, or the borders of the cells) run across its whole width.
            std::vector<std::pair<int, float>> at;   // height (rounded), width covered
            for (const Box& m : r.members) {
                if (m.height() > 2 || m.width() < 3 * std::max(m.height(), 0.5f)) continue;
                const int y = static_cast<int>(std::lround((m.y0 + m.y1) / 2));
                auto it = std::ranges::find_if(at, [&](const auto& p) { return std::abs(p.first - y) <= 1; });
                if (it == at.end()) at.emplace_back(y, m.width());
                else it->second += m.width();
            }
            const auto rules = static_cast<std::size_t>(
                std::ranges::count_if(at, [&](const auto& p) { return p.second >= 0.9f * b.width(); }));
            const bool text_rows = (rows >= 3 && 2 * rows >= inside) ||
                                   (listing >= 3 && 5 * listing >= 3 * inside);   // a framed listing
            if (numbered) {
                // the author calls it a figure: rules across it are the grid of a chart
                keep = !text_rows || r.shapes >= 4 * o.min_paths;
            } else {
                keep = !table && !text_rows && rules < 3 && r.shapes >= o.min_paths &&
                       cover <= o.max_text_cover * b.width() * b.height();
            }
        }
        if (!keep) return std::nullopt;
        Judged j{Figure{page, b, r.picture ? Figure::Kind::Picture : Figure::Kind::Drawing, caption, 0}, caption_line,
                 best, caption_below};
        // one picture and nothing else: its key tells a repeat of it on other pages
        if (r.picture && r.shapes == 1 && r.members.empty() && r.first < image_keys.size()) {
            j.figure.picture = image_keys[r.first];
        }
        return j;
    };

    std::vector<std::optional<Judged>> judged;
    for (const Region& r : regions) judged.push_back(judge(r, static_cast<std::size_t>(-1)));
    // One caption, one figure. Where two regions take the same caption, the
    // one on the side where the captions of this page stand gets it (below
    // the figure, unless the page's other captions stand above), else the
    // nearer; the other is judged again without it.
    std::size_t below_count = 0, above_count = 0;
    for (std::size_t i = 0; i < judged.size(); ++i) {
        if (!judged[i] || judged[i]->caption_line == static_cast<std::size_t>(-1)) continue;
        const bool shared = std::ranges::any_of(judged, [&](const std::optional<Judged>& o2) {
            return o2 && &*o2 != &*judged[i] && o2->caption_line == judged[i]->caption_line;
        });
        if (!shared) (judged[i]->caption_below ? below_count : above_count) += 1;
    }
    const bool captions_below = below_count >= above_count;
    for (std::size_t i = 0; i < judged.size(); ++i) {
        if (!judged[i] || judged[i]->caption_line == static_cast<std::size_t>(-1)) continue;
        for (std::size_t k = 0; k < judged.size(); ++k) {
            if (k == i || !judged[k] || judged[k]->caption_line != judged[i]->caption_line) continue;
            std::size_t loser;
            if (judged[i]->caption_below != judged[k]->caption_below) {
                loser = judged[i]->caption_below == captions_below ? k : i;
            } else {
                loser = judged[k]->caption_distance < judged[i]->caption_distance ? i : k;
            }
            const std::size_t line = judged[loser]->caption_line;
            judged[loser] = judge(regions[loser], line);
            if (loser == i) break;
        }
    }
    std::vector<Figure> out;
    for (auto& j : judged) {
        if (j) out.push_back(std::move(j->figure));
    }
    std::ranges::sort(out, [](const Figure& a, const Figure& b) { return a.box.y0 < b.box.y0; });
    return out;
}

}  // namespace htpd
