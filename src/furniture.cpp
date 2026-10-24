#include "htpd/furniture.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <utility>

#include "htpd/text.hpp"

namespace htpd {

namespace {

// Baselines closer than this (in points) belong to the same slot.
constexpr float kSlotTolerance = 2.0f;
// A slot must be present on this share of the pages that have text.
constexpr double kMinCoverage = 0.4;
// Share of a slot's pages that must show a repeating text ...
constexpr double kMinRepeated = 0.6;
// ... or share the same first or last two words.
constexpr double kMinSameAffix = 0.8;
// Share of a slot's pages on which only furniture lies further out.
constexpr double kMinAttached = 0.8;
// A running header is set apart from the text body. A line whose baseline is
// closer than this many of its font sizes to the next line of the body (further in)
// belongs to the body: a heading or the header row of a table that starts
// right below the running header ("Register 30.2.43: ...", "2.1 Summary").
constexpr float kGluedToBody = 2.5f;
// A line with the number of its page stays furniture, however close, and so
// does a text on this share of the pages (the bar of a web page printed over the text).
constexpr double kEverywhere = 0.9;

// "iv", "xii" ... page numbers of front matter.
bool is_roman_numeral(std::string_view s) {
    if (s.empty() || s.size() > 7) return false;
    return std::ranges::all_of(s, [](char c) {
        return c == 'i' || c == 'v' || c == 'x' || c == 'l' || c == 'c';
    });
}

// A candidate line: one that lies in the top or bottom band of its page.
struct BandLine {
    std::size_t page = 0;
    std::size_t index = 0;    // position in pages[page]
    bool top = true;
    float offset = 0;         // distance of the baseline from the page edge
    std::string key;          // furniture_key of the whole line
    std::string head;         // first two words of the key ("" if it has fewer)
    std::string tail;         // last two words of the key
    bool remove = false;
    std::size_t pattern = 0;  // the slot (index into the report's patterns) it is removed with
    bool page_number = false; // holds the number of its page (one that grows with the page)
    bool everywhere = false;  // the same text (digits ignored) stands on almost every page
};

// The numbers written in a line (runs of up to five digits).
std::vector<long> numbers_in(std::string_view text) {
    std::vector<long> out;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] < '0' || text[i] > '9') {
            ++i;
            continue;
        }
        std::size_t j = i;
        long value = 0;
        while (j < text.size() && text[j] >= '0' && text[j] <= '9') value = value * 10 + (text[j++] - '0');
        if (j - i <= 5) out.push_back(value);
        i = j;
    }
    return out;
}

// Number of different pages on which each value occurs.
class PageCounter {
public:
    void add(const std::string& value, std::size_t page) {
        if (value.empty()) return;
        Entry& entry = counts_[value];
        if (entry.seen && entry.last_page == page) return;   // count a page once
        entry.seen = true;
        entry.last_page = page;
        ++entry.pages;
    }

    std::size_t pages(const std::string& value) const {
        const auto it = counts_.find(value);
        return it == counts_.end() ? 0 : it->second.pages;
    }

    // The value found on the most pages, and on how many.
    std::pair<std::string, std::size_t> most_common() const {
        std::pair<std::string, std::size_t> best{{}, 0};
        for (const auto& [value, entry] : counts_) {
            if (entry.pages > best.second || (entry.pages == best.second && value < best.first)) {
                best = {value, entry.pages};
            }
        }
        return best;
    }

private:
    struct Entry {
        std::size_t pages = 0;
        std::size_t last_page = 0;
        bool seen = false;
    };
    std::unordered_map<std::string, Entry> counts_;
};

void set_affixes(BandLine& line) {
    const std::string_view key = line.key;
    const std::size_t first_space = key.find(' ');
    if (first_space == std::string_view::npos) return;   // a single word has no affix
    const std::size_t second_space = key.find(' ', first_space + 1);
    line.head = std::string(key.substr(0, second_space));
    const std::size_t last_space = key.rfind(' ');
    const std::size_t before_last = last_space == 0 ? std::string_view::npos
                                                    : key.rfind(' ', last_space - 1);
    line.tail = std::string(before_last == std::string_view::npos ? key : key.substr(before_last + 1));
}


// ---- Chrome's print template -------------------------------------------------

