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
* On a scanned page the size of a line comes from the height of its
  letters ("Pin" reaches higher than "on", "type" lower), and bold from the
  width of its strokes. A short line clearly larger or bold is a heading;
  the rest of a wrapped table cell (starting in lower case) and a row of a
  table are not. Text read from a picture on a page with text never is.
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
frames or backgrounds over the whole page are left out. Each "Figure N"
caption gives one figure, and its caption is that line (with the next line
when it holds only the number). `--figures off` stores none.

A picture the PDF shows on many pages (a logo, a repeated photo) is drawn,
stored and read by OCR once: the later figures point at the first one. A
1000-page sample with one picture per page, seven different ones, converts
in 12 s into a file of 1.5 MB.

### Tagged PDFs

Word, LibreOffice, InDesign, Chrome and others write the structure of the
document into the PDF (a "tagged PDF"): which text is a heading of which
level, which is a cell of which table, a list item, a figure with its
caption, or page furniture. The converter reads this tree with PDFium and
uses it where it can be trusted. What it gives (FORMAT.md 4.6):

* `H`, `H1`-`H6`: headings with their level (`heading_from: tags`), when the
  PDF has no usable bookmarks and the tags name at least as many headings as
  a printed table of contents or numbered headings in the text show. With
  bookmarks the bookmarks stay the source.
* `Table`: the rows with every cell exact, also cells of several lines and
  cells the page shows far apart; a cell over several columns or rows has
  `[>]` or `[=]` after its text and the cells it covers are empty.
* `L`/`LI`: every list item is one paragraph.
* `Figure` and `Caption`: one figure with its caption.
* `Artifact` text at the top or bottom of a page: taken out, listed as
  removed header/footer lines.

Trust, checked per element and per page: all the text an element names must
be on its page; its characters must stand together in the order the PDF
stores them; the cells of a table must lie in the order of the page, with
nothing untagged or tagged otherwise on the table; a heading tag on a long
paragraph is no heading. An element that fails is read by the ordinary rules,
so the result is what a PDF without tags gives, and the notes count these
(`Left to the ordinary rules ...`). Text that is not tagged is never dropped.
A PDF without tags gives exactly the same file as before.

Cost and gain (`tests/bench`, `tests/zoo`):

* What it costs: PDFium builds the tree of one page from the structure of
  the whole document, so one call of `FPDF_StructTree_GetForPage` costs about
  0.5 to 1.5 ms per page and grows faster than the document does. A Word
  datasheet repeated to 41, 81, 161 and 321 pages (dense tags, 100 elements
  per page) converts in 0.056, 0.105, 0.216 and 0.433 s with `--tags off` and
  in 0.124, 0.284, 0.639 and 1.849 s with `--tags on`. That call is about
  three quarters of the added time (counted in instructions); walking the tree and the checks of
  `src/tags.cpp` are about a sixth. The marks of the characters cost nothing
  measurable. PDFium offers no cheaper way to read the tree, so the tree
  costs more than 5 % of the conversion time of every tagged PDF tried (2 to
  5 ms more for the 3-page documents of the zoo, which convert in 5 to 8 ms). A PDF without tags is
  not slower, and neither is a tagged PDF with `--tags off`.
* So the default `--tags auto` reads the tags only where they pay for
  themselves: where the headings found without them are poor. A tagged PDF
  is converted once without its tags (headings from the bookmarks, a printed
  contents, the style sheet and the text). When these headings are **poor** -
  none, fewer than 2 + one for every 15 pages (one for a document of one or
  two pages), or all on one level in a text of more than 1.5 pages per heading
  - the PDF is converted again with its tags, at any page count. When they
  are good, the tags are not read, and the notes say so (`its structure tags
  were not read (it has bookmarks, which give the headings)` or `(the headings
  found without them are good)`) and name `--tags on`. The first conversion
  costs about as much as `--tags off`; it is thrown away when the second one
  is made. Measured on a generated Word-like PDF of 320 pages with 40
  elements a page and no bookmarks (`tests/bench/make_long_tagged.py`, best
  of 5): headings set like body text (`long-tagged-flat`): `--tags off`
  0.100 s and 40 sections, `--tags on` 0.363 s and 360, `auto` 0.463 s and 360
  (the gate found the headings poor and paid for the first conversion as
  well: +0.36 s, 9 times the sections); headings set bold and larger
  (`long-tagged-styled`): `off` 0.103 s, `on` 0.359 s, `auto` 0.105 s - the
  same 360 sections as `on`, without its 0.25 s. Until S2 a PDF of more than
  200 pages was never read in `auto`, and one with bookmarks never.
