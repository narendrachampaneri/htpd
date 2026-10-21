"""The systems the harness can measure.

Every system gets one bundle and one question and gives an `Answer`:
the sections in the order it ranks them, the page it points to first, the
text it would hand to an AI model, and whether it says "not found".

Baselines - HTPD must beat them:
  B0  the whole document text
  B1  fixed chunks of about 500 tokens, BM25
  B3  HTPD sections, BM25 only

The HTPD reader and its ablations are in eval/systems_htpd.py.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Any, Callable

from htpd_reader import Block, Bundle
from htpd_reader.bm25 import BM25
from htpd_reader.tokens import count_tokens, plain_words

TOP_K_CONTEXT = 5  # units handed to the AI by the ranking baselines
CHUNK_TOKENS = 500
_WORD_AND_SPACE = re.compile(r"\S+\s*")


@dataclass
class Answer:
    ranked: list[str]  # section IDs, best first, no repeats
    top_page: int | None  # the page the system points to first
    context: str  # the text handed to the AI model
    not_found: bool = False
    extra: dict[str, Any] = field(default_factory=dict)


@dataclass
class Unit:
    """A piece of a document that a system ranks: a section or a chunk."""

    text: str
    blocks: list[tuple[str, Block]]  # (section ID, block), in order
    sections: list[str]  # section IDs, in order, no repeats


@dataclass
class Context:
    """What a system may need besides the bundle (nothing yet: every system
    runs from the bundle alone)."""


@dataclass
class System:
    name: str
    label: str
    group: str  # the heading the system stands under in the report
    prepare: Callable[[Bundle, Context], Any]
    answer: Callable[[Any, str], Answer]
    ranks: bool = True  # False: recall and pages do not apply (B0)


REGISTRY: dict[str, System] = {}


def register(system: System) -> System:
    REGISTRY[system.name] = system
    return system


# --------------------------------------------------------------------------
# helpers shared by systems


def best_page(blocks: list[tuple[str, Block]], question: str) -> int | None:
    """Page of the block that shares the most words with the question."""
    q = set(plain_words(question))
    best, best_n = None, -1
    for _, b in blocks:
        n = len(q & set(plain_words(b.text)))
        if n > best_n and b.page is not None:
            best, best_n = b.page, n
    return best


def section_units(bundle: Bundle) -> list[Unit]:
    out = []
    for s in bundle.sections:
        text = s.title + "\n\n" + s.text if s.blocks else s.title
        out.append(Unit(text, [(s.id, b) for b in s.blocks], [s.id]))
    return out


def chunk_units(bundle: Bundle, size: int = CHUNK_TOKENS) -> list[Unit]:
    """Fixed chunks over the text of the whole document, in order.

    Pieces (a title, a block, or a part of a long block) are added until a
    chunk has `size` tokens; section borders are ignored, as in the usual
    way of chunking a PDF.
    """
    pieces: list[tuple[str, str, Block | None]] = []  # (section, text, block)
    for s in bundle.sections:
        pieces.append((s.id, s.title, None))
        for b in s.blocks:
            if count_tokens(b.text) <= size:
                pieces.append((s.id, b.text, b))
                continue
            # cut a long block into pieces of `size` tokens; each piece is an
            # exact slice of the text (line ends of tables and code are kept)
            start = n = 0
            for m in _WORD_AND_SPACE.finditer(b.text):
                n += count_tokens(m.group(0))
                if n >= size:
                    piece = b.text[start:m.end()].rstrip()
                    pieces.append((s.id, piece, Block(b.kind, piece, b.page, b.line)))
                    start, n = m.end(), 0
            if start < len(b.text):
                piece = b.text[start:].rstrip()
                pieces.append((s.id, piece, Block(b.kind, piece, b.page, b.line)))
    units: list[Unit] = []
    texts: list[str] = []
    blocks: list[tuple[str, Block]] = []
    secs: list[str] = []
    n = 0
    for sid, text, block in pieces:
        texts.append(text)
        if block is not None:
            blocks.append((sid, block))
        if sid not in secs:
            secs.append(sid)
        n += count_tokens(text)
        if n >= size:
            units.append(Unit("\n\n".join(texts), blocks, secs))
            texts, blocks, secs, n = [], [], [], 0
    if texts:
        units.append(Unit("\n\n".join(texts), blocks, secs))
    return units


def whole_text(bundle: Bundle) -> str:
    return "\n\n".join(u.text for u in section_units(bundle))


def answer_from_units(units: list[Unit], order: list[int], question: str, k: int = TOP_K_CONTEXT) -> Answer:
    ranked: list[str] = []
    for i in order:
        for s in units[i].sections:
            if s not in ranked:
                ranked.append(s)
    if not order:
        return Answer([], None, "", not_found=True)
    top = units[order[0]]
    context = "\n\n".join(units[i].text for i in order[:k])
    return Answer(ranked, best_page(top.blocks, question), context)


# --------------------------------------------------------------------------
# the baselines


def _b0_prepare(bundle: Bundle, ctx: Context) -> str:
    return whole_text(bundle)


register(System("B0", "B0 whole document", "Baselines", _b0_prepare,
                lambda text, q: Answer([], None, text), ranks=False))


def _bm25_prepare(units: list[Unit]) -> tuple[list[Unit], BM25]:
    return units, BM25([plain_words(u.text) for u in units])


def _bm25_answer(state: tuple[list[Unit], BM25], question: str) -> Answer:
    units, index = state
    order = [i for i, _ in index.top(plain_words(question), k=50)]
    return answer_from_units(units, order, question)


register(System("B1", "B1 chunks, BM25", "Baselines",
                lambda b, ctx: _bm25_prepare(chunk_units(b)), _bm25_answer))
register(System("B3", "B3 HTPD sections, BM25", "Baselines",
                lambda b, ctx: _bm25_prepare(section_units(b)), _bm25_answer))

