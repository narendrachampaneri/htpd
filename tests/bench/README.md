# The HTPD benchmark

`run_bench.py` measures the converter on real PDFs: how fast it is, how big
the `.htpd` file is, and how much of the PDF's text, headings, table rows and
figures the file holds - page by page. Every change to the converter is
judged with it: run it before the change, run it after, compare.

It changes nothing in the converter. It needs `build/htpd`
(`make`), Python 3.10+ and `pdftotext` (poppler-utils, the reference for the
text of a page). No other library: the bookmarks of a PDF are read by a small
reader in the script itself.

## Quick start

```sh
make
python3 tests/bench/run_bench.py --tag before          # all documents that are available
# ... change the converter, make ...
python3 tests/bench/run_bench.py --tag after
python3 tests/bench/run_bench.py --compare build/bench/before.json build/bench/after.json
```

Each run writes `build/bench/<tag>.json` (all numbers), `<tag>.md` (the same
as tables) and `<tag>.details.json` (per document: the worst pages with the
text that is missing and extra, missing headings, missing figure captions).
`build/` is not committed.

`--compare` prints, for every document, each number that changed (time only
above the noise: 10 % and 0.05 s), the totals, and the limits of the work
rules:

| Limit | Rule |
|---|---|
| accuracy | no number of the "better is higher/lower" kind is worse, for any document |
| time | total conversion time at most 10 % above before |
| size | total `.htpd` size at most 5 % above before (unless the new data is the point of the change: then say how much and why) |

The exit code is 1 when a limit is broken, so a script can stop on it. A
number marked `WORSE` is not always a defect (a change that finds more
headings can add "extra" ones), but each one needs an explanation.

## The documents

| Set | Where | In git? |
|---|---|---|
| `private` | `private/bench/*.pdf` - your own PDFs | never: `private/` is git-ignored; nothing from it is committed, uploaded or put into a prompt |
| `eval` | `eval/docs/*.pdf` - the five public manuals of the evaluation (`eval/get_docs.sh`) | no (downloaded, checked by SHA-256) |
| `zoo` | `build/zoo/*.pdf` - the document zoo (`tests/zoo/make_zoo.py`; only the makers installed on the machine). Its Word, slides and `word-*` PDFs are tagged PDFs: `word-tagged` has no bookmarks, `word-flat` also sets its headings like body text, `word-hf` has a running header and a page number | no (made) |
| `data` | `tests/data/*.pdf` - the small test PDFs (`tagged*.pdf` are tagged by hand) | yes |

`tests/bench/make_long_tagged.py <folder>` writes two tagged PDFs of 320 pages without bookmarks (headings set
like body text, and headings set bold and larger) to measure what the tag gate of `--tags auto` costs and gives:
`run_bench.py <folder>` (see "Tagged PDFs" in [`docs/CONVERTER.md`](../../docs/CONVERTER.md)).

The default (`--set auto`) takes `private` when it holds PDFs, then every
public set that is present. `--set public` takes only `data,eval,zoo`;
`--set eval,zoo` any list; a folder as argument takes that folder.
`--fetch` downloads the eval PDFs and makes the zoo when they are missing.
`--only REGEX` runs the documents whose `set/name` matches.

`tests/bench/baseline-public.json` and `.md` are the numbers of the public set
at the time the bench was made (eval and `data`; the zoo of that machine: Word,
slides and Chromium-printed web pages). The accuracy numbers do not depend
on the machine; the times do. Always run a fresh "before" on your own machine.

## What is measured

All numbers are per document and for all documents together.

**Speed and size.** `time s` is the wall time of `htpd convert --single`,
the fastest of `--repeat` runs (default 3); `cpu s` the CPU time of that run.
`size B` is the size of the `.htpd` file; `figures B` the part of it from the
line `# Figures` to the end (the base64 pictures).
The conversions run one after the other so they do not disturb each
other; the comparison with the PDFs runs in parallel (`--jobs`).

**Text.** For each page the words of `pdftotext -layout` are compared with
the words under every `<!-- page N -->` of the file (all sections that hold
that page, in file order). A word is letters and digits; punctuation does not
count (so `inter-` + `rupt` equals `inter-rupt`), and symbols like `°` `±`
`→` do.

| Number | Meaning | Better |
|---|---|---|
| `words in order` | share of the PDF's words found on their page in the same order (longest common subsequence) | higher |
| `words on page` | share found on their page, in any order | higher |
| `pages ok` of `pages with text` | pages with at least 99 % of their words in order and at most 5 % extra words | higher |
| `lost words` | words of the PDF that are on no page of the file | lower |
| `moved words` | words missing on their page that stand on another page (a wrong page mark) | lower |
| `invented words` | words of the file that are in no page of the PDF | lower |
| `missing pieces`, `extra pieces` | runs of consecutive missing / extra words (a cell, a line); the details file shows examples | lower |
| `headers kept` | running headers/footers that the converter listed as removed but that are still in the text of a page | lower |

