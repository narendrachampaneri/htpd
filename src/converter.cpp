#include "htpd/converter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <future>
#include <thread>
#include <unordered_map>
#include <format>
#include <memory>
#include <optional>

#include "htpd/bundle_writer.hpp"
#include "htpd/columns.hpp"
#include "htpd/figures.hpp"
#include "htpd/layout.hpp"
#include "htpd/links.hpp"
#include "htpd/pdf_reader.hpp"
#include "htpd/png.hpp"
#include "htpd/sections.hpp"
#include "htpd/sha256.hpp"
#include "htpd/tables.hpp"
#include "htpd/tags.hpp"
#include "htpd/structure.hpp"
#include "htpd/styles.hpp"
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
    std::size_t table_gaps = 0;           // column gaps found from the strips rows leave free
    LatticeReport lattice;                // tables read from the rules drawn on the pages (tables.hpp)
    OcrReport ocr;
    std::vector<FigureFile> figures;
    TagReport tags;                       // what the PDF's structure tags gave (tags.hpp)
    std::string tag_title;                // the text of the tags' Title element, "" when there is none
    bool wants_tags = false;              // Auto: the PDF is tagged and its headings came out poor - convert again with the tags
    StyleSheet styles;                    // the styles of the text and their roles (styles.hpp)
    std::vector<std::string> style_notes; // one line per role, for the conversion notes
    Producer producer = Producer::Unknown;
    std::string producer_text;            // the PDF's Producer entry, as it is
};

// A figure is drawn at most this large (pixels): enough to read its labels.
constexpr long long kMaxFigurePixels = 2'500'000;

