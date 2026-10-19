# HTPD converter

`htpd` turns a PDF into an **HTPD bundle**: a folder of small text files that
an AI model can read piece by piece instead of reading the whole document.

* Every heading of the PDF becomes one file (a *node*).
* Every node knows its **connections**: the section above it, the sections
  below it, the one before and the one after.
* Every paragraph keeps its **page number**, and every file names the PDF
  it came from (with a checksum), so each statement can be checked.
* The text is kept as it stands in the PDF. Nothing is summarised and
  nothing is made up.

This is **part A** of the HTPD idea (the converter). **Part B**, the tool and
model that tell an AI how to read a bundle, is in Python in `reader/`,
`eval/` and `model/` (see "Part B" below; the plan is `docs/PART-B.md`).
`docs/FORMAT.md` describes the format, so that B (or anybody) can read the
bundles.

The converter is C++ (C++23). There is no database program: the bundle
folder on the disk *is* the database. Part B needs only Python 3.10+ and
PyYAML; models are optional extras.

## Quick start (Ubuntu)

```sh
sudo apt install build-essential curl      # g++ 13 or newer is needed
./scripts/get_pdfium.sh                    # downloads the PDF library once
make                                       # builds build/htpd
make test                                  # all C++ tests
./build/htpd convert tests/data/sample.pdf -o /tmp/sample.htpd
```

Then open `/tmp/sample.htpd/document.md`. The same bundle is in
`examples/sample.htpd/`, so you can look at one without building anything.

To hand a document to an AI chat (Claude, ChatGPT, ...) as **one file**:

```sh
./build/htpd convert manual.pdf --single        # writes manual.htpd
```

Upload `manual.htpd` instead of the PDF. It starts with the rules the model
must follow (answer only from this file, give the page of every fact, say
"Not found in this file." when the answer is not there), then the contents
and every section with its pages. `examples/single-file/sample.htpd` is one.

(If the system answers "Permission denied" to the script, start it with
`bash scripts/get_pdfium.sh`.)

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

All fields and rules are in [`docs/FORMAT.md`](docs/FORMAT.md).

## What you need

| | |
|---|---|
| System | Linux. Built and tested on Ubuntu 24.04 (x86-64). |
| Compiler | g++ 13 or newer. The code uses C++23 (`std::expected`, `std::format`). Ubuntu 24.04 has g++ 13. On Ubuntu 22.04 the standard g++ 11 is too old; install a newer g++ first. Clang was not tried. |
| Build tool | GNU make, or CMake 3.20 or newer. |
| PDF library | PDFium, the PDF engine of Chrome. `scripts/get_pdfium.sh` downloads it (about 4 MB). |
| For the script | bash, curl or wget, tar. |

Nothing else. No Python, no database, no other library.

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
htpd convert manual.pdf                  # writes manual.htpd in the current folder
htpd convert manual.pdf -o out/manual    # another output folder
htpd convert manual.pdf --force          # replace a bundle made before
htpd info manual.pdf                     # show what would be found, write nothing
```

`htpd --help` lists all options. The important ones:

| Option | What it does |
|---|---|
| `--headings auto` | Default. Uses the bookmarks of the PDF, and adds numbered headings found in the text below them. |
| `--headings bookmarks` | Only the bookmarks. |
| `--headings text` | Ignores bookmarks; finds headings in the text. |
| `--headings none` | No headings: one section per page. |
| `--keep-headers` | Keeps the running headers and footers in the text. |
| `--dehyphenate` | `inter-` + `rupt` becomes `interrupt` instead of `inter-rupt`. |
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

One PDF goes through six steps. Each step is one small file.

| Step | File | What happens |
|---|---|---|
| 1. Read | `src/pdf_reader_pdfium.cpp` | PDFium gives every character with its position, size and font. Page rotation is removed here, and a page whose text mainly runs sideways or upside down is turned, so all later steps see upright pages. This is the only file that knows PDFium. |
| 2. Lines | `src/layout.cpp` | Characters become lines. Wide gaps are marked (table columns). |
| 3. Headers and footers | `src/furniture.cpp` | Lines that repeat at the page edge on many pages are taken out and reported. |
| 4. Headings | `src/headings.cpp` | From the bookmarks of the PDF, and from the text: section numbers, font size, bold, another font. |
| 5. Sections | `src/sections.cpp` | The text is cut at the headings. Lines become paragraphs, table rows and code blocks. The tree of sections is built. |
| 6. Write | `src/bundle_writer.cpp` | The files of the bundle are written. |

`src/converter.cpp` runs the steps. `cli/main.cpp` is the command-line
tool, a thin layer on top. `src/text.cpp` and `src/sha256.cpp` are helpers.

The source tree:

```
include/htpd/     the headers (the public one is converter.hpp)
src/              the converter library
cli/main.cpp      the command-line tool
tests/            test_main.cpp, and four small test PDFs in tests/data
scripts/          get_pdfium.sh
docs/FORMAT.md    the format
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