Running headers and footers that the converter lists under "Removed ... running header/footer lines" in
its conversion notes are expected to be missing: they are taken out of the PDF's page before the
comparison. Where it names only one example of a header that changes (a chapter title with the page),
the lines in the same place on at least three pages that share two words with the example count too.
A one- or two-word pattern (a page number) must be a whole line. Headings of the file that are not in
the text of their page (a running title, "Page 3-4") are labels, not text: they are not counted as
invented words.

**Headings.** The bookmarks of the PDF, or - when it has none - a table of contents printed in its first pages
(lines that end in a page number after dots or wide space; at least five), against `# Contents` of the file.
Titles are compared without their number and without punctuation.

| Number | Meaning | Better |
|---|---|---|
| `headings found` of `headings in PDF` | titles of the PDF that are in `# Contents` | higher |
| `headings missing` | the rest | lower |
| `headings extra` | titles in `# Contents` that are not in the PDF's list (numbered headings found in the text, "Front matter", parts cut by pages) | no verdict |

A document with neither bookmarks nor a printed contents has `-` here.

**Tables.** The PDF side: lines of `pdftotext -layout` with three or more cells (gaps of two spaces or more)
that have another such line within two lines - a table, not a heading set in three pieces.
The file side: lines with ` | ` in a `text` block.

| Number | Meaning | Better |
|---|---|---|
| `rows found` of `rows in PDF` | PDF rows whose words (in order) are inside one row of the file on the same page; a row with a joined wrapped cell still counts | higher |
| `rows in file` | lines with ` | ` | no verdict |
| `stray rows` | rows of the file (three or more cells) that hold no PDF row | lower |

**Figures.** The PDF side: lines that start with `Figure N.`, `Figure N:`, `Fig. N -` or are only `Figure N`
(not a line of a list of figures that ends in a page number); each (page, label) once.
The file side: the `![F` image lines.

| Number | Meaning | Better |
|---|---|---|
| `figures found` of `captions in PDF` | captions with an image line of the same label on the same page | higher |
| `figures in file` | `![F` lines (pictures without a caption count) | no verdict |
| `duplicate figures` | a second image line for the same caption on a page | lower |
| `figures no image` | an image line `#F<n>` with no picture in `# Figures` | lower |
| `notes = figures` | 1 when the number of figures in `# Conversion notes` equals the number of `![F` lines | higher |

## How far to trust the numbers

The reference is `pdftotext`, not the truth. Be aware of what it gets wrong, so you do not chase it:

* **Reading order.** `pdftotext -layout` reads across columns and tables by line. HTPD keeps the order
  the PDF stores (FORMAT.md 4.2). On two-column pages and in tables, words are "in order" for one and not for
  the other, so `words in order` is below `words on page`. This difference is the same before and after; compare.
* **Wrapped names.** A long name broken over two lines (`_TLSGD_ADR_` / `PREL21`) is two words for
  `pdftotext` and one for the converter: one missing and one extra piece.
* **Rotated text, scans, other scripts.** `pdftotext` drops or garbles sideways pages and some Indic
  scripts. A PDF without a text layer (a scan) has no reference: its text, table and figure numbers are `-`
  (it still counts for time, size and headings from bookmarks).
* **Table rows and captions** are found by rules on the layout text; they miss tables set with single spaces and
  count lines of a layout that only look like rows. The numbers are good for before/after, not as absolute
  measures of the converter. The converter's own measure with a known truth is `tests/zoo/run_zoo.py`.
* **Time.** The converter uses all cores. Run on a quiet machine; the single documents under 1 s move by 10 % or
  more between runs. Use `--repeat 5` to decide a close case, and look at the totals, not at one small document.

## Your PDFs

Put them into `private/bench/` (git-ignored). Names and contents never leave the machine; do not paste
a report of them into a commit message or an issue. The reports stay in `build/bench/`.
If `private/bench/` is empty the tool still runs on the public sets.

## Other options

```
--converter PATH         another build (for example one built with -fsanitize=address,undefined: a crash or
                         a finding makes that document fail and the run exit with 1)
--converter-args '...'   extra options for `htpd convert`, for example '--ocr off', '--headings text' or '--tags on'
                         (the structure tags of a tagged PDF are read by default only when the headings found without
                         them are poor)
--repeat N               conversions per document, the fastest counts (default 3)
--jobs N                 parallel comparisons (default 4); the conversions are always sequential
--tag NAME --out DIR     names of the report files (default `bench`) and their folder (`build/bench`)
--compare A.json B.json  compare two reports (`--md FILE` also saves the comparison)
```

## Test of the tool

```sh
python3 -m unittest discover -s tests/bench -p 'test_bench.py'
```

Tests the words, the alignment, the removal of headers, the reading of a `.htpd` file and of PDF bookmarks, the
rules for rows and captions, the comparison and the limits - and, with `build/htpd` and `pdftotext` present, one
conversion of `tests/data/sample.pdf` end to end.
