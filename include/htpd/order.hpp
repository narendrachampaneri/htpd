// HTPD - the reading order of a page with more than one column.
//
// The converter keeps the order in which the PDF stores the text (FORMAT.md 7). That is the reading
// order of almost every PDF, also with two columns, so a page is only touched where its stored order
// is clearly not the order a person reads:
//
//   - the lines of two columns come row by row (a line of the left column, a line of the right
//     one), or the page is stored bottom to top: the blocks of the page are read from the page;
//   - a box, a pull quote or a figure stands in the middle of a paragraph that goes on below it: the
//     paragraph is read first, the box after it;
//   - a caption or a box is stored far from its place: it goes to its place at the page;
//   - a margin note is read after the paragraph beside it; footnotes are read last.
//
// The blocks come from an XY-cut: the page is cut at the widest strip free of text (also of a figure,
// a table, a listing or a drawn frame), down and across, again and again, into columns and bands.
// Tables, listings, figures and framed or shaded areas are blocks of their own and are never cut. A
// page whose stored order is consistent with its blocks is not changed: not the order of its columns,
// not a table stored cell by cell. A page of a single column in good order stays as it is, byte for byte.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "htpd/styles.hpp"
#include "htpd/types.hpp"

namespace htpd {

// Auto: the pages that need it are put into reading order (see above). Stored: the order of the PDF, always.
enum class OrderMode { Auto, Stored };

struct OrderOptions {
    const StyleSheet* styles = nullptr;   // footnotes are found by their style (Role::Footnote)
    std::span<const FontFacts> fonts;     // monospace text is a listing: a block of its own
    std::span<const Box> frames;          // drawn frames and shaded areas of the page (display space)
    float page_width = 0, page_height = 0;
};

struct OrderReport {
    std::size_t pages = 0;                // pages whose order changed
    std::size_t lines_split = 0;          // lines of two columns stored as one, cut into their columns
    std::vector<int> page_numbers;        // 0-based, the first few
};

// Puts the lines of one page (display space, stored order) into reading order. Returns true when the
// order changed; `report` counts what was done.
bool order_page(std::vector<Line>& lines, const OrderOptions& options, OrderReport& report);

}  // namespace htpd
