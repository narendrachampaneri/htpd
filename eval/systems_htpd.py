"""The HTPD reader as the harness measures it, and one row per part switched off.

  htpd-rerank      words + tree + section numbers + the trained re-ranker: the reader's default
  htpd-words       the same without the re-ranker
  htpd-no-words    without word search
  htpd-no-tree     without the path-of-titles list
  htpd-no-numbers  without "section 3.1 in the question goes first"
  htpd-plain       plain words instead of the technical tokenizer

The "not found" threshold of each system is read from eval/tuned.json, which
`python -m eval tune SYSTEM` writes from the tuning split (never the test split).
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from htpd_reader import Bundle
from htpd_reader.ask import Asker
from htpd_reader.retriever import Settings

from .systems import Answer, Context, System, register

TUNED = Path(__file__).resolve().parent / "tuned.json"
GROUP = "HTPD"

VARIANTS: dict[str, tuple[str, dict[str, Any]]] = {
    "htpd-rerank": ("HTPD reader (words, tree, re-ranker)", {}),
    "htpd-words": ("HTPD reader without the re-ranker", {"rerank": False}),
    "htpd-no-words": ("HTPD without word search", {"words": False}),
    "htpd-no-tree": ("HTPD without the tree", {"tree": False}),
    "htpd-no-numbers": ("HTPD without section numbers", {"numbers": False}),
    "htpd-plain": ("HTPD with plain words", {"technical": False}),
}


def tuned(name: str, path: Path = TUNED) -> dict[str, Any]:
    if not path.is_file():
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    value = data.get(name, {})
    return value if isinstance(value, dict) else {}


def make(name: str, label: str, changes: dict[str, Any]) -> System:
    def prepare(bundle: Bundle, ctx: Context) -> Asker:
        return Asker(bundle, Settings(**{**changes, **tuned(name)}))

    def answer(asker: Asker, question: str) -> Answer:
        r = asker.ask(question)
        return Answer([n.id for n in r.ranked], r.top_page, r.pack.text, r.not_found,
                      {"coverage": r.retrieval.coverage, "nf_score": r.retrieval.coverage,
                       "nf_fixed": bool(r.retrieval.missing)})

    return System(name, label, GROUP, prepare, answer)


for _name, (_label, _changes) in VARIANTS.items():
    register(make(_name, _label, _changes))