bool digits_at(std::string_view s, std::size_t& i, std::size_t min_n, std::size_t max_n) {
    std::size_t n = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9' && n < max_n) {
        ++i;
        ++n;
    }
    return n >= min_n;
}

bool char_at(std::string_view s, std::size_t& i, char c) {
    if (i < s.size() && s[i] == c) {
        ++i;
        return true;
    }
    return false;
}

// "10/24/26, 5:10 AM Title", "24.10.2026, 05:10 Title", "2026-10-24, 05:10 Title": the date and time
// Chrome puts above every page. Returns the length of the date and time, 0 when the line does not start so.
std::size_t chrome_date_time(std::string_view s) {
    std::size_t i = 0;
    if (!digits_at(s, i, 1, 4)) return 0;
    if (i >= s.size() || (s[i] != '/' && s[i] != '.' && s[i] != '-')) return 0;
    const char sep = s[i++];
    if (!digits_at(s, i, 1, 4) || !char_at(s, i, sep) || !digits_at(s, i, 1, 4)) return 0;
    if (!char_at(s, i, ',')) return 0;
    char_at(s, i, ' ');
    if (!digits_at(s, i, 1, 2) || !char_at(s, i, ':') || !digits_at(s, i, 2, 2)) return 0;
    if (char_at(s, i, ':')) digits_at(s, i, 2, 2);
    const std::size_t before_ampm = i;
    char_at(s, i, ' ');
    if (i + 1 < s.size() && (s[i] == 'A' || s[i] == 'P') && s[i + 1] == 'M') {
        i += 2;
    } else {
        i = before_ampm;
    }
    return i;
}

// "1/2" or "https://example.org/page 1/2": the address and page count Chrome puts below every page.
// Gives the page number n and the count N, 0 when the line is not such a footer.
bool chrome_counter(std::string_view s, long& n, long& total) {
    const std::size_t slash = s.rfind('/');
    if (slash == std::string_view::npos || slash + 1 >= s.size()) return false;
    std::size_t k = slash + 1;
    long b = 0;
    for (; k < s.size(); ++k) {
        if (s[k] < '0' || s[k] > '9' || b > 99999) return false;
        b = b * 10 + (s[k] - '0');
    }
    std::size_t start = slash;
    while (start > 0 && s[start - 1] >= '0' && s[start - 1] <= '9') --start;
    if (start == slash || slash - start > 5) return false;
    long a = 0;
    for (std::size_t m = start; m < slash; ++m) a = a * 10 + (s[m] - '0');
    const std::string_view before = s.substr(0, start);
    // Nothing before the count, or an address and a blank.
    if (!before.empty()) {
        if (before.back() != ' ') return false;
        const std::string_view address = before.substr(0, before.find(' '));
        if (!(address.starts_with("http://") || address.starts_with("https://") || address.starts_with("file://"))) {
            return false;
        }
    }
    n = a;
    total = b;
    return true;
}

// The topmost line of a page's top band and the lowest of its bottom band (index in the page, -1 when none).
struct EdgeLines {
    int top = -1, bottom = -1;
};

EdgeLines edge_lines(const std::vector<Line>& page, float height, double band) {
    EdgeLines edge;
    float top_y = 1e9f, bottom_y = -1e9f;
    for (std::size_t i = 0; i < page.size(); ++i) {
        const Line& l = page[i];
        if (l.figure > 0 || l.text.empty()) continue;
        const double centre = (l.y0 + l.y1) / 2.0;
        if (centre < band * height && l.baseline < top_y) {
            top_y = l.baseline;
            edge.top = static_cast<int>(i);
        }
        if (centre > (1.0 - band) * height && l.baseline > bottom_y) {
            bottom_y = l.baseline;
            edge.bottom = static_cast<int>(i);
        }
    }
    return edge;
}

