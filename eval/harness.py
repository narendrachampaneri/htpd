"""Run a system on the questions and measure it; write eval/report.md.

Numbers about the reader come only from here (AGENTS.md rule 10). The report is
made from the result files in eval/results/, never typed by hand.
"""

from __future__ import annotations

import datetime as _dt
import hashlib
import json
import platform
import statistics
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from htpd_reader import Bundle, open_bundle
from htpd_reader.tokens import count_tokens

from .questions import KINDS, Question, load, norm
from .systems import REGISTRY, Context, System, whole_text

EVAL = Path(__file__).resolve().parent
DOCS_LIST = EVAL / "docs.list"
RESULTS = EVAL / "results"
REPORT = EVAL / "report.md"
SPLITS = ("test", "tune", "train", "all")


def doc_splits(path: Path = DOCS_LIST) -> dict[str, str]:
    """bundle name -> "test", "tune" or "train", from eval/docs.list."""
    out: dict[str, str] = {}
    if not path.is_file():
        return out
    for line in path.read_text(encoding="utf-8").split("\n"):
        line = line.split("#", 1)[0].strip()
        if line:
            name, split = line.split()[:2]
            out[name] = split
    return out


def split_of(q: Question, docs: dict[str, str]) -> str:
    """Test documents give test questions and tuning documents tuning questions;
    of the other documents a quarter is for tuning."""
    if docs.get(q.bundle) in ("test", "tune"):
        return docs[q.bundle]
    if docs.get(q.bundle) is None:
        return "unknown"
    return "tune" if int(hashlib.sha256(q.id.encode()).hexdigest(), 16) % 4 == 0 else "train"


@dataclass
class Row:
    q: Question
    ranked: list[str]
    top_page: int | None
    not_found: bool
    tokens: int
    seconds: float
    extra: dict[str, Any] | None = None
    has_answer: bool = False  # the text handed to the AI holds every part of the answer

    def rank(self) -> int | None:
        for i, s in enumerate(self.ranked, 1):
            if s in self.q.gold_sections:
                return i
        return None


def run(system: System, questions: list[Question], bundles_dir: Path, ctx: Context) -> list[Row]:
    rows: list[Row] = []
    states: dict[str, Any] = {}
    bundles: dict[str, Bundle] = {}
    for q in questions:
        if q.bundle not in bundles:
            bundles[q.bundle] = open_bundle(bundles_dir / f"{q.bundle}.htpd")
            states[q.bundle] = system.prepare(bundles[q.bundle], ctx)
        t0 = time.perf_counter()
        a = system.answer(states[q.bundle], q.question)
        dt = time.perf_counter() - t0
        text = norm(a.context)
        has = q.kind != 4 and not a.not_found and all(norm(x) in text for x in q.answers)
        rows.append(Row(q, a.ranked, a.top_page, a.not_found, count_tokens(a.context), dt, a.extra, has))
    return rows


def _mean(xs: list[float]) -> float | None:
    return statistics.fmean(xs) if xs else None


def measure(system: System, rows: list[Row], whole_tokens: dict[str, int]) -> dict[str, Any]:
    answerable = [r for r in rows if r.q.kind != 4]
    absent = [r for r in rows if r.q.kind == 4]
    m: dict[str, Any] = {"questions": len(rows), "answerable": len(answerable), "not_in_document": len(absent)}
    if system.ranks and answerable:
        ranks = [r.rank() for r in answerable]
        for k in (1, 5, 10):
            m[f"recall@{k}"] = _mean([1.0 if x is not None and x <= k else 0.0 for x in ranks])
        m["mrr"] = _mean([1.0 / x if x else 0.0 for x in ranks])
        m["page_accuracy"] = _mean([1.0 if (r.top_page in r.q.gold_pages and not r.not_found) else 0.0
                                    for r in answerable])
    else:
        for k in (1, 5, 10):
            m[f"recall@{k}"] = None
        m["mrr"] = m["page_accuracy"] = None
    m["not_found_accuracy"] = _mean([1.0 if r.not_found else 0.0 for r in absent]) if absent else None
    m["false_not_found"] = _mean([1.0 if r.not_found else 0.0 for r in answerable]) if answerable else None
    m["answer_in_text"] = _mean([1.0 if r.has_answer else 0.0 for r in answerable]) if answerable else None
    m["tokens_mean"] = _mean([float(r.tokens) for r in rows])
    m["tokens_vs_whole"] = _mean([r.tokens / whole_tokens[r.q.bundle] for r in rows if whole_tokens.get(r.q.bundle)])
    m["ms_per_question"] = _mean([r.seconds * 1000 for r in rows])
    by_kind: dict[str, Any] = {}
    for kind in KINDS:
        rs = [r for r in answerable if r.q.kind == kind]
        if rs and system.ranks:
            by_kind[str(kind)] = {"n": len(rs), "recall@5": _mean([1.0 if (x := r.rank()) and x <= 5 else 0.0 for r in rs])}
    m["by_kind"] = by_kind
    return m