// Puts the place of a figure after the last line of the page above it.
void insert_figure_mark(std::vector<Line>& page, const Figure& f, int id) {
    Line mark;
    mark.page = f.page;
    mark.x0 = f.box.x0;
    mark.y0 = f.box.y0;
    mark.x1 = f.box.x1;
    mark.y1 = f.box.y1;
    mark.baseline = f.box.y1;
    mark.figure = id;
    std::size_t at = 0;
    for (std::size_t i = 0; i < page.size(); ++i) {
        const bool above = page[i].figure == 0 ? (page[i].y0 + page[i].y1) / 2 < f.box.y0
                                               : page[i].y0 <= f.box.y0;   // figures side by side keep their order
        if (above) at = i + 1;
    }
    page.insert(page.begin() + static_cast<std::ptrdiff_t>(at), std::move(mark));
}

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
    bool figures = false;
    float figure_dpi = 120;
    OcrMode ocr = OcrMode::Off;
    OcrOptions ocr_options;
    bool keep_furniture = false;
    double margin_band = 0.09;
    HeadingMode headings = HeadingMode::Auto;
    std::function<void(int, int)> on_progress;
    TagMode tags = TagMode::Auto;
    TableMode tables = TableMode::Auto;
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
        long same_as = -1;   // the same picture was read before: its words are used again
    };
    std::vector<Picture> pictures;   // every picture to read, in page order
    const float scale = options.ocr_options.dpi / 72.0f;
    std::unordered_map<std::size_t, std::size_t> read_before;   // hash of the drawn picture -> picture
    std::size_t jobs = 0;                                         // pictures handed to the pool
    std::vector<std::size_t> job_of;                              // picture -> its job in the pool
    // Hands a drawn picture to OCR, unless the very same picture was read before.
    // The key of a picture of the PDF (its stored data and the size it is
    // shown at), or 0: then the drawn pixels are the key.
    const auto box_key = [](std::uint64_t picture, const Box& box) {
        return picture == 0 ? std::size_t{0}
                            : static_cast<std::size_t>(picture ^ (static_cast<std::uint64_t>(std::lround(box.width())) << 40) ^
                                                       (static_cast<std::uint64_t>(std::lround(box.height())) << 20));
    };
    // Hands a picture to OCR, unless the very same picture was read before;
    // `draw` makes the picture only when it must be read.
    const auto read_picture = [&](int p, const Box& box, bool whole, std::size_t key, auto draw) {
        std::optional<Image> drawn;
        if (key == 0) {
            drawn = draw();
            if (!drawn) return;
            key = std::hash<std::string_view>{}(std::string_view(
                      reinterpret_cast<const char*>(drawn->pixels.data()), drawn->pixels.size())) ^
                  (static_cast<std::size_t>(drawn->width) << 20) ^ static_cast<std::size_t>(drawn->height);
        }
        const auto found = read_before.find(key);
        if (found == read_before.end() && !drawn) {
            drawn = draw();
            if (!drawn) return;
        }
        const auto [it, fresh] = read_before.try_emplace(key, pictures.size());
        if (!fresh) ++a.ocr.reused;
        pictures.push_back({p, box, whole, fresh ? -1L : static_cast<long>(it->second)});
        job_of.push_back(fresh ? jobs : job_of[it->second]);
        if (fresh) {
            pool->add(OcrJob{p, box, scale, std::move(*drawn), whole});
            ++jobs;
        }
    };

    // Figures: drawn here (the PDF library works on this thread), packed as
    // PNG on the other cores, a few at a time so that memory stays small.
    std::deque<std::pair<std::size_t, std::future<std::string>>> packing;   // figure index, PNG
    std::unordered_map<std::size_t, int> figure_seen;                       // picture key -> figure id
    const std::size_t max_packing = std::max(2u, std::thread::hardware_concurrency());
    const auto collect_one = [&] {
        a.figures[packing.front().first].png = packing.front().second.get();
        packing.pop_front();
    };

    // Bookmarks: the headings of the document.
    const std::vector<OutlineEntry> outline = doc.outline();
    a.outline_entries = outline.size();
    // In Auto mode a tagged PDF is first read without its tags: they are read (convert() makes the
    // conversion again) only when the headings come out poor (see the end of this function).
    const bool tagged = doc.is_tagged();
    std::string unused_reason;
    const bool read_tags = should_read_tags(options.tags, tagged, HeadingQuality::Unknown, unused_reason);
    const bool probing = options.tags == TagMode::Auto && tagged;

    // 1. Pages -> lines.
    std::vector<std::vector<Line>> pages(static_cast<std::size_t>(page_count));
    std::vector<float> heights(pages.size(), 0.0f);
    a.page_transforms.resize(pages.size());
    a.page_links.resize(pages.size());
    std::vector<std::vector<Rule>> page_rules(pages.size());   // the lines drawn on each page (tables.hpp)
    for (int p = 0; p < page_count; ++p) {
        const auto index = static_cast<std::size_t>(p);
        if (auto page = doc.read_page(p, read_tags)) {
            heights[index] = page->height;
            a.page_transforms[index] = page->user_to_display;
            a.skipped_rotated += page->skipped_rotated;
            a.unmapped += page->unmapped;
            pages[index] = build_tagged_lines(*page, a.tags).lines;
            a.page_links[index] = std::move(page->links);
            if (options.tables == TableMode::Auto && page->rules.size() >= 4) page_rules[index] = std::move(page->rules);

            // A page with (almost) no text that shows something is a scan (or
            // its text is drawn as shapes): OCR reads all of it. Without OCR its
            // content is kept as a picture of the whole page.
            const bool scanned = letters_on(pages[index]) < 20 && page->objects > 0;
            const bool unread = scanned && !ocr_ready();
            if (unread) ++a.ocr.pages_unread;

            // Draws a figure, or points at the same picture stored before.
            const auto store_figure = [&](const Figure& f, float dpi) {
                // The same picture shown before (a logo, a repeated photo): stored once, not drawn again.
                const std::size_t key = box_key(f.picture, f.box);
                if (key != 0) {
                    const auto seen = figure_seen.find(key);
                    if (seen != figure_seen.end()) {
                        FigureFile file;
                        file.id = static_cast<int>(a.figures.size()) + 1;
                        file.page = p;
                        file.kind = f.kind;
                        file.caption = f.caption;
                        file.same_as = seen->second;
                        const FigureFile& first = a.figures[static_cast<std::size_t>(seen->second) - 1];
                        file.width = first.width;
                        file.height = first.height;
                        a.figures.push_back(std::move(file));
                        insert_figure_mark(pages[index], f, a.figures.back().id);
                        return;
                    }
                }
                float fig_scale = dpi / 72.0f;
                const double pixels = static_cast<double>(f.box.width()) * f.box.height() * fig_scale * fig_scale;
                if (pixels > kMaxFigurePixels) fig_scale *= static_cast<float>(std::sqrt(kMaxFigurePixels / pixels));
                auto image = doc.render(p, f.box, fig_scale, true);
                if (!image) return;
                FigureFile file;
                file.id = static_cast<int>(a.figures.size()) + 1;
                file.page = p;
                file.kind = f.kind;
                file.caption = f.caption;
                Image picture = grey_if_possible(std::move(*image));
                file.width = picture.width;
                file.height = picture.height;
                a.figures.push_back(std::move(file));
                if (key != 0) figure_seen.emplace(key, a.figures.back().id);
                insert_figure_mark(pages[index], f, a.figures.back().id);
                if (packing.size() >= max_packing) collect_one();
                packing.emplace_back(a.figures.size() - 1,
                                     std::async(std::launch::async,
                                                [img = std::move(picture)] { return png_bytes(img); }));
            };

            if (options.figures && unread) {
                // Nothing of this page can be read as text: the page itself is
                // stored, so that an AI that sees pictures can still read it.
                const Figure whole{p, Box{0, 0, page->width, page->height}, Figure::Kind::Picture,
                                   std::format("Page {}: picture of the whole page (it has no text layer)", p + 1), 0};
                store_figure(whole, options.figure_dpi);
                ++a.ocr.page_pictures;
            } else if (options.figures && (!page->images.empty() || !page->paths.empty() || !page->forms.empty())) {
                const std::vector<FontFacts> fonts = doc.fonts();
                std::vector<char> code;
                code.reserve(pages[index].size());
                for (const Line& l : pages[index]) code.push_back(l.font < fonts.size() && fonts[l.font].monospace);
                // On a page without text the shapes are often its letters (text
                // drawn as outlines): only the pictures on it are figures there.
                const bool scan = letters_on(pages[index]) < 20;
                const std::span<const Box> paths = scan ? std::span<const Box>{} : std::span<const Box>(page->paths);
                const std::span<const Box> forms = scan ? std::span<const Box>{} : std::span<const Box>(page->forms);
                std::vector<Figure> found = find_figures(p, page->width, page->height, page->images, paths, forms,
                                                         pages[index], page->turned != 0, {}, code, page->image_keys);
                // The figures and captions the PDF's tags name (not on a scan, not on a turned page).
                if (!scan && page->turned == 0 && page->tags.present()) {
                    a.tags.figures += merge_tag_figures(found, tagged_figures(*page), p, page->width, page->height,
                                                        FigureOptions{}.margin_band);
                }
                for (const Figure& f : found) {
                    // A scanned page is one big picture: OCR reads it, it is no figure.
                    if (scan && f.box.width() * f.box.height() > 0.6f * page->width * page->height) continue;
                    store_figure(f, options.figure_dpi);
                }
            }

            // OCR: a scanned page is read whole. On other pages, read the
            // pictures that are large enough and have no text of the PDF on them.
            if (scanned && pool) {
                const Box whole{0, 0, page->width, page->height};
                read_picture(p, whole, true, 0, [&]() -> std::optional<Image> {
                    auto image = doc.render(p, whole, scale);
                    return image ? std::optional<Image>(std::move(*image)) : std::nullopt;
                });
            } else if (!scanned && page->turned == 0) {
                for (std::size_t k = 0; k < page->images.size(); ++k) {
                    const Box& box = page->images[k];
                    if (box.width() < kMinPictureWidth || box.height() < kMinPictureHeight ||
                        box.width() * box.height() < kMinPictureArea || has_text_inside(pages[index], box)) {
                        continue;
                    }
                    if (!ocr_ready()) break;
                    const std::uint64_t picture = k < page->image_keys.size() ? page->image_keys[k] : 0;
                    read_picture(p, box, false, box_key(picture, box), [&]() -> std::optional<Image> {
                        auto image = doc.render(p, box, scale);
                        return image ? std::optional<Image>(std::move(*image)) : std::nullopt;
                    });
                }
            }
        }
        if (options.on_progress) options.on_progress(p + 1, page_count);
    }
    while (!packing.empty()) collect_one();
    // The same picture on many pages (a PDF stores it once): stored once.
    {
        std::unordered_map<std::string_view, int> first;
        for (FigureFile& f : a.figures) {
            if (f.same_as != 0) continue;   // already known as a repeat
            const auto [it, inserted] = first.try_emplace(f.png, f.id);
            if (!inserted) f.same_as = it->second;
        }
        for (FigureFile& f : a.figures) {
            if (f.same_as != 0) f.png.clear();
        }
    }
    if (pool) {
        OcrPool::Done done = pool->finish();
        a.ocr.failed = done.failed;
        a.ocr.words_dropped = done.words_dropped;
        for (std::size_t i = 0; i < pictures.size(); ++i) {
            const Picture& pic = pictures[i];
            // The words of this picture: read for it, or read for the same picture elsewhere.
            std::vector<Line> found = done.lines[job_of[i]];
            if (pic.same_as >= 0) {
                const Picture& src = pictures[static_cast<std::size_t>(pic.same_as)];
                for (Line& l : found) {
                    const float dx = pic.box.x0 - src.box.x0, dy = pic.box.y0 - src.box.y0;
                    l.page = pic.page;
                    l.x0 += dx;
                    l.x1 += dx;
                    l.y0 += dy;
                    l.y1 += dy;
                    l.baseline += dy;
                    l.origin_x += dx;
                    for (float& g : l.gap_begin) g += dx;
                    for (float& g : l.gap_end) g += dx;
                    for (float& g : l.space_begin) g += dx;
                    for (float& g : l.space_end) g += dx;
                }
            }
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
        if (std::ranges::none_of(page, [](const Line& l) { return !l.text.empty(); })) ++a.pages_without_text;   // a figure mark is no text
    }

    a.fonts = doc.fonts();   // measured while the pages were read

    // 1e. The style sheet: the styles of the text and their roles, before headers, footers and headings are looked for.
    a.producer = classify_producer(a.meta.creator, a.meta.producer);
    a.producer_text = a.meta.producer;
    {
        StyleOptions style_options;
        style_options.band = options.margin_band;
        style_options.page_count = static_cast<std::size_t>(page_count);
        a.styles = build_style_sheet(pages, heights, a.fonts, a.producer, style_options);
        a.style_notes = a.styles.describe(a.fonts);
    }

    // 2. Running headers and footers.
    const std::size_t marked_artifacts = options.keep_furniture ? 0 : count_artifacts(pages, heights);
    if (!options.keep_furniture) {
        FurnitureOptions furniture;
        furniture.band = options.margin_band;
        furniture.styles = &a.styles;
        furniture.producer = a.producer;
        a.furniture = remove_furniture(pages, heights, furniture);
    }
    if (!options.keep_furniture) {
        // What the PDF marks as page furniture and the ordinary rules did not find.
        const std::vector<ArtifactLine> artifacts = take_artifacts(pages, heights);
        a.tags.artifact_lines = marked_artifacts;
        add_artifacts(a.furniture, artifacts, pages.size());
    }

    // 2b. Tables drawn with lines: the cells the rules make; then tables set tight: their columns,
    // from the strips every row leaves free.
    std::int32_t lattice_id = kLatticeTableId;
    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (page_rules[i].empty()) continue;
        const LatticeReport r = find_lattice_tables(pages[i], page_rules[i], lattice_id);
        a.lattice.tables += r.tables;
        a.lattice.rows += r.rows;
        a.lattice.spans += r.spans;
        a.lattice.left += r.left;
    }
    for (auto& page : pages) {
        std::vector<char> code;
        code.reserve(page.size());
        for (const Line& l : page) code.push_back(l.font < a.fonts.size() && a.fonts[l.font].monospace && l.ocr == Line::Ocr::None);
        // Monospace lines that are the rows of a table (a name in a cell, a whole table set in a monospace font) are no listing.
        const std::vector<char> rows = monospace_table_rows(page, code);
        for (std::size_t i = 0; i < code.size(); ++i) code[i] = code[i] && !rows[i];
        a.table_gaps += mark_table_columns(page, code);
    }

    // 3. One flat list of lines.
    std::size_t total = 0;
    for (const auto& page : pages) total += page.size();
    a.lines.reserve(total);
    for (auto& page : pages) {
        for (Line& line : page) a.lines.push_back(std::move(line));
    }

    // 4. Headings.
    HeadingOptions heading_options;
    heading_options.mode = options.headings;
    heading_options.styles = &a.styles;
    heading_options.producer = a.producer;
    a.headings = detect_headings(a.lines, outline, a.page_transforms, heading_options);

    // 4b. The tags: read only where the headings are poor without them.
    if (probing) {
        const HeadingQuality quality = assess_headings(a.headings, page_count);
        a.wants_tags = should_read_tags(options.tags, tagged, quality, a.tags.unread_reason);
        a.tags.unread = !a.wants_tags && !a.tags.unread_reason.empty();
        if (a.wants_tags) return a;   // convert() makes the conversion again with the tags
    }
    if (a.tags.any()) {
        // The title the tags name, when the PDF names none.
        for (std::size_t i = 0; i < a.lines.size() && a.tag_title.empty(); ++i) {
            if (!a.lines[i].tag_title) continue;
            std::string title;
            for (std::size_t k = i; k < a.lines.size() && a.lines[k].tag == a.lines[i].tag && a.lines[k].page == a.lines[i].page; ++k) {
                if (!title.empty()) title += ' ';
                title += text::collapse_spaces(a.lines[k].text);
            }
            if (title.size() >= 3 && title.size() <= 200) a.tag_title = title;
            break;
        }
    }

    // 5. Parts for documents with few headings, and for long stretches of text.
    if (options.headings != HeadingMode::None) {
        a.structure = add_structure(a.headings.headings, a.lines, a.furniture, page_count);
    }
    return a;
}

