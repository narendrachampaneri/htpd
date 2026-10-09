#include "htpd/furniture.hpp"

#include <algorithm>
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
};

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

FurnitureReport remove_furniture(std::vector<std::vector<Line>>& pages,
                                 std::span<const float> page_heights,
                                 const FurnitureOptions& options) {
    FurnitureReport report;
    if (pages.size() < options.min_pages + 1) return report;   // too short to judge

    // 1. Candidates: the lines inside the top and bottom band of every page.
    std::vector<BandLine> band;
    std::size_t text_pages = 0;
    for (std::size_t p = 0; p < pages.size(); ++p) {
        if (!pages[p].empty()) ++text_pages;
        const float height = p < page_heights.size() ? page_heights[p] : 0.0f;
        if (height <= 0) continue;
        for (std::size_t i = 0; i < pages[p].size(); ++i) {
            const Line& line = pages[p][i];
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
            if (line->page == previous_page || keys.pages(line->key) < options.min_pages) continue;
            previous_page = line->page;
            ++repeated_pages;
        }
        for (const std::size_t page : slot_pages) {
            if (!zone_blocked[page]) ++attached_pages;
        }
        const auto [best_head, head_pages] = heads.most_common();
        const auto [best_tail, tail_pages] = tails.most_common();
        const bool same_head = head_pages >= options.min_pages &&
                               static_cast<double>(head_pages) >= kMinSameAffix * page_count;
        const bool same_tail = tail_pages >= options.min_pages &&
                               static_cast<double>(tail_pages) >= kMinSameAffix * page_count;

        const bool is_furniture =
            slot_pages.size() >= options.min_pages &&
            page_count >= kMinCoverage * static_cast<double>(text_pages) &&
            static_cast<double>(attached_pages) >= kMinAttached * page_count &&
            (static_cast<double>(repeated_pages) >= kMinRepeated * page_count || same_head || same_tail);

        if (!is_furniture) {
            for (const std::size_t page : slot_pages) zone_blocked[page] = true;
            continue;
        }

        // Remove the lines that show the repetition; anything else in the
        // slot stays (and blocks the slots further in on its page).
        FurniturePattern pattern;
        pattern.top = slot.front().top;
        pattern.variants = 0;
        const BandLine* first_removed = nullptr;
        std::vector<std::size_t> removed_pages;
        std::vector<std::string_view> removed_keys;
        for (BandLine& line : slot) {
            line.remove = keys.pages(line.key) >= options.min_pages ||
                          (same_head && line.head == best_head) ||
                          (same_tail && line.tail == best_tail);
            if (!line.remove) {
                zone_blocked[line.page] = true;
                continue;
            }
            removed_pages.push_back(line.page);
            removed_keys.push_back(line.key);
            if (first_removed == nullptr || line.page < first_removed->page) first_removed = &line;
        }
        if (first_removed == nullptr) continue;
        std::ranges::sort(removed_pages);
        removed_pages.erase(std::unique(removed_pages.begin(), removed_pages.end()), removed_pages.end());
        std::ranges::sort(removed_keys);
        removed_keys.erase(std::unique(removed_keys.begin(), removed_keys.end()), removed_keys.end());
        pattern.pages = removed_pages.size();
        pattern.variants = removed_keys.size();
        pattern.sample = pages[first_removed->page][first_removed->index].text;
        report.patterns.push_back(std::move(pattern));
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

}  // namespace htpd