def evaluate(name: str, split: str, questions_paths: list[Path], bundles_dir: Path,
             ctx: Context, only_checked: bool = False, save: bool = True,
             docs_list: Path = DOCS_LIST, results_dir: Path = RESULTS) -> dict[str, Any]:
    system = REGISTRY[name]
    docs = doc_splits(docs_list)
    qs = load(questions_paths)
    if split != "all":
        qs = [q for q in qs if split_of(q, docs) == split]
    if only_checked:
        qs = [q for q in qs if q.checked]
    if not qs:
        raise SystemExit(f"no questions for split {split!r}")
    rows = run(system, qs, bundles_dir, ctx)
    whole = {b: count_tokens(whole_text(open_bundle(bundles_dir / f"{b}.htpd"))) for b in {q.bundle for q in qs}}
    result = {
        "system": name, "label": system.label, "group": system.group, "split": split,
        "only_checked": only_checked, "bundles": sorted(whole), "when": _dt.date.today().isoformat(),
        "machine": f"{platform.system()} {platform.machine()}, Python {platform.python_version()}",
        "metrics": measure(system, rows, whole),
        "misses": [{"id": r.q.id, "rank": r.rank(), "top": r.ranked[:3]} for r in rows
                   if r.q.kind != 4 and (r.rank() or 99) > 5][:50],
    }
    if save:
        results_dir.mkdir(parents=True, exist_ok=True)
        (results_dir / f"{name}-{split}.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def best_threshold(rows: list[tuple[float, bool, bool]]) -> tuple[float, float]:
    """(threshold, balanced accuracy) for rows of (score, already not found, absent).

    A question is "not found" when another rule says so or its score is below
    the threshold. Balanced accuracy is the mean of "not found" said for
    absent questions and not said for answerable ones. Of the thresholds with
    the best score, the middle of their range is taken: it keeps the widest
    margin to the questions on both sides.
    """
    absent = [(c, fixed) for c, fixed, is_absent in rows if is_absent]
    present = [(c, fixed) for c, fixed, is_absent in rows if not is_absent]
    scores = []
    for step in range(0, 101):
        t = step / 100
        caught = sum(1 for c, fixed in absent if fixed or c < t) / len(absent)
        kept = sum(1 for c, fixed in present if not fixed and c >= t) / len(present)
        scores.append((t, (caught + kept) / 2))
    top = max(s for _, s in scores)
    best = [t for t, s in scores if s >= top - 1e-12]
    # the first run of best thresholds (they are contiguous in practice)
    run_end = 0
    while run_end + 1 < len(best) and abs(best[run_end + 1] - best[run_end] - 0.01) < 1e-9:
        run_end += 1
    return round((best[0] + best[run_end]) / 2, 2), top


def tune_not_found(name: str, questions_paths: list[Path], bundles_dir: Path, ctx: Context,
                   docs_list: Path = DOCS_LIST, out: Path | None = None) -> dict[str, Any]:
    """Pick the "not found" threshold on the tuning split and store it.

    The threshold is the one with the best balanced accuracy: the mean of
    "not found" said for questions the document does not answer, and not
    said for questions it does answer.
    """
    docs = doc_splits(docs_list)
    qs = [q for q in load(questions_paths) if split_of(q, docs) == "tune"]
    if not any(q.kind == 4 for q in qs) or not any(q.kind != 4 for q in qs):
        raise SystemExit("the tuning split needs answerable and not-found questions")
    rows = run(REGISTRY[name], qs, bundles_dir, ctx)
    # nf_score: the number a system says "not found" below (the coverage of the question);
    # nf_fixed: "not found" by a rule that has no threshold (a word the file never uses)
    best = best_threshold([((r.extra or {}).get("nf_score", 0.0), bool((r.extra or {}).get("nf_fixed", False)),
                            r.q.kind == 4) for r in rows])
    out = out or (EVAL / "tuned.json")
    data = json.loads(out.read_text(encoding="utf-8")) if out.is_file() else {}
    data[name] = {"not_found_below": best[0]}
    out.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return {"system": name, "not_found_below": best[0], "balanced_accuracy": best[1], "questions": len(qs)}


def agent_questions(qs: list[Question], n: int = 20, docs_list: Path = DOCS_LIST) -> list[Question]:
    """A fixed spread of test questions for the end-to-end check with an AI agent:
    checked ones first, every kind, the same ones every time."""
    docs = doc_splits(docs_list)
    test = [q for q in qs if split_of(q, docs) == "test"]
    test.sort(key=lambda q: (not q.checked, hashlib.sha256(q.id.encode()).hexdigest()))
    out: list[Question] = []
    kinds = sorted({q.kind for q in test})
    while len(out) < min(n, len(test)):
        for k in kinds:
            pick = next((q for q in test if q.kind == k and q not in out), None)
            if pick is not None and len(out) < n:
                out.append(pick)
    return out


def agent_score(name: str, qs: list[Question], answers: Path, results_dir: Path = RESULTS) -> dict[str, Any]:
    """End-to-end: did the agent cite a right page, and say "not found" when it should?

    The agent answers each question of `agent-sheet` through the MCP server; a
    person writes down per question the pages the answer cites and whether it
    said the document does not answer it. Nothing else is judged here.
    """
    by_id = {q.id: q for q in qs}
    got = [json.loads(x) for x in answers.read_text(encoding="utf-8").splitlines() if x.strip()]
    rows = []
    for a in got:
        q = by_id.get(a["id"])
        if q is None:
            raise SystemExit(f"unknown question {a['id']!r}")
        rows.append((q, [int(p) for p in a.get("pages", [])], bool(a.get("not_found", False))))
    answerable = [(q, p, nf) for q, p, nf in rows if q.kind != 4]
    absent = [(q, p, nf) for q, p, nf in rows if q.kind == 4]
    metrics: dict[str, Any] = {
        "questions": len(rows), "answerable": len(answerable), "not_in_document": len(absent),
        "recall@1": None, "recall@5": None, "recall@10": None, "mrr": None,
        "page_accuracy": _mean([1.0 if (set(p) & set(q.gold_pages)) and not nf else 0.0 for q, p, nf in answerable]),
        "not_found_accuracy": _mean([1.0 if nf else 0.0 for q, p, nf in absent]) if absent else None,
        "false_not_found": _mean([1.0 if nf else 0.0 for q, p, nf in answerable]) if answerable else None,
        "tokens_mean": 0.0, "tokens_vs_whole": None, "ms_per_question": 0.0, "by_kind": {},
    }
    result = {"system": f"agent-{name}", "label": f"Agent {name} through the MCP server",
              "group": "AI agents", "split": "test", "only_checked": False,
              "bundles": sorted({q.bundle for q, _, _ in rows}), "when": _dt.date.today().isoformat(),
              "machine": "answers scored by hand", "metrics": metrics, "misses": []}
    results_dir.mkdir(parents=True, exist_ok=True)
    (results_dir / f"agent-{name}-test.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


# --------------------------------------------------------------------------
# the report


def _f(x: Any, pct: bool = False) -> str:
    if x is None:
        return "-"
    return f"{100 * x:.1f} %" if pct else f"{x:.3f}"


def write_report(results_dir: Path = RESULTS, out: Path = REPORT) -> str:
    results = [json.loads(p.read_text(encoding="utf-8")) for p in sorted(results_dir.glob("*.json"))]
    lines = ["# HTPD evaluation report", "",
             "Written by `python -m eval report` from `eval/results/`. Never edit by hand.", "",
             "Recall@k: a gold section is among the first k. MRR: mean of 1/rank of the first gold section.",
             "Page: the first page the system points to is a gold page. Not found: share of questions",
             "about things the document does not hold that get \"not found\". False not found: share of",
             "questions the document does answer that still get \"not found\" (lower is better). Answer in text:",
             "share of answerable questions whose exact answer stands in the text handed to the AI. Tokens: words and",
             "punctuation marks handed to the AI (one measure for all systems); \"of whole\" is the",
             "share of the whole document.", ""]
    for split in ("test", "tune", "train", "all"):
        rs = [r for r in results if r["split"] == split]
        if not rs:
            continue
        lines += [f"## Split: {split}", ""]
        for name in sorted({r.get("group", r.get("phase", "")) for r in rs}):
            group = [r for r in rs if r.get("group", r.get("phase", "")) == name]
            lines += [f"### {name}", "",
                      "| System | Questions | R@1 | R@5 | R@10 | MRR | Page | Not found | False not found | Answer in text | Tokens | Of whole | ms/question |",
                      "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
            for r in sorted(group, key=lambda r: r["system"]):
                m = r["metrics"]
                checked = " (checked only)" if r.get("only_checked") else ""
                lines.append(
                    f"| {r['label']}{checked} | {m['questions']} | {_f(m['recall@1'])} | {_f(m['recall@5'])} | "
                    f"{_f(m['recall@10'])} | {_f(m['mrr'])} | {_f(m['page_accuracy'])} | "
                    f"{_f(m['not_found_accuracy'])} | {_f(m.get('false_not_found'))} | {_f(m.get('answer_in_text'))} | {m['tokens_mean']:.0f} | {_f(m['tokens_vs_whole'], True)} | "
                    f"{m['ms_per_question']:.1f} |")
            bundles = sorted({b for r in group for b in r["bundles"]})
            when = sorted({r["when"] for r in group})
            lines += ["", f"Bundles: {', '.join(bundles)}. Run on {', '.join(when)}, {group[0]['machine']}.", ""]
    text = "\n".join(lines).rstrip() + "\n"
    out.write_text(text, encoding="utf-8")
    return text
