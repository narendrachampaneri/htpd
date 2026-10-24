#include "htpd/columns.hpp"

#include "htpd/tables.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace htpd {

namespace {

struct Word {
    float x0, x1;
};

// The words of a line with their extent, or nothing when the line does not
// carry the place of each space.
std::vector<Word> words_of(const Line& line) {
    const std::size_t spaces = static_cast<std::size_t>(std::ranges::count(line.text, ' '));
    if (line.space_begin.size() != spaces || line.space_end.size() != spaces) return {};
    std::vector<Word> out;
    out.reserve(spaces + 1);
    float x0 = line.x0;
    for (std::size_t k = 0; k < spaces; ++k) {
        out.push_back({x0, line.space_begin[k]});
        x0 = line.space_end[k];
    }
    out.push_back({x0, line.x1});
    return out;
}

struct Strip {
    float x0, x1;
};

// Free strips: the parts of [left, right] that no word of any row covers.
std::vector<Strip> free_strips(const std::vector<std::vector<Word>>& rows, float left, float right) {
    std::vector<Word> all;
    for (const auto& r : rows) all.insert(all.end(), r.begin(), r.end());
    std::ranges::sort(all, {}, &Word::x0);
    std::vector<Strip> out;
    float reach = left;
    for (const Word& w : all) {
        if (w.x0 > reach) out.push_back({reach, w.x0});
        reach = std::max(reach, w.x1);
    }
    if (right > reach) out.push_back({reach, right});
    return out;
}

// Makes space number `k` of the line a column gap (when it is not one yet).
bool mark_gap(Line& line, std::size_t k) {
    std::size_t at = 0, seen = 0;
    for (; at < line.text.size(); ++at) {
        if (line.text[at] == ' ' && seen++ == k) break;
    }
    if (at >= line.text.size()) return false;
    const auto offset = static_cast<std::uint32_t>(at);
    const auto it = std::ranges::lower_bound(line.gaps, offset);
    if (it != line.gaps.end() && *it == offset) return false;
    const auto i = static_cast<std::ptrdiff_t>(it - line.gaps.begin());
    if (line.gap_begin.size() != line.gaps.size() || line.gap_end.size() != line.gaps.size()) return false;
    line.gaps.insert(it, offset);
    line.gap_begin.insert(line.gap_begin.begin() + i, line.space_begin[k]);
    line.gap_end.insert(line.gap_end.begin() + i, line.space_end[k]);
    return true;
}

// "1.", "a)", "(2)", "•": the mark of a list item at the start of a line.
bool list_mark(std::string_view text) {
    const std::string_view first = text.substr(0, text.find(' '));
    if (first.empty() || first.size() > 6) return false;
    if (first == "\xE2\x80\xA2" || first == "\xE2\x97\xA6" || first == "\xE2\x96\xAA" || first == "-" ||
        first == "*") {
        return true;
    }
    // "A.4", "2.1": the number of a step or a clause
    if (first.size() >= 3 && first.find('.') != std::string_view::npos &&
        std::ranges::all_of(first, [](char c) { return c == '.' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'); }) &&
        first.back() != '.') {
        return true;
    }
    const char last = first.back();
    if (last != '.' && last != ')') return false;
    std::string_view core = first.substr(0, first.size() - 1);
    if (!core.empty() && core.front() == '(') core.remove_prefix(1);
    return !core.empty() && (std::ranges::all_of(core, [](char c) { return c >= '0' && c <= '9'; }) ||
                             (core.size() == 1 && ((core[0] >= 'a' && core[0] <= 'z') || (core[0] >= 'A' && core[0] <= 'Z'))));
}

}  // namespace

std::size_t mark_table_columns(std::vector<Line>& lines, std::span<const char> skip, const ColumnOptions& o) {
    const auto usable = [&](std::size_t i) {
        return (i >= skip.size() || !skip[i]) && lines[i].figure == 0 && !lines[i].text.empty() &&
               lines[i].ocr != Line::Ocr::Picture;
    };
    const auto close = [&](std::size_t a, std::size_t b) {   // b directly below a
        const Line& x = lines[a];
        const Line& y = lines[b];
        const float size = std::max({x.size, y.size, 1.0f});
        const float step = y.baseline - x.baseline;
        return x.page == y.page && step > 0.3f * size && step <= o.max_row_step * size;
    };

    std::size_t added = 0;
    // Two passes: first blocks of rows with a sure gap; then, among the lines
    // left, blocks with only wide spaces (a table set tight).
    for (int pass = 0; pass < 2; ++pass)
    for (std::size_t begin = 0; begin < lines.size();) {
        const bool tight = pass == 1;
        const auto seed = [&](std::size_t i) {
            if (!usable(i) || lines[i].table || words_of(lines[i]).empty()) return false;
            if (!tight) return !lines[i].gaps.empty();
            const float size_i = std::max(lines[i].size, 1.0f);
            for (std::size_t k = 0; k < lines[i].space_begin.size(); ++k) {
                if (lines[i].space_end[k] - lines[i].space_begin[k] >= o.min_gutter * size_i) return true;
            }
            return false;
        };
        if (!seed(begin)) {
            ++begin;
            continue;
        }
        std::size_t end = begin + 1;
        std::vector<char> filler(lines.size(), 0);   // lines inside the block that have no sure gap of their own
        while (end < lines.size()) {
            if (seed(end) && close(end - 1, end)) {
                ++end;
                continue;
            }
            // One or two lines between rows that hold the rest of a cell that wraps ("controller."
            // under a description): they belong to the block when a row follows them.
            std::size_t k = end;
            // (A line in another size or weight - a heading between two tables - is no such rest.)
            const Line& anchor = lines[end - 1];
            while (k < lines.size() && k < end + 2 && usable(k) && !lines[k].table && !seed(k) && !words_of(lines[k]).empty() &&
                   close(k - 1, k) && std::fabs(lines[k].size - anchor.size) <= 0.1f * std::max(anchor.size, 1.0f) &&
                   lines[k].bold == anchor.bold && lines[k].figure == 0) {
                ++k;
            }
            if (k > end && k < lines.size() && seed(k) && close(k - 1, k)) {
                for (std::size_t f = end; f < k; ++f) filler[f] = 1;
                end = k + 1;
                continue;
            }
            break;
        }
        // A tight block needs more: four rows and two column gaps (a "river"
        // of spaces in running text never makes two).
        if (end - begin < (tight ? o.min_rows + 1 : o.min_rows)) {
            begin = end;
            continue;
        }

        std::vector<std::vector<Word>> rows;
        float left = 1e30f, right = -1e30f, size = 0;
        for (std::size_t i = begin; i < end; ++i) {
            rows.push_back(words_of(lines[i]));
            left = std::min(left, lines[i].x0);
            right = std::max(right, lines[i].x1);
            size = std::max(size, lines[i].size);
        }
        // Strips inside the block, wide enough, with text left and right of them in most rows.
        std::vector<Strip> gutters;
        for (const Strip& s : free_strips(rows, left, right)) {
            if (s.x0 <= left || s.x1 >= right || s.x1 - s.x0 < o.min_gutter * size) continue;
            std::size_t both = 0, counted = 0;
            for (std::size_t r = 0; r < rows.size(); ++r) {
                if (filler[begin + r]) continue;   // the rest of a cell has text on one side only
                ++counted;
                const bool l = std::ranges::any_of(rows[r], [&](const Word& w) { return w.x1 <= s.x0; });
                const bool rr = std::ranges::any_of(rows[r], [&](const Word& w) { return w.x0 >= s.x1; });
                both += l && rr;
            }
            if (both * 10 >= counted * 6) gutters.push_back(s);
        }
        if (gutters.empty() || (tight && gutters.size() < 2)) {
            begin = end;
            continue;
        }

        // A line inside the block without a gap of its own must keep to the columns: text across a gutter
        // (a sentence between two tables) is no rest of a cell.
        bool crosses = false;
        for (std::size_t i = begin; i < end && !crosses; ++i) {
            if (!filler[i]) continue;
            for (const Strip& g : gutters) {
                const float mid = (g.x0 + g.x1) / 2;
                crosses = crosses || std::ranges::any_of(words_of(lines[i]), [&](const Word& w) { return w.x0 < mid && w.x1 > mid; });
            }
        }
        if (crosses) {
            begin = end;
            continue;
        }

        // Marks the spaces on the gutters of line i; false when a word of it crosses one.
        const auto apply = [&](std::size_t i, bool dry_run, std::size_t& marked) {
            const Line& line = lines[i];
            const std::vector<Word> ws = words_of(line);
            if (ws.empty()) return false;
            for (const Strip& g : gutters) {
                const float mid = (g.x0 + g.x1) / 2;
                for (const Word& w : ws) {
                    if (w.x0 < mid && w.x1 > mid) return false;   // a word across a column gap
                }
            }
            if (dry_run) {
                std::size_t split = 0;
                for (std::size_t k = 0; k < line.space_begin.size(); ++k) {
                    for (const Strip& g : gutters) {
                        const float mid = (g.x0 + g.x1) / 2;
                        split += line.space_begin[k] <= mid && line.space_end[k] >= mid;
                    }
                }
                marked = split;
                return true;
            }
            for (std::size_t k = 0; k < line.space_begin.size(); ++k) {
                for (const Strip& g : gutters) {
                    const float mid = (g.x0 + g.x1) / 2;
                    if (line.space_begin[k] <= mid && line.space_end[k] >= mid) {
                        marked += mark_gap(lines[i], k);
                        break;
                    }
                }
            }
            return true;
        };

        // One column gap only: a list ("1.  Hook the edge ...") is no table.
        if (gutters.size() == 1) {
            std::size_t marks = 0;
            for (std::size_t i = begin; i < end; ++i) marks += list_mark(lines[i].text);
            if (marks * 2 >= end - begin) {
                begin = end;
                continue;
            }
        }
        std::size_t count = 0;
        for (std::size_t i = begin; i < end; ++i) apply(i, false, count);
        // The rows just above and below without a sure gap (a header row, a
        // last row): each word goes to the column its middle lies in. The
        // row joins the table when its words go to at least two columns from
        // left to right and the spaces between columns are wide - in running
        // text they are not.
        const auto side_row = [&](std::size_t i) {
            const Line& line = lines[i];
            if (!usable(i) || !line.gaps.empty() || (i < skip.size() && skip[i])) return false;
            const std::vector<Word> ws = words_of(line);
            if (ws.size() < 2) return false;
            // Which column each word is in: by where it starts (cells set
            // left, the usual case), else by its middle, else by its end.
            std::vector<std::size_t> column(ws.size());
            const auto place = [&](int by) {
                for (std::size_t w = 0; w < ws.size(); ++w) {
                    std::size_t c = 0;
                    while (c < gutters.size() &&
                           (by == 0   ? ws[w].x0 >= gutters[c].x1 - 0.5f * size
                            : by == 1 ? (ws[w].x0 + ws[w].x1) / 2 > (gutters[c].x0 + gutters[c].x1) / 2
                                      : ws[w].x1 > gutters[c].x0 + 0.5f * size)) {
                        ++c;
                    }
                    column[w] = c;
                    if (w > 0 && column[w] < column[w - 1]) return false;
                }
                return column.front() != column.back();
            };
            if (!place(0) && !place(1) && !place(2)) return false;
            const float size_here = std::max(line.size, 1.0f);
            std::vector<std::size_t> marks;
            for (std::size_t k = 0; k + 1 < ws.size(); ++k) {
                if (column[k] == column[k + 1]) continue;
                if (line.space_end[k] - line.space_begin[k] < 0.75f * o.min_gutter * size_here) return false;
                marks.push_back(k);
            }
            for (const std::size_t k : marks) count += mark_gap(lines[i], k);
            return true;
        };
        std::size_t top = begin;   // the first line of the table, header row included
        for (std::size_t i = begin; i-- > 0;) {
            if (!close(i, i + 1) || !side_row(i)) break;
            lines[i].table = true;
            top = i;
        }
        for (std::size_t i = end; i < lines.size(); ++i) {
            if (!close(i - 1, i) || !side_row(i)) break;
            end = i + 1;
        }
        for (std::size_t i = begin; i < end; ++i) lines[i].table = true;
        {
            // Which lines only hold the rest of cells of a row next to them.
            std::vector<float> cuts;
            for (const Strip& g : gutters) cuts.push_back((g.x0 + g.x1) / 2);
            mark_row_continuations(lines, top, end, cuts);
        }
        added += count;
        begin = end;
    }
    return added;
}

}  // namespace htpd
