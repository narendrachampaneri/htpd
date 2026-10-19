"""The answer from the file: the exact lines that answer a question, with pages.

No model writes anything here. The best lines are picked from the sections the
retriever (and decider) ranked first: a table row with its header row, or a
sentence of a paragraph, or a line of code - each word for word as it stands
in the file, with its page and section. When the retriever says "not found",
the answer is "Not found in this file" and why.

    from htpd_reader.answer import answer
    a = answer(asker, "What is the maximum NPU frequency?")
    print(a.text())
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Any

from .ask import Asker, Result
from .bundle import Block, Node
from .tokens import STOPWORDS, technical_tokens

NOT_FOUND = "Not found in this file."
_SENTENCE_END = re.compile(r"(?<=[.!?])\s+(?=[A-Z0-9(\"'])")


@dataclass
class Line:
    text: str  # exact text from the file
    page: int | None
    section: Node
    kind: str  # "row", "sentence" or "code"
    header: str | None = None  # the first row of the table, for a row
    score: float = 0.0

    def to_dict(self) -> dict[str, Any]:
        return {"text": self.text, "page": self.page, "section": self.section.id, "title": self.section.title,
                "kind": self.kind, "header": self.header, "score": round(self.score, 3)}


@dataclass
class Answer:
    question: str
    found: bool
    lines: list[Line] = field(default_factory=list)
    reason: str = ""
    result: Result | None = None

    def text(self) -> str:
        if not self.found:
            return f"{NOT_FOUND} ({self.reason})" if self.reason else NOT_FOUND
        out = []
        for ln in self.lines:
            where = f"page {ln.page}" if ln.page is not None else "no page"
            out.append(f"[{where}, {ln.section.title}]")
            if ln.header:
                out.append(f"  {ln.header}")
            out.append(f"  {ln.text}")
        return "\n".join(out)

    def to_dict(self) -> dict[str, Any]:
        return {"question": self.question, "found": self.found, "reason": self.reason,
                "lines": [ln.to_dict() for ln in self.lines]}


def pieces(node: Node) -> list[Line]:
    """Every line a block can give: table rows, sentences, code lines."""
    out: list[Line] = []
    for b in node.blocks:
        out += _pieces_of_block(node, b)
    return out


def _pieces_of_block(node: Node, b: Block) -> list[Line]:
    if b.kind == "table":
        rows = b.text.split("\n")
        header = rows[0] if len(rows) > 1 else None
        return [Line(r, b.page, node, "row", header if i else None) for i, r in enumerate(rows) if r.strip()]
    if b.kind == "code":
        return [Line(r, b.page, node, "code") for r in b.text.split("\n") if r.strip()]
    # Paragraphs: one line in the file may hold several sentences; and a
    # converted table that lost its columns is a paragraph with " | " cells.
    if " | " in b.text:
        return [Line(r, b.page, node, "row") for r in b.text.split("\n") if r.strip()]
    return [Line(s, b.page, node, "sentence") for s in _SENTENCE_END.split(b.text) if s.strip()]


def score_line(line: Line, terms: dict[str, float]) -> float:
    words = set(technical_tokens(line.text))
    if line.header:
        words |= {w for w in technical_tokens(line.header)}
    got = sum(w for t, w in terms.items() if t in words)
    # a short line that holds the words is a better answer than a long one
    return got / (1.0 + 0.02 * len(line.text.split()))


def answer(asker: Asker, question: str, sections: int = 3, lines: int = 3) -> Answer:
    r = asker.ask(question)
    if r.not_found:
        return Answer(question, False, reason=r.retrieval.reason, result=r)
    idf = asker.retriever.word_index.idf
    top_idf = max(idf.values(), default=1.0)
    terms = {t: idf.get(t, top_idf) for t in dict.fromkeys(technical_tokens(question)) if t not in STOPWORDS}
    if not terms:
        return Answer(question, False, reason="the question has no words to look for", result=r)
    candidates: list[Line] = []
    for rank, node in enumerate(r.ranked[:sections]):
        weight = 1.0 / (1.0 + 0.15 * rank)  # the retriever's order still counts
        for ln in pieces(node):
            ln.score = score_line(ln, terms) * weight
            if ln.score > 0:
                candidates.append(ln)
    candidates.sort(key=lambda x: -x.score)
    best: list[Line] = []
    for ln in candidates:
        if all(ln.text != b.text for b in best):
            best.append(ln)
        if len(best) == lines:
            break
    if not best:
        return Answer(question, False, reason="no line of the best sections holds the words of the question",
                      result=r)
    return Answer(question, True, best, result=r)