// Chrome's header and footer: taken out when they stand on every page exactly as Chrome writes
// them (the same date and time above; below, the page n of N for every page). Adds a pattern to the
// report for the header and one for the footer, and removes the lines from the pages.
void remove_chrome_template(std::vector<std::vector<Line>>& pages, std::span<const float> heights, double band,
                            FurnitureReport& report) {
    std::vector<EdgeLines> edges(pages.size());
    bool header = true, footer = true;
    std::string date;
    for (std::size_t p = 0; p < pages.size(); ++p) {
        const float height = p < heights.size() ? heights[p] : 0.0f;
        if (height <= 0) return;
        edges[p] = edge_lines(pages[p], height, band);
        if (edges[p].top < 0) {
            header = false;
        } else {
            const std::string_view text = pages[p][static_cast<std::size_t>(edges[p].top)].text;
            const std::size_t n = chrome_date_time(text);
            if (n == 0 || (!date.empty() && text.substr(0, n) != date)) header = false;
            else if (date.empty()) date = std::string(text.substr(0, n));
        }
        long n = 0, total = 0;
        if (edges[p].bottom < 0 || !chrome_counter(pages[p][static_cast<std::size_t>(edges[p].bottom)].text, n, total) ||
            n != static_cast<long>(p) + 1 || total != static_cast<long>(pages.size())) {
            footer = false;
        }
    }
    for (const bool top : {true, false}) {
        if (top ? !header : !footer) continue;
        FurniturePattern pattern;
        pattern.top = top;
        pattern.pages = pages.size();
        pattern.page_text.resize(pages.size());
        std::vector<std::string> keys;
        for (std::size_t p = 0; p < pages.size(); ++p) {
            const int at = top ? edges[p].top : edges[p].bottom;
            const std::string& text = pages[p][static_cast<std::size_t>(at)].text;
            if (pattern.sample.empty()) pattern.sample = text;
            pattern.page_text[p] = text;
            keys.push_back(furniture_key(text));
        }
        std::ranges::sort(keys);
        pattern.variants = static_cast<std::size_t>(std::unique(keys.begin(), keys.end()) - keys.begin());
        report.removed_lines += pages.size();
        report.patterns.push_back(std::move(pattern));
        // Remove: the indices are those of the page as it was, so the footer goes first when both are removed.
    }
    for (std::size_t p = 0; p < pages.size(); ++p) {
        std::vector<int> gone;
        if (header) gone.push_back(edges[p].top);
        if (footer) gone.push_back(edges[p].bottom);
        std::ranges::sort(gone, std::greater{});
        for (const int at : gone) pages[p].erase(pages[p].begin() + at);
    }
}

}  // namespace

std::string furniture_key(std::string_view text) {
    std::string key;
    bool in_digits = false;
    for (char c : text::to_lower_ascii(text::collapse_spaces(text))) {
        if (c >= '0' && c <= '9') {
            if (!in_digits) key.push_back('#');
            in_digits = true;
        } else {
            in_digits = false;
            key.push_back(c);
        }
    }
    if (is_roman_numeral(key)) key = "#";
    return key;
}

