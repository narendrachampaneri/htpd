"""Question files: eval/questions/*.jsonl, one question per line.

Fields (docs/PART-B.md section 4):

  id            unique, e.g. "debug-0042"
  bundle        name of the bundle in eval/bundles/, e.g. "riscv-debug"
  kind          1 fact, 2 value from a table, 3 needs two sections, 4 not in the document
  question      the question
  answer        the exact words of the document that answer it: a string, or a
                list of strings for kind 3 (one per section); "" for kind 4
  gold_sections IDs of the sections that hold the answer; [] for kind 4
  gold_pages    pages that hold the answer; [] for kind 4
  checked       true when a person has checked the question by hand
  author        who wrote it ("claude", "person", ...)

`validate` keeps a question only when the answer really stands in the gold
sections, on the gold pages. It works on the text of the bundle with the
spaces made equal and the letters made lower case; nothing else is changed.
"""

from __future__ import annotations

import json
import re
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Iterable

from htpd_reader import Bundle

KINDS = {1: "fact", 2: "table value", 3: "two sections", 4: "not found"}
_SPACE = re.compile(r"\s+")


@dataclass
class Question:
    id: str
    bundle: str
    kind: int
    question: str
    answer: str | list[str]
    gold_sections: list[str]
    gold_pages: list[int]
    checked: bool = False
    author: str = ""
    extra: dict[str, Any] = field(default_factory=dict)

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "Question":
        known = {"id", "bundle", "kind", "question", "answer", "gold_sections", "gold_pages", "checked", "author"}
        return cls(
            id=str(d["id"]), bundle=str(d["bundle"]), kind=int(d["kind"]), question=str(d["question"]),
            answer=d.get("answer", ""), gold_sections=list(d.get("gold_sections", [])),
            gold_pages=[int(p) for p in d.get("gold_pages", [])], checked=bool(d.get("checked", False)),
            author=str(d.get("author", "")), extra={k: v for k, v in d.items() if k not in known},
        )

    def to_dict(self) -> dict[str, Any]:
        d = asdict(self)
        extra = d.pop("extra")
        d.update(extra)
        return d

    @property
    def answers(self) -> list[str]:
        return [self.answer] if isinstance(self.answer, str) else list(self.answer)


def norm(text: str) -> str:
    return _SPACE.sub(" ", text).strip().lower()


def load(paths: Iterable[Path]) -> list[Question]:
    out: list[Question] = []
    seen: set[str] = set()
    for path in paths:
        for i, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
            if not line.strip():
                continue
            try:
                q = Question.from_dict(json.loads(line))
            except (ValueError, KeyError, TypeError) as e:
                raise ValueError(f"{path}:{i}: not a valid question: {e}") from None
            if q.id in seen:
                raise ValueError(f"{path}:{i}: question ID {q.id!r} is used twice")
            seen.add(q.id)
            out.append(q)
    return out


def save(path: Path, questions: list[Question]) -> None:
    path.write_text("".join(json.dumps(q.to_dict(), ensure_ascii=False) + "\n" for q in questions), encoding="utf-8")


def answer_pages(bundle: Bundle, section_id: str, answer: str) -> list[int]:
    """The pages of the blocks of a section that hold the answer."""
    node = bundle.nodes.get(section_id)
    if node is None:
        return []
    want = norm(answer)
    pages = [b.page for b in node.blocks if b.page is not None and want in norm(b.text)]
    if not pages and want in norm(node.title):
        pages = [node.page_start] if node.page_start else []
    return sorted(set(pages))


def validate(bundle: Bundle, q: Question) -> list[str]:
    """Why a question cannot be kept. [] means it is good."""
    why: list[str] = []
    if q.kind not in KINDS:
        return [f"kind {q.kind} is not one of {sorted(KINDS)}"]
    if not q.question.strip():
        why.append("the question is empty")
    if q.kind == 4:
        if q.gold_sections or q.gold_pages:
            why.append("a not-found question has no gold sections or pages")
        return why
    if not q.gold_sections:
        return why + ["no gold section"]
    missing = [s for s in q.gold_sections if s not in bundle.nodes]
    if missing:
        return why + [f"gold section {s!r} is not in the bundle" for s in missing]
    answers = q.answers
    if not answers or any(not a.strip() for a in answers):
        return why + ["the answer is empty"]
    if q.kind == 3 and (len(set(q.gold_sections)) < 2 or len(answers) < 2):
        why.append("a two-section question needs two gold sections and two answer parts")
    found_pages: set[int] = set()
    for a in answers:
        hits = {s: answer_pages(bundle, s, a) for s in q.gold_sections}
        if not any(hits.values()):
            why.append(f"the answer {a!r} does not stand in the gold sections")
        for pages in hits.values():
            found_pages.update(pages)
    if q.kind == 3 and not why:
        holders = [s for s in q.gold_sections if any(answer_pages(bundle, s, a) for a in answers)]
        if len(holders) < 2:
            why.append("the answer parts all stand in one section")
    if q.kind == 2 and not why:
        in_table = any(
            b.kind == "table" and norm(a) in norm(b.text)
            for s in q.gold_sections for b in bundle.nodes[s].blocks for a in answers)
        if not in_table:
            why.append("a table question needs its answer in a table")
    if not why and not set(q.gold_pages) <= found_pages | _title_pages(bundle, q.gold_sections):
        why.append(f"gold pages {q.gold_pages} are not pages of the answer {sorted(found_pages)}")
    if not why and not q.gold_pages:
        why.append("no gold page")
    return why


def _title_pages(bundle: Bundle, ids: list[str]) -> set[int]:
    return {bundle.nodes[i].page_start for i in ids if i in bundle.nodes and bundle.nodes[i].page_start}
