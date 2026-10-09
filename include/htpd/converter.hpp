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
    std::size_t headings_from_text = 0;  // found in the text below the bookmark level
    FurnitureReport furniture;
    std::size_t skipped_rotated = 0;
    std::size_t unmapped = 0;
    std::size_t pages_without_text = 0;
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
