# eval - the evaluation harness (for developers)

This folder measures how well part B answers questions: does it find the
right section and page, does it say "Not found in this file." when the
answer is not there, and how many tokens does it hand to the AI. It
compares HTPD with simple baselines (whole document, plain word search).

**You do not need it to use HTPD.** It is a tool for working on HTPD
itself.

## State

The harness and its tests are written; it has **not been run yet**, so
there is no `eval/report.md` and there are no numbers. Still missing:

* `docs.list` names the public test documents, but their download links
  and SHA-256 are not filled in yet (`-`).
* `questions/` is empty: the question files (`*.jsonl`, format in
  `questions.py`) are still to be written and checked by hand.

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
