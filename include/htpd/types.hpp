// HTPD - core data types shared by every stage of the converter.
//
// Coordinate convention used everywhere after the PDF reader:
//   "display space" = what a person sees on screen, in PDF points (1/72 inch),
//   origin at the TOP-LEFT of the page, x grows right, y grows DOWN.
//   Page rotation is already applied, so text on a landscape page is upright.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace htpd {

inline constexpr const char* kVersion = "0.1.1";
inline constexpr const char* kFormatVersion = "0.1";   // HTPD field schema
inline constexpr const char* kOkfVersion = "0.2";      // OKF spec the bundle follows

struct Error {
    std::string message;
};

template <class T>
using Result = std::expected<T, Error>;

inline std::unexpected<Error> fail(std::string message) {
    return std::unexpected(Error{std::move(message)});
}

// 2-D affine map: x' = a*x + c*y + e,  y' = b*x + d*y + f  (PDF matrix order).
struct Affine {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

    constexpr void apply(float x, float y, float& ox, float& oy) const {
        ox = a * x + c * y + e;
        oy = b * x + d * y + f;
    }
};

// One character as placed on the page.
struct Glyph {
    char32_t cp = 0;          // Unicode code point; U' ' for any kind of space
    bool is_space = false;    // true: only `cp` is meaningful (no geometry)
    bool bold = false;
    bool span = false;        // from the /ActualText of a span: its place is only roughly known
    std::uint16_t font = 0;   // same number = same font in this document; 0 = unknown
    float x0 = 0, y0 = 0;     // top-left of the tight glyph box (display space)
    float x1 = 0, y1 = 0;     // bottom-right
    float origin_x = 0;       // x of the glyph origin (where the pen stood)
    float baseline = 0;       // y of the glyph origin
    float size = 0;           // effective font size in points
};

// A link of a page that jumps to another place in the same document.
struct PageLink {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // its box on its page (display space)
    std::string text;                       // the words under the box, as on the page
    int target_page = -1;                   // 0-based page it jumps to
    bool has_target_point = false;          // false: it jumps to the page as a whole
    float target_x = 0, target_y = 0;       // the point it jumps to (PDF user space of that page)
};

// A rectangle on the page, display space (points, y down).
struct Box {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float width() const { return x1 - x0; }
    float height() const { return y1 - y0; }
};

// A drawn image, rows from the top: one byte per pixel (grey, channels 1)
// or three (red, green, blue, channels 3).
struct Image {
    int width = 0, height = 0;
    int channels = 1;
    std::vector<std::uint8_t> pixels;
};

struct PageText {
    int index = 0;                       // 0-based page index
    float width = 0, height = 0;         // display size in points
    Affine user_to_display;              // PDF user space -> display space
    std::vector<Glyph> glyphs;           // in the order the PDF library reports
    std::size_t skipped_rotated = 0;     // glyphs dropped: not upright on the page
    int turned = 0;                      // quarter turns applied so that the main text is upright
    std::size_t unmapped = 0;            // glyphs without a Unicode mapping
    std::vector<PageLink> links;         // jumps to other places of the document
    std::vector<Box> images;             // picture objects (photos, scans, pictures of text)
    std::vector<std::uint64_t> image_keys;   // for each picture: a hash of its data (the same picture, the same key)
    std::vector<Box> paths;              // line and shape objects (drawings, table rules, frames)
    std::vector<Box> forms;              // form objects (groups drawn as one, often a whole drawing)
    std::size_t objects = 0;             // everything drawn on the page: text, pictures, lines, shapes
};

