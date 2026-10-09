// HTPD - heading detection.
//
// Two sources, in order of trust:
//   1. PDF bookmarks  - written by the author, so they are the most reliable.
//      Each bookmark is located in the page text so the heading line itself
//      can be taken out of the body.
//   2. The text       - numbered lines ("5.2.1 Clock tree") that are set in a
//      larger, bolder or different font than the body text, checked against
//      the numbering sequence.
//
// Bookmarks often stop at the second or third heading level. In Auto mode the
// numbered headings found in the text are therefore added below a bookmark
// when their number extends the bookmark's number ("9.1.2" under "9.1").
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "htpd/types.hpp"

namespace htpd {

enum class HeadingMode {
    Auto,        // bookmarks, refined with numbered headings from the text
    Bookmarks,   // bookmarks only
    Text,        // ignore bookmarks
    None,        // no headings: one section per page
};

struct HeadingOptions {
    HeadingMode mode = HeadingMode::Auto;
    // Auto mode uses bookmarks when the PDF has at least this many usable ones.
    std::size_t min_bookmarks = 3;
};

struct HeadingResult {
    std::vector<Heading> headings;        // sorted by position in the text
    HeadingSource source = HeadingSource::None;
    float body_size = 0;                  // most common font size of the text
    std::size_t bookmarks_total = 0;      // when bookmarks were used
    std::size_t bookmarks_located = 0;    // ... of which were found in the text
    std::size_t text_added = 0;           // headings added below the bookmark level
};

// `lines` are all lines of the document in reading order (page by page).
// `page_transforms[p]` converts bookmark destinations of page p to display space.
HeadingResult detect_headings(std::span<const Line> lines,
                              std::span<const OutlineEntry> outline,
                              std::span<const Affine> page_transforms,
                              const HeadingOptions& options = {});

// ---- building blocks, exposed for testing --------------------------------

struct SectionNumber {
    std::vector<int> parts;   // "5.2.1" -> {5,2,1}; letters: "A.1" -> {1000,1}
    std::string text;         // as written, without a trailing dot
    bool keyword = false;     // came from "Chapter 3", "Appendix A", ...
};

// Parses a section number at the start of `line`. `rest` receives the text
// after the number. Returns nothing when the line does not start with one.
std::optional<SectionNumber> parse_section_number(std::string_view line, std::string_view& rest);

// True when `next` may directly follow `prev` in a table of contents:
// 2.1 -> 2.2, 2.1 -> 2.1.1, 2.1.3 -> 2.2, 2.9 -> 3.
bool is_successor(std::span<const int> prev, std::span<const int> next);

// The font size most of the text is set in.
float body_font_size(std::span<const Line> lines);

}  // namespace htpd
