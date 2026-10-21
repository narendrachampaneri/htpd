# eval - the evaluation harness (for developers)

This folder measures how well the reader answers questions: does it find
the right section and page, does it say "Not found in this file." when the
answer is not there, is the exact answer in the text it hands to the AI,
and how many tokens does that take. It compares HTPD with simple baselines
(the whole document, plain word search). The numbers are in
[`report.md`](report.md).

**You do not need it to use HTPD.** It is a tool for working on HTPD
itself.

## What is here

* `docs.list` pins five public manuals (download link and SHA-256):
  the RISC-V Debug Specification and the Arm AAPCS64 for **test**, the
  RISC-V privileged manual and the Arm ELF manual for **tuning** (the
  "not found" threshold and every default of the reader), the RISC-V
  unprivileged manual for **training** only.
* `questions/` holds 254 questions (133 test, 121 tuning) of four kinds:
  a fact, a value from a table, an answer from two sections, and a question
  the document does not answer. Every answer is copied from the document
  and checked by `validate` against its section and page. They were
  written with an AI; `python -m eval review` lets a person check them one
  by one (`"checked": true`).
* `results/` holds the measured runs, `report.md` the table made from them.

## How it is used

```sh
make                                  # the converter, build/htpd
python3 -m pip install -e .           # the reader
eval/get_docs.sh --from ~/pdfs        # or download by the links in docs.list
python -m eval validate               # keeps only questions whose answer is in the file
python -m eval tune htpd-rerank       # "not found" threshold, on the tuning split
python -m eval run htpd-rerank        # the reader on the test split -> results/
python -m eval report                 # report.md from results/
python -m eval systems                # all systems that can be compared
```

The rules (AGENTS.md rule 10): numbers come only from this harness, on the
test split; nothing is tuned or trained on a test document.

The tests of the harness run without any document:
`PYTHONPATH=reader:. python3 -m unittest discover -s eval/tests -t .`
