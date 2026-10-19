# Background: how HTPD came to be

Status as of 19 October 2026. Read this to understand why the project is
the way it is. The rules are in `AGENTS.md`; the format is in
`docs/FORMAT.md`; the plan for part B is in `docs/PART-B.md`.

## The idea

A normal AI ("X") reads a large document, such as a 300-page PDF, as one
long text. It loses details, uses many tokens and cannot say on which page
something stands. The owner of this project, Narendra, wants an AI "Y"
with two parts:

* **A - the converter.** It only converts any document (PDF first) into
  HTPD: a set of connected nodes that an AI can read piece by piece.
* **B - the reader tool and model**, "the strength and power". It tells
  any AI how to read HTPD and picks the right pieces for a question.
  Anybody who already has HTPD bundles should be able to add B to their
  own work.

## Decisions taken at the start

* No new format from scratch: an HTPD bundle is a valid bundle of Google's
  Open Knowledge Format (OKF 0.2), plus HTPD's own fields under `htpd:`.
* Every node keeps the original text and its page numbers. Part A never
  summarises or rewrites.
* No promises that are not measured (for example "answers in
  milliseconds").
* Part A is the hard part and comes first, PDF first.
* Part A is written in C++ (C++23) only, on Linux (Ubuntu). Python and a
  UI come later.

## Part A - done (version 0.1.0)

* `htpd convert file.pdf` writes a bundle: `index.md`, `log.md`,
  `document.md` and one file per heading in `sections/`. Each section file
  has its pages, its parent, children, previous and next section, and the
  SHA-256 of the PDF.
* PDFium (version chromium/8086) reads the PDF; `scripts/get_pdfium.sh`
  downloads it and checks its SHA-256. Builds with make and with CMake.
* Checked: 63 tests; 99 deliberate breaks of rules in the code, each one
  caught by a test; no warnings with strict flags; clean under
  AddressSanitizer and UndefinedBehaviorSanitizer.
* Tried on three real manuals: RISC-V Debug Specification (119 pages,
  164 sections), Arm AAPCS64 (65 pages, 125 sections), RISC-V Instruction
  Set Manual (906 pages, 1015 sections, 2.6 s). And on 301 odd and damaged
  PDFs from PDFium's own tests: no crash, no hang.
* What the real manuals taught (all fixed): header/footer removal at
  first deleted real text, so it now needs a repeated position at the page
  edge; bookmarks are often too coarse, so numbered headings found in the
  text are added below them; some headings use another font at text size;
  code listings need their spacing, so monospace fonts are measured from
  their character widths; empty table cells are only marked when the
  columns are certain; text that looks like Markdown gets one backslash.
* Known limits: tables only as rows of text, no OCR, no figures, formulas
  lose their form, only English documents tested, reading order is the
  order stored in the PDF, Linux only.

## Part B - next

The plan is in `docs/PART-B.md`: Python, offline first, free and open
models only. Reader → retriever → decider → packer. The decider is a
small decision model (an open cross-encoder of 22M parameters, fine-tuned
on HTPD data on a CPU) - that is what makes B "stronger", not a big
language model. Everything runs locally on a normal PC; the models are
optional. B is offered to any AI through an MCP server and a
`SKILL.md`. Every part must beat simple baselines on test questions from
documents that were never used for training. Seven phases; phase 1 is
the reader.

## How the work is done

* One phase at a time, each on its own branch. The author of a phase does
  not approve it: before a merge, the phase is reviewed and all tests are
  run.
* Test first; a task is done only when all tests pass.
* The rules for coding agents are in `AGENTS.md`.

## History

* 6-7 October 2026: the idea was discussed; part A was written, tested on
  real manuals and delivered as `htpd-0.1.0.zip`.
* 7 October: `AGENTS.md` (rules for coding agents) added.
* 9 October: the plan for part B written (`docs/PART-B.md`); the
  repository put on GitHub.
* 19 October: the code of part B, phases 1 to 6, written and tested, one
  branch per phase. Narendra decided that a phase needs no separate review;
  the author runs all tests before a merge. The measurements wait for the
  public PDFs and the models (see the table in `docs/PART-B.md` section 5).
  Laya was dropped from the plan: HTPD must stay local, free and light,
  and Laya needs a GPU to train and is slow on a CPU.
