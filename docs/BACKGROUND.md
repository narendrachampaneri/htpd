# Background: why HTPD is the way it is

The rules for coding agents are in `AGENTS.md`, the format in
`docs/FORMAT.md`, the converter in `docs/CONVERTER.md`, the reader in
`docs/READER.md`.

## The idea

An AI that reads a large document - a 300-page PDF, say - as one long text
loses details, uses many tokens and cannot say on which page something
stands. HTPD gives the AI the document in a form made for it:

* **The converter** turns a document into an HTPD file: the document's own
  sections, every paragraph, table and code block with its page, the
  tree of headings, the links between sections, and the text inside
  pictures read by OCR. It never summarises or rewrites.
* **The reader** finds, for a question, the few sections that answer it and
  hands exactly that text, with its pages, to any AI - or says "Not found
  in this file." with the reason. It also comes as an MCP server for AI
  agents and as a skill for AI apps.

A single `.htpd` file also works with no tool at all: it starts with
reading rules that tell any AI how to use it.

## Decisions

* **No new format from scratch.** An HTPD bundle is a valid bundle of the
  Open Knowledge Format (OKF 0.2), plus HTPD's own fields under `htpd:`.
* **Exact text with pages.** Every piece keeps the original text and its
  page. Nothing is summarised, rewritten or guessed - not by the converter,
  not by OCR (unsure words are left out), not by the reader.
* **Local, free and light.** Everything runs on a normal PC, offline,
  without a GPU and without any model download. The converter is C++23
  with PDFium; OCR uses the free Tesseract program when it is installed.
  The reader is plain Python. Large language models were considered for
  the reader and dropped: they need a GPU to train and are slow on a CPU.
  The reader's one learned part, the re-ranker, is a few kilobytes of
  weights.
* **Measured, not promised.** Every claim about the reader comes from the
  evaluation harness on public test documents (`eval/report.md`). Test
  documents are never used for tuning or training.
* **Private documents stay private.** They are never committed, uploaded or
  put into a prompt for a cloud model.

## What real documents taught

* Header and footer removal at first deleted real text; it now needs a
  repeated position at the page edge.
* Bookmarks are often too coarse, so numbered headings found in the text
  are added below them; documents with few headings get parts where their
  running header changes and parts of about two pages.
* Code listings need their spacing, so monospace fonts are measured from
  their character widths. Empty table cells are only marked when the
  columns are certain. Raised numbers stay raised (`2³¹`, not `231`).
* A question in the user's words is the hard case for "not found": the
  rule "the document never mentions this word" must allow other forms of a
  word and short forms used in tables.
