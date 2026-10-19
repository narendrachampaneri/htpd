"""Check a bundle against docs/FORMAT.md and list every problem.

Errors break a promise of the format (a missing node, a tree that does not
agree with itself, text without a page). Warnings are odd but readable.
"""

from __future__ import annotations

import hashlib
import posixpath
import re
from pathlib import Path
from urllib.parse import unquote

from .bundle import _LINK as _LINK_RE
from .bundle import DOCUMENT_ID, FORMAT_VERSIONS, Bundle, Problem

_SECTION_FILE = re.compile(r"^sections/([0-9]{4,})-([a-z0-9-]+)$")


def check(bundle: Bundle, pdf: str | Path | None = None) -> list[Problem]:
    """All problems: those found while reading, then those of the whole bundle."""
    out: list[Problem] = list(bundle.problems)
    out += _check_document(bundle)
    out += _check_sections(bundle)
    out += _check_tree(bundle)
    out += _check_order(bundle)
    out += _check_links(bundle)
    if pdf is not None:
        out += _check_pdf(bundle, Path(pdf))
    return out


def errors(problems: list[Problem]) -> list[Problem]:
    return [p for p in problems if p.severity == "error"]


# --------------------------------------------------------------------------


def _check_document(b: Bundle) -> list[Problem]:
    out: list[Problem] = []
    d = b.document
    h = d.htpd
    f = d.path
    if not h:
        return [Problem("error", "no-htpd-fields", f, "document.md has no htpd: fields")]
    if str(h.get("version")) not in FORMAT_VERSIONS:
        out.append(Problem("warning", "unknown-version", f,
                           f"format version {h.get('version')!r} is not one this reader knows ({', '.join(FORMAT_VERSIONS)})"))
    if h.get("node") != "document":
        out.append(Problem("error", "wrong-node", f, f"htpd.node is {h.get('node')!r}, not 'document'"))
    if h.get("id") != DOCUMENT_ID:
        out.append(Problem("error", "wrong-id", f, f"htpd.id is {h.get('id')!r}, not 'document'"))
    for key in ("source_file", "source_sha256", "pages", "sections", "children"):
        if key not in h:
            out.append(Problem("error", "missing-field", f, f"htpd.{key} is missing"))
    count = h.get("sections")
    if isinstance(count, int) and count != len(b.sections):
        out.append(Problem("error", "section-count", f,
                           f"htpd.sections says {count}, but sections/ has {len(b.sections)} files"))
    if "children" in h and not isinstance(h.get("children"), list):
        out.append(Problem("error", "bad-field", f, "htpd.children is not a list"))
    return out


def _check_sections(b: Bundle) -> list[Problem]:
    out: list[Problem] = []
    pages = b.page_count
    sha = b.source_sha256
    for s in b.sections:
        f = s.path
        h = s.htpd
        if not h:
            out.append(Problem("error", "no-htpd-fields", f, "the file has no htpd: fields"))
            continue
        if h.get("node") != "section":
            out.append(Problem("error", "wrong-node", f, f"htpd.node is {h.get('node')!r}, not 'section'"))
        if h.get("id") != s.id:
            out.append(Problem("error", "wrong-id", f, f"htpd.id is {h.get('id')!r}, but the file is {s.id!r}"))
        if not _SECTION_FILE.match(s.id):
            out.append(Problem("warning", "file-name", f, "file name is not NNNN-<title>.md"))
        for key in ("order", "level", "page_start", "page_end", "span_page_end", "parent", "children"):
            if key not in h:
                out.append(Problem("error", "missing-field", f, f"htpd.{key} is missing"))
        if not s.title:
            out.append(Problem("error", "missing-field", f, "title is missing"))
        ps, pe, se = s.page_start, s.page_end, s.span_page_end
        if None not in (ps, pe, se):
            if not ps <= pe <= se:  # type: ignore[operator]
                out.append(Problem("error", "page-range", f,
                                   f"pages must be page_start <= page_end <= span_page_end, not {ps}, {pe}, {se}"))
            if ps < 1 or (pages is not None and se > pages):  # type: ignore[operator]
                out.append(Problem("error", "page-range", f, f"pages {ps}-{se} lie outside 1-{pages}"))
            for blk in s.blocks:
                if blk.page is not None and not ps <= blk.page <= pe:  # type: ignore[operator]
                    out.append(Problem("error", "page-mark-range", f,
                                       f"page mark {blk.page} lies outside the pages of the section ({ps}-{pe})", blk.line))
                    break
        if sha is not None and h.get("source_sha256") not in (None, sha):
            out.append(Problem("error", "source-mismatch", f, "source_sha256 differs from document.md"))
        if "# Text" not in s.body.split("\n"):
            out.append(Problem("error", "missing-part", f, "the part # Text is missing"))
    return out