// The conversion; in Auto mode a tagged PDF whose headings come out poor is converted again with its tags.
Result<Analysis> analyse_with_gate(const std::filesystem::path& input, const AnalysisOptions& options) {
    auto first = analyse(input, options);
    if (!first || !first->wants_tags) return first;
    AnalysisOptions again = options;
    again.tags = TagMode::On;
    return analyse(input, again);
}

}  // namespace

Result<ConvertReport> convert(const ConvertOptions& options) {
    const auto started = std::chrono::steady_clock::now();
    if (options.input.empty()) return fail("no input file given");

    auto digest = sha256_file(options.input);
    if (!digest) return std::unexpected(digest.error());

    auto analysed = analyse_with_gate(options.input, {options.password, options.figures, options.figure_dpi, options.ocr,
                                            options.ocr_options,
                                            options.keep_furniture, options.margin_band, options.headings,
                                            options.on_progress, options.tags, options.tables});
    if (!analysed) return std::unexpected(analysed.error());
    Analysis& a = *analysed;

    SectionOptions section_options;
    section_options.dehyphenate = options.dehyphenate;
    section_options.fonts = std::move(a.fonts);
    std::vector<Section> sections = build_sections(a.lines, a.headings.headings, section_options);
    attach_links(sections, a.lines, a.page_links, a.page_transforms);

    BundleInfo info;
    info.meta = a.meta;
    info.printed_title = printed_title(a.lines, a.headings.body_size);
    info.source_file = text::sanitize_utf8(options.input.filename().string());
    info.source_sha256 = digest->sha256_hex;
    info.source_bytes = digest->bytes;
    info.generated_at = utc_timestamp_now();
    info.heading_source = a.headings.source;
    info.body_size = a.headings.body_size;
    info.bookmarks_total = a.headings.bookmarks_total;
    info.bookmarks_located = a.headings.bookmarks_located;
    info.contents_entries = a.headings.contents_entries;
    info.contents_located = a.headings.source == HeadingSource::Contents ? a.headings.headings.size() : 0;
    info.headings_from_text = a.headings.text_added;
    info.headings_from_tags_below = a.headings.tags_added;
    info.running_titles = a.structure.running_titles;
    info.page_parts = a.structure.page_parts;
    info.furniture_removed = !options.keep_furniture;
    info.furniture = a.furniture;
    info.tags = a.tags;
    info.lattice = a.lattice;
    info.tag_title = a.tag_title;
    info.producer = a.producer;
    info.producer_text = a.producer_text;
    info.style_notes = std::move(a.style_notes);
    for (const Heading& h : a.headings.headings) info.headings_from_tags += h.source == HeadingSource::Tags;
    info.skipped_rotated = a.skipped_rotated;
    info.unmapped = a.unmapped;
    info.pages_without_text = a.pages_without_text;
    info.ocr = a.ocr;
    std::uintmax_t figure_bytes = 0;
    for (const FigureFile& f : a.figures) figure_bytes += f.png.size();
    const std::size_t figure_count = a.figures.size();
    info.figures = std::move(a.figures);
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
    report.contents_entries = info.contents_entries;
    report.contents_located = info.contents_located;
    report.headings_from_text = a.headings.text_added;
    report.running_titles = a.structure.running_titles;
    report.page_parts = a.structure.page_parts;
    report.furniture = std::move(a.furniture);
    report.tags = info.tags;
    report.lattice = info.lattice;
    report.headings_from_tags = info.headings_from_tags;
    report.skipped_rotated = a.skipped_rotated;
    report.unmapped = a.unmapped;
    report.pages_without_text = a.pages_without_text;
    report.ocr = std::move(a.ocr);
    report.figures = figure_count;
    report.figure_bytes = figure_bytes;
    report.files = written->files;
    report.source_bytes = digest->bytes;
    report.bundle_bytes = written->bytes;
    report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return report;
}

Result<InspectReport> inspect(const std::filesystem::path& input, const std::string& password,
                              HeadingMode headings, double margin_band) {
    auto analysed = analyse_with_gate(input, {password, false, 120, OcrMode::Off, {}, false, margin_band, headings, {}});
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
