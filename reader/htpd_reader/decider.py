"""Phase 5: the decider - re-orders the retriever's candidates and says "not found".

A decider looks at the question and a short card of each candidate section
(its path of titles and first words) and gives each one a probability of
holding the answer. The candidates are then sorted by that probability; when
even the best one is below `not_found_below`, the answer is "not found".

Scorer (optional, runs on a CPU, open licence):

* CrossEncoderScorer - an open cross-encoder reranker,
  `cross-encoder/ms-marco-MiniLM-L-6-v2` (22M parameters, Apache-2.0),
  untrained on HTPD data (phase 5a), or the same model fine-tuned on the
  phase 4 data on a CPU (model/train_cross_encoder.py).

The library is not needed by the rest of htpd_reader.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Protocol, Sequence

from .bundle import Bundle, Node
from .retriever import Retrieval, card_text

DEFAULT_CROSS_ENCODER = "cross-encoder/ms-marco-MiniLM-L-6-v2"


class Scorer(Protocol):
    name: str

    def score(self, question: str, cards: Sequence[str]) -> list[float]:
        """A probability (0..1) for each card that it answers the question."""
        ...


@dataclass
class Decider:
    scorer: Scorer
    candidates: int = 20  # how many of the retriever's candidates are scored
    not_found_below: float = 0.0  # 0 disables "not found"
    card_words: int = 120

    last_best: float = 0.0  # best probability of the last decision (for tuning "not found")

    def decide(self, bundle: Bundle, question: str, retrieval: Retrieval) -> tuple[list[Node], bool]:
        self.last_best = 0.0
        head = [c.node for c in retrieval.candidates[: self.candidates]]
        tail = [c.node for c in retrieval.candidates[self.candidates:]]
        if not head:
            return [], True
        probs = self.scorer.score(question, [card_text(bundle, n, self.card_words) for n in head])
        order = sorted(range(len(head)), key=lambda i: (-probs[i], i))
        self.last_best = probs[order[0]]
        named = any("number" in c.ranks for c in retrieval.candidates[:1])
        not_found = (not named) and self.not_found_below > 0 and self.last_best < self.not_found_below
        return [head[i] for i in order] + tail, not_found


def sigmoid(x: float) -> float:
    return 1.0 / (1.0 + math.exp(-x)) if x > -60 else 0.0


class CrossEncoderScorer:
    def __init__(self, model: str = DEFAULT_CROSS_ENCODER, device: str = "cpu", max_length: int = 384):
        try:
            from sentence_transformers import CrossEncoder  # type: ignore
        except ImportError as e:  # pragma: no cover - depends on the machine
            from .embed import NotAvailable

            raise NotAvailable("the cross-encoder needs: python3 -m pip install sentence-transformers") from e
        self.name = model
        self._model = CrossEncoder(model, device=device, max_length=max_length)

    def score(self, question: str, cards: Sequence[str]) -> list[float]:
        if not cards:
            return []
        raw = self._model.predict([(question, c) for c in cards], show_progress_bar=False)
        vals = [float(x) for x in raw]
        # some models already give probabilities, most give logits
        if all(0.0 <= v <= 1.0 for v in vals):
            return vals
        return [sigmoid(v) for v in vals]
