// HTPD - tagged PDF: use the structure tree the PDF carries.
//
// A tagged PDF (Word, LibreOffice, InDesign, Chrome with tags ...) says in its
// own structure tree what each piece of text is: a heading of level N, a cell
// of a table, a list item, a figure with its caption, or page furniture (an
// /Artifact). PDFium gives the tree and, for every character, the
// marked-content ID that ties it to an element (pdf_reader_pdfium.cpp).
//
// The tags are used where they can be trusted, and only there:
//   - every piece of text an element names is found on its page, as the
//     characters that carry its marked-content ID;
//   - the characters of an element stand together in the order the PDF stores
//     them (nothing else is between them), and a table's cells are laid out
//     in the order of its rows and columns on the page;
//   - nothing untagged, or tagged otherwise, lies inside a table.
// Where a test fails, the element is left to the ordinary rules (layout,
// headings, tables from positions): the text is built exactly as for a PDF
// without tags. Text that is not tagged is never dropped; only lines the PDF
// marks as pagination artifacts (header, footer, page number) at the edge of
// the page are taken out, and listed.
//
// What the tags give:
//   H, H1-H6          Line::tag_heading (headings.cpp turns it into headings)
//   Table/TR/TH/TD    rows with cells (Line::cells), spans written as [>] / [=]
//   L/LI/Lbl/LBody    Line::tag_item on the first line of each item
//   Artifact          lines taken out of the page at its edge, listed as removed furniture
//   Figure + Caption  figures with their caption (figures.cpp)
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "htpd/figures.hpp"
#include "htpd/furniture.hpp"
#include "htpd/types.hpp"

namespace htpd {

// Whether the structure tags of a PDF are read.
//   Auto  the default. Read them when the PDF is tagged, has no bookmarks (then
//         they give the headings) and has at most kAutoTagPages pages. PDFium
//         builds the tree of a page from the whole structure of the document,
//         so reading it costs time that grows faster than the document does.
//   On    read them whenever the PDF has a structure tree.
//   Off   never.
enum class TagMode { Auto, On, Off };

inline constexpr int kAutoTagPages = 200;

// Whether to read the tags of a PDF: `tagged` is what the PDF says about itself,
// `bookmarks` the number of bookmarks that point at a page. When the answer is
// no for a tagged PDF in Auto mode, `why` says why.
bool should_read_tags(TagMode mode, bool tagged, std::size_t bookmarks, int pages, std::string& why);

// What the tags gave, over the whole document.
struct TagReport {
    bool unread = false;               // the PDF is tagged, but Auto did not read the tags
    std::string unread_reason;         // ... and why
    std::size_t pages = 0;             // pages that have a structure tree
    std::size_t headings = 0;          // heading elements used
    std::size_t tables = 0;            // tables taken from the tags
    std::size_t table_rows = 0;
    std::size_t spans = 0;             // cells that span rows or columns
    std::size_t lists = 0;             // lists with items used
    std::size_t list_items = 0;
    std::size_t figures = 0;           // figures taken from the tags
    std::size_t artifact_lines = 0;    // lines taken out as artifacts
    // Elements the tags named but that were left to the ordinary rules.
    std::size_t headings_left = 0;
    std::size_t tables_left = 0;
    std::size_t items_left = 0;
    bool any() const { return pages > 0; }
};

// A line of page furniture the PDF marks as an artifact.
struct ArtifactLine {
    int page = 0;
    bool top = true;
    std::string text;
};

struct TaggedPage {
    std::vector<Line> lines;   // lines the PDF marks as artifacts have Line::artifact set
};

// Adds the artifact lines to the report of removed headers and footers: one
// pattern per position and text (digits ignored), as remove_furniture does.
void add_artifacts(FurnitureReport& report, std::span<const ArtifactLine> artifacts, std::size_t page_count);

// The figures the tags name on one page: a Figure element with a picture or
// drawing (its box) and the text of its Caption, if it has one.
struct TagFigure {
    Box box;
    bool picture = false;     // only pictures are drawn there (no shapes)
    std::string caption;      // the Caption element's text, or the figure's alternate text
    bool has_caption = false; // false: `caption` is the alternate text (may be empty)
};

// Lines of one page. A page without a structure tree gives exactly
// build_lines(page). Sets the `tag` of the page's glyphs (so `page` is changed).
TaggedPage build_tagged_lines(PageText& page, TagReport& report);

// How many lines the PDF marks as artifacts at the top or bottom of their page.
std::size_t count_artifacts(const std::vector<std::vector<Line>>& pages, std::span<const float> page_heights);

// Takes out the lines the PDF marks as artifacts (Line::artifact) in the top or
// bottom fifth of their page and returns them. Run it after remove_furniture:
// that step sees the artifacts too, so a header it finds on most pages is
// found on all of them, and what it leaves is taken here.
std::vector<ArtifactLine> take_artifacts(std::vector<std::vector<Line>>& pages, std::span<const float> page_heights);

// The figures of the page's structure tree. Only reads.
std::vector<TagFigure> tagged_figures(const PageText& page);

// Puts the figures of the tags together with those found by the ordinary rules
// (`found`, top to bottom): a figure both name gets the caption of the tags when
// the rules found none; a figure only the tags name is added unless it is an
// icon, a frame round the page or a logo in the margin. Returns how many
// figures the tags gave something to.
std::size_t merge_tag_figures(std::vector<Figure>& found, std::span<const TagFigure> tagged, int page, float width,
                              float height, double margin_band);

}  // namespace htpd
