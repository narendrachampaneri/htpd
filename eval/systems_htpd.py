"""Phase 3 systems: the HTPD retriever and packer, and one row per part switched off.

  htpd             words + meaning + tree + section numbers, packed
  htpd-words       without meaning search (needs no model; runs anywhere)
  htpd-no-words    without word search
  htpd-no-meaning  same as htpd-words, named for the ablation table
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
PHASE = "Phase 3 - retriever and packer"

VARIANTS: dict[str, tuple[str, dict[str, Any]]] = {
    "htpd": ("HTPD: words + meaning + tree", {}),
    "htpd-words": ("HTPD: words + tree (no model)", {"meaning": False}),
    "htpd-no-words": ("HTPD without word search", {"words": False}),
    "htpd-no-meaning": ("HTPD without meaning search", {"meaning": False}),
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
        settings = Settings(**{**changes, **tuned(name)})
        embedder = ctx.embedder() if settings.meaning else None
        return Asker(bundle, settings, embedder=embedder, cache_dir=ctx.cache_dir)

    def answer(asker: Asker, question: str) -> Answer:
        r = asker.ask(question)
        return Answer([n.id for n in r.ranked], r.top_page, r.pack.text, r.not_found,
                      {"coverage": r.retrieval.coverage, "nf_score": r.retrieval.coverage})

    return System(name, label, PHASE, prepare, answer)


for _name, (_label, _changes) in VARIANTS.items():
    register(make(_name, _label, _changes))
