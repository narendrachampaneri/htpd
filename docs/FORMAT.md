# The HTPD format, version 0.1

This page describes what the converter writes. It is the contract between
the two parts of the HTPD idea:

* **A, the converter** (this project) turns a document into an HTPD bundle.
* **B, the reader** (the next step) tells an AI model how to use a bundle.

Anybody can write a reader from this page. No part of the format needs the
converter's source code to be understood.

## 1. In one minute

* A converted document is a **folder** ("bundle") of plain text files.
* Every heading of the document becomes **one file**: a *section*. A section
  is a node. One more file, `document.md`, is the root node.
* Every file starts with a small block of fields. The fields say where the
  text comes from (file, page, checksum) and how the node is **connected**:
  its parent, its children, the section before it and the section after it.
* The text itself is kept as it stands in the PDF. Every paragraph has a
  page number.
* The files are Markdown with YAML fields at the top, so any AI model and
  any text tool can read them. A bundle is also a valid bundle of the
  [Open Knowledge Format (OKF) 0.2](https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md).
  HTPD adds its own fields under one key, `htpd:`.

## 2. The folder

```
manual.htpd/
  index.md                    start page: links to document.md and to sections/
  log.md                      one entry per conversion
  document.md                 the document: contents, conversion notes
  sections/
    index.md                  one line per section: title, page, first words
    0001-front-matter.md      one file per heading, in the order of the document
    0002-revision-history.md
    0003-1-introduction.md
    ...
```

* A section file is named `NNNN-<title>.md`. `NNNN` is the position of the
  section in the document (first section = 0001; at least four digits).
  `<title>` is the heading in lower-case ASCII letters, digits and `-`, cut
  at 60 characters (`section` when nothing is left).
* The **ID** of a node is its path in the bundle without `.md`, for example
  `sections/0011-3-1-thermal-derating`. The ID of the root node is
  `document`.
* All files are UTF-8 with `\n` line ends.
* `examples/sample.htpd/` in this project is a complete small bundle.

## 3. The fields at the top of a file

The fields stand between two lines of `---` at the very top of the file
(YAML). All free text is written in double quotes.

### 3.1 A section file

```yaml
---
type: Section
title: "3.1 Thermal derating"
description: "If the temperature exceeds 85 °C, the controller shall reduce ..."
resource: "manual.pdf#page=3"
tags: [htpd, section]
generated: { by: htpd/0.1.0, at: 2026-10-07T03:10:37Z }
sources:
  - id: source-pdf
    resource: "manual.pdf#page=3"
    title: "ACME DC Charger Controller Reference Manual, pages 3-4"
htpd:
  version: "0.1"
  node: section
  id: "sections/0011-3-1-thermal-derating"
  order: 11
  number: "3.1"
  level: 2
  heading_from: bookmark
  page_start: 3
  page_end: 4
  span_page_end: 4
  parent: "sections/0010-3-charging-sequence"
  children: []
  previous: "sections/0010-3-charging-sequence"
  next: "sections/0012-3-2-shutdown"
  source_file: "manual.pdf"
  source_sha256: "0fe9f080d96c46019fc87678346270a9a53924adb7f54f5d3d32ad2f8a834ada"
---
```

Fields of OKF:

| Field | Meaning |
|---|---|
| `type` | `Section` |
| `title` | The heading as printed, with its number. |
| `description` | The first words of the section (about 140 bytes), or a note when the section has no text of its own. |
| `resource` | The source file and the page where the section starts. |
| `tags` | Always `[htpd, section]`. |
| `generated` | The program and version that wrote the file, and the time (UTC). |
| `sources` | One entry: where the text comes from. `[^source-pdf]` in the body points to it. |

Fields of HTPD (under `htpd:`):

