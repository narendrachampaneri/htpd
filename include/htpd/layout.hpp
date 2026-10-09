// HTPD - layout: turn the glyphs of one page into lines of text.
#pragma once

#include <vector>

#include "htpd/types.hpp"

namespace htpd {

struct LayoutOptions {
    // A glyph stays on the current line while its baseline is within this
    // fraction of the font size (keeps superscripts and subscripts attached).
    float baseline_tolerance = 0.45f;
    // A horizontal gap wider than this many font sizes is a column gap.
    float column_gap = 2.0f;
    // A gap wider than this always gets a space, even if the PDF has none.
    float forced_space_gap = 1.0f;
};

// Lines of one page in reading order.
std::vector<Line> build_lines(const PageText& page, const LayoutOptions& options = {});

}  // namespace htpd
