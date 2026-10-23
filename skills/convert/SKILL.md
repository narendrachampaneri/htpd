---
name: convert
description: Convert a PDF into an HTPD file (.htpd) so questions about it can be answered with page numbers. Use when the user wants a PDF turned into HTPD or asks about a PDF in the project.
argument-hint: "[file.pdf] [--single]"
license: Apache-2.0 (see LICENSE)
---

# Converting a PDF into HTPD

The HTPD converter turns a PDF into an `.htpd` file: the document's own
sections, every paragraph, table and code block with its page, its figures
as pictures with their captions, and text inside pictures read by OCR. It
runs on this computer; the PDF is never uploaded.

## Convert

Run (the first run downloads the converter for Linux x64, about 4 MB, once):

```
bash "${CLAUDE_PLUGIN_ROOT}/skills/convert/scripts/convert.sh" "${CLAUDE_PLUGIN_DATA}" "<file>.pdf" --single
```

* `--single` writes one file, `<file>.htpd`, next to the PDF: the whole
  document with reading rules, for any AI chat. Leave it out for the folder
  form (`<file>.htpd/`, one file per section).
* `-o <path>` writes somewhere else; `--force` replaces an earlier result.
* `--ocr off` skips OCR; `--figures off` leaves the pictures out (a smaller
  file).

The converter prints what it found: sections, pages, tables, figures, and
the pages it could not read. Tell the user the result in one or two lines.
If it says pages hold no text and OCR is not installed, say that
`sudo apt install tesseract-ocr` lets it read scanned pages.

The ready converter is for Linux x64 and WSL. On another system the script
says so; the converter can then be built from source
(https://github.com/narendrachampaneri/htpd#build-from-source), or an `htpd`
program on the PATH (or in `$HTPD_CONVERTER`) is used.

## Then

Answer questions about the document with the `htpd` skill of this plugin
(or the `htpd` MCP tools): only from the file, with pages, or
"Not found in this file."

When the user gave arguments, they are: $ARGUMENTS