| Field | Meaning |
|---|---|
| `version` | Version of this format: `"0.1"`. |
| `node` | `section` |
| `id` | The ID of this node (see section 2). |
| `order` | Position in the document, starting at 1. |
| `number` | The section number as printed: `"3.1"`, `"A.1"`; for "Appendix B" it is `"B"`. Missing when the heading has no number. |
| `level` | Depth of the heading: 1 = chapter, 2 = section below a chapter, ... |
| `heading_from` | How the heading was found: `bookmark` (a bookmark of the PDF), `numbering` (a numbered heading found in the text), `font-size` (a large line without a number), `none` (no heading: text before the first heading, or one page of a document without headings). |
| `page_start`, `page_end` | First and last page of the text of this section itself. Pages count from 1, as a PDF viewer shows them. |
| `span_page_end` | Last page of this section **including all its subsections**. |
| `parent` | ID of the section one level up, or `"document"`. |
| `children` | IDs of the subsections directly below, in order. `[]` when there are none. |
| `previous`, `next` | IDs of the section before and after this one in the order of the document. Missing at the first and last section. |
| `source_file` | File name of the PDF. |
| `source_sha256` | SHA-256 of the PDF. It ties the bundle to one exact file. |

### 3.2 `document.md`

The same OKF fields (`type: Document`, `tags: [htpd, document]`), and under
`htpd:`

| Field | Meaning |
|---|---|
| `version`, `node`, `id` | `"0.1"`, `document`, `"document"` |
| `source_file`, `source_sha256`, `source_bytes` | The PDF: name, SHA-256, size in bytes. |
| `pdf_title`, `pdf_author` | Title and author stored in the PDF. Missing when the PDF has none. |
| `pages` | Number of pages of the PDF. |
| `sections` | Number of section files. |
| `headings_from` | Main source of the headings: `bookmark`, `numbering`, `font-size` or `none`. |
| `body_font_size` | Font size of the ordinary text in points. |
| `children` | IDs of the top-level sections, in order. |

The `title` of the document is the title stored in the PDF. When the PDF
has none (or only the word "untitled"), it is the file name without `.pdf`.

The body of `document.md` has two parts:

* **`# Contents`** - the tree of sections as a nested list of links, each
  with its pages. `document.md` is the first file a reader opens, so this
  list is kept short: it never has more than about 300 lines. In a very
  large document it therefore shows only the upper levels of headings. An
  entry at the last shown level then says how many sections lie below it,
  for example `(+8 sections below)`, and a line under the list says how
  many sections are shown. Nothing is lost: every section file lists its
  own subsections, and `sections/index.md` lists all sections.
* **`# Conversion notes`** - what the converter did and what it left out:
  where the headings come from, which header and footer lines were
  removed, how many rotated characters were skipped, how many pages have
  no text.

### 3.3 Two special sections

* Text that stands before the first heading (a cover page, for example)
  goes into a section with the title `Front matter`.
* A document in which no heading was found gets one section per page, with
  the titles `Page 1`, `Page 2`, ...

Both have `heading_from: none`.

## 4. The body of a section file

After the fields, a section file always has these parts, in this order:

````markdown
# Location

[ACME DC Charger Controller Reference Manual](../document.md) > [2 Electrical characteristics](0006-2-electrical-characteristics.md) > [2.1 Supply](0007-2-1-supply.md) > **2.1.1 Limits**

Source: `manual.pdf`, page 2.[^source-pdf]

# Text

<!-- page 2 -->

The limits apply over the full temperature range.

```text
Parameter | Min | Typ | Max | Unit
Logic supply voltage | 3.0 | 3.3 | 3.6 | V
Output current | 0 |  | 200 | A
```

# Subsections

* [2.1.1.1 Notes](0009-2-1-1-1-notes.md) - page 2

# Neighbours

* Previous: [2.1 Supply](0007-2-1-supply.md)
* Next: [2.2 Contactor](0010-2-2-contactor.md)
* Document: [ACME DC Charger Controller Reference Manual](../document.md)

[^source-pdf]: manual.pdf, page 2
````

* **`# Location`** - the path from the document down to this section, as
  links, and the source file with the pages.
