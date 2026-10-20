"""Command line: python -m htpd_reader (or htpd-read) COMMAND BUNDLE ...

  contents BUNDLE              the tree of sections with pages
  section  BUNDLE REF          one section: REF is an ID, a file name or a number
  search   BUNDLE WORDS...     sections that hold all the words
  ask      BUNDLE QUESTION     the exact lines of the file that answer it, with pages,
                               or "Not found in this file" and why
  check    BUNDLE [--pdf FILE] every problem of the bundle; exit 1 on errors

--json gives the same as JSON. Text is printed exactly as the bundle has it.
"""

from __future__ import annotations

import argparse
import json
import sys
from typing import Any, Sequence

from . import __version__
from .bundle import Bundle, BundleError, Node
from .check import check, errors
from .packer import DEFAULT_BUDGET
from .search import search


def _pages(n: Node) -> str:
    a, b = n.page_start, n.page_end
    if a is None:
        return "no page"
    return f"page {a}" if a == b or b is None else f"pages {a}-{b}"


def _print_json(value: Any) -> None:
    json.dump(value, sys.stdout, ensure_ascii=False, indent=2)
    sys.stdout.write("\n")


def cmd_contents(b: Bundle, args: argparse.Namespace) -> int:
    if args.json:
        _print_json({"title": b.title, "pages": b.page_count, "sections": len(b),
                     "source_file": b.source_file, "source_sha256": b.source_sha256,
                     "contents": b.contents()})
        return 0
    print(f"{b.title} - {b.page_count} pages, {len(b)} sections")
    for n, depth in b.walk():
        if args.depth is not None and depth >= args.depth:
            continue
        print(f"{'  ' * depth}{n.title} ({_pages(n)})  [{n.id}]")
    return 0


def cmd_section(b: Bundle, args: argparse.Namespace) -> int:
    n = b.find(args.ref)
    if n is None or n.id == "document":
        print(f"htpd-read: no section {args.ref!r} in {b.root}", file=sys.stderr)
        return 2
    if args.json:
        data = n.summary()
        data.update({"path": [x for x in b.path_titles(n)], "description": n.description,
                     "blocks": [blk.to_dict() for blk in n.blocks]})
        _print_json(data)
        return 0
    print(" > ".join(b.path_titles(n)))
    print(f"{_pages(n)}  [{n.id}]")
    page = None
    for blk in n.blocks:
        if blk.page != page:
            page = blk.page
            print(f"\n--- page {page} ---")
        print()
        print(blk.text)
    if not n.blocks:
        print("\n(no text directly under this heading)")
    kids = b.children(n)
    if kids:
        print("\nSubsections:")
        for k in kids:
            print(f"  {k.title} ({_pages(k)})  [{k.id}]")
    return 0


def cmd_search(b: Bundle, args: argparse.Namespace) -> int:
    hits = search(b, " ".join(args.words), limit=args.limit, any_word=args.any)
    if args.json:
        _print_json([h.to_dict() for h in hits])
        return 0
    if not hits:
        print("not found")
        return 1
    for h in hits:
        page = h.block.page if h.block else h.node.page_start
        print(f"{h.score:6.1f}  {h.node.title}  (page {page})  [{h.node.id}]")
    return 0


def cmd_ask(b: Bundle, args: argparse.Namespace) -> int:
    from .answer import answer
    from .ask import Asker
    from .retriever import TUNED_NOT_FOUND_BELOW, Settings

    embedder = None
    if args.meaning:
        from .embed import SentenceTransformerEmbedder

        embedder = SentenceTransformerEmbedder()
    asker = Asker(b, Settings(meaning=args.meaning, not_found_below=TUNED_NOT_FOUND_BELOW),
                  embedder=embedder, budget=args.budget)
    a = answer(asker, " ".join(args.question), lines=args.lines)
    if args.json:
        data = a.to_dict()
        if args.context and a.result is not None:
            data["context"] = a.result.pack.text
        _print_json(data)
        return 0 if a.found else 1
    print(a.text())
    if args.context and a.found and a.result is not None:
        print("\n" + a.result.pack.text)
    return 0 if a.found else 1


def cmd_check(b: Bundle, args: argparse.Namespace) -> int:
    problems = check(b, pdf=args.pdf)
    bad = errors(problems)
    if args.json:
        _print_json({"ok": not bad, "errors": len(bad), "warnings": len(problems) - len(bad),
                     "problems": [p.to_dict() for p in problems]})
    else:
        for p in problems:
            print(p)
        print(f"{b.root}: {len(b)} sections, {len(bad)} errors, {len(problems) - len(bad)} warnings")
    return 1 if bad else 0


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(prog="htpd-read", description="Read an HTPD bundle.")
    ap.add_argument("--version", action="version", version=f"htpd-read {__version__}")
    sub = ap.add_subparsers(dest="command", required=True)

    def add(name: str, help_: str) -> argparse.ArgumentParser:
        p = sub.add_parser(name, help=help_)
        p.add_argument("bundle", help="folder of the bundle")
        p.add_argument("--json", action="store_true", help="print JSON")
        return p

    p = add("contents", "the tree of sections")
    p.add_argument("--depth", type=int, help="show only this many levels")
    p.set_defaults(func=cmd_contents)
    p = add("section", "the text of one section")
    p.add_argument("ref", help="ID, file name or section number")
    p.set_defaults(func=cmd_section)
    p = add("search", "sections that hold all the words")
    p.add_argument("words", nargs="+")
    p.add_argument("--limit", type=int, default=10)
    p.add_argument("--any", action="store_true", help="any of the words is enough")
    p.set_defaults(func=cmd_search)
    p = add("ask", "the exact lines of the file that answer a question")
    p.add_argument("question", nargs="+")
    p.add_argument("--lines", type=int, default=5, help="how many lines to give (default 5)")
    p.add_argument("--context", action="store_true", help="also print the sections the lines come from")
    p.add_argument("--budget", type=int, default=DEFAULT_BUDGET, help=f"most tokens of context (default {DEFAULT_BUDGET})")
    p.add_argument("--meaning", action="store_true", help="also meaning search (needs sentence-transformers)")
    p.set_defaults(func=cmd_ask)
    p = add("check", "list every problem of the bundle")
    p.add_argument("--pdf", help="also check that the bundle was made from this PDF")
    p.set_defaults(func=cmd_check)
    return ap


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        b = Bundle.open(args.bundle)
    except BundleError as e:
        print(f"htpd-read: {e}", file=sys.stderr)
        return 2
    try:
        return int(args.func(b, args))
    except BrokenPipeError:  # e.g. piped into head
        return 0


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
