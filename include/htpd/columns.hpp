// HTPD - the columns of tables whose gaps are narrow.
//
// The line builder marks a column gap when the space between two words is
// wider than two font sizes. Many tables are set tighter (LaTeX, web pages,
// slides): "VIN Supply voltage -0.5 32 V" has gaps of about one font size.
// One line alone cannot tell such a gap from a wide space, but the rows of a
// table can: their columns leave the same strip of the page free in every
// row. Starting from lines that have a sure column gap, this finds the
// strips that stay free in all rows of the block, and marks the spaces that
// lie on them as column gaps - also in the header row above, when its words
// fit the columns. A line of running text crosses the strips and is left as
// it is.
#pragma once

#include <span>
#include <vector>

#include "htpd/types.hpp"

namespace htpd {

struct ColumnOptions {
    float min_gutter = 0.8f;     // a free strip at least this many font sizes wide
    std::size_t min_rows = 3;    // rows of a block with a sure gap, at least
    float max_row_step = 2.6f;   // rows at most this many font sizes apart
};

// The lines of one page, in reading order. `skip[i]` true: leave line i as it
// is (code listings, figures). Returns the number of gaps added.
std::size_t mark_table_columns(std::vector<Line>& lines, std::span<const char> skip, const ColumnOptions& options = {});

}  // namespace htpd
