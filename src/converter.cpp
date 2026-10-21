#include "htpd/converter.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>

#include "htpd/bundle_writer.hpp"
#include "htpd/layout.hpp"
#include "htpd/links.hpp"
#include "htpd/pdf_reader.hpp"
#include "htpd/sections.hpp"
#include "htpd/sha256.hpp"
#include "htpd/structure.hpp"
#include "htpd/text.hpp"

namespace htpd {

namespace {

// Everything learned from reading the PDF, before any file is written.
struct Analysis {
    DocumentMeta meta;
    std::vector<Line> lines;              // whole document, reading order
    std::vector<Affine> page_transforms;
    std::vector<std::vector<PageLink>> page_links;   // per page
    std::vector<FontFacts> fonts;         // indexed by Line::font
    HeadingResult headings;
    StructureReport structure;
    FurnitureReport furniture;
    std::size_t outline_entries = 0;
    std::size_t skipped_rotated = 0;
    std::size_t unmapped = 0;
    std::size_t pages_without_text = 0;
    OcrReport ocr;
};

// Pictures smaller than this (points) hold no text worth reading: icons, logos, rules.
constexpr float kMinPictureWidth = 60, kMinPictureHeight = 14, kMinPictureArea = 3000;

std::size_t letters_on(const std::vector<Line>& lines) {
    std::size_t n = 0;
    for (const Line& l : lines) n += text::letter_count(l.text);
    return n;
}

// A picture whose place already holds text of the PDF (a scan with a hidden
// text layer, a photo with a caption on it) is not read again.
bool has_text_inside(const std::vector<Line>& lines, const Box& box) {
    std::size_t inside = 0;
    for (const Line& l : lines) {
        const float cx = (l.x0 + l.x1) / 2, cy = (l.y0 + l.y1) / 2;
        if (cx >= box.x0 && cx <= box.x1 && cy >= box.y0 && cy <= box.y1 && ++inside >= 2) return true;
    }
    return false;
}



struct AnalysisOptions {
    std::string password;
    OcrMode ocr = OcrMode::Off;
    OcrOptions ocr_options;
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

    // OCR: only when the program is there (and not switched off). It is
    // looked for when the first picture needs it, so a PDF without pictures
    // costs nothing.
    std::unique_ptr<OcrPool> pool;
    bool looked = options.ocr == OcrMode::Off;
    const auto ocr_ready = [&]() -> bool {
        if (!looked) {
            looked = true;
            const OcrEngine engine = find_ocr(options.ocr_options);
            if (engine.available) {
                a.ocr.engine = engine.version;
                pool = std::make_unique<OcrPool>(options.ocr_options);
            }
        }
        return pool != nullptr;
    };
    if (options.ocr == OcrMode::On && !ocr_ready()) {
        return fail(std::format("OCR was asked for, but '{}' with the language '{}' is not installed",
                                options.ocr_options.program, options.ocr_options.language));
    }
    struct Picture {
        int page;
        Box box;
        bool whole_page;
    };
    std::vector<Picture> pictures;   // in the order they were handed to the pool
    const float scale = options.ocr_options.dpi / 72.0f;

