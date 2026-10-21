// HTPD - page furniture: running headers, footers and page numbers.
//
// These repeat on every page and carry no content, so they are removed from
// the text. Nothing is removed silently: the report lists what was removed.
//
// A line is only treated as furniture when ALL of this holds:
//   - it lies in the top or bottom band of the page,
//   - lines sit at that same distance from the page edge on a large share
//     of the pages (a "slot"),
//   - nothing but other furniture lies between the slot and the page edge,
//   - the lines of the slot repeat: either the same text (digits ignored) on
//     most pages, or the same first/last two words ("... | Page 12").
// Text that merely repeats at the top of some pages - a table header on
// continuation pages, a sub-heading such as "Synopsis" - fails these tests
// and stays in the text.
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "htpd/types.hpp"

namespace htpd {

struct FurnitureOptions {
    // Only lines inside the top or bottom band of the page are candidates.
    // The band is this fraction of the page height.
    double band = 0.09;
    // A text counts as repeating when it is found on at least this many pages.
    std::size_t min_pages = 3;
};

// What was removed from one position (slot) of the page.
struct FurniturePattern {
    std::string sample;         // text of the first line that was removed
    std::size_t pages = 0;      // number of pages it was removed from
    std::size_t variants = 1;   // number of different texts (digits ignored)
    bool top = true;            // header (true) or footer (false)
    // The removed text on each page, by page index ("" where none): a running
    // header that names the chapter tells where a chapter starts.
    std::vector<std::string> page_text;
};

struct FurnitureReport {
    std::size_t removed_lines = 0;
    std::vector<FurniturePattern> patterns;
};

// `pages[i]` holds the lines of page i; `page_heights[i]` its height.
FurnitureReport remove_furniture(std::vector<std::vector<Line>>& pages,
                                 std::span<const float> page_heights,
                                 const FurnitureOptions& options = {});

// The comparison key of a header/footer line: lower case, every run of
// digits replaced by '#', so "Page 12" and "Page 13" are the same pattern.
std::string furniture_key(std::string_view text);

}  // namespace htpd