* **63 automatic tests** (`make test`). Most build their input in memory.
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
* **No OCR.** A scanned page has no text for the converter. It counts such
  pages and tells you.
* **Tables are rows of text.** Cells are split by ` | `. A cell with several
  lines of text is not put together again, and a row with only two cells
  is written as a plain paragraph. An empty cell is only marked when
  another row of the same table (on the same page) has all cells, so that
  the columns are certain. Always check important numbers from a table
  against the PDF page.
* **No pictures.** Figures are not converted. Words inside a drawing that
  are real text in the PDF do come out, as loose lines.
* **Formulas** lose their form (fractions, indices, roots).
* **Reading order** is the order in which the PDF stores the text. That is
  right for almost all PDFs, also with two columns. The converter does not
  work out columns by itself.
* **Headings.** Bookmarks are the best source. Without bookmarks the
  converter needs numbered headings (`2.1 Supply`) or headings in a larger
  font. A document with neither gets one section per page.
* **Headers and footers** are only searched in documents with four or more
  pages. A header that is different on every page and follows no pattern
  stays in the text. Everything that is removed is listed in
  `document.md`.
* **Footnotes** stay where they stand on the page, so they can sit in the
  middle of a section's text.
* **The bundle is not smaller than the text of the PDF.** It is organised,
  not compressed. The gain is that an AI can read 3 kB instead of 5 MB.
  The converter does not make any AI faster or smarter by itself; that
  depends on part B.
* **Linux only** for now.

## Part B: reading bundles

A bundle already holds what a reader needs: the contents list with pages,
one short description per section, the tree, the order, and the page of
every paragraph. Part B uses this: a reader, a search, and a decision model
that pick the right sections and hand their exact text, with pages, to any
AI model, through an MCP server.

```sh
python3 -m pip install -e .                   # htpd-read and htpd-mcp
htpd-read contents examples/sample.htpd
htpd-read check examples/sample.htpd
htpd-mcp --bundles ~/manuals                  # MCP server over stdio
python3 -m pip install pytest && python3 -m pytest     # Python tests
```

* `reader/` - the package `htpd_reader`: reader, check, search, retriever,
  packer, decider, MCP server (`reader/README.md`).
* `eval/` - test questions, the harness and `eval/report.md`
  (`python -m eval --help`).
* `model/` - training data and the decider models (`model/README.md`).
* `skills/htpd/` - the **htpd skill** for AI apps (see "Adding HTPD to an AI" below).

### Adding HTPD to an AI

**Any app that supports skills** (the Claude app, Claude Code and other
agents that read `SKILL.md` files):

```sh
python3 scripts/build_skill.py        # writes dist/htpd-skill.zip
```

* Claude app: Customize > Skills, upload `htpd-skill.zip`, switch it on.
  Code execution must be on. Then upload a `.htpd` file to a chat and ask.
* Claude Code and other agents: unpack the zip into their skills folder
  (for Claude Code `~/.claude/skills/`, giving `~/.claude/skills/htpd/`).

The skill needs only Python 3.10+ in the app's sandbox, nothing else. It
answers from the file with pages, or says "Not found in this file." and
why. In an app without code it still tells the model how to read the file.

**Tools that use MCP servers** instead of skills:

To use it from Claude Code: `claude mcp add htpd -- htpd-mcp --bundles ~/manuals`.
Other agents (Antigravity and the like) take the same command in their MCP
settings: command `htpd-mcp`, arguments `--bundles /path/to/bundles`.

The plan, phase by phase, with the tests that B must pass, is in
[`docs/PART-B.md`](docs/PART-B.md). The rules for the coding agents are in
[`AGENTS.md`](AGENTS.md).

## Licences

* No licence has been chosen for this code yet. Add one before you publish
  it.
* PDFium has its own licence (BSD style, with an Apache 2.0 part) and
  contains other libraries. After the download all their licence texts are
  in `third_party/pdfium/licenses/` (PDFium itself: `pdfium.txt`). If you
  give the program to others together with `libpdfium.so`, give them these
  files too.
* `tests/data/make_test_pdfs.py` (optional, needs the Python package
  `reportlab`) only makes the test PDFs again. The PDFs are already there.
