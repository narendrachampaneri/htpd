"""Question in, exact text with pages out: retriever -> (decider) -> packer.

    from htpd_reader import open_bundle
    from htpd_reader.ask import Asker
    asker = Asker(open_bundle("manual.htpd"))
    result = asker.ask("What is the reset value of ndmreset?")
    print(result.pack.text)      # hand this to any AI model
    print(result.top_page)

The decider (phase 5) is optional. Without it, the retriever's order and its
"not found" rule are used as they are.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Protocol

from .bundle import Bundle, Node
from .packer import Pack, best_block, pack
from .retriever import Retrieval, Retriever, Settings


class Decider(Protocol):
    """Re-orders candidates and decides "not found" (phase 5)."""

    def decide(self, bundle: Bundle, question: str, retrieval: Retrieval) -> tuple[list[Node], bool]: ...


@dataclass
class Result:
    question: str
    retrieval: Retrieval
    ranked: list[Node]
    pack: Pack
    top_page: int | None

    @property
    def not_found(self) -> bool:
        return self.pack.not_found

    def to_dict(self) -> dict[str, Any]:
        d = self.pack.to_dict()
        d["top_page"] = self.top_page
        d["coverage"] = round(self.retrieval.coverage, 4)
        d["ranked"] = [n.id for n in self.ranked[:20]]
        return d


class Asker:
    def __init__(self, bundle: Bundle, settings: Settings | None = None, embedder: Any = None,
                 decider: Decider | None = None, budget: int = 1500, max_sections: int = 5,
                 cache_dir: Any = None):
        self.bundle = bundle
        self.retriever = Retriever(bundle, settings, embedder=embedder, cache_dir=cache_dir)
        self.decider = decider
        self.budget = budget
        self.max_sections = max_sections

    def ask(self, question: str) -> Result:
        r = self.retriever.retrieve(question)
        if self.decider is not None:
            ranked, not_found = self.decider.decide(self.bundle, question, r)
        else:
            ranked, not_found = [c.node for c in r.candidates], r.not_found
        p = pack(self.bundle, question, ranked, budget=self.budget, max_sections=self.max_sections,
                 not_found=not_found)
        top = None
        if not p.not_found and p.sections:
            blk = best_block(p.sections[0].node, question)
            top = blk.page if blk is not None else p.sections[0].node.page_start
        return Result(question, r, ranked, p, top)
