// HTPD - the public entry point of the library.
//
//     htpd::ConvertOptions options;
//     options.input = "manual.pdf";
//     auto report = htpd::convert(options);
//     if (!report) std::cerr << report.error().message;
//
// The command-line tool (cli/main.cpp) is a thin wrapper around these two
// functions; a Python binding or a UI can call them in the same way.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "htpd/furniture.hpp"
#include "htpd/headings.hpp"
#include "htpd/ocr.hpp"
#include "htpd/tables.hpp"
#include "htpd/tags.hpp"
#include "htpd/types.hpp"

namespace htpd {

struct ConvertOptions {
    std::filesystem::path input;         // the PDF
    std::filesystem::path output_dir;    // empty: "<input name>.htpd" in the current folder
    std::string password;
    bool force = false;                  // replace a previous bundle in output_dir
    bool keep_furniture = false;         // keep running headers and footers
    double margin_band = 0.09;           // header/footer zone, fraction of page height
    HeadingMode headings = HeadingMode::Auto;
    bool dehyphenate = false;            // "inter-" + "rupt" -> "interrupt"
    bool single_file = false;            // write one file "<input name>.htpd" instead of a folder
    bool figures = true;                 // store figures (pictures and drawings) as PNG pictures
    float figure_dpi = 120;              // resolution of the stored figures
    OcrMode ocr = OcrMode::Auto;         // read text from pictures with Tesseract, when installed
    OcrOptions ocr_options;
    TagMode tags = TagMode::Auto;        // use the structure tags of a tagged PDF where they can be trusted
    TableMode tables = TableMode::Auto;  // read the cells of a table from the lines drawn on the page, too
    TablePages table_pages = TablePages::Split;   // a table over a page break: one block per page, or one block with a row-level page mark
    std::function<void(int done, int total)> on_progress;   // called once per page
};

struct ConvertReport {
    std::filesystem::path output_dir;
    std::string title;
    int pages = 0;
    std::size_t lines = 0;
    std::size_t sections = 0;
    std::size_t paragraphs = 0;
    HeadingSource heading_source = HeadingSource::None;
    std::size_t bookmarks_total = 0;
    std::size_t bookmarks_located = 0;
    std::size_t contents_entries = 0;   // a printed table of contents: its entries
    std::size_t contents_located = 0;   // ... found as headings in the text
    std::size_t headings_from_text = 0;  // found in the text below the bookmark level
    std::size_t running_titles = 0;      // sections started where the running header changes
    std::size_t page_parts = 0;          // parts of long text, cut by pages
    FurnitureReport furniture;
    LatticeReport lattice;               // tables read from the lines drawn on the pages
    TagReport tags;                      // what the structure tags of the PDF gave
    std::size_t headings_from_tags = 0;  // headings that come from the tags
    std::size_t skipped_rotated = 0;
    std::size_t unmapped = 0;
    std::size_t pages_without_text = 0;
    OcrReport ocr;
    std::size_t figures = 0;             // figures stored as pictures
    std::uintmax_t figure_bytes = 0;     // their PNG bytes
    std::size_t files = 0;
    std::uintmax_t source_bytes = 0;
    std::uintmax_t bundle_bytes = 0;
    double seconds = 0;
};

// Converts a PDF into an HTPD bundle on disk.
Result<ConvertReport> convert(const ConvertOptions& options);

struct InspectReport {
    DocumentMeta meta;
    std::size_t bookmarks = 0;
    float body_size = 0;
    HeadingSource heading_source = HeadingSource::None;
    std::vector<Heading> headings;
    FurnitureReport furniture;
    std::size_t pages_without_text = 0;
    std::size_t skipped_rotated = 0;
};

// Analyses a PDF without writing anything: what `convert` would find.
Result<InspectReport> inspect(const std::filesystem::path& input, const std::string& password = {},
                              HeadingMode headings = HeadingMode::Auto, double margin_band = 0.09);

}  // namespace htpd