    // 1. Pages -> lines.
    std::vector<std::vector<Line>> pages(static_cast<std::size_t>(page_count));
    std::vector<float> heights(pages.size(), 0.0f);
    a.page_transforms.resize(pages.size());
    a.page_links.resize(pages.size());
    for (int p = 0; p < page_count; ++p) {
        const auto index = static_cast<std::size_t>(p);
        if (auto page = doc.read_page(p)) {
            heights[index] = page->height;
            a.page_transforms[index] = page->user_to_display;
            a.skipped_rotated += page->skipped_rotated;
            a.unmapped += page->unmapped;
            pages[index] = build_lines(*page);
            a.page_links[index] = std::move(page->links);

            // A page with (almost) no text that shows something is a scan:
            // read all of it. On other pages, read the pictures that are
            // large enough and have no text of the PDF on them.
            const bool scanned = letters_on(pages[index]) < 20 && page->objects > 0;
            if (scanned && !ocr_ready()) ++a.ocr.pages_unread;
            if (scanned && pool) {
                const Box whole{0, 0, page->width, page->height};
                if (auto image = doc.render(p, whole, scale)) {
                    pictures.push_back({p, whole, true});
                    pool->add({p, whole, scale, std::move(*image), true});
                }
            } else if (!scanned && page->turned == 0) {
                for (const Box& box : page->images) {
                    if (box.width() < kMinPictureWidth || box.height() < kMinPictureHeight ||
                        box.width() * box.height() < kMinPictureArea || has_text_inside(pages[index], box)) {
                        continue;
                    }
                    if (!ocr_ready()) break;
                    if (auto image = doc.render(p, box, scale)) {
                        pictures.push_back({p, box, false});
                        pool->add({p, box, scale, std::move(*image), false});
                    }
                }
            }
        }
        if (options.on_progress) options.on_progress(p + 1, page_count);
    }
    if (pool) {
        OcrPool::Done done = pool->finish();
        a.ocr.failed = done.failed;
        a.ocr.words_dropped = done.words_dropped;
        for (std::size_t i = 0; i < pictures.size(); ++i) {
            std::vector<Line>& found = done.lines[i];
            const Picture& pic = pictures[i];
            auto& page = pages[static_cast<std::size_t>(pic.page)];
            const std::size_t before = page.size();
            if (pic.whole_page) {
                merge_page_lines(page, std::move(found));
                if (page.size() == before) continue;
                ++a.ocr.pages_read;
                a.ocr.lines += page.size() - before;
            } else {
                insert_picture_lines(page, std::move(found), pic.box);
                if (page.size() == before) continue;
                ++a.ocr.pictures_read;
                a.ocr.lines += page.size() - before;
            }
            a.ocr.pages.push_back(pic.page);
        }
        std::ranges::sort(a.ocr.pages);
        a.ocr.pages.erase(std::unique(a.ocr.pages.begin(), a.ocr.pages.end()), a.ocr.pages.end());
    }
    for (const auto& page : pages) {
        if (page.empty()) ++a.pages_without_text;
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

    // 5. Parts for documents with few headings, and for long stretches of text.
    if (options.headings != HeadingMode::None) {
        a.structure = add_structure(a.headings.headings, a.lines, a.furniture, page_count);
    }
    return a;
}

}  // namespace

Result<ConvertReport> convert(const ConvertOptions& options) {
    const auto started = std::chrono::steady_clock::now();
    if (options.input.empty()) return fail("no input file given");

    auto digest = sha256_file(options.input);
    if (!digest) return std::unexpected(digest.error());

    auto analysed = analyse(options.input, {options.password, options.ocr, options.ocr_options,
                                            options.keep_furniture, options.margin_band, options.headings,
                                            options.on_progress});
    if (!analysed) return std::unexpected(analysed.error());
    Analysis& a = *analysed;

    SectionOptions section_options;
    section_options.dehyphenate = options.dehyphenate;
    section_options.fonts = std::move(a.fonts);
    std::vector<Section> sections = build_sections(a.lines, a.headings.headings, section_options);
    attach_links(sections, a.lines, a.page_links, a.page_transforms);

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
    info.running_titles = a.structure.running_titles;
    info.page_parts = a.structure.page_parts;
    info.furniture_removed = !options.keep_furniture;
    info.furniture = a.furniture;
    info.skipped_rotated = a.skipped_rotated;
    info.unmapped = a.unmapped;
    info.pages_without_text = a.pages_without_text;
    info.ocr = a.ocr;
    for (const Section& s : sections) info.links += s.links.size();

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
    report.running_titles = a.structure.running_titles;
    report.page_parts = a.structure.page_parts;
    report.furniture = std::move(a.furniture);
    report.skipped_rotated = a.skipped_rotated;
    report.unmapped = a.unmapped;
    report.pages_without_text = a.pages_without_text;
    report.ocr = std::move(a.ocr);
    report.files = written->files;
    report.source_bytes = digest->bytes;
    report.bundle_bytes = written->bytes;
    report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return report;
}

Result<InspectReport> inspect(const std::filesystem::path& input, const std::string& password,
                              HeadingMode headings, double margin_band) {
    auto analysed = analyse(input, {password, OcrMode::Off, {}, false, margin_band, headings, {}});
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
