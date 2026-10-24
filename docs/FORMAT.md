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
  figures/                    the pictures of the figures, when there are any (section 4.5)
    F0001.png
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
| `heading_from` | How the heading was found: `bookmark` (a bookmark of the PDF), `numbering` (a numbered heading found in the text), `font-size` (a large line without a number), `contents` (an entry of a table of contents printed in the document, found as a title in the text; its number comes from the contents, also where the text shows the title without it), `running-title` (a document with few headings: the running header of the pages changes here, and its text is the title), `pages` (a part of a long stretch of text, cut by pages: see 3.3), `tags` (a heading of the PDF's own structure tree, H or H1-H6, with the level the tags give: see 4.6), `none` (no heading: text before the first heading, or one page of a document without headings). For `running-title` and `pages` the title is not in the text of the page. |
| `page_start`, `page_end` | First and last page of the text of this section itself. Pages count from 1, as a PDF viewer shows them. |
| `span_page_end` | Last page of this section **including all its subsections**. |
| `ocr_pages` | Only when some of its text was read from pictures by OCR (section 8): the pages of its own text that hold such text, e.g. `[5, 6]`. A word on these pages may be misread. |
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
| `headings_from` | Main source of the headings: `bookmark`, `numbering`, `font-size`, `contents`, `tags` or `none`. |
| `body_font_size` | Font size of the ordinary text in points. |
| `children` | IDs of the top-level sections, in order. |

The `title` of the document is the title stored in the PDF. When the PDF
has none (or only "untitled" or "(anonymous)"), it is the text of the `Title`
element of the PDF's tags (when the tags were read, section 4.6), then the
title printed on the first page - its line or lines in the largest font,
clearly larger than the text - and when there is none, the file name without
`.pdf`.

The body of `document.md` has two parts:

* **`# Contents`** - the tree of sections as a nested list of links, each
  with its pages. `document.md` is the first file a reader opens, so this
  list is kept short: it never has more than about 300 lines. In a very
  large document it therefore shows only the upper levels of headings. An
  entry at the last shown level then says how many sections lie below it,
  for example `(+8 sections below)`, and a line under the list says how
  many sections are shown. Nothing is lost: every section file lists its
  own subsections, and `sections/index.md` lists all sections.
  Where the title was not written by the author (`heading_from: none` or
  `pages`: "Front matter", "Page 5", "Report - pages 12-13"), the entry
  ends with the first words of its text, so the list shows what the part
  is about: `- starts: `Byte ordering of the core.``. The same holds for
  the contents of the single file.
* **`# Conversion notes`** - what the converter did and what it left out:
  where the headings come from, which program made the PDF and how it was
  read (`Made by: ..., read as LibreOffice.`), the styles of the text and
  their roles (`heading 2: Arial Bold 12 pt, 84 lines`; one line for body
  text, each heading level, captions, table text, code, header/footer,
  footnotes), which header and footer lines were removed, how many rotated
  characters were skipped, how many pages have no text. These lines are for
  a person or an AI to read; no field of the format depends on them.

### 3.3 Two special sections

* Text that stands before the first heading (a cover page, for example)
  goes into a section with the title `Front matter`. It is never the parent
  of a section: a `5.2` heading after it without a `5` stays at the top.
* A document in which no heading was found gets one section per page, with
  the titles `Page 1`, `Page 2`, ...

Both have `heading_from: none`.

A long stretch of text gets parts, so that no section is too long for an AI
to point into: the own text of a section that runs over more than 3 pages
(8 pages in a document with headings enough, that is with at least 3
headings and one per 15 pages) is cut into parts of about 2 pages. A part is
a subsection one level down, with `heading_from: pages` and the title
`<title of the section> - pages 12-13`; in a document with few headings the
topics its running header gives those pages follow
(`... - pages 3-4: Section 3: Methods; Section 4: Results`). A part starts
where a sentence ends, or at a page that starts with a larger line (a slide
title), never next to a table row (also a row stored cell by cell, whose
cells stand on one line of the page): no paragraph is broken and a table
keeps its header row. In the contents the pages of a part are not written
twice.

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

# Links

* `limits` → [3.1 Thermal derating](0011-3-1-thermal-derating.md) - page 3

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
* **`# Links`** - the links of the PDF from this text to other sections
  (section 4.4). This part is missing when there are none.
* **`# Subsections`** - links to the direct subsections with their pages.
  This part is missing when there are none.
* **`# Neighbours`** - links to the section before and after, and to the
  document.
* The last line is the footnote that belongs to `sources`.

### 4.1 Page marks

`<!-- page N -->` stands on a line of its own. Everything below it comes
from page N, until the next mark. A paragraph of text that runs from one
page to the next is cut where the next page begins, and the mark of that
page stands between its two parts (a table or a listing stands under the
page where it starts).

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
  cells is written as a plain paragraph. (A table that the tags of the PDF
  describe is the exception: section 4.6.) A line of justified text - every
  space wide, all about equally wide, mostly ordinary words - is not a row.
* **A table drawn with lines** (the page draws a grid of horizontal and vertical rules, or thin bars as
  borders): the cells are the boxes the lines make. A row has one entry per column of the grid; a cell
  that spans columns or rows is written in the cell it starts in with the same marks as for tags
  (section 4.6: `[>]` for one more column, `[>>]` for two, `[=]` for one more row), and the cells it
  covers are empty. This needs a grid of at least two columns and two rows, in which every word lies
  inside one cell; it also gives a table of two columns. A chart with grid lines is no table. Values stacked
  in the cells of a row ("Level 1..4 falling | 2.62 | 2.70 | 2.78": a number on every line of two or more
  cells) are one row per line. The conversion notes say how many tables were read this way.
* **A row whose cells wrap, in a table set apart**: where the columns are certain and the rows are set
  apart (the space between two rows is wider than between two lines of one cell), a line with nothing in
  the first column, none of whose pieces is a number, is the rest of the cells of the row next to it. It goes
  to the row above; when it stands closer to the row below (a table whose cells are set in the middle of tall
  rows: the first line of the description stands above the line with the offset), it goes in front of the
  cells of the row below. A number under a number is a row of its own. Columns are also found through such
  a line: one or two lines between rows that keep to the columns do not end the table.
* **A cell with several lines** stays one cell: a line right under a row
  that starts in a column after the first, and whose pieces each stay in
  one column, is the rest of a cell and is joined to it with one space.
* **A table stored cell by cell** (the name, then every line of a long
  description, then the short cells beside it) is put together by the
  positions on the page: after a header row with three or more cells, the
  lines that stay inside the columns belong to the table. A header printed
  in pieces right above the first row ("Gross" over "Revenue", each
  heading on its own) is the header too, when its pieces stand in three or
  more blocks side by side and none runs across another (a caption does).
  Text that crosses the columns ends the table, and so does a figure or a
  heading set alone between the rows. A line in a monospace font that fits
  the columns stays in the table (a name in a cell is no listing). A header
  in pieces may also stand beside the first row line, on its line and the
  line below ("100" over "LQFP" beside "ALT0 ALT1 ..."): then that line is
  part of the header. A cell set so tight against the next
  column that no wide gap shows is split where that column starts, when a
  space wider than one between words lies there; a cell set in the middle
  of its column belongs to it. Pieces of one cell are joined with one space,
  except where a name is broken inside itself because the column is too
  narrow - only when that is certain: the first part ends with `_` or `/`
  (`UART2_` + `RX`), the next part starts with `_` or is a lone `)`
  (`field` + `_2`), a short number follows a name with digits or `_`
  (`LCD_P2` + `4` = `LCD_P24`), both parts hold `_`, or a rest of at most
  three letters follows one word as wide as its column (`DEFAU` + `LT`).
* **The rows of such a table** are bands across the page: pieces in
  different columns whose baselines lie less than a line apart form one
  row (a cell in the middle of a row whose other cells wrap). A band with
  text in only one column, or none in the first, and no line with two
  cells, is the rest of a cell that wraps: it joins the row above, or the
  row below when that one is clearly nearer, never one more than two lines
  away. A header cell printed over two columns of the rows ("GAAP Code Q1"
  over the codes and the values) is split at its space between them.
* **Sub-rows**: values stacked in one cell ("406" over "386" over "792")
  are never a cell that wraps. When every stacked column has the same
  number of lines and a column of labels beside them ends with as many list
  items set at one place ("• 25 °C", "• -40 °C", "• 105 °C"), each value
  line becomes the row of its label, in order; the lines above the labels
  are the title row. When the counts differ, the row stays as it is.
* When the columns are not certain, the lines are written as they stand,
  one per line, inside the block. No text is moved into a cell it does
  not stand in, and none is left out.
* **A table over a page break.** By default a table that goes on over a page break is one block per page,
  each under the mark of its page. With `--table-pages join` the parts are one block, and the page changes
  where a line `<!-- page N -->` stands between two rows (a line of its own inside the block; it is not a
  row: the rows after it are on page N, and the page of the block is the page of its first row). The parts
  are joined only when they stand on consecutive pages, have the same number of columns at the same left
  edge, and either the next part begins with the same first row (a header printed again - it is kept as a
  row, so no text is lost) or the table ends at the bottom of its page and goes on at the top of the next
  (not more than three lines above it). A table under another heading is not joined. The conversion notes
  say how many tables were joined, and the rules for reading the file get an item 14.
* A **list of terms** - a short bold term and right below it its
  description, indented - gives one paragraph per term with its description.
* **Monospace text in a table is table text**, not code: four lines or more one under the other, each with
  two or more sure column gaps (more than two font sizes), whose first two gaps lie on stretches of the page
  they have in common, are the rows of a table (a name in a cell, or a whole table set in a monospace
  font). A listing, with at most one gap in its lines, stays a monospace block.
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

### 4.4 Links of the PDF

Many PDFs link a word of the text to another place of the document: a
register name to its definition, "see section 3.6" to that section. These
links are kept, without changing the text, as a list after it:

```
* `dmcontrol` → [3.14.2 Debug Module Control (dmcontrol, at 0x10)](0044-3-14-2-debug-module-control-dmcontrol-at-0x10.md) - page 35
```

One line per link: the words of the link as inline code, `→`, a Markdown
link to the section it leads to, and the page of the place it leads to. How the converter decides:

* A link stands in the section whose text holds the first line under its box.
* It leads to the section of the line closest below the place it jumps to,
  so a heading right below that place wins. A link to a page as a whole
  leads to the section of the first line of that page.
* Links to the same section, links whose words hold no letter (page numbers
  in a list of contents), links to the web and to other files are left out. The same words leading to the same section are listed once, in the
  order of the text.

### 4.5 Figures

A figure - a picture of the PDF (a photo, a screenshot, a scanned diagram)
or a drawing made of lines and shapes (a block diagram, a timing diagram, a
chart, a register layout) - is stored as a picture, at its place in the
text:

```
<!-- figure F12 -->
![F12: Figure 3-2. Clock tree](../figures/F0012.png)
```

* `F<n>` numbers the figures from 1 in the order of the document. The
  picture is a PNG file in `figures/`, named `F<nnnn>.png` (at least four
  digits). The same picture on many pages (a PDF stores it once) is stored
  once: the image line of a later figure points at the picture of the
  first one (`../figures/F0003.png`, or `#F3` in the single file). It shows the region of the page the figure covers, at 120 dpi by
  default (`--figure-dpi`), in grey when it has no colour.
* The words of the image line are the caption: the line "Figure N ..." next
  to the figure (with the line below it when it holds only the number, and
  also further below when only short labels stand between), or a short
  title centred just below it; without one, what
  it is and its page (`picture on page 16`, `drawing on page 62`). They are
  written like the words of a Markdown link (`[`, `]` and `\` get a
  backslash). The caption also stays in the text where the PDF has it.
* Words inside a figure stay in the text: the words of a drawing as the PDF
  has them, the words of a picture when OCR read them (section 8); they
  follow the image line.
* What is a figure: a picture larger than an icon (about 2 x 1.3 cm), not
  in the page margin and not covering almost the whole page; lines and
  shapes that form one region with a caption "Figure N", or with ten or more
  shapes and little text. A region with a caption "Table N" nearer than any
  "Figure N", or table rows, is a table (section 4.2); so is one without a
  caption with rules across its whole width at three or more heights (with
  a caption those are the grid of a chart); a frame around lines of code is
  a listing. A background or frame over the whole page is never part of a
  region. Each caption belongs to one figure only: where two regions stand
  next to the same caption, it goes to the one on the side where the other
  captions of the page stand (below the figure when they say nothing),
  else to the nearer.
* `--figures off` stores no figures; the text is then the same as without
  this section.

### 4.6 Structure from the tags of the PDF

A tagged PDF (Word, LibreOffice, InDesign and others write them) carries a
structure tree: it says which text is a heading of which level, which is a
cell of which table, a list item, a figure with its caption, or page
furniture. The converter reads that tree (the notes say "Structure from PDF
tags" with the counts) and uses it **only where it can be trusted**: where
every piece of text an element names is found on its page and the page agrees
with the tree. Everywhere else it reads the page as it reads a PDF without
tags (the text is then the same as with `--tags off`), and the notes count the
elements it left to the ordinary rules.

| Tag | What it gives |
|---|---|
| `H`, `H1`-`H6` | A heading with the level of the tag (`H`: one plus the number of `Sect` elements around it), `heading_from: tags`. Used when the PDF has no usable bookmarks, and the tags name at least as many headings as a printed table of contents or numbered headings in the text show. With bookmarks, a tag heading that lies deeper than the bookmark above it (a bookmark that is a tag heading itself, with a smaller tag level) is added one level below it, but only where the style of its text is a heading style of the document too (`heading_from: tags`). The lines of a heading are its title and are not repeated in the text. A heading tag on more than 200 characters, or on text that is not in one piece, is no heading. |
| `Table`, `TR`, `TH`, `TD` | The rows of the table with every cell exactly, also a cell of several lines (joined with one space). The block is a table (section 4.2) also when it has only two columns. An empty cell is written as nothing between two bars. A cell that spans columns or rows is written in the cell it starts in, with a mark after its text: `[>]` for one more column, `[>>]` for two more, `[=]` for one more row (a cell can have both). The cells it covers are written empty, so every row has the same number of cells. Where the tags leave out an empty cell, the cells are put into their columns by their positions on the page; where that is not certain, the table is read by the ordinary rules. A table is only taken when its cells lie in the order of the page, nothing else is stored between them, and no text lies on it that the table does not name. |
| `L`, `LI`, `Lbl`, `LBody` | Every list item is a paragraph of its own, also when its label is no list mark (`A.`, `§1`); the lines of an item stay together. |
| `Figure`, `Caption` | A figure (section 4.5) with the text of its `Caption` as caption (the element right before or after it, or inside it), or its alternate text when it has no caption. A figure the tags name and the ordinary rules do not find is stored too, unless it is an icon, a frame round the page or a logo in the page margin. |
| `Artifact` | Header, footer and page number lines (text marked as an artifact, in the top or bottom fifth of the page) are taken out and listed in the notes like any other running header or footer (section 7). PDFium does not tell the type of an artifact, so an artifact of any type in the page margins is taken. |

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
* Outside blocks, the lines `# Location`, `# Text`, `# Links`,
  `# Subsections` and `# Neighbours` cut the body into its parts, and a line of the form
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
  and page numbers, are taken out. (In a tagged PDF also lines the PDF
  marks as artifacts at the page edge, repeating or not: section 4.6.) `# Conversion notes` in `document.md`
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
* **Raised and lowered characters** stay in their line. A raised or
  lowered group of digits (with signs, brackets, or a raised n or i) is
  written in Unicode superscript or subscript characters, as it stands in
  the PDF: `2³¹`, `m²`, `s⁻¹`, `x₀`. Any other raised or lowered text (a ™,
  the "th" of "4th", the "DD" of V<sub>DD</sub>) is written as ordinary
  characters, and so is a sign alone. A reader that searches should treat
  `²` and `2` alike (Unicode NFKC).
* **Pages whose text runs sideways or upside down** (a wide table printed
  sideways, a page turned by /Rotate) are turned first, so that their main
  text is read upright, in order.
* **Rotated text** that runs another way than the main text of its page (turned
  by more than 30 degrees: labels at the side, "DRAFT" across the page) is
  left out. The notes give the number of characters.
* **Order**: the text is kept in the order in which the PDF stores it,
  which is the reading order in almost all PDFs. Only a page that is
  stored from bottom to top, or in no order at all, is sorted from top to
  bottom.
* **One backslash** may be added at the start of a paragraph (section 4.3).

## 8. What is not in a bundle

* Pictures that are not figures: icons, logos in the page margin, the
  background of a page. Figures are stored as pictures (section 4.5).
* Text that exists only as a picture, when OCR is not available. Words
  inside a picture - a scanned page, a screenshot, a slide stored as a
  picture, the labels of a figure stored as an image - are read by OCR when
  the free Tesseract program is
  installed on the computer that converts: they stand in the text where the
  picture stands, and the section lists those pages as `ocr_pages`. OCR text
  may hold misread words; a word Tesseract was not sure of is left out, not
  guessed. Text of the PDF itself always wins: what OCR reads on top of it is
  not added again. Without Tesseract a scanned page has no text at all, and
  the notes say how many pages could not be read.
* The exact form of tables. Rows and cells are kept as text. Lines around
  cells are not shown, and a cell that spans several columns is only shown
  where the tags of the PDF describe the table (section 4.6); a cell with
  several lines is joined when the columns are certain (section 4.2), and
  paragraph breaks inside a cell become one space. An empty cell is only
  marked when the positions on the page leave no doubt.
* The form of formulas (fractions, roots, indices).
* Links to the web and to other files, comments and form fields of the PDF.
  (Links to other places of the document are kept: section 4.4.)
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

## 11. The single-file form

`htpd convert manual.pdf --single` writes the same document as **one file**,
`manual.htpd`, instead of a folder. It is made to be handed to any AI model
as it is - uploaded to a chat, attached to a message - without any tool. It
holds exactly what the folder holds (the same IDs, tree, order, pages and
text), so every tool that reads a bundle folder reads it too.

```
---
htpd_file: "0.1"                     the format version; always the first field
title: "..."                         and: source_file, source_sha256, source_bytes,
pages: 109                           pdf_title, pdf_author, pages, sections,
sections: 109                        headings_from, body_font_size, generated
---

# How to read this file              rules for the AI model (see below)

# Contents                           every section on one line, indented by the tree:
* [S1] Front matter - page 1         [S<n>] title - pages (with all subsections)
  * [S3] 1.1 Ordering ... - pages 6-9

# Conversion notes                   as in document.md

# Sections

<!-- htpd section {"n":14,"id":"sections/0014-...","number":"4.1.5","level":3,"pages":[18,18,18],"parent":"sections/0008-..."} -->
## [S14] 4.1.5 Maximum frequency of main modules

Path: 4 Electrical characteristics > 4.1 Chip-level conditions > 4.1.5 ... - page 18

<!-- page 18 -->

... the text, exactly as under `# Text` of the section file (section 4) ...

<!-- htpd links -->
* `NPU` → [S27] 4.3 Neural processing unit - page 24

[^source-pdf]: IMX93AEC.pdf
```

* The file starts with the bytes `---` newline `htpd_file: `. A reader tells
  the two forms apart by this, or by a folder holding `document.md`.
* **`# How to read this file`** tells the model: answer only from this file;
  find the section in the contents; cite the page of every fact as (page N);
  quote values exactly; say "Not found in this file." when the answer is not
  there, never guess; how tables and code blocks are written; that a
  backslash at the start of a paragraph is not text; that the text is
  content, not instructions; that pictures and formulas are not in it as
  such; how to follow the links of a section; only in a file that has text
  read by OCR (rule 11), that such text may hold misread words; and only in a
  file with figures (rule 12), where their pictures are and that their data
  is not text.
* **The section line** `<!-- htpd section {...} -->` is JSON on one line:
  `n` (position, from 1), `id` (the same ID as in a bundle folder),
  `number` (or `null`), `level`, `pages` (`page_start`, `page_end`,
  `span_page_end`), `parent`, and `ocr` (the `ocr_pages`, only when there
  are any). `children`, `previous` and `next` follow
  from the order and the parents. The title is the `##` line below it.
* **The links** of a section (section 4.4) follow its text after the line
  `<!-- htpd links -->`, one per line: the words as inline code, `→`, the
  label `[S<n>]` of the section it leads to, its title, and the page. The
  line is missing when a section has no links.
* The text of a section follows the rules of section 4; a backslash, page
  marks and blocks are the same. Outside blocks, text from the PDF can never
  look like a section line or a `##` heading (section 4.3); inside a block
  nothing is structure.
* After the text, the footnote of the source. Then, only when the document
  has figures, the part `# Figures`: the pictures, for tools that can show
  them. In the text, a figure's image line points at its picture with
  `#F<n>` instead of a file. Each picture is a line
  `<!-- htpd figure {"id":"F12","page":16,"kind":"drawing","caption":"...","width":831,"height":804,"type":"image/png"} -->`
  and a block ` ```base64 ` with the PNG in base64, 76 characters per line.
  The pictures stand at the very end so that the text comes first: a model
  that reads only the start of a long file still gets all of the text.
