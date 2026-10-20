# eval - the evaluation harness (for developers)

This folder measures how well part B answers questions: does it find the
right section and page, does it say "Not found in this file." when the
answer is not there, and how many tokens does it hand to the AI. It
compares HTPD with simple baselines (whole document, plain word search).

**You do not need it to use HTPD.** It is a tool for working on HTPD
itself.

## State

The first numbers are in [`report.md`](report.md).

* `docs.list` pins five public manuals (download link and SHA-256):
  the RISC-V Debug Specification and the Arm AAPCS64 for **test**, the
  RISC-V privileged manual and the Arm ELF manual for **tuning** (the
  "not found" threshold and every default of the reader), the RISC-V
  unprivileged manual for **training** only.
* `questions/` holds 254 questions (133 test, 121 tuning) of four kinds:
  a fact, a value from a table, an answer from two sections, and a question
  the document does not answer. Every answer is copied from the document
  and checked by `validate` against its section and page. They were
  written with an AI and are not yet checked by a person (`"checked":
  false`); docs/PART-B.md asks for at least 100 test questions checked by
  hand.

## How it is used

```sh
make                                  # the converter, build/htpd
python3 -m pip install -e .           # htpd_reader
eval/get_docs.sh --from ~/pdfs        # or fill in the URLs in docs.list
python -m eval validate               # keeps only questions whose answer is in the file
python -m eval tune htpd-words        # "not found" threshold, on the tuning split
python -m eval run htpd-words         # the test split -> eval/report.md
python -m eval systems                # all systems that can be compared
```

The rules (docs/PART-B.md section 4, AGENTS.md rule 10): numbers come
only from this harness, on the test split; nothing is tuned or trained on
a test document.

The tests of the harness run without any document:
`PYTHONPATH=reader:. python3 -m unittest discover -s eval/tests -t .`
