// HTPD - figures: find the pictures and drawings of a page, with their captions.
//
// A figure is stored in the HTPD file as a picture (PNG), at its place in the
// text, with its page and caption, so an AI that can see pictures can look at
// it. Two kinds are found:
//
//   Picture  an image of the PDF (a photo, a screenshot, a scanned diagram),
//            large enough to be more than an icon or a logo.
//   Drawing  lines and shapes that form one region (a block diagram, a
//            timing diagram, a chart, a register layout). It counts as a
//            figure when a caption "Figure N" stands next to it, or when it
//            has many lines, little text and is no table.
//
// A caption "Table N" next to a drawing, rows of a table inside it, or rules
// across its whole width at three or more heights make it a table: tables are
// text already (FORMAT.md 4.2). A frame around lines of code is a listing. A region that covers
// almost the whole page (a frame) or lies in the page margin (a logo, a
// rule) is never a figure.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "htpd/types.hpp"

namespace htpd {

struct Figure {
    enum class Kind { Picture, Drawing };
    int page = 0;           // 0-based
    Box box;                // display space, points
    Kind kind = Kind::Picture;
    std::string caption;    // "Figure 3-2. Clock tree", or "" when none stands next to it
    std::uint64_t picture = 0;   // a figure that is exactly one picture: its key (PageText::image_keys), else 0
};

// A figure as it is stored: its picture as PNG.
struct FigureFile {
    int id = 0;             // from 1, in the order of the document
    int page = 0;           // 0-based
    Figure::Kind kind = Figure::Kind::Picture;
    std::string caption;
    int width = 0, height = 0;   // pixels
    std::string png;             // empty when `same_as` is set
    int same_as = 0;             // the id of an earlier figure with the very same picture (a logo on every page)
};

struct FigureOptions {
    float min_width = 60, min_height = 36, min_area = 4000;   // points
    float join_gap = 8;            // shapes closer than this belong to one drawing
    std::size_t min_paths = 10;    // a drawing without a caption needs this many shapes
    float max_text_cover = 0.2f;   // ... and text on at most this share of its area
    float caption_reach = 40;      // a caption stands at most this far above or below
    double margin_band = 0.07;     // the page margin, as a share of the page height
};

// The figures of one page, top to bottom. `lines` are the lines of the page
// in display space; images, paths and forms are the drawn objects (they are
// in the same space unless the page was turned: then only pictures count).
// `code[i]` is 1 when lines[i] is set in a monospace font: a framed listing
// is code, not a figure.
std::vector<Figure> find_figures(int page, float width, float height, std::span<const Box> images,
                                 std::span<const Box> paths, std::span<const Box> forms,
                                 std::span<const Line> lines, bool turned, const FigureOptions& options = {},
                                 std::span<const char> code = {}, std::span<const std::uint64_t> image_keys = {});

// ---- building blocks, exposed for testing --------------------------------

// "Figure 3-2. Clock tree", "Fig. 4 ...", "FIGURE A.1 ..." - not "Figures 3
// and 4 show" or a sentence that only names a figure later in the line.
bool is_figure_caption(std::string_view line);
bool is_table_caption(std::string_view line);

}  // namespace htpd
