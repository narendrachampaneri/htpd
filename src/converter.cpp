#include "htpd/converter.hpp"

#include <chrono>
#include <format>

#include "htpd/bundle_writer.hpp"
#include "htpd/layout.hpp"
#include "htpd/pdf_reader.hpp"
#include "htpd/sections.hpp"
#include "htpd/sha256.hpp"
#include "htpd/text.hpp"

namespace htpd {

namespace {

// Everything learned from reading the PDF, before any file is written.
struct Analysis {
    DocumentMeta meta;
    std::vector<Line> lines;              // whole document, reading order
    std::vector<Affine> page_transforms;
    std::vector<FontFacts> fonts;         // indexed by Line::font
    HeadingResult headings;
    FurnitureReport furniture;
    std::size_t outline_entries = 0;
    std::size_t skipped_rotated = 0;
    std::size_t unmapped = 0;
    std::size_t pages_without_text = 0;
};

struct AnalysisOptions {
    std::string password;
    bool keep_furniture = false;
    double margin_band = 0.09;
    HeadingMode headings = HeadingMode::Auto;
    std::function<void(int, int)> on_progress;
};

Result<Analysis> analyse(const std::filesystem::path& input, const AnalysisOptions& options) {
    auto opened = PdfDocument::open(input, options.password);
    if (!opened) return std::unexpected(opened.error());
    const PdfDocument& doc = *opened;

    Analysis a;
    a.meta = doc.meta();
    const int page_count = a.meta.page_count;
    if (page_count <= 0) return fail(std::format("'{}' has no pages", input.string()));

    // 1. Pages -> lines.
    std::vector<std::vector<Line>> pages(static_cast<std::size_t>(page_count));
    std::vector<float> heights(pages.size(), 0.0f);
    a.page_transforms.resize(pages.size());
    for (int p = 0; p < page_count; ++p) {
        const auto index = static_cast<std::size_t>(p);
        if (auto page = doc.read_page(p)) {
            heights[index] = page->height;
            a.page_transforms[index] = page->user_to_display;
            a.skipped_rotated += page->skipped_rotated;
            a.unmapped += page->unmapped;
            pages[index] = build_lines(*page);
        }
        if (pages[index].empty()) ++a.pages_without_text;
        if (options.on_progress) options.on_progress(p + 1, page_count);
    }

    a.fonts = doc.fonts();   // measured while the pages were read

    // 2. Running headers and footers.
    if (!options.keep_furniture) {
        FurnitureOptions furniture;
        furniture.band = options.margin_band;
        a.furniture = remove_furniture(pages, heights, furniture);
    }

    // 3. One flat list of lines.
    std::size_t total = 0;
    for (const auto& page : pages) total += page.size();
    a.lines.reserve(total);
    for (auto& page : pages) {
        for (Line& line : page) a.lines.push_back(std::move(line));
    }

    // 4. Headings.
    const std::vector<OutlineEntry> outline = doc.outline();
    a.outline_entries = outline.size();
    HeadingOptions heading_options;
    heading_options.mode = options.headings;
    a.headings = detect_headings(a.lines, outline, a.page_transforms, heading_options);
    return a;
}

}  // namespace

Result<ConvertReport> convert(const ConvertOptions& options) {
    const auto started = std::chrono::steady_clock::now();
    if (options.input.empty()) return fail("no input file given");

    auto digest = sha256_file(options.input);
    if (!digest) return std::unexpected(digest.error());

    auto analysed = analyse(options.input, {options.password, options.keep_furniture,
                                            options.margin_band, options.headings,
                                            options.on_progress});
    if (!analysed) return std::unexpected(analysed.error());
    Analysis& a = *analysed;

    SectionOptions section_options;
    section_options.dehyphenate = options.dehyphenate;
    section_options.fonts = std::move(a.fonts);
    std::vector<Section> sections = build_sections(a.lines, a.headings.headings, section_options);

    BundleInfo info;
    info.meta = a.meta;
    info.source_file = text::sanitize_utf8(options.input.filename().string());
    info.source_sha256 = digest->sha256_hex;
    info.source_bytes = digest->bytes;
    info.generated_at = utc_timestamp_now();
    info.heading_source = a.headings.source;
    info.body_size = a.headings.body_size;
    info.bookmarks_total = a.headings.bookmarks_total;
    info.bookmarks_located = a.headings.bookmarks_located;
    info.headings_from_text = a.headings.text_added;
    info.furniture_removed = !options.keep_furniture;
    info.furniture = a.furniture;
    info.skipped_rotated = a.skipped_rotated;
    info.unmapped = a.unmapped;
    info.pages_without_text = a.pages_without_text;

    std::filesystem::path out_dir = options.output_dir;
    if (out_dir.empty()) out_dir = options.input.stem().string() + ".htpd";

    WriteOptions write_options;
    write_options.force = options.force;
    auto written = options.single_file ? write_single_file(out_dir, sections, info, write_options)
                                       : write_bundle(out_dir, sections, info, write_options);
    if (!written) return std::unexpected(written.error());

    ConvertReport report;
    report.output_dir = out_dir;
    report.title = document_title(info);
    report.pages = a.meta.page_count;
    report.lines = a.lines.size();
    report.sections = sections.size();
    for (const Section& s : sections) report.paragraphs += s.paragraphs.size();
    report.heading_source = a.headings.source;
    report.bookmarks_total = a.headings.bookmarks_total;
    report.bookmarks_located = a.headings.bookmarks_located;
    report.headings_from_text = a.headings.text_added;
    report.furniture = std::move(a.furniture);
    report.skipped_rotated = a.skipped_rotated;
    report.unmapped = a.unmapped;
    report.pages_without_text = a.pages_without_text;
    report.files = written->files;
    report.source_bytes = digest->bytes;
    report.bundle_bytes = written->bytes;
    report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return report;
}

Result<InspectReport> inspect(const std::filesystem::path& input, const std::string& password,
                              HeadingMode headings, double margin_band) {
    auto analysed = analyse(input, {password, false, margin_band, headings, {}});
    if (!analysed) return std::unexpected(analysed.error());
    Analysis& a = *analysed;

    InspectReport report;
    report.meta = a.meta;
    report.bookmarks = a.outline_entries;
    report.body_size = a.headings.body_size;
    report.heading_source = a.headings.source;
    report.headings = std::move(a.headings.headings);
    report.furniture = std::move(a.furniture);
    report.pages_without_text = a.pages_without_text;
    report.skipped_rotated = a.skipped_rotated;
    return report;
}

}  // namespace htpd