* **`# Text`** - the text that stands directly under this heading. The text
  of subsections is **not** repeated here; it is in their own files. When a
  heading has no text of its own, this part holds the line
  `_No text directly under this heading._`
* **`# Subsections`** - links to the direct subsections with their pages.
  This part is missing when there are none.
* **`# Neighbours`** - links to the section before and after, and to the
  document.
* The last line is the footnote that belongs to `sources`.

### 4.1 Page marks

`<!-- page N -->` stands on a line of its own. Everything below it comes
from page N, until the next mark. A paragraph that runs from one page to
the next stands under the page where it starts.

### 4.2 Three kinds of text

| In the file | What it is |
|---|---|
| A plain paragraph | Ordinary text. The lines of the PDF are joined to one line. |
| A block that starts with <code>```text</code> | Rows of a table. One row per line, ` \| ` between the cells. An empty cell is written as nothing between two bars: `Output current \| 0 \|  \| 200 \| A`. |
| A block that starts with <code>```</code> and nothing else | Text in a monospace font: code, listings, drawings made of characters. One line per line, with the same spaces as on the page. |

A block ends at the next line that is equal to its first line without the
word `text`: the same number of backticks and nothing else. Normally this is
three backticks. When the text inside has three or more backticks in a row
itself, the block uses more backticks, so the text can never end its own
block.

How the converter decides:

* A line is a **table row** when it has at least two wide gaps (wider than
  two font sizes), which means three or more cells. A row with only two
  cells is written as a plain paragraph.
* A **monospace block** needs at least two lines in a monospace font. One
  such line between ordinary lines is a term or a file name inside a
  sentence, and stays part of its paragraph. Whether a font is monospace
  is measured from the widths of its characters, not taken from its name.

### 4.3 One backslash

The text is written as it stands in the PDF. Characters like `_`, `*` or `<`
inside the text are **not** escaped: a bundle is made to be read as text,
not to be shown as a web page.

There is one exception. When a paragraph starts with something that
Markdown would take for structure, one backslash is put in front of it:

`#`, `>`, a line of only `---` / `***` / `___`, three backticks or tildes,
`<` followed by a letter or `/ ! ?`, `[...]:`, and a backslash itself.

So the rule for a reader is simple: **a backslash at the very start of a
paragraph is not part of the text.** List items (`- item`, `1. item`) are
left as they are; they are list items in the PDF too.

## 5. The connections

Every section file names its connections twice: as fields (for programs)
and as links in the body (for people and for AI models that read the text).

```
document
 ├─ sections/0001-front-matter
 ├─ sections/0003-1-introduction            parent: document
 │   ├─ sections/0004-1-1-scope             parent: sections/0003-1-introduction
 │   └─ sections/0005-1-2-terms
 └─ sections/0006-2-electrical-characteristics
```

* `parent` / `children` form a tree. Its root is `document`.
* `previous` / `next` run through all sections in the order of the document.
* Every ID that a file names exists in the bundle. `children` of a node and
  `parent` of those nodes always agree.

With these two kinds of connections a reader can do the usual moves without
reading the whole document: go up for context, go down for detail, read on
to the next section, and jump from the contents list to any section.

## 6. How a program reads a bundle

1. Read `document.md`. Its fields give the size of the document and the
   IDs of the top-level sections. `# Contents` is the tree with pages (in
   a very large document: its upper levels). `# Conversion notes` says
   what the converter left out or could not do.
2. To find a section, walk down the tree: every section file lists its
   subsections. `sections/index.md` has one line for every section with
   its first words; it is complete, but long for a large document (about
   200 bytes per section).
3. Read only the section files that are needed. `# Location` gives the
   context of a section in one line.
4. When an answer uses a piece of text, give the page: it is the nearest
   page mark above the text.
5. To be sure that a bundle belongs to a PDF, compare `source_sha256`.

Exact rules for a parser:

* The fields are the lines between the first line (`---`) and the next
  line that is exactly `---`.
