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

#include "htpd/furniture.hpp"
#include "htpd/types.hpp"

namespace htpd {

struct BundleInfo {
    DocumentMeta meta;
    std::string source_file;            // file name only, e.g. "manual.pdf"
    std::string source_sha256;
    std::uintmax_t source_bytes = 0;
    std::string generated_at;           // ISO 8601 UTC, e.g. 2026-10-06T12:00:00Z

    HeadingSource heading_source = HeadingSource::None;
    float body_size = 0;
    std::size_t bookmarks_total = 0;
    std::size_t bookmarks_located = 0;
    std::size_t headings_from_text = 0; // found in the text below the bookmark level

    bool furniture_removed = true;      // false when the user kept headers/footers
    FurnitureReport furniture;
    std::size_t skipped_rotated = 0;
    std::size_t unmapped = 0;
    std::size_t pages_without_text = 0;
    std::size_t links = 0;               // links from one section to another
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
