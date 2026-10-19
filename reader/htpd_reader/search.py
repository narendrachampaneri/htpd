"""A simple word search over the sections of a bundle.

This is phase 1's plain search: every word of the query must stand in the
section (title or text). Ranking counts the words, titles count three times.
The stronger search (BM25 with a technical tokenizer, meaning search, the
tree) is `retriever.py`.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Any

from .bundle import Block, Bundle, Node

_WORD = re.compile(r"\w+", re.UNICODE)


def words(text: str) -> list[str]:
    return [w.lower() for w in _WORD.findall(text)]


@dataclass(frozen=True)
class Hit:
    node: Node
    score: float
    block: Block | None  # the block with the most query words, if any

    def to_dict(self) -> dict[str, Any]:
        return {
            "id": self.node.id,
            "title": self.node.title,
            "score": self.score,
            "page": self.block.page if self.block else self.node.page_start,
            "block": self.block.to_dict() if self.block else None,
        }


def search(bundle: Bundle, query: str, limit: int = 10, any_word: bool = False) -> list[Hit]:
    terms = list(dict.fromkeys(words(query)))
    if not terms:
        return []
    hits: list[Hit] = []
    for node in bundle.sections:
        title_words = words(node.title)
        block_words = [words(b.text) for b in node.blocks]
        present = set(title_words).union(*block_words) if block_words else set(title_words)
        found = [t for t in terms if t in present]
        if not found or (not any_word and len(found) < len(terms)):
            continue
        score = 0.0
        for t in found:
            score += 3 * title_words.count(t) + sum(bw.count(t) for bw in block_words)
        best: Block | None = None
        best_count = 0
        for b, bw in zip(node.blocks, block_words):
            c = sum(1 for t in terms if t in bw)
            if c > best_count:
                best, best_count = b, c
        hits.append(Hit(node, score, best))
    hits.sort(key=lambda h: (-h.score, h.node.order or 0))
    return hits[:limit]
