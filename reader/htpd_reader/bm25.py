"""BM25 (Okapi) over a list of documents, pure Python.

    index = BM25([tokens_of_doc0, tokens_of_doc1, ...])
    index.top(query_tokens, k=10)  ->  [(doc_number, score), ...]
"""

from __future__ import annotations

import heapq
import math
from collections import Counter


class BM25:
    def __init__(self, docs: list[list[str]], k1: float = 1.2, b: float = 0.75):
        self.k1, self.b = k1, b
        self.n = len(docs)
        self.lengths = [len(d) for d in docs]
        self.avg = (sum(self.lengths) / self.n) if self.n else 0.0
        self.postings: dict[str, list[tuple[int, int]]] = {}
        for i, d in enumerate(docs):
            for term, tf in Counter(d).items():
                self.postings.setdefault(term, []).append((i, tf))
        self.idf = {t: math.log(1 + (self.n - len(p) + 0.5) / (len(p) + 0.5)) for t, p in self.postings.items()}

    def scores(self, query: list[str]) -> dict[int, float]:
        out: dict[int, float] = {}
        k1, b, avg = self.k1, self.b, self.avg or 1.0
        for term in set(query):
            posting = self.postings.get(term)
            if not posting:
                continue
            idf = self.idf[term]
            for i, tf in posting:
                norm = tf + k1 * (1 - b + b * self.lengths[i] / avg)
                out[i] = out.get(i, 0.0) + idf * tf * (k1 + 1) / norm
        return out

    def top(self, query: list[str], k: int = 10) -> list[tuple[int, float]]:
        s = self.scores(query)
        return heapq.nsmallest(k, s.items(), key=lambda x: (-x[1], x[0]))
