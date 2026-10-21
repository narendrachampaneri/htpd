# The re-ranker - model card

The reader finds about 20 candidate sections for a question by word search
and the tree of titles (`docs/READER.md`). The re-ranker puts the best of
them first.

## What it is

Five small neural networks (`reader/htpd_reader/rerank.py`, weights in
`reader/htpd_reader/rerank.json`, a few kilobytes): one hidden layer of 16
units each, their scores averaged. They look at 14 numbers a person would
also look at:

* the ranks the section got from word search, the tree and their fusion;
* how much of the question (weighted by how rare its words are) the
  section holds, and how much its best single line or table row holds;
* whether the specific terms of the question (register names, acronyms)
  and its word pairs stand in it;
* how much of the question its title holds, whether the question names its
  number, its length, its depth, whether it is a list of contents.

Running it needs only the Python standard library and a few milliseconds
per question, so the command line, the MCP server and the skill all use
it. It never changes the "not found" rule, a section the question names
stays first, and without word search it is not used. Without
`rerank.json` (or with `Settings(rerank=False)`) the reader keeps the order
of the retriever.

## Training

```sh
python -m model.train_reranker eval/bundles/riscv-isa.htpd private/bundles/more.htpd ...
```

* numpy only, about 2 minutes on a CPU, no GPU;
* about 2 700 questions made by rules (`model/questions.py`: a section
  title, a row and column of a table, a technical term that stands in few
  sections) from three training manuals: the RISC-V unprivileged ISA
  (`riscv-isa` in `eval/docs.list`), an Arm architecture reference manual
  and an NXP KM34 data sheet (both converted with HTPD and kept in
  `private/`, never committed);
* test and tuning documents are refused by name and by the SHA-256 of their
  PDF, and a manual whose text overlaps a tuning manual was left out;
* the loss is listwise: the right section must get the highest score among
  the candidates of its question.

Chosen on the tuning split: the network size, the training manuals (a C
book and a Python book made it worse on hardware manuals and were left
out), the weight of title questions (0.3). Then the test split was measured
once.

## Numbers

From `eval/report.md`, 133 test questions on two public manuals:

| | R@1 | R@5 | MRR | Page | Answer in the text |
|---|---|---|---|---|---|
| without the re-ranker (`htpd-words`) | 0.658 | 0.892 | 0.757 | 0.613 | 0.856 |
| with the re-ranker (`htpd-rerank`, the default) | 0.766 | 0.946 | 0.837 | 0.631 | 0.856 |

"Not found" is the same with and without it.

## Limits

* Three training manuals, all technical references in English. Other kinds
  of documents (books, slides, forms) were not in its training.
* The questions it learns from are made by rules, not written by people;
  questions in a user's own words differ from them.
