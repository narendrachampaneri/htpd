"""Training questions made by rules from TRAINING documents only.

The re-ranker (model/train_reranker.py) learns from questions whose right
section is known because the question was made from it, by fixed rules (no
AI):

* the title of a section ("Where is Clock tree described?");
* a row and a column of a table ("What is the Reset of READY?");
* a technical term that stands in few sections ("What is mstatus?");
* question files written for training documents (`file_questions`).

A bundle whose name is a test or tuning document in eval/docs.list, or
whose source SHA-256 is the one of such a document, is refused: nothing from
them is ever used for training (AGENTS.md rule 10).
"""

from __future__ import annotations

import hashlib
import re
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

from htpd_reader import Bundle

ROOT = Path(__file__).resolve().parents[1]
DOCS_LIST = ROOT / "eval" / "docs.list"

_TERM = re.compile(r"\b(?:[A-Za-z]+_[A-Za-z0-9_]+|[a-z]+[A-Z][A-Za-z0-9]*|[A-Z]{3,}[0-9]*|[a-z]{2,}[0-9]+[a-z0-9]*)\b")
_NUMBER_PREFIX = re.compile(r"^(?:appendix\s+)?[A-Z]?\d+(?:\.\d+)*\s+|^[A-Z](?:\.\d+)+\s+", re.IGNORECASE)


class TestDocumentError(Exception):
    pass


@dataclass(frozen=True)
class Q:
    question: str
    gold: str  # section ID
    source: str  # title, table, term, file


def h(text: str) -> int:
    return int(hashlib.sha256(text.encode("utf-8")).hexdigest(), 16)


# --------------------------------------------------------------------------
# guarding the test split


def test_documents(docs_list: Path = DOCS_LIST) -> tuple[set[str], set[str]]:
    """Names and pinned SHA-256 values of the test and tuning documents."""
    names, shas = set(), set()
    if docs_list.is_file():
        for line in docs_list.read_text(encoding="utf-8").split("\n"):
            parts = line.split("#", 1)[0].split()
            if len(parts) >= 2 and parts[1] in ("test", "tune"):
                names.add(parts[0])
                if len(parts) >= 3 and parts[2] != "-":
                    shas.add(parts[2].lower())
    return names, shas


def refuse_test_documents(bundles: dict[str, Bundle], docs_list: Path = DOCS_LIST) -> None:
    names, shas = test_documents(docs_list)
    for name, b in bundles.items():
        if name in names or (b.source_sha256 or "").lower() in shas:
            raise TestDocumentError(f"{name} is a test or tuning document: it is never used for training")


# --------------------------------------------------------------------------
# questions made by rules


def bare_title(title: str) -> str:
    return _NUMBER_PREFIX.sub("", title).strip()


def title_questions(b: Bundle) -> Iterator[Q]:
    templates = ("What does the document say about {t}?", "Where is {t} described?",
                 "How does {t} work?", "Explain {t}.")
    for s in b.sections:
        t = bare_title(s.title)
        # a title the author did not write ("Page 5", "... - pages 12-13") makes no question
        if s.heading_from in ("none", "pages") or len(t) < 4 or not s.blocks or t.lower() in ("introduction", "overview"):
            continue
        yield Q(templates[h(s.id) % len(templates)].format(t=t), s.id, "title")


def table_questions(b: Bundle, per_table: int = 3) -> Iterator[Q]:
    for s in b.sections:
        for blk in s.blocks:
            if blk.kind != "table":
                continue
            rows = blk.rows()
            if len(rows) < 2:
                continue
            header = rows[0]
            made = 0
            for row in rows[1:]:
                if made >= per_table or len(row) != len(header):
                    continue
                key = row[0].strip()
                if not re.search(r"[A-Za-z]", key) or len(key) > 40:
                    continue
                cols = [j for j in range(1, len(header)) if re.search(r"[A-Za-z]", header[j]) and row[j].strip()]
                if not cols:
                    continue
                j = cols[h(s.id + key) % len(cols)]
                yield Q(f"What is the {header[j].strip()} of {key}?", s.id, "table")
                made += 1


def term_questions(b: Bundle, per_section: int = 3, max_sections: int = 3) -> Iterator[Q]:
    where: dict[str, Counter[str]] = {}
    for s in b.sections:
        for blk in s.blocks:
            for m in _TERM.finditer(blk.text):
                where.setdefault(m.group(0), Counter())[s.id] += 1
    per: Counter[str] = Counter()
    templates = ("What is {t}?", "What does {t} do?", "Where is {t} defined?")
    for term in sorted(where):
        c = where[term]
        if len(c) > max_sections:
            continue
        gold, n = c.most_common(1)[0]
        if n < 2 and len(c) > 1:
            continue
        if per[gold] >= per_section:
            continue
        per[gold] += 1
        yield Q(templates[h(term) % len(templates)].format(t=term), gold, "term")


def file_questions(b: Bundle, name: str, paths: list[Path]) -> Iterator[Q]:
    from eval.questions import load, validate

    for q in load(paths):
        if q.bundle != name or q.kind == 4:
            continue
        if validate(b, q):
            continue
        for g in q.gold_sections:
            yield Q(q.question, g, "file")


def rule_questions(b: Bundle) -> list[Q]:
    """All questions the rules make from one bundle."""
    return [*title_questions(b), *table_questions(b), *term_questions(b)]
