// HTPD - structure for documents with few or no headings.
//
// A document whose author gave it no headings, or only a title, would become
// one long section. That is hard for an AI to use: every answer points at the
// same place. Two rules give such a document parts it can point at:
//
//   1. Running titles. When the running header of the pages (removed as page
//      furniture) changes - "Section 2: Background" on pages 3-9, "Section 3:
//      Methods" from page 10 - each change starts a section with that text as
//      its title. Only used when the document has few headings, and only when
//      the header changes seldom (a header that changes on every page, such
//      as a book title on even pages and a chapter title on odd pages, is no
//      chapter mark).
//   2. Page parts. Own text of a section that runs over more than
//      `max_pages` pages is cut into parts of about `part_pages` pages,
//      titled "2.2 CSR listing - pages 12-13" (with the topics a running
//      header gives those pages, if any). A cut is only made where a sentence
//      ends (or at the top of a page after a short line) and never next to a
//      table row, so no paragraph is broken and a table keeps its header.
//
// Neither rule adds or drops text: they only add headings that point at lines
// (line_count 0: the title is not in the text).
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "htpd/furniture.hpp"
#include "htpd/types.hpp"

namespace htpd {

struct StructureOptions {
    // "Few headings": fewer than 3, or fewer than pages / this. Then running
    // titles are used and text is cut after `max_pages` pages.
    std::size_t pages_per_heading = 15;
    int max_pages = 3;    // own text longer than this many pages is cut ...
    int max_pages_headed = 8;   // ... in a document with headings enough: this many
    int part_pages = 2;   // ... into parts of about this many pages
};

struct StructureReport {
    std::size_t running_titles = 0;   // sections started by a running header
    std::size_t page_parts = 0;       // parts cut by pages
};

// `headings` (sorted by line_index) gets the new headings, sorted again.
StructureReport add_structure(std::vector<Heading>& headings, std::span<const Line> lines,
                              const FurnitureReport& furniture, int page_count,
                              const StructureOptions& options = {});

}  // namespace htpd
