# The HTPD converter

`htpd` is the C++ program that turns a PDF into HTPD: a **bundle folder**
(one file per section) or, with `--single`, **one `.htpd` file**. This page
is the full documentation of the converter. The format itself is described
in [`FORMAT.md`](FORMAT.md); the overview is in the [README](../README.md).

## What you get

```
sample.htpd/
  index.md                      start page
  log.md                        when and from which file the bundle was made
  document.md                   contents of the document, conversion notes
  sections/
    index.md                    one line per section
    0001-front-matter.md
    0002-revision-history.md
    0003-1-introduction.md
    0004-1-1-scope.md
    ...
```

A section file, shortened:

````markdown
---
type: Section
title: "2.1.1 Limits"
htpd:
  id: "sections/0008-2-1-1-limits"
  number: "2.1.1"
  level: 3
  page_start: 2
  page_end: 2
  parent: "sections/0007-2-1-supply"
  children: []
  previous: "sections/0007-2-1-supply"
  next: "sections/0009-2-2-contactor"
  source_file: "sample.pdf"
  source_sha256: "0fe9f080d96c..."
---

# Location

[ACME DC Charger Controller Reference Manual](../document.md) > [2 Electrical characteristics](0006-2-electrical-characteristics.md) > [2.1 Supply](0007-2-1-supply.md) > **2.1.1 Limits**

Source: `sample.pdf`, page 2.[^source-pdf]

# Text

<!-- page 2 -->

```text
Parameter | Min | Typ | Max | Unit
Logic supply voltage | 3.0 | 3.3 | 3.6 | V
Output current | 0 |  | 200 | A
```
````

All fields and rules are in [`FORMAT.md`](FORMAT.md).

## What you need

| | |
|---|---|
| System | Linux. Built and tested on Ubuntu 24.04 (x86-64). |
| Compiler | g++ 13 or newer. The code uses C++23 (`std::expected`, `std::format`). Ubuntu 24.04 has g++ 13. On Ubuntu 22.04 the standard g++ 11 is too old; install a newer g++ first. Clang was not tried. |
| Build tool | GNU make, or CMake 3.20 or newer. |
| PDF library | PDFium, the PDF engine of Chrome. `scripts/get_pdfium.sh` downloads it (about 4 MB). |
| For the script | bash, curl or wget, tar. |

Nothing else. No Python, no database, no other library.

