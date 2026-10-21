# HTPD - rules for coding agents

Read these before you change anything:

* `README.md` - what HTPD is, quick start, how to use it with an AI.
* `docs/CONVERTER.md` - the converter: build, use, limits.
* `docs/FORMAT.md` - the file format. It is the contract between the
  converter and the reader.
* `docs/READER.md` - the reader: how it finds the answer, its rules, how it
  is measured.
* `docs/BACKGROUND.md` - why the project is the way it is: the idea and the
  decisions.

What is true: these five files and this one. If anything else (notes kept
by a tool, a generated summary, your own notes) says something different,
these files win - fix the other thing, or ask.

## Build and test

The converter (C++):

```sh
./scripts/get_pdfium.sh     # first time only
make                        # builds build/htpd
make test                   # all tests must pass
```

* Compiler: g++ 13 or newer, C++23. The build must stay free of warnings.
* There are two builds. The Makefile finds `src/*.cpp` by itself. A new
  source file must also be added to `CMakeLists.txt`.

The reader (Python 3.10+, folders `reader/`, `eval/`, `model/`): run its
tests with `python -m pytest` from the repository root (without pytest:
`PYTHONPATH=reader:. python -m unittest discover -s reader/tests -t .`, the
same for `eval/tests` and `model/tests`). Tests must not need the internet.

## Rules

1. Run the tests after every change. A task is not done before they pass.
2. Never change or delete a test to make it pass. If a test looks wrong,
   stop and ask.
3. Every new rule in the code gets a test that fails without the rule.
4. PDFium is used in one file only: `src/pdf_reader_pdfium.cpp`. No PDFium
   header in any other file.
5. C++ errors are returned as values (`htpd::Result<T>`, which is
   `std::expected`). No exception for a bad PDF.
6. No new library without asking. The converter is C++ with PDFium only (it
   may run the Tesseract program for OCR when it is installed; it never
   needs it). The reader uses the Python standard library only (PyYAML when
   it is installed); training the re-ranker uses numpy.
7. Never lose text and never make text up. When a case is not certain,
   keep the text as it stands.
8. The output format is a contract. When a change alters what is written
   into a bundle, update `docs/FORMAT.md` in the same change, make the
   example again, and say so:
   `cd examples && ../build/htpd convert ../tests/data/sample.pdf --force`
9. Small commits, one topic each.
10. Numbers about the reader come only from the evaluation harness
    (`eval/report.md`), on the test split. Never type a result by hand,
    never tune on the test split, never train on a test document.
11. Documents in `private/` are never committed, never uploaded and never
    put into a prompt for a cloud model.
12. Files of your own tools - editors, AI assistants, their notes and
    plans - are never committed. `.gitignore` lists the usual ones; read
    `git status` before every commit.

## How we work

* Small, tested steps. Before anything is pushed, all tests pass (C++ and
  Python) and the owner has agreed. A review by someone else is welcome but
  not required.

## Facts that are easy to get wrong

* The fields for the order of the sections are `previous` and `next`
  (not `prev`).
* `log.md` is a short history, one entry per conversion. It is not a
  report of the run. The report is `# Conversion notes` in `document.md`.
* Python is not part of the converter's build.
  `tests/data/make_test_pdfs.py` needs only `reportlab`, and it only makes
  the test PDFs again.
* `--headings auto` means: use the bookmarks, and add the numbered
  headings found in the text below them. It does not "pick the best mode".
* `src/layout.cpp` does not work out columns. It keeps the order in which
  the PDF stores the text. Only a page stored from bottom to top, or in no
  order at all, is sorted. (Inside a table, `src/sections.cpp` puts cells
  together by position: FORMAT.md 4.2.)
* A table row needs three or more cells. An empty cell is only written
  when the columns are certain (`rows_text` in `src/sections.cpp`).
* Whether a font is monospace is measured from the widths of its
  characters. It is not taken from the font name.

## The converter, step by step

| Step | File |
|---|---|
| 1. Read the PDF | `src/pdf_reader_pdfium.cpp` |
| 1b. OCR of scanned pages and pictures (runs the Tesseract program, when installed) | `src/ocr.cpp` |
| 2. Characters to lines | `src/layout.cpp` |
| 3. Headers and footers | `src/furniture.cpp` |
| 4. Headings | `src/headings.cpp` |
| 4b. Structure for few headings, parts of long text | `src/structure.cpp` |
| 5. Sections, paragraphs, tables, code | `src/sections.cpp` |
| 6. Links between sections | `src/links.cpp` |
| 7. Write the bundle | `src/bundle_writer.cpp` |

`src/converter.cpp` runs the steps. `cli/main.cpp` is the command-line
tool. The public header is `include/htpd/converter.hpp`.
