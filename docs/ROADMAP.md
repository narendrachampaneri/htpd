# The converter roadmap

This file is the plan of the work on the converter and where it stands. A new
work session reads it, does **the first part marked "open"**, and when the part
is finished marks it "done" and adds one line to the log at the end. A session
starts from this file and the repository, not from memory or chat history.

## The goal

The product is the `.htpd` file. An AI that reads only this file must get the
same facts, values and pages as a careful reader of the PDF.

Optimize for **the amount of accurate, useful information an AI can retrieve
per byte of file and per second of conversion time**. Do not optimize for file
size alone, nor for speed alone. Correct text, pages, structure, tables and
figures come first.

## Rules for every part

**Limits.** These are measured before and after the part, on the same machine:

1. **Accuracy is never worse** on any measure, for any PDF of the public bench.
   The measures are text coverage, page correctness, headings, table rows, the
   table ground truth and the layout ground truth, and figures. AGENTS.md
   rule 7 applies: never lose text, never invent text, and when unsure keep the
   text as it stands. Accuracy has no trade-off.
2. **Speed.** Total conversion time of the bench is at most 10 % slower by
   default.
3. **Size.** `.htpd` files are at most 5 % larger by default.
4. **Hygiene.** `make test`, the Python tests and `tests/bench/test_bench.py`
   pass. Neither build (make, CMake) gives a warning. One run under
   AddressSanitizer/UBSan over the public bench is clean.

**Trade-offs** (speed and size only, never accuracy). Measure the cost
(seconds, bytes) and the gain (which numbers improve, by how much). Within the
limits, a clear gain may be on by default. Beyond the limits, the feature is
optional and off by default: give it an option and a line in
`docs/CONVERTER.md`. A feature with no measurable gain is not kept.

**Scope and sources:**

- One part per session. Don't start the next part, rewrite modules that work,
  or change finished steps unless the part says so.
- Use only public material: the bench sets and test PDFs you build. The
  owner's PDFs are not available and are not asked for.
- Every new rule gets a test in `tests/test_main.cpp` that fails without the
  rule.
- Code from other projects only under MIT, BSD or Apache-2.0 (name it in
  `NOTICE`). GPL/AGPL projects are used for ideas only. No new dependency.
  Everything in the converter is C++23.
- What the converter writes goes in `docs/FORMAT.md`. Behaviour and options go
  in `docs/CONVERTER.md`.
- No release and no version change; the owner publishes releases.

## The check, step by step

This is the same check for every part, kept small. Run long commands quietly
and read only their summaries.

```sh
# 1. once, at the start
./scripts/get_pdfium.sh                      # if build/ has no libpdfium.so
make -j"$(nproc)" && make test
mkdir -p build/before && cp build/htpd build/libpdfium.so build/before/
python3 tests/bench/run_bench.py --fetch --set public --tag before

# 2. while working: tests, your own cases, and which documents change at all
make test
python3 tests/bench/same_output.py --a build/before/htpd --set public

# 3. once, at the end
make test
PYTHONPATH=reader:. python3 -m unittest discover -s reader/tests -t . -q
PYTHONPATH=reader:. python3 -m unittest discover -s eval/tests -t . -q
PYTHONPATH=reader:. python3 -m unittest discover -s model/tests -t . -q
python3 tests/bench/test_bench.py
python3 tests/bench/run_bench.py --set public --tag after
python3 tests/bench/run_bench.py --compare build/bench/before.json build/bench/after.json
python3 tests/bench/same_output.py --a build/before/htpd --set public
```

**A document whose file did not change needs no further check.**
`same_output.py` lists the files that changed, and the expensive checks only
run on those:

- **Eval.** Run it when an eval manual changed. Use
  `PYTHONPATH=reader:. python3 -m eval run htpd-rerank`, first with `--split
  test` and then with `--split tune`. Run it once with `build/before/htpd`
  copied to `build/htpd`, and once with the new build. Explain each question
  that changed. Never commit eval results.
- **Zoo.** Run it when a zoo document changed:
  `PYTHONPATH=reader python3 tests/zoo/run_zoo.py build/zoo` with
  `--converter build/before/htpd`, then with the new build.
- **Stress and sanitizer.** Run these once, at the end, whatever changed:
  `tests/stress/run.py`, and `run_bench.py --converter <an ASan/UBSan build>
  --repeat 1`.

**Reports are short.** Use at most 40 lines:

- what changed;
- the numbers before → after that changed (one table);
- which bench documents changed and why;
- the trade-off decision;
- what is not done;
- the branch and commits.

Don't narrate steps while working.

**Commits.** Make small commits, one per finished and tested step, under the
owner's authorship (`git config` from the last commit). Push to `main` when
everything passes. If the session may only push to its own branch, push there,
open a pull request to `main`, and say so in the report.

## Done