Optional, for OCR: the free [Tesseract](https://github.com/tesseract-ocr/tesseract)
program (`sudo apt install tesseract-ocr`; more languages:
`tesseract-ocr-deu`, ...). The converter runs it as a program, so the build
does not need it.

### OCR

A page with (almost) no text that shows something is a scan - or a page
whose letters are drawn as shapes (a web page printed to PDF): the converter
draws it at 300 dpi and Tesseract reads all of it. On a page that has text,
each picture larger than about 2 x 0.5 cm is read when no text of the PDF
lies on it (a scan with a hidden text layer is not read twice). Several
Tesseract processes run at the same time, one per CPU core. Rules:

* A word Tesseract is not sure of (confidence below 60) is left out, not
  guessed; the notes count them. In a line it reads well (7 of 10 words
  sure) a word with confidence 30 or more is kept when it has the form of a
  real word (`GB`, `eMMC`, `HVSW1_2_OUT`, not `PLMUuy`).
* The mark of a list item - a small dot, ring or square before the words,
  which OCR reads as "e", "o" or not at all - becomes `•`, `◦` or `▪`, and
  each item its own paragraph.
* Text of the PDF always wins: an OCR line that lies on text of the PDF is
  dropped.
* Text read from a picture on a page with text is the content of a figure:
  it is never taken for a heading, and never split into table columns.
* Text read by OCR never becomes a heading by its size alone; on a scanned
  page a numbered heading ("3.2 Clock tree") does, checked by its sequence.
* The pages that hold OCR text are listed per section (`ocr_pages`).
* Without Tesseract such a page is not lost: it is stored as a picture of
  the whole page (a figure "Page N: picture of the whole page"), so an AI
  that sees pictures can still read it. Only a search of the text cannot.

Speed: about 1-3 s per scanned page per core. Pictures on normal pages
cost little; the 5600-page i.MX 93 manual, with 41 pictures read, takes
about 12 s more on two cores.

### Figures

Pictures (photos, screenshots, scanned diagrams) and drawings made of lines
and shapes (block diagrams, timing diagrams, charts, register layouts) are
stored as PNG pictures at their place in the text, with their caption and
page (FORMAT.md 4.5). The converter draws the region of the figure with
PDFium at 120 dpi (`--figure-dpi`) and packs it as PNG itself, on all cores;
no library is added. A drawing counts as a figure when a caption "Figure N"
stands next to it, or when it has many shapes and little text; a caption
"Table N", table rows, rules across its whole width, or a frame around code
make it a table or a listing instead. Icons, logos in the page margin and
frames around the whole page are left out. `--figures off` stores none.

A picture the PDF shows on many pages (a logo, a repeated photo) is drawn,
stored and read by OCR once: the later figures point at the first one. A
1000-page sample with one picture per page, seven different ones, converts
in 12 s into a file of 1.5 MB.

## Build

### 1. Get PDFium

```sh
./scripts/get_pdfium.sh
```

This puts PDFium 157.0.8086.0 into `third_party/pdfium`. That is the
version all tests were run with. The script compares the download with a
known SHA-256 and refuses a file that differs.

The files come from <https://github.com/bblanchon/pdfium-binaries>, a
community project that compiles Google's PDFium. It is not Google itself.
PDFium reads files from unknown sources, so take a newer version from time
to time:

```sh
PDFIUM_VERSION=latest ./scripts/get_pdfium.sh
```

You can also use a PDFium you built yourself: give its folder (the one with
`include/fpdfview.h` and `lib/libpdfium.so`) as `PDFIUM_ROOT`.

### 2a. Build with make

```sh
make                 # build/htpd
make test            # builds and runs the tests
make clean
make PDFIUM_ROOT=/path/to/pdfium       # PDFium somewhere else
make CXX=g++-14                        # another compiler
```

The build puts a copy of `libpdfium.so` next to the program. `build/htpd`
and `build/libpdfium.so` belong together: to use the program somewhere
else, copy both files into the same folder.

### 2b. Or build with CMake

```sh
cmake -S . -B build-cmake                  # add -DPDFIUM_ROOT=/path/to/pdfium if needed
cmake --build build-cmake -j
ctest --test-dir build-cmake --output-on-failure
cmake --install build-cmake --prefix ~/.local     # optional: bin/htpd and lib/libpdfium.so
```

Both ways build the same sources with the same options.

## Use

```sh
htpd convert manual.pdf                  # writes the folder manual.htpd/ in the current folder
htpd convert manual.pdf --single         # writes ONE file manual.htpd, to hand to an AI chat
htpd convert manual.pdf -o out/manual    # another output folder
htpd convert manual.pdf --force          # replace a bundle made before
htpd info manual.pdf                     # show what would be found, write nothing
```

`htpd --help` lists all options. The important ones:

| Option | What it does |
|---|---|
| `--single` | One `.htpd` file instead of a folder: reading rules for the AI, the contents, every section with its pages ([FORMAT.md](FORMAT.md) section 11). |
| `--headings auto` | Default. Uses the bookmarks of the PDF, and adds numbered headings found in the text below them. Without bookmarks: a table of contents printed in the document, or the headings found in the text. |
| `--headings bookmarks` | Only the bookmarks. |
| `--headings text` | Ignores bookmarks; finds headings in the text. |
| `--headings none` | No headings: one section per page. |
| `--keep-headers` | Keeps the running headers and footers in the text. |
| `--dehyphenate` | `inter-` + `rupt` becomes `interrupt` instead of `inter-rupt`. |
| `--ocr auto\|on\|off` | Read text from pictures and scanned pages with Tesseract. `auto` (default): when it is installed. `on`: stop with an error when it is not. `off`: never. |
| `--ocr-lang <langs>` | Tesseract languages, e.g. `eng+deu` (default `eng`). Each must be installed (`tesseract --list-langs`). |
| `--figures on\|off` | Store figures (pictures and drawings) as PNG pictures in the bundle (default `on`). |
| `--figure-dpi <n>` | Resolution of the stored figures, 50 to 300 (default 120). |
| `--margin-band 0.09` | How much of the page height at the top and bottom is searched for headers and footers. |
| `--password <text>` | For an encrypted PDF. Other users of the computer can see the command line while the program runs. |

After a conversion the program says what it did and what it left out:

```
Converted "ACME DC Charger Controller Reference Manual"
  output folder   sample.htpd
  pages           5
  sections        14  (headings from: bookmark)
  bookmarks       13 (13 located in the page text)
  paragraphs      34
  files written   18 (30.5 kB; the PDF is 8.5 kB)
  time            0.02 s
Left out of the text:
  header/footer lines     8
      header on 4 pages: "ACME DC Charger Controller Reference Manual Rev 1.2"
      footer on 4 pages: "ACME Confidential Page 2"
  rotated characters      0
Start reading at sample.htpd/document.md
```

Run `htpd info` first on a new kind of document. It shows the headings and
the header/footer lines the converter would take, so you see at once
whether it understands the document.

The program never deletes anything that is not its own. An output folder
that is not empty is only replaced with `--force`, and only when it holds
a bundle.

## How it works

One PDF goes through seven steps. Each step is one small file.

| Step | File | What happens |
|---|---|---|
| 1. Read | `src/pdf_reader_pdfium.cpp` | PDFium gives every character with its position, size and font, and the links of each page. Page rotation is removed here, and a page whose text mainly runs sideways or upside down is turned, so all later steps see upright pages. This is the only file that knows PDFium. |
| 1b. OCR | `src/ocr.cpp` | Pages without a text layer and pictures without text of the PDF on them are drawn (by PDFium, in `pdf_reader_pdfium.cpp`) and read by the Tesseract program, several at a time, when it is installed. See "OCR" above. |
| 1c. Figures | `src/figures.cpp`, `src/png.cpp` | The pictures and drawings of each page that are figures, with their caption; drawn by PDFium and packed as PNG (several at a time). See "Figures" above. |
| 2. Lines | `src/layout.cpp` | Characters become lines. Wide gaps are marked (table columns); raised and lowered numbers stay raised (2³¹). |
| 3. Headers and footers | `src/furniture.cpp` | Lines that repeat at the page edge on many pages are taken out and reported. |
| 4. Headings | `src/headings.cpp` | From the bookmarks of the PDF; or from a table of contents printed in the document, whose titles are looked for in the text after it, in order (also when the text shows them without their number); or from the text: section numbers, font size, bold, another font. |
| 4b. Structure | `src/structure.cpp` | For documents with few headings: a running header that changes starts a section with its text as title. Text longer than a few pages is cut into parts of about 2 pages, where a sentence ends (FORMAT.md 3.3). |
| 5. Sections | `src/sections.cpp` | The text is cut at the headings. Lines become paragraphs, table rows (wrapped cells and tables stored cell by cell are put together) and code blocks. The tree of sections is built. |
| 6. Links | `src/links.cpp` | The links of the pages become links from section to section. |
| 7. Write | `src/bundle_writer.cpp` | The files of the bundle are written. |

`src/converter.cpp` runs the steps. `cli/main.cpp` is the command-line
tool, a thin layer on top. `src/text.cpp` and `src/sha256.cpp` are helpers.

The source tree:

```
include/htpd/     the headers (the public one is converter.hpp)
src/              the converter library
cli/main.cpp      the command-line tool
tests/            test_main.cpp, small test PDFs in tests/data, the stress test in tests/stress
scripts/          get_pdfium.sh
docs/FORMAT.md    the format
docs/CONVERTER.md this page
examples/         a converted bundle to look at
```

### Use it from your own C++ code

The command-line tool only calls two functions. A Python binding or a UI
can call the same two.

```cpp
#include <iostream>
#include "htpd/converter.hpp"

int main() {
    htpd::ConvertOptions options;
    options.input = "manual.pdf";
    options.output_dir = "manual.htpd";
    const auto report = htpd::convert(options);
    if (!report) {
        std::cerr << report.error().message << '\n';
        return 1;
    }
    std::cout << report->sections << " sections written\n";
}
```

`htpd::inspect()` analyses a PDF without writing anything. Errors come back
as values (`std::expected`); the library does not throw for a bad PDF.

## What was tested

* **70 automatic tests** (`make test`). Most build their input in memory.
  The others convert four small PDFs in `tests/data` through the real PDF
  library: rotated pages, scaled fonts, text spaced only by kerning,
  two columns, code listings, fonts whose names lie about their widths.
* **The tests were tested.** 99 times one rule of the converter was broken
  on purpose. Every time at least one test failed.
* **Compiler checks.** No warning with `-Wall -Wextra -Wpedantic -Wshadow
  -Wconversion -Wsign-conversion` and more. All tests and all documents
  below also ran under AddressSanitizer and UndefinedBehaviorSanitizer
  without a finding.
* **Three real manuals:**

  | Document | Pages | Sections | Bookmarks found in the text | Time |
  |---|---|---|---|---|
  | RISC-V Debug Specification | 119 | 164 | 164 of 164 | 0.3 s |
  | Arm AAPCS64 (procedure call standard) | 65 | 125 | 124 of 124 | 0.2 s |
  | RISC-V Instruction Set Manual | 906 | 1015 | 218 of 218 | 2.6 s |

  In the third one the bookmarks only go down to sections like `2.1`. The
  converter found 797 more numbered headings in the text (`2.1.1` and
  deeper), so the largest section file is 35 kB instead of 210 kB. In each of the three manuals,
  half of the section files are smaller than 3 kB. `document.md`, the file
  a reader opens first, is 31 kB for the 906 pages.
  Every bundle was checked by a separate script: valid YAML, no broken
  link, every connection points to an existing node.
* **301 odd and damaged PDFs** from PDFium's own test collection, in two
  modes each: no crash, no hang. 273 were converted, 28 were refused with a
  clear message (damaged, password needed, no pages).

## Limits (please read)

This is version 0.1. It does one job, text with structure, and it is
honest about the rest.

* **Only tested with English technical documents.** Text in scripts that
  need shaping (Gujarati, Hindi, Arabic, ...) may come out in a wrong
  order. This was not tested.
* **OCR needs Tesseract.** Scanned pages and words inside pictures are read
  only when the free Tesseract program is installed (see "OCR" above).
  Without it a scanned page has no text - it is kept as a picture of the
  page, which a search of the text cannot find; the converter tells you. OCR text may hold misread words, and Tesseract reads small
  or blurred text and handwriting badly; the bundle marks the pages
  (`ocr_pages`).
* **Tables are rows of text.** Cells are split by ` | `. A cell that wraps
  onto several lines is joined, and a table that the PDF stores cell by
  cell (register field tables) is put together by position, when the
  columns are certain; otherwise the lines stay as they stand. A row with
  only two cells is written as a plain paragraph, and a cell that spans
  several columns is not shown as such. Empty cells are only marked
  when another row of the same table (on the same page) has all cells, so
  that the columns are certain. Tables whose first column wraps too (change
  histories, lists of references) can still come out as rows plus loose
  lines. Always check important numbers from a table against the PDF page.
* **Figures are found by rules.** A drawing without a caption "Figure N"
  must have many shapes and little text; a small drawing without a caption
  is not stored. Charts and diagrams that are one picture are stored, but
  what they mean is only in the picture: an AI must look at it.
* **Formulas** lose their form (fractions, indices, roots).
* **Reading order** is the order in which the PDF stores the text. That is
  right for almost all PDFs, also with two columns. The converter does not
  work out columns by itself.
* **Headings.** Bookmarks are the best source. Without bookmarks the
  converter uses a table of contents printed in the document (its titles
  must stand alone on a line in the text), numbered headings (`2.1 Supply`)
  or headings in a larger font. A title the page hides (under a bar of a
  web page printed to PDF) cannot be found: its text joins the section
  before it. A document with neither gets one section per page; one with few
  headings still gets parts: where its running header changes, and parts
  of about 2 pages, titled by their pages.
* **Headers and footers** are only searched in documents with four or more
  pages. A header that is different on every page and follows no pattern
  stays in the text. Everything that is removed is listed in
  `document.md`.
* **Footnotes** stay where they stand on the page, so they can sit in the
  middle of a section's text.
* **The bundle is not smaller than the text of the PDF.** It is organised,
  not compressed. The gain is that an AI can read 3 kB instead of 5 MB.
  The converter does not make any AI faster or smarter by itself; that
  depends on the reader (part B).
* **Linux only** for now.