def _check_tree(b: Bundle) -> list[Problem]:
    out: list[Problem] = []
    nodes = b.nodes
    for n in [b.document, *b.sections]:
        for c in n.children:
            if c not in nodes:
                out.append(Problem("error", "missing-node", n.path, f"child {c!r} does not exist"))
            elif nodes[c].parent != n.id:
                out.append(Problem("error", "parent-child-mismatch", n.path,
                                   f"lists {c!r} as a child, but its parent is {nodes[c].parent!r}"))
        if len(set(n.children)) != len(n.children):
            out.append(Problem("error", "duplicate-child", n.path, "a child is listed twice"))
    for s in b.sections:
        p = s.parent
        if p is None:
            continue
        if p not in nodes:
            out.append(Problem("error", "missing-node", s.path, f"parent {p!r} does not exist"))
            continue
        parent = nodes[p]
        if s.id not in parent.children:
            out.append(Problem("error", "parent-child-mismatch", s.path,
                               f"names {p!r} as parent, but is not among its children"))
        if parent.id != DOCUMENT_ID and None not in (s.level, parent.level) and s.level <= parent.level:  # type: ignore[operator]
            out.append(Problem("warning", "level", s.path,
                               f"level {s.level} is not deeper than the level {parent.level} of its parent"))
        if parent.id != DOCUMENT_ID and None not in (s.span_page_end, parent.span_page_end) \
                and s.span_page_end > parent.span_page_end:  # type: ignore[operator]
            out.append(Problem("error", "page-range", s.path, "span_page_end lies after the span of its parent"))
    # every section must hang below the document, without a loop
    for s in b.sections:
        seen = {s.id}
        cur = s
        while cur.parent and cur.parent != DOCUMENT_ID:
            if cur.parent in seen or cur.parent not in nodes:
                if cur.parent in seen:
                    out.append(Problem("error", "cycle", s.path, "the parents form a loop"))
                break
            seen.add(cur.parent)
            cur = nodes[cur.parent]
    return out


def _check_order(b: Bundle) -> list[Problem]:
    out: list[Problem] = []
    nodes = b.nodes
    secs = b.sections
    for i, s in enumerate(secs):
        if s.order is not None and s.order != i + 1:
            out.append(Problem("error", "order", s.path, f"htpd.order is {s.order}, but the file is number {i + 1}"))
        m = _SECTION_FILE.match(s.id)
        if m and s.order is not None and int(m.group(1)) != s.order:
            out.append(Problem("error", "order", s.path, f"file number {m.group(1)} differs from htpd.order {s.order}"))
        want_prev = secs[i - 1].id if i > 0 else None
        want_next = secs[i + 1].id if i + 1 < len(secs) else None
        for key, got, want in (("previous", s.previous, want_prev), ("next", s.next, want_next)):
            if got is not None and got not in nodes:
                out.append(Problem("error", "missing-node", s.path, f"{key} {got!r} does not exist"))
            elif got != want:
                out.append(Problem("error", "order-link", s.path, f"{key} is {got!r}, expected {want!r}"))
        if s.next and s.next in nodes and nodes[s.next].previous != s.id:
            out.append(Problem("error", "order-link", s.path,
                               f"next is {s.next!r}, but that section's previous is {nodes[s.next].previous!r}"))
    top = [s.id for s in secs if s.parent == DOCUMENT_ID]
    if isinstance(b.document.htpd.get("children"), list) and b.document.children != top:
        out.append(Problem("error", "parent-child-mismatch", b.document.path,
                           "htpd.children of document.md are not the top-level sections in order"))
    return out


def _check_links(b: Bundle) -> list[Problem]:
    """Every relative link outside the text of the PDF must point to a file."""
    out: list[Problem] = []
    files: list[tuple[str, list[tuple[int, str]]]] = [(n.path, n.links) for n in [b.document, *b.sections]]
    for extra in ("index.md", "sections/index.md"):
        p = b.root / extra
        if p.is_file():
            try:
                text = p.read_text(encoding="utf-8")
            except (OSError, UnicodeDecodeError):
                out.append(Problem("error", "unreadable", extra, "cannot read file"))
                continue
            links = [(i + 1, m.group(1)) for i, line in enumerate(text.split("\n"))
                     for m in _LINK_RE.finditer(line)]
            files.append((extra, links))
        else:
            out.append(Problem("warning", "missing-file", extra, f"{extra} is missing"))
    for path, links in files:
        base = posixpath.dirname(path)
        for line, target in links:
            if "://" in target or target.startswith(("#", "mailto:")):
                continue
            target = unquote(target.split("#", 1)[0])
            if not target:
                continue
            full = posixpath.normpath(posixpath.join(base, target))
            if full.startswith("..") or not (b.root / full).exists():
                out.append(Problem("error", "broken-link", path, f"link to {target!r} points to nothing", line))
    return out


def _check_pdf(b: Bundle, pdf: Path) -> list[Problem]:
    try:
        digest = hashlib.sha256(pdf.read_bytes()).hexdigest()
    except OSError as e:
        return [Problem("error", "unreadable", str(pdf), f"cannot read PDF: {e.strerror or e}")]
    if digest != b.source_sha256:
        return [Problem("error", "source-mismatch", "document.md",
                        f"{pdf.name} has SHA-256 {digest}, the bundle was made from {b.source_sha256}")]
    return []

