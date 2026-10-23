// HTPD - OCR: text from pictures (scanned pages, pictures of text).
//
// The converter does not contain an OCR engine. It runs the free Tesseract
// program (https://github.com/tesseract-ocr/tesseract) when it is installed,
// and works without it. Each picture is written as a grey image to a
// temporary folder; several Tesseract processes read them at the same time.
//
// Tesseract reports every word with its box and how sure it is (0-100). A
// word it is not sure of is left out rather than guessed (AGENTS.md rule 7:
// never make text up), and so is a line of only such words. Lines read by
// OCR are marked (`Line::ocr`), so the bundle can say which pages hold text
// that was recognised rather than read.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "htpd/types.hpp"

namespace htpd {

struct OcrOptions {
    std::string program = "tesseract";   // name on the PATH, or a full path
    std::string language = "eng";        // Tesseract language(s), e.g. "eng+deu"
    float dpi = 300;                     // resolution of the drawn pictures
    unsigned threads = 0;                // Tesseract processes at once; 0 = one per CPU core
    int min_word_confidence = 60;        // words below this are left out
};

enum class OcrMode {
    Auto,   // read pictures when the OCR program is installed (default)
    On,     // the same, but it is an error when the program is missing
    Off,    // never
};

// What OCR did during one conversion.
struct OcrReport {
    std::string engine;                 // "tesseract 5.3.4", empty when not found or off
    std::size_t pages_read = 0;         // whole pages read (pages without a text layer)
    std::size_t pictures_read = 0;      // pictures read on pages that have text
    std::size_t reused = 0;             // pictures seen before (the same picture on another page): read once
    std::size_t lines = 0;              // lines of text found
    std::size_t words_dropped = 0;      // words left out: OCR was not sure of them
    std::size_t failed = 0;             // pictures the program could not read
    std::size_t pages_unread = 0;       // pages without text that were not read (OCR off or missing)
    std::size_t page_pictures = 0;      // ... of them kept as a picture of the whole page (a figure)
    std::vector<int> pages;             // 0-based pages that hold text read by OCR
};

// What is known about the OCR program: whether it runs, and its version.
struct OcrEngine {
    bool available = false;
    std::string version;   // e.g. "tesseract 5.3.4"
};
OcrEngine find_ocr(const OcrOptions& options);

// One picture to read: the place it was drawn from, and the picture.
struct OcrJob {
    int page = 0;           // 0-based page
    Box region;             // display space of the page, points
    float scale = 1;        // pixels per point of `image`
    Image image;
    bool whole_page = false;   // a scanned page; otherwise a picture on a page with text
};

// Reads pictures with a pool of Tesseract processes. `add` hands a picture
// over (and waits while too many are waiting); `finish` waits for all and
// returns the lines of each job, in the order the jobs were added.
class OcrPool {
public:
    explicit OcrPool(const OcrOptions& options);
    ~OcrPool();
    OcrPool(const OcrPool&) = delete;
    OcrPool& operator=(const OcrPool&) = delete;

    void add(OcrJob job);
    struct Done {
        std::vector<std::vector<Line>> lines;   // per job
        std::size_t failed = 0;                 // jobs Tesseract could not read
        std::size_t words_dropped = 0;          // words left out: too unsure
    };
    Done finish();

private:
    struct Impl;
    Impl* impl_;
};

// Putting the lines read by OCR into the lines of a page. A line read by OCR
// that lies on text of the PDF is that text again: the exact text of the PDF
// is kept and the OCR line is left out (drop_known_text).
void drop_known_text(std::vector<Line>& found, const std::vector<Line>& page);
// A scanned page: its own few lines and the lines read by OCR, top to bottom.
void merge_page_lines(std::vector<Line>& page, std::vector<Line> found);
// The lines of a picture go after the last line of the page above the picture.
void insert_picture_lines(std::vector<Line>& page, std::vector<Line> found, const Box& box);

// ---- building blocks, exposed for testing --------------------------------

// Lines from Tesseract's TSV output (`tesseract image out tsv`): words with
// their boxes in pixels, put back into page points with `region` and `scale`.
struct TsvLines {
    std::vector<Line> lines;
    std::size_t words_dropped = 0;
};
// `whole_page`: lines of a scanned page (wide gaps become table columns);
// otherwise lines of a picture (no columns: a figure is no table).
TsvLines lines_from_tsv(std::string_view tsv, int page, const Box& region, float scale, int min_word_confidence,
                        bool whole_page = true);

// The grey image as a binary PGM file (P5), which Tesseract reads.
std::string pgm_bytes(const Image& image);

}  // namespace htpd