* Tags also give the document a title when the PDF has none (the `Title`
  element, after the `/Title` of the PDF and before the title printed on the
  first page), and, with bookmarks that stop early, the deeper headings of
  the tags: a tag heading below a bookmark that is a tag heading itself, with
  a smaller tag level, is added one level below it - but only when the style
  of its text is a heading style of the document as well (see "Styles and
  producers").
* What it gives: on Word exports without bookmarks whose headings look like
  body text, the headings found go from 0 of 6 documents to all 6 (3, 1, 1,
  1, 2 and 2 sections become 16, 4, 5, 4, 12 and 9); the table rows of the
  zoo document "datasheet" 0.81 to 0.88. With `--tags on`, tagged documents
  that have bookmarks gain too: a running header and a page number on a
  one-page document are taken out (the noise in the text of such a document
  falls from 0.12 to 0.03).

### Tables

Three ways to know where the cells of a table are, tried in this order (`src/tables.cpp`, with
`src/columns.cpp` and `src/sections.cpp`). Where none is certain, the lines stay as they stand: no text is
moved into a cell it does not stand in, none is left out.

| Method | Evidence | What it gives |
|---|---|---|
| Tags | the structure tree of a tagged PDF (see "Tagged PDFs") | every cell exactly, spans |
| Lattice | the lines drawn on the page: horizontal and vertical rules, and thin filled bars (how Word and LibreOffice draw borders) | the cells are the boxes the lines make, also cells over several columns or rows (`[>]`, `[=]`, as for tags) |
| Stream | the positions of the text: columns from the strips the rows leave free, rows from bands across the page | rows and cells of a table without lines |

**Lattice** takes the rules of a page, joins those that lie on one line, finds the grid they make and the
cells (a box without the line between two atoms is one cell, so a cell can span). The words go into the cell
their middle lies in. The grid is used only when it is consistent, otherwise the page is left to the
stream method: every cell is a rectangle, no word crosses a line of the grid, at least 40 % of the cells hold
text and two rows have two cells with text (a chart with grid lines, a frame round a paragraph and a page
border are no tables); no cell of four lines or more shares its row with another such cell (rows whose
rules the page does not draw: the lines are read by the text); and every line lies in the table as a whole
or outside it. In a cell, the lines are joined with a space - except where a name is broken inside itself
because the column is too narrow ("LCD_P2" over "4", "US-GAAP" over "-1010", "($1,433,600." over "00)"):
there the geometry decides, since the line before is one word that reaches the right limit of the cell.
Values stacked in a cell - two cells of a row with a number on each of their lines, the other tall cells
with as many lines at the same heights ("Level 1..4 falling | 2.62 | 2.70 | 2.78") - become one row per line.
`--tables text` switches the lattice method off.

**Joining the lines of a cell** (all methods): a space, except where the next line certainly continues the
last word: the first part ends with `_` or `/`; the next starts with `_` or is a lone `)`; a short number
follows a name that has letters and digits or `_` ("LCD_P2" + "4"; two numbers one under the other are not a
name); a code in capitals breaks at its hyphen ("US-GAAP" + "-1010"); an amount is completed ("($1,433,600."
+ "00)", "$1,433," + "600.00"); two halves of a name with `_` in lower case or in capitals ("CAN_FD_CONT" +
"ROL_STATUS" - but not "SPI_CLK" + "SPI_MISO", two names that start alike); a rest of at most three letters
under a word as wide as its column ("DEFAU" + "LT"). Never a character is invented or dropped.

### Styles and producers

**The style sheet** (`src/styles.cpp`). Before headers, footers and headings
are looked for, every line is put into a style: font, size (half points),
weight, italic and fill colour. For each style the converter counts how much
of the text it holds, how often it starts a block of lines, stands alone
(a block of at most 3 lines and 20 words that ends without a full stop),
is followed by body text, carries a section number or the word "Figure" or
"Table", and where on the page it stands. The styles get a role:

| Role | Rule |
|---|---|
| body | the style with the most text (not code, not at the page edge) |
| code | the font is monospace (measured, see AGENTS.md) |
| header/footer | at least 90 % of its lines are in the top or bottom band, on 3 pages or more, and smaller than the body |
| footnote | smaller than the body, 70 % of its lines in the lower third of the page |
| caption | at least 60 % of its lines start "Figure N", "Fig. N", "Table N" (not in the body style) |
| table text | at least 60 % of its lines are rows of a table |
| heading 1, 2, ... | rare (at most 12 % of the text), 3 blocks or more on 2 pages or more (2 blocks in a document of 3 pages or less), at least 90 % short, 80 % ending without a full stop, 80 % followed by body, table or code text or by another heading style; in the size of the body, also one block more than the minimum, each directly before body text. Levels: larger first, then bold, then, for two styles of one size and weight, the one that stands before the other, or has the smaller section depth |

The notes of every bundle list the roles found, one line each (`heading 2:
Verdana 15 pt #0091BD, 49 lines`, FORMAT.md section 3.2).

The roles are used only where they make a decision more certain; where the
style sheet is unsure, today's rules decide, and every heading found without
it is still found:

* **Headings.** A line in a heading style that the size rule does not take
  (the same size as the text, set apart by colour, weight, font or italic) is
  a heading; its level is one below the nearest larger heading style. A line
  that starts like a caption is never promoted.
* **Headers and footers.** A small style that stands in one slot of the band
  on at least 70 % of the pages with text (4 pages or more, one line a page)
  is page furniture although its text changes from page to page (a chapter
  title). A style larger than the body is a title, as on slides, and stays.
* **Tags.** The deeper tag headings under bookmarks (see "Tagged PDFs").

**Producers.** The Producer and Creator entries of the PDF tell which program
made it: Word, LibreOffice, LaTeX (pdfTeX, XeTeX, LuaTeX), Chrome/Skia (web
pages, Confluence), wkhtmltopdf, InDesign; anything else is "other" and
unknown (no entry) is "unknown" - those get the general rules only. The notes
say it (`Made by: Skia/PDF m153, read as Chrome/Skia.`). The habits used:

* **Chrome/Skia**: its print template puts the date and time before the title
  above every page and the page count (`n/N`, with an address in front of it
  when Chrome writes one) below. When the topmost line of every page starts
  with the same date and time, it is taken out as a header; when the lowest
  line of every page is `n/N` with n the number of the page and N the number
  of pages, it is a footer - also in a document of one or two pages, where
  nothing repeats three times. They are listed in the notes like any removed
  header or footer. (On the bench: the 1-page German, Hindi and Gujarati web
  pages of the zoo.)
* **LaTeX**: a chapter is set as the line `Chapter 3` (or `Appendix A`) with
  its title in a larger size on the next line. With a LaTeX producer the two
  lines are one heading, `Chapter 3 Results`, number 3, level 1. Not shown on
  the bench of this machine, which has no LaTeX: tested with a PDF made for
  it (`tests/data/latex_chapters.pdf`); the zoo makes LaTeX books where
  `xelatex` is installed.
* **Word / LibreOffice**: no habit of its own. Their heading styles show in
  the style sheet and, where the PDF is tagged, in the tags (the gate above).

Cost and gain (`tests/bench`, 57 documents, best of 5, converter before S2 and
after on the same machine): total time 3.17 s to 3.22 s (+1.4 %; the style
sheet is one pass over the lines), total size +0.2 % (the notes: about 300
bytes a bundle), text, headings, table rows and figures of the public documents
unchanged, eval (test and tune split) unchanged question by question. What it
gives: a document whose headings differ from the text only by colour or
weight goes from 2 sections to 12 (`tests/data/styles_weight.pdf`), a LaTeX
chapter is one heading, the date, title, address and `1/1` that Chrome
prints on a one-page web page leave the text (`german-web`: words in order
84.0 % to 91.7 %). One cost in the bench numbers is a measuring effect: the
Hindi and Gujarati web pages show 6 more "lost words", the date and time of
the removed Chrome header, which `pdftotext` mixes into another line and the
bench cannot take out of the PDF page; the removed header is listed in the
notes with its text.

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
| `--tags auto\|on\|off` | The structure tags of a tagged PDF (headings, tables, lists, figures, page furniture), used where they agree with the page text. `auto` (default): when the PDF is tagged and the headings found without the tags are poor (see "Tagged PDFs"). `on`: whenever the PDF has tags. `off`: read every PDF as if it had no tags. Reading the tags costs time, see "Tagged PDFs". |
| `--tables auto\|text` | How the cells of a table are found. `auto` (default): from the PDF's tags, from the lines drawn on the page (lattice) and from the text. `text`: not from the lines drawn on the page. See "Tables". |
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
| 1. Read | `src/pdf_reader_pdfium.cpp` | PDFium gives every character with its position, size and font, and the links of each page. Where the PDF says what glyphs mean (`/ActualText`, used for Gujarati, Hindi and other scripts whose letters are made of several glyphs), that text is taken, once. Page rotation is removed here, and a page whose text mainly runs sideways or upside down is turned, so all later steps see upright pages. This is the only file that knows PDFium. |
| 1b. OCR | `src/ocr.cpp` | Pages without a text layer and pictures without text of the PDF on them are drawn (by PDFium, in `pdf_reader_pdfium.cpp`) and read by the Tesseract program, several at a time, when it is installed. See "OCR" above. |
| 1c. Figures | `src/figures.cpp`, `src/png.cpp` | The pictures and drawings of each page that are figures, with their caption; drawn by PDFium and packed as PNG (several at a time). See "Figures" above. |
| 1d. Tags | `src/tags.cpp` | In a tagged PDF: the structure tree and the marked-content ID of every character (read in `pdf_reader_pdfium.cpp`) tell which characters are a heading, a table cell, a list item, a figure or page furniture. See "Tagged PDFs" above. |
| 2. Lines | `src/layout.cpp` | Characters become lines. Wide gaps are marked (table columns); raised and lowered numbers stay raised (2³¹). |
| 2b. Styles | `src/styles.cpp` | The style sheet: every line into a style, the styles into roles (body, headings, captions, table text, code, header/footer, footnote), and the producer of the PDF. See "Styles and producers" above. |
| 3. Headers and footers | `src/furniture.cpp` | Lines that repeat at the page edge on many pages are taken out and reported (in a document of two or three pages: on every page). A running header is set apart from the text: a line that stands right on top of the text below it (closer than 2.5 of its font sizes) stays in the text - a heading or the header row of a table at the top of the page - unless it holds the number of its page or stands on almost every page. |
| 3a. Ruled tables | `src/tables.cpp` | Tables drawn with lines: the cells the rules make, spans, stacked values, names broken in a cell. See "Tables". The rules of a page come from `src/pdf_reader_pdfium.cpp`. |
| 3b. Columns | `src/columns.cpp` | A table set tight (LaTeX, web pages, slides) has column gaps narrower than two font sizes. Its rows leave the same strips of the page free; the spaces on those strips become column gaps, also in the header row. Running text crosses the strips and is left alone. |
| 4. Headings | `src/headings.cpp` | From the bookmarks of the PDF (each found as its heading line on its page - the line in the largest font, so not a running header with the same words - or at the top of the next page); or from a table of contents printed in the document, whose titles are looked for in the text after it, in order (also when the text shows them without their number); or from the text: section numbers (also `Register 30.2.43: ...`, one level below its section), font size, bold, another font, and a heading style of the style sheet (a colour or a weight of its own). A numbered cell of a table row (another cell beside it on its line) is no heading. Large titles in a smaller style than the numbered headings, used three times or more, are headings one level below them. |
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
tests/            test_main.cpp, small test PDFs in tests/data, the stress test in tests/stress,
                  the document zoo in tests/zoo, the benchmark in tests/bench
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

* **156 automatic tests** (`make test`). Most build their input in memory.
  The others convert small PDFs in `tests/data` through the real PDF
  library: rotated pages, scaled fonts, text spaced only by kerning,
  two columns, code listings, fonts whose names lie about their widths,
  scanned pages, figures, `/ActualText`, and tagged PDFs written by hand
  (`tagged.pdf`: headings set in the body font, a table with a cell of three
  lines, a cell the tags leave out, cells over two columns and two rows, a
  list, a drawing with a caption, a header that changes on every page;
  `tagged_disagree.pdf`: tags that do not fit the page, which must be read as
  if they were not there).
* **The document zoo** (`tests/zoo`). The same documents - a datasheet, a
  manual, a report, and short ones in German, Hindi and Gujarati - are made
  into PDFs by the programs real documents come from: Word export
  (LibreOffice), LaTeX (with and without bookmarks, two columns, a book), web pages
  printed by Chromium and wkhtmltopdf, slide decks, scans (straight and
  turned, with noise), pages whose letters are drawn as shapes, and tagged
  Word exports (`word-tagged`: no bookmarks, so only the tags say what a
  heading is; `word-hf`: with a running header and a footer with the page
  number). As the
  text of every document is known, `run_zoo.py` measures each `.htpd` file:
  how much of the text it holds in order, how much else, which headings and
  table rows it found.

  ```sh
  python3 tests/zoo/make_zoo.py build/zoo
  PYTHONPATH=reader python3 tests/zoo/run_zoo.py build/zoo [--show manual-scan]
  ```

  Over the 33 English PDFs: 99 % of the text in order, 96 % of the
  headings (the scans and shape-letter pages: 86 %), 89 % of the table rows
  exact (on scans OCR misreads some numbers: always check a value from a scan
  against the page). Hindi and Gujarati from Word: 91 % and 99 % of the text.
* **The benchmark** (`tests/bench`). `run_bench.py` converts every PDF of a folder
  (your own in `private/bench/`, the five public manuals of the evaluation, the zoo, the
  small test PDFs) and compares each `.htpd` file with its PDF page by page: time, size, text
  in order and on the right page, lost and invented words, headings, table rows, figures.
  `--compare before.json after.json` shows what a change did and whether it stays within
  the limits (accuracy never worse, time at most +10 %, size at most +5 %). How to use it and
  what each number means: [`tests/bench/README.md`](../tests/bench/README.md).
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

* **Mostly tested with English technical documents.** German, Hindi and
  Gujarati were tested (the document zoo). Hindi and Gujarati come out well
  from Word and LibreOffice; from web pages printed by Chrome and from LaTeX
  some words are split or run together, because those PDFs do not say where
  a word ends. Arabic and Hebrew (right to left) were not tested.
* **OCR needs Tesseract.** Scanned pages and words inside pictures are read
  only when the free Tesseract program is installed (see "OCR" above).
  Without it a scanned page has no text - it is kept as a picture of the
  page, which a search of the text cannot find; the converter tells you. OCR text may hold misread words, and Tesseract reads small
  or blurred text and handwriting badly; the bundle marks the pages
  (`ocr_pages`).
* **Tags are only as good as the program that wrote them.** A PDF may carry
  wrong or incomplete tags (a heading tag on a paragraph, a table with its
  cells in another order, untagged text between the cells). The checks above
  catch most of this, not all: a tag that is wrong in a way the page does not
  show (a body paragraph tagged as `H2` and short) is believed. PDFium does
  not tell the type of an artifact, so text marked as an artifact in the top or
  bottom fifth of the page is taken out whatever its type.
* **Tables are rows of text.** Cells are split by ` | `. A cell that wraps
  onto several lines is joined, and a table that the PDF stores cell by
  cell (register field tables, ledgers whose cells are set in the middle of
  the row, datasheet tables with stacked conditions and values) is put
  together by position, when the columns are certain; otherwise the lines
  stay as they stand. A row with
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
* **Headers and footers** in a document of two or three pages are only
  removed when they stand on every page; a one-page document keeps them. A header that is different on every page and follows no pattern
  stays in the text, and so does a header printed right on top of the text
  (closer than 2.5 of its font sizes) that has no page number and is not on
  almost every page. Everything that is removed is listed in
  `document.md`.
* **Footnotes** stay where they stand on the page, so they can sit in the
  middle of a section's text.
* **The bundle is not smaller than the text of the PDF.** It is organised,
  not compressed. The gain is that an AI can read 3 kB instead of 5 MB.
  The converter does not make any AI faster or smarter by itself; that
  depends on the reader (part B).
* **Linux only** for now.
