// HTPD - PDF reader interface.
//
// This is the ONLY part of HTPD that talks to a PDF library. The header is
// library-neutral; the implementation lives in src/pdf_reader_pdfium.cpp.
// To support another PDF library later, add another implementation file.
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "htpd/types.hpp"

namespace htpd {

class PdfDocument {
public:
    // Opens a PDF. `password` may be empty.
    static Result<PdfDocument> open(const std::filesystem::path& path,
                                    const std::string& password = {});

    PdfDocument(PdfDocument&&) noexcept;
    PdfDocument& operator=(PdfDocument&&) noexcept;
    ~PdfDocument();

    int page_count() const;

    // The PDF says it is tagged (/MarkInfo /Marked true).
    bool is_tagged() const;
    DocumentMeta meta() const;

    // Bookmarks in document order, flattened, with their nesting level.
    std::vector<OutlineEntry> outline() const;

    // All characters of one page (0-based index) with geometry in display space.
    // With `with_tags` false the structure tree of a tagged PDF is not read
    // (PageText::tags stays empty, no glyph has a mark): the page is read as
    // if the PDF had no tags.
    Result<PageText> read_page(int index, bool with_tags = true) const;

    // Draws a part of a page (display space, points) with `scale` pixels per
    // point: in grey (what a scanner would see, for OCR) or in colour (a
    // figure). The page is drawn as the PDF shows it (the turn of
    // `PageText::turned` is not applied).
    Result<Image> render(int index, const Box& region, float scale, bool colour = false) const;

    // Facts about the fonts seen so far, indexed by Glyph::font. Call it after
    // the pages have been read: fonts are measured while reading.
    std::vector<FontFacts> fonts() const;

private:
    struct Impl;
    explicit PdfDocument(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace htpd
