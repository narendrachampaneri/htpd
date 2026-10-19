"""Phase 5 systems: the HTPD retriever, then a decider, then the packer.

  htpd-ce          + open cross-encoder, not trained on HTPD data (phase 5a)
  htpd-ce-trained  + the same cross-encoder fine-tuned on the phase 4 data

The retriever is the full one of phase 3 (words, meaning, tree, numbers). The
models are read from model/checkpoints/ (see model/README.md); the cross-encoder
of htpd-ce is downloaded once. The "not found" threshold comes from
eval/tuned.json (`python -m eval tune SYSTEM`, tuning split only).
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import Any

from htpd_reader import Bundle
from htpd_reader.ask import Asker
from htpd_reader.decider import DEFAULT_CROSS_ENCODER, CrossEncoderScorer, Decider
from htpd_reader.retriever import Settings

from .systems import Answer, Context, System, register
from .systems_htpd import tuned

CHECKPOINTS = Path(__file__).resolve().parents[1] / "model" / "checkpoints"
PHASE = "Phase 5 - decider"

LABELS = {
    "htpd-ce": "HTPD + cross-encoder (untrained)",
    "htpd-ce-trained": "HTPD + cross-encoder (trained on HTPD data)",
}


def default_scorer(name: str) -> Any:
    if name == "htpd-ce":
        return CrossEncoderScorer(os.environ.get("HTPD_CROSS_ENCODER", DEFAULT_CROSS_ENCODER))
    if name == "htpd-ce-trained":
        return CrossEncoderScorer(os.environ.get("HTPD_CE_TRAINED", str(CHECKPOINTS / "cross-encoder-htpd")))
    raise KeyError(name)


def make(name: str) -> System:
    def prepare(bundle: Bundle, ctx: Context) -> tuple[Asker, Decider]:
        scorer = (ctx.scorer_factory or default_scorer)(name)
        decider = Decider(scorer, **tuned(name))
        embedder = ctx.embedder()
        asker = Asker(bundle, Settings(), embedder=embedder, decider=decider, cache_dir=ctx.cache_dir)
        return asker, decider

    def answer(state: tuple[Asker, Decider], question: str) -> Answer:
        asker, decider = state
        r = asker.ask(question)
        best = getattr(decider, "last_best", 0.0)
        return Answer([n.id for n in r.ranked], r.top_page, r.pack.text, r.not_found, {"nf_score": best})

    return System(name, LABELS[name], PHASE, prepare, answer)


for _name in LABELS:
    register(make(_name))
