"""Phase 4: make training data for the decider from the TRAINING documents only.

    python -m model.make_data eval/bundles/riscv-isa.htpd [more bundles] [--questions FILE ...]

Three decisions (docs/PART-B.md section 3):

  relevant  (question, card of a section)            -> 1 / 0
  child     (question, cards of the subsections)     -> which one leads to the answer
  enough    (question, text collected so far)        -> 1 / 0

Questions come from two places:
* made from the bundle by fixed rules (no AI): the title of a section, a row
  and column of a table, a technical term that stands in few sections;
* question files (`--questions`, the format of eval/questions) written by a
  person or an AI for training documents.

Hard negatives: the siblings of the right section, its neighbours, and the
sections a word search confuses it with. The ancestors of the right section
are never used as negatives (they hold its context).

A bundle whose name is a test document in eval/docs.list, or whose source
SHA-256 is the one of a test document, is refused: nothing from the test
documents is ever used for training (AGENTS.md rule 10).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterator

from htpd_reader import Bundle, Node, open_bundle
from htpd_reader.retriever import Retriever, Settings, card_text
from htpd_reader.tokens import technical_tokens

ROOT = Path(__file__).resolve().parents[1]
DOCS_LIST = ROOT / "eval" / "docs.list"
OUT = ROOT / "model" / "data"
MAX_OPTIONS = 8  # options of a "child" question (Laya gets worse with many)
NEGATIVES = 4  # negatives per question for "relevant"
TEXT_WORDS = 300  # words of a section in an "enough" example

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
    """Names and pinned SHA-256 values of the test documents."""
    names, shas = set(), set()
    if docs_list.is_file():
        for line in docs_list.read_text(encoding="utf-8").split("\n"):
            parts = line.split("#", 1)[0].split()
            if len(parts) >= 2 and parts[1] == "test":
                names.add(parts[0])
                if len(parts) >= 3 and parts[2] != "-":
                    shas.add(parts[2].lower())
    return names, shas


def refuse_test_documents(bundles: dict[str, Bundle], docs_list: Path = DOCS_LIST) -> None:
    names, shas = test_documents(docs_list)
    for name, b in bundles.items():
        if name in names or (b.source_sha256 or "").lower() in shas:
            raise TestDocumentError(f"{name} is a test document: it is never used for training")


# --------------------------------------------------------------------------
# questions made by rules


def bare_title(title: str) -> str:
    return _NUMBER_PREFIX.sub("", title).strip()


def title_questions(b: Bundle) -> Iterator[Q]:
    templates = ("What does the document say about {t}?", "Where is {t} described?",
                 "How does {t} work?", "Explain {t}.")
    for s in b.sections:
        t = bare_title(s.title)
        if s.heading_from == "none" or len(t) < 4 or not s.blocks or t.lower() in ("introduction", "overview"):
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


# --------------------------------------------------------------------------
# examples


def negatives(b: Bundle, r: Retriever, q: Q, k: int = NEGATIVES) -> list[Node]:
    """Hard negatives: by turns, a section the word search confuses with the
    right one, and a sibling or neighbour of the right one."""
    gold = b.node(q.gold)
    banned = {gold.id} | {a.id for a in b.ancestors(gold)} | set(gold.children)
    lookalikes = [c.node for c in r.retrieve(q.question, k=10).candidates]
    parent = b.parent(gold)
    near = [x for x in b.children(parent) if x.id != gold.id] if parent is not None else []
    near += [x for x in (b.previous(gold), b.next(gold)) if x is not None]
    out: list[Node] = []
    for i in range(max(len(lookalikes), len(near))):
        for group in (lookalikes, near):
            if i < len(group) and group[i].id not in banned and group[i] not in out and len(out) < k:
                out.append(group[i])
    return out


def child_example(b: Bundle, q: Q) -> Iterator[dict[str, Any]]:
    gold = b.node(q.gold)
    path = [b.document] + b.ancestors(gold) + [gold]
    for parent, right in zip(path, path[1:]):
        kids = b.children(parent)
        if len(kids) < 2:
            continue
        others = [k for k in kids if k.id != right.id]
        start = h(q.question + parent.id) % len(others)
        others = (others[start:] + others[:start])[: MAX_OPTIONS - 1]
        options = sorted(others + [right], key=lambda n: n.order or 0)
        yield {"task": "child", "question": q.question, "context": " > ".join(b.path_titles(parent)),
               "options": [card_text(b, o, max_words=25) for o in options],
               "answer": options.index(right), "gold": q.gold, "source": q.source}


def some_text(node: Node) -> str:
    return " ".join(node.text.split()[:TEXT_WORDS])


def examples_for(b: Bundle, name: str, r: Retriever, q: Q) -> Iterator[dict[str, Any]]:
    gold = b.node(q.gold)
    base = {"bundle": name, "gold": q.gold, "source": q.source}
    yield {"task": "relevant", "question": q.question, "card": card_text(b, gold), "label": 1, **base}
    negs = negatives(b, r, q)
    for n in negs:
        yield {"task": "relevant", "question": q.question, "card": card_text(b, n), "label": 0,
               "negative": n.id, **base}
    for ex in child_example(b, q):
        ex["bundle"] = name
        yield ex
    if gold.blocks:
        yield {"task": "enough", "question": q.question, "text": some_text(gold), "label": 1, **base}
    if negs and negs[0].blocks:
        yield {"task": "enough", "question": q.question, "text": some_text(negs[0]), "label": 0,
               "negative": negs[0].id, **base}


def generate(bundles: dict[str, Bundle], question_files: list[Path] | None = None,
             docs_list: Path = DOCS_LIST) -> list[dict[str, Any]]:
    refuse_test_documents(bundles, docs_list)
    seen: set[str] = set()
    out: list[dict[str, Any]] = []
    for name, b in bundles.items():
        r = Retriever(b, Settings(meaning=False))
        qs: list[Q] = [*title_questions(b), *table_questions(b), *term_questions(b)]
        if question_files:
            qs += list(file_questions(b, name, question_files))
        for q in qs:
            for ex in examples_for(b, name, r, q):
                payload = {k: ex[k] for k in ("task", "question", "card", "options", "text", "label", "answer")
                           if k in ex}
                key = hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest()
                if key in seen:
                    continue
                seen.add(key)
                # split by section, so one section never feeds both train and dev
                ex["split"] = "dev" if h(name + ex["gold"]) % 10 == 0 else "train"
                ex["id"] = key[:16]
                out.append(ex)
    out.sort(key=lambda e: e["id"])
    return out


def write(examples: list[dict[str, Any]], out: Path = OUT) -> dict[str, int]:
    out.mkdir(parents=True, exist_ok=True)
    counts: Counter[str] = Counter()
    files = {s: (out / f"{s}.jsonl").open("w", encoding="utf-8") for s in ("train", "dev")}
    try:
        for e in examples:
            files[e["split"]].write(json.dumps(e, ensure_ascii=False) + "\n")
            counts[f"{e['split']}/{e['task']}"] += 1
    finally:
        for f in files.values():
            f.close()
    return dict(sorted(counts.items()))


def write_check_sheet(examples: list[dict[str, Any]], n: int, path: Path) -> None:
    """A page of n examples, spread over tasks and sources, for a person to check."""
    picked = sorted(examples, key=lambda e: h("check" + e["id"]))[:n]
    lines = ["# Training examples to check by hand", "",
             f"{len(picked)} examples picked at random. For each one, mark [x] when the label is right,",
             "or write what is wrong. A label that is often wrong means a rule of make_data.py is wrong.", ""]
    for e in picked:
        lines.append(f"## [ ] {e['id']} - {e['task']} ({e['source']}, {e['bundle']})")
        lines.append(f"**Question:** {e['question']}")
        if e["task"] == "relevant":
            lines += [f"**Label:** {'relevant' if e['label'] else 'not relevant'}", "", "```", e["card"], "```"]
        elif e["task"] == "child":
            lines.append(f"**Right option:** {e['answer'] + 1}")
            lines += [f"{i + 1}. {o.splitlines()[0]}" for i, o in enumerate(e["options"])]
        else:
            lines += [f"**Label:** {'enough' if e['label'] else 'not enough'}", "", "```", e["text"][:600], "```"]
        lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="python -m model.make_data")
    ap.add_argument("bundles", nargs="+", help="bundle folders of TRAINING documents (name = folder without .htpd)")
    ap.add_argument("--questions", nargs="*", default=[], help="question files for training documents")
    ap.add_argument("--out", default=str(OUT))
    ap.add_argument("--check", type=int, default=200, help="examples to write into check.md for a person")
    args = ap.parse_args(argv)
    bundles = {Path(p).name.removesuffix(".htpd"): open_bundle(p) for p in args.bundles}
    try:
        ex = generate(bundles, [Path(p) for p in args.questions])
    except TestDocumentError as e:
        print(f"refused: {e}", file=sys.stderr)
        return 2
    counts = write(ex, Path(args.out))
    write_check_sheet(ex, args.check, Path(args.out) / "check.md")
    print(f"{len(ex)} examples from {', '.join(bundles)}")
    for k, v in counts.items():
        print(f"  {k:16} {v}")
    print(f"written to {args.out}/ (train.jsonl, dev.jsonl, check.md)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
