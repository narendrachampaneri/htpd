"""python -m eval COMMAND

  run SYSTEM [--split test]   measure one system (B0, B1, B3, htpd-rerank, ...); saves eval/results/
  report                      write eval/report.md from eval/results/
  tune SYSTEM                 set the "not found" threshold on the tuning split (eval/tuned.json)
  validate                    check every question against its bundle; exit 1 on a bad one
  review [--bundle NAME]      check unchecked test questions by hand, one by one
  stats                       how many questions of each kind, split and state
  systems                     list the systems
  agent-sheet [--n 20]        the test questions to ask an AI agent through the MCP server
  agent-score NAME FILE       score the agent's answers (JSONL: id, pages, not_found) -> eval/results/
"""

from __future__ import annotations

import argparse
import importlib
import json
import sys
from collections import Counter
from pathlib import Path

from htpd_reader import open_bundle

from . import harness
from .questions import KINDS, load, save, validate
from .systems import REGISTRY, Context

EVAL = Path(__file__).resolve().parent


def _question_files(arg: list[str] | None) -> list[Path]:
    return [Path(p) for p in arg] if arg else sorted((EVAL / "questions").glob("*.jsonl"))


def main(argv: list[str] | None = None) -> int:
    import eval.systems_htpd  # noqa: F401  (registers the HTPD systems)
    ap = argparse.ArgumentParser(prog="python -m eval")
    ap.add_argument("--bundles", default=str(EVAL / "bundles"), help="folder with the <name>.htpd bundles")
    ap.add_argument("--questions", nargs="*", help="question files (default eval/questions/*.jsonl)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("run")
    p.add_argument("system", choices=sorted(REGISTRY))
    p.add_argument("--split", default="test", choices=harness.SPLITS)
    p.add_argument("--checked", action="store_true", help="only questions checked by a person")
    sub.add_parser("report")
    p = sub.add_parser("tune")
    p.add_argument("system", choices=sorted(REGISTRY))
    sub.add_parser("validate")
    p = sub.add_parser("review")
    p.add_argument("--bundle")
    sub.add_parser("stats")
    sub.add_parser("systems")
    p = sub.add_parser("agent-sheet")
    p.add_argument("--n", type=int, default=20)
    p = sub.add_parser("agent-score")
    p.add_argument("name", help="the agent, e.g. claude-code or antigravity")
    p.add_argument("answers", help="JSONL: one line per question: {id, pages: [..], not_found: bool}")
    args = ap.parse_args(argv)
    bundles = Path(args.bundles)

    if args.cmd == "systems":
        for s in REGISTRY.values():
            print(f"{s.name:16} {s.label}  ({s.group})")
        return 0
    if args.cmd == "report":
        print(harness.write_report(), end="")
        return 0
    if args.cmd == "tune":
        t = harness.tune_not_found(args.system, _question_files(args.questions), bundles, Context())
        print(f"{t['system']}: not found below coverage {t['not_found_below']:.2f} "
              f"(balanced accuracy {t['balanced_accuracy']:.3f} on {t['questions']} tuning questions); "
              "saved in eval/tuned.json")
        return 0
    if args.cmd == "run":
        ctx = Context()
        r = harness.evaluate(args.system, args.split, _question_files(args.questions), bundles, ctx, args.checked)
        m = r["metrics"]
        print(f"{r['label']} on {args.split}: {m['questions']} questions, R@5 {harness._f(m['recall@5'])}, "
              f"MRR {harness._f(m['mrr'])}, page {harness._f(m['page_accuracy'])}, "
              f"not found {harness._f(m['not_found_accuracy'])}, tokens {m['tokens_mean']:.0f} "
              f"({harness._f(m['tokens_vs_whole'], True)} of whole), {m['ms_per_question']:.1f} ms")
        harness.write_report()
        return 0

    files = _question_files(args.questions)
    qs = load(files)
    if args.cmd == "stats":
        docs = harness.doc_splits()
        c = Counter((harness.split_of(q, docs), KINDS[q.kind], q.checked) for q in qs)
        print(f"{len(qs)} questions")
        for (split, kind, checked), n in sorted(c.items()):
            print(f"  {split:6} {kind:13} {'checked' if checked else 'not checked':12} {n}")
        return 0
    cache: dict[str, object] = {}

    def bundle(name: str):
        if name not in cache:
            cache[name] = open_bundle(bundles / f"{name}.htpd")
        return cache[name]

    if args.cmd == "validate":
        bad = 0
        for q in qs:
            why = validate(bundle(q.bundle), q)
            if why:
                bad += 1
                print(f"{q.id}: " + "; ".join(why))
        print(f"{len(qs)} questions, {bad} bad")
        return 1 if bad else 0
    if args.cmd == "review":
        return _review(files, bundle, args.bundle)
    if args.cmd == "agent-sheet":
        for q in harness.agent_questions(qs, args.n):
            print(json.dumps({"id": q.id, "bundle": q.bundle, "question": q.question}, ensure_ascii=False))
        return 0
    if args.cmd == "agent-score":
        r = harness.agent_score(args.name, qs, Path(args.answers))
        m = r["metrics"]
        print(f"{args.name}: {m['questions']} questions, page {harness._f(m['page_accuracy'])}, "
              f"not found {harness._f(m['not_found_accuracy'])}")
        harness.write_report()
        return 0
    return 2


def _review(files: list[Path], bundle, only: str | None) -> int:
    """Show each unchecked test question with its section text; y keeps it."""
    docs = harness.doc_splits()
    for f in files:
        qs = load([f])
        changed = False
        for q in qs:
            if q.checked or harness.split_of(q, docs) != "test" or (only and q.bundle != only):
                continue
            b = bundle(q.bundle)
            print("\n" + "=" * 70)
            print(f"{q.id}  kind {q.kind} ({KINDS[q.kind]})\nQ: {q.question}\nA: {q.answer}\npages: {q.gold_pages}")
            for sid in q.gold_sections:
                n = b.nodes[sid]
                print(f"\n--- {n.title} (pages {n.page_start}-{n.page_end})")
                print(n.text[:3000])
            ans = input("\nCorrect and fair? [y]es / [n]o, drop it / [s]kip / [q]uit: ").strip().lower()
            if ans == "q":
                break
            if ans == "y":
                q.checked, changed = True, True
            elif ans == "n":
                q.extra["dropped"] = True
                changed = True
        if changed:
            save(f, [q for q in qs if not q.extra.get("dropped")])
    return 0


if __name__ == "__main__":
    sys.exit(main())
