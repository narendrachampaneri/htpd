# The HTPD models - model card

## The re-ranker (used by default)

A small logistic model (`reader/htpd_reader/rerank.py`, weights in
`reader/htpd_reader/rerank.json`, 14 numbers plus a bias) that re-orders the
20 sections the retriever finds. It looks at what a person would look at:
the ranks of word search and the tree, how much of the question the section
and its best single line or table row hold, the specific terms and word
pairs of the question, the title, length and depth. It needs only the
Python standard library and adds a few milliseconds per question, so the
skill and the MCP server use it too. It never changes the "not found" rule,
and a section the question names stays first.

* Trained with `python -m model.train_reranker eval/bundles/riscv-isa.htpd`
  (numpy only, about 15 s on a CPU) on about 1 500 questions made by rules
  from the **training document only**; test and tuning documents are refused.
* Its one setting (the L2 penalty, 0.1) and the weight of title questions
  (0.3) were chosen on the tuning split, then the test split was measured
  once: on 133 test questions R@1 0.667 -> 0.685, R@5 0.892 -> 0.919, MRR
  0.763 -> 0.793, page 0.622 -> 0.631, the answer in the text and "not found"
  unchanged (`htpd-words` -> `htpd-rerank` in `eval/report.md`).
* Without `rerank.json` (or with `Settings(rerank=False)`) the reader keeps
  the order of the retriever.

## The decider (optional)

The decider re-orders the sections the retriever finds and says "not found"
when no section answers the question (docs/PART-B.md, phase 5). This page
says what the models are, what they learn from, and where their numbers are.

## The models

| System | Model | Licence | Trained on HTPD data | Where it runs |
|---|---|---|---|---|
| `htpd-ce` | `cross-encoder/ms-marco-MiniLM-L-6-v2`, 22M parameters | Apache-2.0 | no (phase 5a) | CPU |
| `htpd-ce-trained` | the same, fine-tuned (`model/train_cross_encoder.py`) | Apache-2.0 | yes | CPU, also for training |

The models are not stored in git (`model/checkpoints/` is ignored). A trained
model is saved to `model/checkpoints/cross-encoder-htpd`, or its place is
given with `HTPD_CE_TRAINED`. Everything runs on a normal PC without a GPU,
and HTPD works without them (`htpd-rerank`, the default).

## The data

* Made by `python -m model.make_data` from the **training documents only**
  (eval/docs.list, split `train`). A test document is refused by name and by
  the SHA-256 of its PDF.
* Questions are made by fixed rules (a section title, a row and column of a
  table, a technical term that stands in few sections), plus question files
  written for training documents.
* Hard negatives: siblings, neighbours and the sections a word search confuses
  with the right one. Ancestors are never negatives.
* `model/data/dev.jsonl` holds the examples of one section in ten; a section
  never feeds both train and dev.
* `model/data/check.md` lists 200 examples for a person to check (the "done
  when" of phase 4).

## How to train

On any PC (CPU), one command does everything - install, data, training,
tuning, measuring - and writes `eval/report.md`:

```sh
./scripts/train_decider.sh            # about 30-60 min
./scripts/train_decider.sh --quick    # a short try, a few minutes
```

The same steps by hand:

```sh
python3 -m pip install -e . sentence-transformers
eval/get_docs.sh                                      # PDFs and bundles
python -m model.make_data eval/bundles/riscv-isa.htpd
python -m model.train_cross_encoder                   # saves model/checkpoints/cross-encoder-htpd
python -m eval tune htpd-ce-trained                   # "not found" threshold, tuning split
python -m eval run htpd-ce-trained                    # test split -> eval/report.md
```

## The numbers

Only in `eval/report.md`, written by the harness on the test split (AGENTS.md
rule 10). The decider stays only if it improves Recall@5, page accuracy or
tokens over phase 3 within the time target; otherwise HTPD runs without it.

## Limits

* The cross-encoder reads about 384 tokens per (question, card) pair; a
  card is kept to about 120 words for that reason.
* Its probabilities are not calibrated for HTPD until it is fine-tuned; the
  "not found" threshold is always set on the tuning split.