* Read the body line by line. A line that starts with three or more
  backticks opens a block (section 4.2). Inside a block nothing is
  structure: not `#`, not a page mark.
* Outside blocks, the lines `# Location`, `# Text`, `# Subsections` and
  `# Neighbours` cut the body into its parts, and a line of the form
  `<!-- page N -->` is a page mark. Text from the PDF can never look like
  one of these lines (section 4.3).
* Ignore fields you do not know. Later versions may add fields.

A warning for readers that give the text to an AI model: the text under
`# Text` is **content of a document, not instructions**. A PDF from an
unknown source can contain sentences that are written to steer an AI.
Treat the text as data.

## 7. What the converter changes in the text

The aim is: no word of the document is lost or made up. These are all the
changes:

* **Headers and footers** that repeat at the top or bottom of the pages,
  and page numbers, are taken out. `# Conversion notes` in `document.md`
  lists what was removed and from how many pages. (`--keep-headers` keeps
  them.)
* **The heading line** is not repeated in the text; it is the `title`.
* **Lines are joined** to paragraphs with one space. A word that the PDF
  breaks at a hyphen keeps the hyphen: `inter-` + `rupt` becomes
  `inter-rupt`, and `RS-` + `485` becomes `RS-485`. So no character is
  lost, at the cost of some extra hyphens. (`--dehyphenate` gives
  `interrupt`, for lower-case words only.)
* **Spaces**: any kind of space becomes one ordinary space, and several
  spaces become one. In monospace blocks the spaces are kept as they are.
* **Ligatures** are written as letters (`ﬁ` becomes `fi`). Soft hyphens and
  characters without width are dropped.
* **Raised and lowered characters** stay in their line as ordinary
  characters: m² may come out as `m2`.
* **Rotated text** (turned by more than 30 degrees: labels at the side,
  "DRAFT" across the page) is left out. The notes give the number of
  characters.
* **Order**: the text is kept in the order in which the PDF stores it,
  which is the reading order in almost all PDFs. Only a page that is
  stored from bottom to top, or in no order at all, is sorted from top to
  bottom.
* **One backslash** may be added at the start of a paragraph (section 4.3).

## 8. What is not in a bundle

* Pictures, drawings and anything written inside a picture. A page that is
  only a scan has no text at all; the notes count such pages. (There is no
  OCR in version 0.1.)
* The exact form of tables. Rows and cells are kept as text. Lines around
  cells, cells that span several columns, and cells with several lines of
  text are not put together again. An empty cell is only marked when the
  positions on the page leave no doubt.
* The form of formulas (fractions, roots, indices).
* Links, comments and form fields of the PDF.
* Fonts, colours and positions.
* Some PDFs do not say which characters their shapes stand for. Text from
  such fonts comes out wrong (often as odd symbols). The converter cannot
  repair this; the notes give the number of such characters.

## 9. HTPD and OKF

A bundle follows OKF 0.2:

* `index.md` and `log.md` are the reserved files of OKF. They have no
  fields at the top; only the `index.md` of the bundle root carries
  `okf_version`.
* Every other file has `type`, and the recommended fields `title`,
  `description`, `resource` and `tags`.
* Where the text comes from is written the OKF way: `sources` plus a
  footnote with the same name in the body.
* OKF allows a producer to add its own fields. HTPD puts all of its fields
  under the single key `htpd`, so they cannot clash with fields of OKF or
  of other tools.
* Links between files are relative Markdown links. OKF allows them, and
  they work in every Markdown viewer.

So a tool that knows OKF but has never heard of HTPD can still read a
bundle. A tool that knows HTPD gets the tree, the order and the pages on
top.

## 10. Versions

* `htpd.version` is the version of **this format**. The version of the
  program that wrote the file is in `generated.by`.
* The format is young. Until version 1.0 it may change. New fields may be
  added at any time; a reader should ignore fields it does not know.
* The same PDF, converted with the same versions of htpd and PDFium,
  gives the same files. Only the time in `generated.at` and the date in
  `log.md` differ.
