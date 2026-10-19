"""Phase 5b: write the phase 4 data as Laya requests, for fine-tuning Laya.

    python -m model.export_laya          # model/data/laya-train.jsonl and laya-dev.jsonl

One line per example, in the request form of the `laya` package
(state + typed questions) with the right answer:

  {"state": "...", "questions": {"relevant": {"type": "choice", ...}}, "answers": {"relevant": "yes"}}

* relevant: state = the card of a section;    choice yes/no
* enough:   state = the text collected so far; choice yes/no
* child:    state = the question and where we are in the document;
            choice among the subsections (labels c1, c2, ...)

Laya's fine-tuning notebook (notebooks/laya_finetune_typed_decisions_2xT4_kaggle.ipynb
in Laya's repository) builds its own dataset in its first cells: point that step at
these files. Check the field names against the notebook of the version you use;
this file follows the request form of Laya's model card. The data comes only from
public training documents, so it may be uploaded to Kaggle.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from htpd_reader.decider import LAYA_QUESTION

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "model" / "data"

ENOUGH_QUESTION = {
    "type": "choice",
    "instructions": "Is the text below enough to answer the question? Question: {question}",
    "criteria": {"yes": "the text holds the answer", "no": "more text is needed"},
}


def to_laya(e: dict[str, Any]) -> dict[str, Any]:
    q = e["question"]
    if e["task"] == "relevant":
        spec = dict(LAYA_QUESTION, instructions=LAYA_QUESTION["instructions"].format(question=q))
        return {"state": e["card"], "questions": {"relevant": spec},
                "answers": {"relevant": "yes" if e["label"] else "no"}}
    if e["task"] == "enough":
        spec = dict(ENOUGH_QUESTION, instructions=ENOUGH_QUESTION["instructions"].format(question=q))
        return {"state": e["text"], "questions": {"enough": spec},
                "answers": {"enough": "yes" if e["label"] else "no"}}
    if e["task"] == "child":
        criteria = {f"c{i + 1}": o.replace("\n", " - ") for i, o in enumerate(e["options"])}
        spec = {"type": "choice", "instructions": "Which subsection most likely holds the answer?",
                "criteria": criteria}
        return {"state": f"Question: {q}\nNow at: {e['context']}", "questions": {"child": spec},
                "answers": {"child": f"c{e['answer'] + 1}"}}
    raise ValueError(f"unknown task {e['task']!r}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="python -m model.export_laya")
    ap.add_argument("--data", default=str(DATA))
    args = ap.parse_args(argv)
    data = Path(args.data)
    for split in ("train", "dev"):
        src = data / f"{split}.jsonl"
        if not src.is_file():
            print(f"missing {src}: run python -m model.make_data first", file=sys.stderr)
            return 2
        lines = [json.dumps(to_laya(json.loads(x)), ensure_ascii=False) for x in src.read_text(encoding="utf-8").splitlines()]
        (data / f"laya-{split}.jsonl").write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"{len(lines)} examples -> {data / f'laya-{split}.jsonl'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
