"""The re-ranker: a small learned model that puts the best of the found sections first.

The retriever finds about 20 candidate sections for a question. The re-ranker
looks at each candidate more closely than word search can, with a few numbers
(features) that a person would also look at:

* how high word search, the tree and the fusion ranked it;
* how much of the question (weighted by how rare its words are) the section
  holds, and how much its best single line or table row holds;
* whether the specific terms of the question (register names, acronyms)
  stand in it, and whether word pairs of the question stand in it in order;
* how much of the question its title holds, whether the question names its
  number, how long and how deep it is, whether it is a list of contents.

A few small neural networks (one hidden layer each, their scores averaged)
turn these into one score. Their weights are trained by
`model/train_reranker.py` on questions made from training manuals only, and
stored in `rerank.json` next to this file. Running it needs only the
Python standard library (the skill has no packages), and a few milliseconds.
"""

from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any

from .bundle import Node
from .tokens import STOPWORDS, technical_tokens

WEIGHTS = Path(__file__).resolve().parent / "rerank.json"

FEATURES = (
    "words_rank", "tree_rank", "fused_rank", "bm25", "text_cover", "line_cover", "table_line",
    "terms_in", "pairs_in", "title_cover", "number_named", "length", "depth", "contents",
)


def _rank(r: int | None) -> float:
    return 1.0 / r if r else 0.0


class Features:
    """The features of the candidates of one retrieval."""

    def __init__(self, retriever: Any):
        self.r = retriever
        self.index = {s.id: i for i, s in enumerate(retriever.sections)}
        self._node_cache: dict[str, tuple[set[str], list[tuple[set[str], bool]], list[str]]] = {}

    def _node(self, node: Node) -> tuple[set[str], list[tuple[set[str], bool]], list[str]]:
        """Tokens of a section, its lines (tokens, is a table row) and its token sequence."""
        got = self._node_cache.get(node.id)
        if got is None:
            from .answer import pieces  # late import: answer imports ask, which imports this module

            seq = self.r.tokens(node.title) + self.r.tokens(node.text)
            lines = []
            for ln in pieces(node):
                words = set(self.r.tokens(ln.text))
                if ln.header:
                    words |= set(self.r.tokens(ln.header))
                lines.append((words, ln.kind == "row"))
            got = (set(seq), lines, seq)
            if len(self._node_cache) < 5000:
                self._node_cache[node.id] = got
        return got

    def of(self, question: str, candidates: list[Any]) -> list[list[float]]:
        from .retriever import _SECTION_REF, specific_terms

        r = self.r
        idf = r.word_index.idf
        top_idf = max(idf.values(), default=1.0)
        q_tokens = [t for t in dict.fromkeys(r.tokens(question)) if t not in STOPWORDS]
        weights = {t: idf.get(t, top_idf) for t in q_tokens}
        total = sum(weights.values()) or 1.0
        q_seq = [t for t in r.tokens(question) if t not in STOPWORDS]
        pairs = {(a, b) for a, b in zip(q_seq, q_seq[1:])}
        terms = {w.lower() for term in specific_terms(question) for w in technical_tokens(term)}
        named = {m.group(1).lower() for m in _SECTION_REF.finditer(question)}
        bm25 = r.word_index.scores(r.tokens(question))
        best_bm25 = max((bm25.get(self.index.get(c.node.id, -1), 0.0) for c in candidates), default=0.0) or 1.0
        out = []
        for rank, c in enumerate(candidates, 1):
            node = c.node
            words, lines, seq = self._node(node)
            title_words = set(r.tokens(node.title))
            best_line, table = 0.0, 0.0
            for lw, is_row in lines:
                cover = sum(w for t, w in weights.items() if t in lw) / total
                if cover > best_line:
                    best_line, table = cover, 1.0 if is_row else 0.0
            seq_pairs = set(zip(seq, seq[1:])) if pairs else set()
            out.append([
                _rank(c.ranks.get("words")),
                _rank(c.ranks.get("tree")),
                1.0 / rank,
                bm25.get(self.index.get(node.id, -1), 0.0) / best_bm25,
                sum(w for t, w in weights.items() if t in words) / total,
                best_line,
                table * best_line,
                (sum(1 for t in terms if t in words) / len(terms)) if terms else 0.0,
                (len(pairs & seq_pairs) / len(pairs)) if pairs else 0.0,
                sum(w for t, w in weights.items() if t in title_words) / total,
                1.0 if (node.number or "").lower() in named else 0.0,
                math.log1p(len(seq)) / 10.0,
                (node.level or 1) / 10.0,
                1.0 if any(k in node.title.lower() for k in ("contents", "index", "revision history",
                                                                  "change history")) else 0.0,
            ])
        return out


class Reranker:
    """Scores candidates with the trained networks (or one logistic model)."""

    def __init__(self, weights: dict[str, Any]):
        self.names = list(weights["features"])
        if self.names != list(FEATURES):
            raise ValueError("the weights belong to other features than this reader computes")
        self.mean = [float(x) for x in weights["mean"]]
        self.scale = [float(x) or 1.0 for x in weights["scale"]]
        n = len(FEATURES)
        if "nets" in weights:
            self.nets = []
            for net in weights["nets"]:
                w1 = [[float(v) for v in row] for row in net["w1"]]  # features x hidden
                b1 = [float(v) for v in net["b1"]]
                w2 = [float(v) for v in net["w2"]]
                if len(w1) != n or any(len(row) != len(b1) for row in w1) or len(w2) != len(b1):
                    raise ValueError("the weights of a network do not fit together")
                # hidden x features: one row per hidden unit, faster in plain Python
                self.nets.append(([list(col) for col in zip(*w1)], b1, w2))
            if not self.nets:
                raise ValueError("no network in the weights")
        else:  # one logistic model
            w = [float(x) for x in weights["weights"]]
            if len(w) != n:
                raise ValueError("the weights do not fit the features")
            self.nets = [([w], [float(weights["bias"])], [1.0])]
            self.linear = True

    @classmethod
    def load(cls, path: Path = WEIGHTS) -> "Reranker | None":
        try:
            return cls(json.loads(path.read_text(encoding="utf-8")))
        except (OSError, ValueError, KeyError, TypeError):
            return None

    def score(self, rows: list[list[float]]) -> list[float]:
        linear = getattr(self, "linear", False)
        out = []
        for row in rows:
            z = [(x - m) / s for x, m, s in zip(row, self.mean, self.scale)]
            total = 0.0
            for units, b1, w2 in self.nets:
                for wu, bu, ou in zip(units, b1, w2):
                    a = bu + sum(w * x for w, x in zip(wu, z))
                    total += ou * (a if linear or a > 0 else 0.0)
            out.append(total / len(self.nets))
        return out

    def rerank(self, features: Features, question: str, candidates: list[Any], keep: int = 20) -> list[Any]:
        head = candidates[:keep]
        scores = self.score(features.of(question, head))
        order = sorted(range(len(head)), key=lambda i: (-scores[i], i))
        return [head[i] for i in order] + candidates[keep:]
