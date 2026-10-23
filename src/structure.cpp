#include "htpd/structure.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

#include "htpd/text.hpp"

namespace htpd {

namespace {

constexpr int kMaxLevel = 6;

bool has_letter(std::string_view s) {
    return std::ranges::any_of(s, [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); });
}

// Index of the first line on page `page` or later.
std::size_t first_line_of(std::span<const Line> lines, int page) {
    const auto it = std::ranges::lower_bound(lines, page, {}, &Line::page);
    return static_cast<std::size_t>(it - lines.begin());
}

// The pages where a running header changes, with the new text. Empty when the
// pattern is no chapter mark.
std::vector<std::pair<int, std::string>> title_changes(const FurniturePattern& p) {
    std::vector<std::pair<int, std::string>> starts;
    std::string previous;
    std::size_t pages_with_text = 0;
    for (std::size_t page = 0; page < p.page_text.size(); ++page) {
        const std::string& t = p.page_text[page];
        if (t.empty()) continue;
        ++pages_with_text;
        std::string key = furniture_key(t);
        if (key == previous) continue;
        previous = std::move(key);
        starts.emplace_back(static_cast<int>(page), text::collapse_spaces(t));
    }
    // A chapter mark: at least two chapters, each a few pages long on average,
    // and words in it (not only a page number).
    if (starts.size() < 2 || (starts.size() - 1) * 3 > pages_with_text) return {};
    if (!std::ranges::all_of(starts, [](const auto& s) { return has_letter(s.second); })) return {};
    return starts;
}

std::size_t add_running_titles(std::vector<Heading>& headings, std::span<const Line> lines,
                               const FurnitureReport& furniture) {
    const FurniturePattern* best = nullptr;
    std::vector<std::pair<int, std::string>> best_starts;
    for (const FurniturePattern& p : furniture.patterns) {
        if (p.variants < 2 || p.page_text.empty()) continue;
        auto starts = title_changes(p);
        // Of several candidates the one with the fewest changes: the coarsest
        // division is the chapters, a finer one is more likely noise.
        if (!starts.empty() && (best == nullptr || starts.size() < best_starts.size())) {
            best = &p;
            best_starts = std::move(starts);
        }
    }
    std::size_t added = 0;
    for (auto& [page, title] : best_starts) {
        const std::size_t index = first_line_of(lines, page);
        if (index >= lines.size()) continue;
        const int at = lines[index].page;
        // A heading of the document near the top of that page already marks it.
        const bool marked = std::ranges::any_of(headings, [&](const Heading& h) {
            return h.page == at && h.line_index >= index && h.line_index < index + 3;
        });
        if (marked) continue;
        Heading h;
        h.title = std::move(title);
        h.level = 1;
        h.page = at;
        h.line_index = index;
        h.line_count = 0;
        h.source = HeadingSource::RunningTitle;
        headings.push_back(std::move(h));
        ++added;
    }
    return added;
}

// A cut between line i-1 and line i keeps paragraphs and tables whole.
bool clean_cut(std::span<const Line> lines, std::size_t i) {
    const Line& before = lines[i - 1];
    const Line& after = lines[i];
    // next to a table row: a table stays whole, with its header row - also
    // one whose rows are two cells, or stored cell by cell (another cell on
    // the same line of the page)
    if (before.gaps.size() >= 2 || after.gaps.size() >= 2 || before.table || after.table) return false;
    const auto cell_of_row = [&](std::size_t k) {
        const Line& line = lines[k];
        if (!line.gaps.empty()) return true;
        const float tolerance = 0.3f * std::max(line.size, 1.0f);
        const std::size_t from = k > 40 ? k - 40 : 0, to = std::min(lines.size(), k + 41);
        for (std::size_t j = from; j < to; ++j) {
            const Line& other = lines[j];
            if (j != k && other.page == line.page && !other.text.empty() &&
                std::fabs(other.baseline - line.baseline) < tolerance && (other.x0 > line.x1 || other.x1 < line.x0)) {
                return true;
            }
        }
        return false;
    };
    if (cell_of_row(i - 1) || cell_of_row(i)) return false;
    if (text::ends_with_sentence_punct(before.text)) return true;
    // At the top of a page that starts with a larger line (the title of a
    // slide), after a short line that does not run on: no sentence is broken
    // there, and it is no table going on (its cells are set in one size).
    const std::string_view b = before.text;
    return after.page != before.page && after.size > before.size * 1.1f && !b.empty() &&
           text::word_count(b) <= 8 && b.back() != '-' && b.back() != ',';
}

std::string pages_title(std::string_view of, int first, int last) {
    const std::string pages = first == last ? std::format("page {}", first + 1)
                                            : std::format("pages {}-{}", first + 1, last + 1);
    if (of.empty()) return std::string(1, static_cast<char>(pages[0] - 'a' + 'A')) + pages.substr(1);
    return std::format("{} - {}", of, pages);
}

// The words a running header gives each page, when it names the page's topic
// ("Section 3: Methodology") rather than the book: the header with the most
// different texts, without the text it shows most often (the book title on
// every other page). Empty strings where it says nothing.
std::vector<std::string> page_labels(const FurnitureReport& furniture) {
    const FurniturePattern* best = nullptr;
    for (const FurniturePattern& p : furniture.patterns) {
        if (p.variants < 2 || p.page_text.empty()) continue;
        if (best == nullptr || p.variants > best->variants) best = &p;
    }
    if (best == nullptr) return {};
    std::vector<std::string> keys(best->page_text.size());
    std::vector<std::pair<std::string, std::size_t>> counts;
    for (std::size_t page = 0; page < keys.size(); ++page) {
        if (best->page_text[page].empty()) continue;
        keys[page] = furniture_key(best->page_text[page]);
        auto it = std::ranges::find(counts, keys[page], &std::pair<std::string, std::size_t>::first);
        if (it == counts.end()) {
            counts.emplace_back(keys[page], 1);
        } else {
            ++it->second;
        }
    }
    const auto common = std::ranges::max_element(counts, {}, &std::pair<std::string, std::size_t>::second);
    std::vector<std::string> labels(keys.size());
    for (std::size_t page = 0; page < keys.size(); ++page) {
        const std::string& t = best->page_text[page];
        // the most common text only counts when every page says something different
        const bool book_title = common != counts.end() && common->second * 3 > keys.size() && keys[page] == common->first;
        if (!t.empty() && !book_title && has_letter(t)) labels[page] = text::collapse_spaces(t);
    }
    return labels;
}

// "Report - pages 12-13: Section 3: Methods; Section 4: Results" - the different labels
// of the pages of a part, at most three.
std::string part_title(std::string_view of, int first, int last, std::span<const std::string> labels) {
    std::string title = pages_title(of, first, last);
    std::vector<std::string> seen;
    for (int page = first; page <= last && static_cast<std::size_t>(page) < labels.size(); ++page) {
        const std::string& label = labels[static_cast<std::size_t>(page)];
        if (label.empty()) continue;
        std::string key = furniture_key(label);   // "Preface | Page 4" and "... Page 5" are one label
        if (std::ranges::find(seen, key) != seen.end()) continue;
        if (seen.size() == 3) {
            title += "; ...";
            break;
        }
        title += seen.empty() ? ": " : "; ";
        title += label;
        seen.push_back(std::move(key));
    }
    return title;
}

std::size_t add_page_parts(std::vector<Heading>& headings, std::span<const Line> lines,
                           std::span<const std::string> labels, int max_pages, const StructureOptions& options) {
    if (headings.empty() || lines.empty()) return 0;   // no headings: build_sections cuts by page
    struct Region {
        std::size_t begin, end;
        int level;
        std::string_view title;   // of the section the parts belong to
    };
    std::vector<Region> regions;
    regions.push_back({0, std::min(headings.front().line_index, lines.size()), 1, "Front matter"});
    std::size_t title_end = 0;
    for (std::size_t i = 0; i < headings.size(); ++i) {
        const Heading& h = headings[i];
        const std::size_t begin = std::min(std::max(h.line_index + h.line_count, title_end), lines.size());
        const std::size_t end = i + 1 < headings.size() ? std::min(headings[i + 1].line_index, lines.size())
                                                        : lines.size();
        regions.push_back({begin, std::max(begin, end), h.level, h.title});
        title_end = begin;
    }

    std::vector<Heading> parts;   // added after the loop: the regions point at the titles
    for (const Region& r : regions) {
        if (r.end <= r.begin) continue;
        const int first = lines[r.begin].page;
        const int last = lines[r.end - 1].page;
        if (last - first + 1 <= max_pages) continue;
        std::vector<std::size_t> cuts;
        int target = first + options.part_pages;
        std::size_t i = r.begin;
        while (target <= last) {
            while (i < r.end && lines[i].page < target) ++i;
            if (i >= r.end) break;
            const int page = lines[i].page;
            std::size_t cut = r.end;
            for (std::size_t k = i; k < r.end && lines[k].page == page; ++k) {
                if (k > r.begin && clean_cut(lines, k)) {
                    cut = k;
                    break;
                }
            }
            if (cut == r.end) {   // no clean place on this page: try the next one
                target = page + 1;
                continue;
            }
            cuts.push_back(cut);
            target = lines[cut].page + options.part_pages;
            i = cut + 1;
        }
        for (std::size_t c = 0; c < cuts.size(); ++c) {
            const std::size_t end = c + 1 < cuts.size() ? cuts[c + 1] : r.end;
            Heading h;
            h.title = part_title(r.title, lines[cuts[c]].page, lines[end - 1].page, labels);
            h.level = std::min(r.level + 1, kMaxLevel);
            h.page = lines[cuts[c]].page;
            h.line_index = cuts[c];
            h.line_count = 0;
            h.source = HeadingSource::Pages;
            parts.push_back(std::move(h));
        }
    }
    const std::size_t added = parts.size();
    headings.insert(headings.end(), std::make_move_iterator(parts.begin()), std::make_move_iterator(parts.end()));
    return added;
}

}  // namespace

StructureReport add_structure(std::vector<Heading>& headings, std::span<const Line> lines,
                              const FurnitureReport& furniture, int page_count, const StructureOptions& options) {
    StructureReport report;
    const auto sort = [&] { std::ranges::stable_sort(headings, {}, &Heading::line_index); };
    // The running header only tells more than the headings when there are few of them.
    const bool few = headings.size() < 3 ||
                     headings.size() * options.pages_per_heading < static_cast<std::size_t>(std::max(page_count, 0));
    if (few) {
        report.running_titles = add_running_titles(headings, lines, furniture);
        sort();
    }
    // With the author's headings a section is a unit of its own: only a very
    // long one is cut (a register list over many pages).
    report.page_parts = add_page_parts(headings, lines, few ? page_labels(furniture) : std::vector<std::string>{},
                                       few ? options.max_pages : options.max_pages_headed, options);
    sort();
    return report;
}

}  // namespace htpd
