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
    DocumentMeta meta() const;

    // Bookmarks in document order, flattened, with their nesting level.
    std::vector<OutlineEntry> outline() const;

    // All characters of one page (0-based index) with geometry in display space.
    Result<PageText> read_page(int index) const;

    // Draws a part of a page (display space, points) as a grey image with
    // `scale` pixels per point: what a scanner would see, for OCR. The page
    // is drawn as the PDF shows it (the turn of `PageText::turned` is not
    // applied).
    Result<GrayImage> render(int index, const Box& region, float scale) const;

    // Facts about the fonts seen so far, indexed by Glyph::font. Call it after
    // the pages have been read: fonts are measured while reading.
    std::vector<FontFacts> fonts() const;

private:
    struct Impl;
    explicit PdfDocument(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace htpd