// One visual line of text.
struct Line {
    int page = 0;                        // 0-based page index
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float baseline = 0;
    float size = 0;                      // dominant font size of the line
    bool bold = false;                   // most glyphs are bold
    std::uint16_t font = 0;              // dominant font of the line (see Glyph::font)
    std::string text;                    // UTF-8, single spaces between words
    // Byte offsets in `text` of spaces that stand for a WIDE horizontal gap
    // (a column gap in a table row, or the gap after a heading number).
    std::vector<std::uint32_t> gaps;
    // For each entry of `gaps`: where the gap begins (right edge of the text
    // before it) and where it ends (left edge of the text after it). Used to
    // put the cells of table rows into their columns.
    std::vector<float> gap_begin, gap_end;
    // Exact horizontal positions, used to rebuild the spacing of monospace
    // text (code listings, diagrams drawn with characters):
    float origin_x = 0;                  // pen position of the first character
    // For every space in `text`, in order: the distance in points from the pen
    // position of the character before it to that of the character after it.
    std::vector<float> space_spans;
    // For every space in `text`, in order: where the text before it ends and the
    // text after it begins (points). Tells the columns of a table whose gaps
    // are too narrow to be sure of in one line alone (see columns.hpp).
    std::vector<float> space_begin, space_end;
    // A row of a table whose columns line up with the rows around it (columns.hpp):
    // a row even with only two cells.
    bool table = false;
    // Read by OCR, not from the text of the PDF: from a scanned page (Page) or
    // from a picture on a page that has text (Picture). Picture text is the
    // content of a figure: it is never taken for a heading.
    enum class Ocr : std::uint8_t { None, Page, Picture } ocr = Ocr::None;
    // Not text: the place of figure number `figure` (from 1) in the text. Its
    // box is the figure's box; its text is empty.
    int figure = 0;
};

// A PDF bookmark (outline entry).
struct OutlineEntry {
    std::string title;
    int level = 1;                       // 1 = top level
    int page = -1;                       // 0-based, -1 = unknown
    std::optional<float> user_x;         // destination point in PDF user space
    std::optional<float> user_y;
};

// What is known about one font of the document (index = Glyph::font).
struct FontFacts {
    bool monospace = false;   // every character has the same width (code, listings)
    float advance = 0.6f;     // typical character width as a fraction of the font size
};

struct DocumentMeta {
    std::string title;
    std::string author;
    std::string subject;
    int page_count = 0;
};

// RunningTitle: the running header of the pages changes there (a document with
// few headings); Pages: a long stretch of text cut into parts of a few pages.
// Contents: a printed table of contents, its titles found in the text.
enum class HeadingSource { Bookmark, Numbering, FontSize, None, RunningTitle, Pages, Contents };

constexpr const char* to_string(HeadingSource s) {
    switch (s) {
        case HeadingSource::Bookmark:  return "bookmark";
        case HeadingSource::Numbering: return "numbering";
        case HeadingSource::FontSize:  return "font-size";
        case HeadingSource::None:      return "none";
        case HeadingSource::RunningTitle: return "running-title";
        case HeadingSource::Pages:     return "pages";
        case HeadingSource::Contents:  return "contents";
    }
    return "none";
}

// A heading located in the flat list of lines.
struct Heading {
    std::string title;                   // full text, e.g. "5.2.1 Clock tree"
    std::string number;                  // "5.2.1", "A.1" or ""
    int level = 1;
    int page = 0;                        // 0-based
    std::size_t line_index = 0;          // first line of / after the heading
    std::size_t line_count = 0;          // lines the heading text occupies (0 = not in text)
    HeadingSource source = HeadingSource::None;
};

enum class ParagraphKind {
    Text,   // ordinary paragraph: its lines are joined
    Rows,   // table rows: one row per '\n', cells split by " | " (an empty cell: nothing between two bars)
    Code,   // monospace block: one line per '\n', spacing kept
    Figure, // a figure stands here: `text` is its number ("12")
};

struct Paragraph {
    int page = 0;                        // page where the paragraph starts (0-based)
    ParagraphKind kind = ParagraphKind::Text;
    std::string text;
};

// A link from the text of a section to another section.
struct SectionLink {
    std::string text;                    // the words of the link, as on the page
    std::size_t target = 0;              // index into the section vector
    int page = 0;                        // 0-based page of the place it jumps to
};

struct Section {
    std::size_t order = 0;               // position in document order, 0-based
    std::string title;
    std::string number;
    int level = 1;
    HeadingSource source = HeadingSource::None;
    std::optional<std::size_t> parent;   // index into the section vector
    std::vector<std::size_t> children;
    int page_start = 0;                  // own text, 0-based inclusive
    int page_end = 0;
    int span_page_end = 0;               // including all subsections
    std::vector<Paragraph> paragraphs;   // own text only (not the subsections')
    std::string file;                    // bundle-relative path, set by the writer
    std::size_t first_line = 0;          // first line of the section (its heading), index into the lines
    std::vector<SectionLink> links;      // links in its text to other sections
    std::vector<int> ocr_pages;          // 0-based pages of its own text that hold text read by OCR
};

}  // namespace htpd
