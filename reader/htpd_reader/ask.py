"""Question in, exact text with pages out: retriever (with its re-ranker) -> packer.

    from htpd_reader import open_bundle
    from htpd_reader.ask import Asker
    asker = Asker(open_bundle("manual.htpd"))
    result = asker.ask("What is the reset value of ndmreset?")
    print(result.pack.text)      # hand this to any AI model
    print(result.top_page)
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .bundle import Bundle, Node
from .packer import DEFAULT_BUDGET, DEFAULT_SECTIONS, Pack, best_block, pack
from .retriever import Retrieval, Retriever, Settings


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
        d["reason"] = self.retrieval.reason if self.not_found else ""
        return d


class Asker:
    def __init__(self, bundle: Bundle, settings: Settings | None = None, budget: int = DEFAULT_BUDGET,
                 max_sections: int = DEFAULT_SECTIONS):
        self.bundle = bundle
        self.retriever = Retriever(bundle, settings)
        self.budget = budget
        self.max_sections = max_sections

    def ask(self, question: str) -> Result:
        r = self.retriever.retrieve(question)
        ranked, not_found = [c.node for c in r.candidates], r.not_found
        reason = r.reason if not_found else ""
        p = pack(self.bundle, question, ranked, budget=self.budget, max_sections=self.max_sections,
                 not_found=not_found, reason=reason)
        top = None
        if not p.not_found and p.sections:
            blk = best_block(p.sections[0].node, question)
            top = blk.page if blk is not None else p.sections[0].node.page_start
        return Result(question, r, ranked, p, top)