static FurnitureReport remove_furniture_general(std::vector<std::vector<Line>>& pages,
                                                std::span<const float> page_heights,
                                                const FurnitureOptions& options) {
    FurnitureReport report;
    // A short document (two or three pages): a line must repeat on every page.
    if (pages.size() < 2) return report;
    const std::size_t min_pages = pages.size() > options.min_pages ? options.min_pages : pages.size();

    // 1. Candidates: the lines inside the top and bottom band of every page.
    std::vector<BandLine> band;
    // Pages with text in the body: an empty page that shows only its running
    // header (the back of a chapter page in a book) does not count against it.
    std::size_t text_pages = 0;
    for (std::size_t p = 0; p < pages.size(); ++p) {
        const float height = p < page_heights.size() ? page_heights[p] : 0.0f;
        if (height <= 0) {
            text_pages += !pages[p].empty();
            continue;
        }
        text_pages += std::ranges::any_of(pages[p], [&](const Line& l) {
            const double centre = (l.y0 + l.y1) / 2.0;
            return l.figure == 0 && centre >= options.band * height && centre <= (1.0 - options.band) * height;
        });
    }
    for (std::size_t p = 0; p < pages.size(); ++p) {
        const float height = p < page_heights.size() ? page_heights[p] : 0.0f;
        if (height <= 0) continue;
        for (std::size_t i = 0; i < pages[p].size(); ++i) {
            const Line& line = pages[p][i];
            if (line.figure > 0) continue;   // the place of a figure, not text
            const double centre = (line.y0 + line.y1) / 2.0;
            const bool top = centre < options.band * height;
            const bool bottom = centre > (1.0 - options.band) * height;
            if (!top && !bottom) continue;
            BandLine candidate;
            candidate.page = p;
            candidate.index = i;
            candidate.top = top;
            candidate.offset = top ? line.baseline : height - line.baseline;
            candidate.key = furniture_key(line.text);
            set_affixes(candidate);
            band.push_back(std::move(candidate));
        }
    }
    if (band.empty()) return report;

    // 2. Order: top zone first; inside a zone from the page edge inwards.
    std::ranges::sort(band, [](const BandLine& a, const BandLine& b) {
        if (a.top != b.top) return a.top;
        if (a.offset != b.offset) return a.offset < b.offset;
        if (a.page != b.page) return a.page < b.page;
        return a.index < b.index;
    });

    // blocked[zone][page]: a line that is NOT furniture lies further out.
    std::vector<bool> blocked[2] = {std::vector<bool>(pages.size(), false),
                                    std::vector<bool>(pages.size(), false)};

    // 3. Slot by slot: lines at the same distance from the page edge.
    for (std::size_t begin = 0; begin < band.size();) {
        std::size_t end = begin + 1;
        while (end < band.size() && band[end].top == band[begin].top &&
               band[end].offset - band[end - 1].offset <= kSlotTolerance) {
            ++end;
        }
        const std::span<BandLine> slot(band.data() + begin, end - begin);
        std::vector<bool>& zone_blocked = blocked[slot.front().top ? 0 : 1];
        begin = end;

        std::vector<std::size_t> slot_pages;
        for (const BandLine& line : slot) slot_pages.push_back(line.page);
        std::ranges::sort(slot_pages);
        slot_pages.erase(std::unique(slot_pages.begin(), slot_pages.end()), slot_pages.end());
        const double page_count = static_cast<double>(slot_pages.size());

        // The lines of one slot are sorted by offset, not by page: count pages
        // per text in page order so that "count a page once" works.
        std::vector<const BandLine*> by_page;
        for (const BandLine& line : slot) by_page.push_back(&line);
        std::ranges::sort(by_page, {}, &BandLine::page);
        PageCounter keys, heads, tails;
        for (const BandLine* line : by_page) {
            keys.add(line->key, line->page);
            heads.add(line->head, line->page);
            tails.add(line->tail, line->page);
        }

        std::size_t repeated_pages = 0, attached_pages = 0;
        std::size_t previous_page = pages.size();
        for (const BandLine* line : by_page) {
            if (line->page == previous_page || keys.pages(line->key) < min_pages) continue;
            previous_page = line->page;
            ++repeated_pages;
        }
        for (const std::size_t page : slot_pages) {
            if (!zone_blocked[page]) ++attached_pages;
        }
        const auto [best_head, head_pages] = heads.most_common();
        const auto [best_tail, tail_pages] = tails.most_common();
        const bool same_head = head_pages >= min_pages &&
                               static_cast<double>(head_pages) >= kMinSameAffix * page_count;
        const bool same_tail = tail_pages >= min_pages &&
                               static_cast<double>(tail_pages) >= kMinSameAffix * page_count;

        // A page number at the start or the end of the line on most pages of
        // the slot: "6 CHAPTER 3. RESULTS", "iv CONTENTS", "Overview 12".
        const auto numbered = [](std::string_view key) {
            return key == "#" || key.starts_with("# ") || key.ends_with(" #");
        };
        std::size_t numbered_pages = 0;
        {
            std::size_t last = pages.size();
            for (const BandLine* line : by_page) {
                if (line->page == last || !numbered(line->key)) continue;
                last = line->page;
                ++numbered_pages;
            }
        }
        // The number of the page: a number in the line that grows with the
        // page ("10 Kinetis ... Rev. 2" on page 10, "11 ..." on page 11).
        {
            PageCounter offsets;
            // Only a number at the start or the end of the line, where page numbers stand.
            const auto page_number_of = [&](const BandLine& line) -> std::optional<long> {
                const std::vector<long> numbers = numbers_in(pages[line.page][line.index].text);
                if (numbers.empty()) return std::nullopt;
                if (line.key == "#" || line.key.starts_with("# ")) return numbers.front();
                if (line.key.ends_with(" #")) return numbers.back();
                return std::nullopt;
            };
            for (const BandLine* line : by_page) {
                if (const auto n = page_number_of(*line)) {
                    offsets.add(std::to_string(*n - static_cast<long>(line->page)), line->page);
                }
            }
            const auto [best_offset, offset_pages] = offsets.most_common();
            if (offset_pages >= min_pages && static_cast<double>(offset_pages) >= 0.5 * page_count) {
                const long offset = std::stol(best_offset);
                for (BandLine& line : slot) {
                    const auto n = page_number_of(line);
                    line.page_number = n && *n - static_cast<long>(line.page) == offset;
                }
            }
        }
        const bool same_number = numbered_pages >= min_pages &&
                                 static_cast<double>(numbered_pages) >= kMinSameAffix * page_count;

        const bool is_furniture =
            slot_pages.size() >= min_pages &&
            page_count >= kMinCoverage * static_cast<double>(text_pages) &&
            static_cast<double>(attached_pages) >= kMinAttached * page_count &&
            (static_cast<double>(repeated_pages) >= kMinRepeated * page_count || same_head || same_tail ||
             same_number);

        // The style sheet makes a slot certain that repeats no text: a small style that stands in
        // this slot on nearly every page with text and nowhere else (a header that names the
        // chapter, a footer with the title of the page). Larger than the body it is a slide
        // title, not a header: the style sheet gives that role to small styles only.
        bool by_style = false;
        if (!is_furniture && options.styles != nullptr && slot_pages.size() >= 4 &&
            static_cast<double>(slot_pages.size()) >= 0.7 * static_cast<double>(text_pages) &&
            static_cast<double>(attached_pages) >= kMinAttached * page_count &&
            slot.size() * 10 <= slot_pages.size() * 12) {
            std::size_t styled = 0;
            for (const BandLine& line : slot) {
                styled += options.styles->role_of(pages[line.page][line.index]) == Role::Furniture;
            }
            by_style = styled * 10 >= slot.size() * 9;
        }

        if (!is_furniture && !by_style) {
            for (const std::size_t page : slot_pages) zone_blocked[page] = true;
            continue;
        }

        // Remove the lines that show the repetition; anything else in the
        // slot stays (and blocks the slots further in on its page).
        FurniturePattern pattern;
        pattern.top = slot.front().top;
        const std::size_t pattern_index = report.patterns.size();
        for (BandLine& line : slot) {
            const bool repeats = keys.pages(line.key) >= min_pages;
            const bool same_affix = (same_head && line.head == best_head) || (same_tail && line.tail == best_tail);
            // A line that only shares its first or last words with the others
            // is kept when it is the only text of its page: then it is the
            // content of the page, not a running header - unless it carries a
            // page number ("6 CHAPTER 3. RESULTS" on the empty back of a
            // chapter page in a book).
            const bool with_number = numbered(line.key);
            line.remove = repeats || (same_number && with_number) ||
                          (same_affix && (pages[line.page].size() > 1 || with_number)) ||
                          (by_style && options.styles->role_of(pages[line.page][line.index]) == Role::Furniture);
            line.pattern = pattern_index;
            line.everywhere = static_cast<double>(keys.pages(line.key)) >= kEverywhere * static_cast<double>(text_pages);
            if (!line.remove) zone_blocked[line.page] = true;
        }
        report.patterns.push_back(std::move(pattern));
    }

    // 3b. A marked line glued to the text body is content: put it back. From
    // the body outwards, so that a heading above a table header row that is
    // put back is put back too.
    {
        std::vector<std::vector<BandLine*>> by_page(pages.size());
        for (BandLine& line : band) {
            if (line.remove) by_page[line.page].push_back(&line);
        }
        for (std::size_t p = 0; p < by_page.size(); ++p) {
            if (by_page[p].empty()) continue;
            std::vector<bool> removed(pages[p].size(), false);
            for (const BandLine* line : by_page[p]) removed[line->index] = true;
            // Innermost first: band is sorted from the page edge inwards.
            std::ranges::sort(by_page[p], [](const BandLine* a, const BandLine* b) {
                if (a->top != b->top) return a->top;
                return a->offset > b->offset;
            });
            for (BandLine* line : by_page[p]) {
                if (line->page_number || line->everywhere) continue;   // never text of the body
                const Line& l = pages[p][line->index];
                float nearest = -1;
                for (std::size_t i = 0; i < pages[p].size(); ++i) {
                    const Line& other = pages[p][i];
                    if (removed[i] || other.figure > 0 || other.text.empty()) continue;
                    const float distance = line->top ? other.baseline - l.baseline : l.baseline - other.baseline;
                    if (distance <= kSlotTolerance) continue;   // the same slot, or further out
                    if (nearest < 0 || distance < nearest) nearest = distance;
                }
                if (nearest > 0 && nearest < kGluedToBody * l.size) {
                    line->remove = false;
                    removed[line->index] = false;
                }
            }
        }
    }

    // What each slot removed, after the lines glued to the body were put back.
    {
        std::vector<const BandLine*> by_page;
        for (const BandLine& line : band) {
            if (line.remove) by_page.push_back(&line);
        }
        std::ranges::stable_sort(by_page, {}, &BandLine::page);
        std::vector<std::vector<std::size_t>> removed_pages(report.patterns.size());
        std::vector<std::vector<std::string_view>> removed_keys(report.patterns.size());
        for (const BandLine* line : by_page) {
            FurniturePattern& pattern = report.patterns[line->pattern];
            const std::string& text = pages[line->page][line->index].text;
            if (pattern.sample.empty() && removed_pages[line->pattern].empty()) pattern.sample = text;
            if (pattern.page_text.empty()) pattern.page_text.resize(pages.size());
            if (pattern.page_text[line->page].empty()) pattern.page_text[line->page] = text;
            removed_pages[line->pattern].push_back(line->page);
            removed_keys[line->pattern].push_back(line->key);
        }
        for (std::size_t k = 0; k < report.patterns.size(); ++k) {
            auto& pages_of = removed_pages[k];
            pages_of.erase(std::unique(pages_of.begin(), pages_of.end()), pages_of.end());
            std::ranges::sort(removed_keys[k]);
            removed_keys[k].erase(std::unique(removed_keys[k].begin(), removed_keys[k].end()), removed_keys[k].end());
            report.patterns[k].pages = pages_of.size();
            report.patterns[k].variants = removed_keys[k].size();
        }
        std::erase_if(report.patterns, [](const FurniturePattern& p) { return p.pages == 0; });
    }

    // 4. Take the marked lines out of the pages.
    std::vector<std::vector<bool>> remove(pages.size());
    for (const BandLine& line : band) {
        if (!line.remove) continue;
        if (remove[line.page].empty()) remove[line.page].assign(pages[line.page].size(), false);
        remove[line.page][line.index] = true;
        ++report.removed_lines;
    }
    for (std::size_t p = 0; p < pages.size(); ++p) {
        if (remove[p].empty()) continue;
        std::vector<Line> kept;
        kept.reserve(pages[p].size());
        for (std::size_t i = 0; i < pages[p].size(); ++i) {
            if (!remove[p][i]) kept.push_back(std::move(pages[p][i]));
        }
        pages[p] = std::move(kept);
    }

    std::ranges::sort(report.patterns, [](const FurniturePattern& a, const FurniturePattern& b) {
        if (a.top != b.top) return a.top;
        if (a.pages != b.pages) return a.pages > b.pages;
        return a.sample < b.sample;
    });
    return report;
}

FurnitureReport remove_furniture(std::vector<std::vector<Line>>& pages,
                                 std::span<const float> page_heights,
                                 const FurnitureOptions& options) {
    FurnitureReport chrome;
    if (options.producer == Producer::Chrome && !pages.empty()) {
        remove_chrome_template(pages, page_heights, options.band, chrome);
    }
    FurnitureReport report = remove_furniture_general(pages, page_heights, options);
    if (chrome.removed_lines > 0) {
        report.removed_lines += chrome.removed_lines;
        report.patterns.insert(report.patterns.end(), std::make_move_iterator(chrome.patterns.begin()),
                               std::make_move_iterator(chrome.patterns.end()));
        std::ranges::sort(report.patterns, [](const FurniturePattern& a, const FurniturePattern& b) {
            if (a.top != b.top) return a.top;
            if (a.pages != b.pages) return a.pages > b.pages;
            return a.sample < b.sample;
        });
    }
    return report;
}

}  // namespace htpd
