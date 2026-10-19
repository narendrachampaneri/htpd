# The HTPD decider - model card

The decider re-orders the sections the retriever finds and says "not found"
when no section answers the question (docs/PART-B.md, phase 5). This page
says what the models are, what they learn from, and where their numbers are.

## The models

| System | Model | Licence | Trained on HTPD data | Where it runs |
|---|---|---|---|---|
| `htpd-ce` | `cross-encoder/ms-marco-MiniLM-L-6-v2`, 22M parameters | Apache-2.0 | no (phase 5a) | CPU |
| `htpd-ce-trained` | the same, fine-tuned (`model/train_cross_encoder.py`) | Apache-2.0 | yes | CPU, also for training |
| `htpd-laya` | Laya `convaiinnovations/laya`, 421M parameters, fine-tuned | Apache-2.0 | yes (phase 5b) | CPU to run; a GPU to train (free Kaggle 2x T4) |

The models are not stored in git (`model/checkpoints/` is ignored). A trained
model is saved to `model/checkpoints/cross-encoder-htpd` or
`model/checkpoints/laya-htpd`, or its place is given with `HTPD_CE_TRAINED` /
`HTPD_LAYA`.

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

On any PC (CPU):

```sh
python3 -m pip install -e . sentence-transformers
eval/get_docs.sh                                      # PDFs and bundles
python -m model.make_data eval/bundles/riscv-isa.htpd
python -m model.train_cross_encoder                   # saves model/checkpoints/cross-encoder-htpd
python -m eval tune htpd-ce-trained                   # "not found" threshold, tuning split
python -m eval run htpd-ce-trained                    # test split -> eval/report.md
```

Laya (needs a GPU to train; Kaggle's free 2x T4 is enough, about four hours):

```sh
python -m model.export_laya                           # model/data/laya-train.jsonl, laya-dev.jsonl
```

Upload these two files to Kaggle (they hold only public training documents),
open Laya's notebook `laya_finetune_typed_decisions_2xT4_kaggle.ipynb`, point
its dataset step at them, train, refit the temperatures on `laya-dev.jsonl`,
and download the result into `model/checkpoints/laya-htpd`. Then on the PC:

```sh
python3 -m pip install laya
python -m eval tune htpd-laya
python -m eval run htpd-laya
```

## The numbers

Only in `eval/report.md`, written by the harness on the test split (AGENTS.md
rule 10). The decider stays only if it improves Recall@5, page accuracy or
tokens over phase 3 within the time target; the numbers decide between the
cross-encoder and Laya.

## Limits

* Laya's base checkpoints are near chance on new decisions without
  fine-tuning, and ship over-confident: the temperatures must be refitted on
  the dev data before its probabilities are used.
* Laya's English checkpoint reads 512 tokens per question; a card is kept to
  about 120 words for that reason.
* The fine-tuning notebook builds its own dataset; check its field names
  against `model/export_laya.py` for the Laya version you use.
