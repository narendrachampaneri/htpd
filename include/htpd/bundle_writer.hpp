// HTPD - bundle writer.
//
// Writes the converted document as an OKF v0.2 bundle (Open Knowledge Format,
// https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md):
//
//   <out>/index.md            directory listing (OKF reserved file)
//   <out>/log.md              conversion history (OKF reserved file)
//   <out>/document.md         type: Document - table of contents, conversion notes
//   <out>/sections/index.md   listing of all sections
//   <out>/sections/NNNN-<slug>.md   type: Section - one file per heading
//
// Every file is plain Markdown with YAML frontmatter, so any AI model or tool
// can read it. HTPD's own fields live under the `htpd:` key of the frontmatter;
// docs/FORMAT.md describes them.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "htpd/figures.hpp"
#include "htpd/furniture.hpp"
#include "htpd/ocr.hpp"
#include "htpd/styles.hpp"
#include "htpd/tags.hpp"
#include "htpd/types.hpp"

namespace htpd {

struct BundleInfo {
    DocumentMeta meta;
    std::string tag_title;              // the text of the Title element of the PDF's tags ("" when there is none)
    std::string printed_title;          // the title printed on the first page ("" when none is clear)
    std::string source_file;            // file name only, e.g. "manual.pdf"
    std::string source_sha256;
    std::uintmax_t source_bytes = 0;
    std::string generated_at;           // ISO 8601 UTC, e.g. 2026-10-06T12:00:00Z

    HeadingSource heading_source = HeadingSource::None;
    float body_size = 0;
    std::size_t bookmarks_total = 0;
    std::size_t bookmarks_located = 0;
    std::size_t contents_entries = 0;   // a printed table of contents: its entries
    std::size_t contents_located = 0;   // ... found as headings in the text
    std::size_t headings_from_text = 0; // found in the text below the bookmark level
    std::size_t headings_from_tags_below = 0;   // taken from the tags below the bookmark level
    std::size_t running_titles = 0;     // sections started where the running header changes
    std::size_t page_parts = 0;         // parts of long text, cut by pages

    bool furniture_removed = true;      // false when the user kept headers/footers
    FurnitureReport furniture;
    TagReport tags;                      // what the PDF's structure tags gave
    std::size_t headings_from_tags = 0;  // headings that come from the tags
    std::size_t skipped_rotated = 0;
    std::size_t unmapped = 0;
    std::size_t pages_without_text = 0;
    std::size_t links = 0;               // links from one section to another
    OcrReport ocr;
    std::vector<FigureFile> figures;     // by id: figures[id - 1]
    Producer producer = Producer::Unknown;   // the program that made the PDF, as styles.hpp classifies it
    std::string producer_text;           // ... its Producer entry
    std::vector<std::string> style_notes;    // one line per style role ("heading 2: Arial Bold 12 pt, 84 lines")
};

struct WriteOptions {
    // Replace a previous HTPD bundle in the output folder.
    bool force = false;
};

struct WriteReport {
    std::size_t files = 0;
    std::uintmax_t bytes = 0;
};

// Writes the bundle and stores each section's file name in `Section::file`.
Result<WriteReport> write_bundle(const std::filesystem::path& out_dir,
                                 std::vector<Section>& sections, const BundleInfo& info,
                                 const WriteOptions& options = {});

// Writes the whole document as ONE file (docs/FORMAT.md section 11): reading
// rules for AI models, the contents, the conversion notes and every section
// with its text and page marks. Made to be handed to an AI model as it is.
Result<WriteReport> write_single_file(const std::filesystem::path& file,
                                      std::vector<Section>& sections, const BundleInfo& info,
                                      const WriteOptions& options = {});

// The title shown for the document: PDF title, or the file name without ".pdf".
std::string document_title(const BundleInfo& info);

// Current time as an ISO 8601 UTC timestamp.
std::string utc_timestamp_now();

}  // namespace htpd
