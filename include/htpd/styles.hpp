// HTPD - the style sheet of a document, and the program that made it.
//
// A document is written with a few styles: body text, headings of two or
// three levels, captions, table text, code, a running header and footer,
// footnotes. This step finds them again before the headings are looked for.
// Every line is put into a style (font, size, weight, italic, colour); for
// each style it is counted how much of the text it holds, how often it starts
// a block of lines, stands alone before body text, carries a number or the
// word "Figure"/"Table", and where on the page it stands. From that the styles
// get a role.
//
// The roles are only used where they make a decision more certain:
//   - a style that is rare, short, stands alone before body text and ends
//     without a full stop is a heading style, however small its difference
//     from the body is (a colour, a weight, a font): the lines set in it are
//     headings (headings.cpp), and the order of such styles gives the levels;
//   - a caption style is never a heading; a heading style is never a header;
//   - a small style that sits in the top or bottom band on most pages is
//     page furniture, even when its text changes from page to page
//     (furniture.cpp).
// Where the style sheet is not sure, the result of the ordinary rules stays.
//
// The producer (Producer/Creator of the PDF) tells which habits to expect:
// Chrome prints a date and the title above and an address and "n/N" below the
// page; LaTeX sets "Chapter N" on a line above the title of a chapter.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "htpd/types.hpp"

namespace htpd {

// ---- the program that made the PDF -----------------------------------------

enum class Producer { Unknown, Word, LibreOffice, LaTeX, Chrome, Wkhtmltopdf, InDesign, Other };

const char* to_string(Producer producer);

// Classifies a PDF by its Creator and Producer entries.
Producer classify_producer(std::string_view creator, std::string_view producer);

// ---- styles ----------------------------------------------------------------

enum class Role : std::uint8_t { Other, Body, Heading, Caption, TableText, Code, Furniture, Footnote };

const char* to_string(Role role);

struct Style {
    std::uint16_t font = 0;
    int size2 = 0;            // size in half points
    bool bold = false;
    bool italic = false;
    std::uint32_t color = 0;  // 0xRRGGBB

    std::size_t lines = 0;    // lines set in this style
    std::size_t bytes = 0;    // their text
    std::size_t pages = 0;    // pages that have such a line
    std::size_t blocks = 0;   // runs of lines of this style on one page
    std::size_t short_blocks = 0;     // ... of at most 3 lines and 20 words
    std::size_t open_blocks = 0;      // ... that end without a full stop, comma or semicolon
    std::size_t before_body = 0;      // ... directly followed by a line in the body style
    std::size_t before_text = 0;      // ... directly followed by body, table or code text
    std::size_t numbered = 0;         // lines that start with a section number
    std::size_t depth_sum = 0;        // ... the sum of their depths ("3.2.1" is 3)
    std::size_t caption_like = 0;     // lines that start with "Figure 3", "Table 2-1"
    std::size_t table_like = 0;       // lines with wide gaps, or rows of a table
    std::size_t edge = 0;             // lines in the top or bottom band of the page
    std::size_t low = 0;              // lines in the lower third of the page
    std::size_t first_line = 0;       // position of the first line (order of appearance)

    Role role = Role::Other;
    int level = 0;            // heading level (1 = highest), 0 for other roles
};

class StyleSheet {
public:
    std::vector<Style> styles;
    int body = -1;                 // index of the body style, -1 when there is none
    float body_size = 0;
    Producer producer = Producer::Unknown;

    // The style of a line: its index, or -1 for a line that has no style
    // (the place of a figure, an empty line, text read from a picture).
    int find(const Line& line) const;
    Role role_of(const Line& line) const;
    // The heading level of the line's style, 0 when its style is no heading style.
    int heading_level(const Line& line) const;

    // One short line per role, for the conversion notes ("heading 2: Arial
    // Bold 12 pt, 84 lines").
    std::vector<std::string> describe(std::span<const FontFacts> fonts) const;

    // Internal: the lookup from the style of a line to its index.
    std::unordered_map<std::uint64_t, int> index;
};

struct StyleOptions {
    double band = 0.09;            // top and bottom band of the page (like FurnitureOptions::band)
    std::size_t min_blocks = 3;    // a heading style needs at least this many blocks ...
    std::size_t page_count = 0;    // ... or 2 when the document has at most 3 pages
};

// `pages[i]` holds the lines of page i, `heights[i]` its height.
StyleSheet build_style_sheet(std::span<const std::vector<Line>> pages, std::span<const float> heights,
                             std::span<const FontFacts> fonts, Producer producer, const StyleOptions& options = {});

// True when the line starts like a caption: "Figure 3", "Fig. 2.1:", "Table 4-2".
bool starts_like_caption(std::string_view text);

}  // namespace htpd
