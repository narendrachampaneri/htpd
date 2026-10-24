// HTPD - sections: cut the document at its headings, build the section tree
// and join the lines of each section into paragraphs.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "htpd/types.hpp"

namespace htpd {

struct SectionOptions {
    // Default (false): a word split as "inter-" / "rupt" becomes "inter-rupt",
    // so no character of the source is lost. True: it becomes "interrupt".
    bool dehyphenate = false;
    // A table over a page break in one block, with a mark where the page changes (tables.hpp).
    bool join_table_pages = false;
    // Facts about the fonts of the document, indexed by Line::font. Two or
    // more lines in a monospace font are kept as code: line by line, with the
    // spacing they have on the page.
    std::vector<FontFacts> fonts;
};

// `lines` are all lines of the document in reading order; `headings` must be
// sorted by line_index. Text before the first heading becomes "Front matter".
// A document without headings gets one section per page.
std::vector<Section> build_sections(std::span<const Line> lines, std::span<const Heading> headings,
                                    const SectionOptions& options = {});

// ---- building blocks, exposed for testing --------------------------------

// Appends the next line of a paragraph (or of a table cell) to `paragraph`:
// with a space, or without one where a word is broken at a hyphen.
void append_wrapped_line(std::string& paragraph, std::string_view line, bool dehyphenate);

// Median distance between the baselines of consecutive lines, in font sizes
// (about 1.2 for single-spaced text).
float typical_line_pitch(std::span<const Line> lines);

std::vector<Paragraph> build_paragraphs(std::span<const Line> lines, float line_pitch,
                                        const SectionOptions& options = {});

// A table row: the line with " | " between its cells.
std::string row_text(const Line& line);

// The rows of one table. When some rows have fewer cells than others and the
// positions on the page show without doubt which columns they belong to, the
// missing cells are written as empty cells ("Output current | 0 |  | 200 | A"),
// so every value stays under its own column heading.
std::string rows_text(std::span<const Line* const> rows);
// The same, where rest[i] = 1 marks a line that holds the rest of a cell of the
// row above it (a cell that wraps onto more lines); it is joined to that cell.
std::string rows_text(std::span<const Line* const> rows, std::span<const std::uint8_t> rest);

}  // namespace htpd