| Part | What it gave | Commit |
|---|---|---|
| S0 | Bench: per-PDF time, size, text in order and on its page, headings, rows, figures; `--compare`; stored baseline | ffe0f0c |
| S1 | Tagged PDFs: headings, lists and tables from the structure tree (`--tags auto`) | ddabcea |
| S2 | Style sheet and producer habits; headings by style; the tag gate | d186699 |
| S3 | Tables: ruled (lattice) and unruled (stream) tables, continuations; table ground truth (29 cases) 2298 → 2379 of 2384 cells | ba2dc56 |
| S4 | Reading order (XY-cut, `src/order.cpp`): columns, side boxes, pull quotes, captions, footnotes, margin notes; layout ground truth 10 371 → 12 300 of 12 302 words in order; pages in good order unchanged byte for byte | 67d3cd5 |

## Open parts, in order

### S5 - Text accuracy: what S3 and S4 left (open)

1. **A word is never cut into two cells.** A column edge worked out from the
   gaps must not fall inside a word (for example, `Sym | bol`). Move it to the
   nearest real gap, or keep the line as text. Find the cases in the public
   bench and add them to `make_tables.py`.
2. **Mixed sizes in one stored line.** When one stored line holds two columns
   set in different font sizes, each part keeps the size of its own glyphs, so
   no paragraph is split.
3. **Header rows of unruled tables.** The header row is found from its
   difference to the rows below: style, a header with no numbers while the
   rows below have them, header cells over two lines joined. Only when sure.
4. **A table that continues from the bottom of one column to the top of the
   next** on the same page is one table. Joining over pages stays as it is
   (`--table-pages join`, off) until S8.
5. **A box that touches the text wrapped around it** (no free strip) is read
   after the paragraph it interrupts.

*Measure:* the table and layout ground truth (add cases for 1-5), the bench,
and the zoo `latex-2col` file if LaTeX is installed.

### S6 - Links and terms (the OKF idea: the file says how its parts relate) (open)

1. **Printed references.** Find references in the text that have no PDF link,
   such as "see Section 4.2", "Table 2-3", "Figure 5", "Chapter 7" and "on page
   45". Write them into the section's existing `<!-- htpd links -->` list with
   the section, or figure, they lead to. Do this only when the target is
   certain and unique.
2. **Table names.** A table caption ("Table 2-3 Exception status and control
   bits") names its table, so a reference to it leads to the right section and
   page.
3. **Terms.** Collect definitions such as "Memory Protection Unit (MPU)" and
   tables of abbreviations into one compact list: term, meaning, section. It is
   on by default only if its gain and size fit the limits.
4. **Free truth.** Many PDFs already carry link annotations. Hide those, let
   the new rules find the targets from the printed text, and score them
   against the real links: found, right, wrong. A wrong target is worse than
   none.

*Measure:* references found and right, wrong targets (must be about 0), size
and time.

### S7 - Speed, size and the published numbers (open)

1. **Speed.** Profile the bench. Remove the largest costs in a way that leaves
   every file the same (`same_output.py` shows no difference).
2. **Size.** Pack figures smaller without loss, for example with better PNG
   compression or an exact palette. Any change that loses detail is optional
   and off.
3. **Published numbers.** Make `tests/bench/baseline-public.json` and `.md`
   again. Update the numbers in the README (Quality) and in
   `docs/CONVERTER.md` from the bench.
4. **Builds.** Both builds give no warnings. The AddressSanitizer run is
   clean. `CMakeLists.txt` lists every source.

### S8 - The reader catches up, then the release is prepared (open)

This part changes the reader (`reader/`, `model/`), not the converter.

1. **Retrain the re-ranker** on files from the current converter, which has
   new table rows and reading order. The target is eval test R@1 and R@5 at
   least at the S2 level (0.775 / 0.946). Tune only on the tune split.
2. **New format.** The reader understands everything new in
   `docs/FORMAT.md` since 0.1.2. Then decide by eval whether
   `--table-pages join` is turned on.
3. **Prepare version 0.2.0.** Update the CHANGELOG and the versions in
   `pyproject.toml`, `.claude-plugin/plugin.json` and `server.json`. The owner
   publishes the release.

After S8 the owner tests with his own PDFs, using the converter session and the
comparison session.

## Not planned now, and why

- **ONNX layout model.** It needs ONNX Runtime and model weights of tens of
  MB, so HTPD would no longer be light. S3 and S4 already cover tables and
  reading order on digital PDFs, so the gain would be mostly on scans. Look at
  it again if the owner's scans fail.
- **libtesseract inside the converter.** It would only make scanned pages
  faster, and it adds a build dependency. Running the Tesseract program works.
- **DOCX, HTML and Markdown input.** Other input formats are a project of
  their own. PDF comes first.
- **A facts index** (lists of name = value). It repeats the table text in more
  bytes, and the AI can read the table rows. The reader can build such an
  index when it loads a file.

## Log

Each session adds one line: date, part, branch and commit, the main numbers.

- 2026-10-24 - S4 merged to main (67d3cd5). Checked on the owner's 8 training
  manuals (not committed):
  - 4 manuals have reordered pages. No word of text was lost.
  - In the ARM manual, tables now stand after the sentence that names them,
    in their own section. 4 PDF links became links inside one section and
    were dropped, which is correct.
