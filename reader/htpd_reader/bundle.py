"""Read an HTPD bundle exactly as docs/FORMAT.md describes it.

Nothing here changes text. The only rule applied to the text of a bundle is
the one of FORMAT.md section 4.3: a backslash at the very start of a
paragraph is not part of the text.

A bundle that breaks the format is still opened as far as possible. What is
wrong is collected in `Bundle.problems`; `check.py` adds the checks that need
the whole bundle.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterator

import os

from . import miniyaml

# PyYAML when it is installed (the C loader is much faster on large bundles);
# otherwise the small reader of the fields HTPD writes, so that the reader
# also runs with nothing but Python (an AI chat's sandbox, for example).
# HTPD_NO_YAML=1 forces the small reader (for its tests).
try:
    if os.environ.get("HTPD_NO_YAML"):
        raise ImportError
    import yaml

    _Loader: Any = getattr(yaml, "CSafeLoader", yaml.SafeLoader)
    _FieldError: tuple[type[Exception], ...] = (yaml.YAMLError, miniyaml.MiniYAMLError)
except ImportError:
    yaml = None
    _Loader = None
    _FieldError = (miniyaml.MiniYAMLError,)


def load_fields(text: str) -> Any:
    """The fields at the top of a file, as Python values."""
    if yaml is not None:
        return yaml.load(text, Loader=_Loader)
    return miniyaml.load(text)


def _field_error_line(e: Exception) -> int | None:
    mark = getattr(e, "problem_mark", None)
    if mark is not None:
        return mark.line + 2
    line = getattr(e, "line", None)
    return line + 1 if isinstance(line, int) else None

FORMAT_VERSIONS = ("0.1",)
DOCUMENT_ID = "document"
PARTS = ("# Location", "# Text", "# Subsections", "# Neighbours")
NO_TEXT_LINE = "_No text directly under this heading._"

_PAGE_MARK = re.compile(r"^<!-- page ([0-9]+) -->$")
_FENCE_OPEN = re.compile(r"^(`{3,})(.*)$")
# A Markdown link outside of the text of the PDF: [label](target)
_LINK = re.compile(r"\[(?:[^\]\\]|\\.)*\]\(([^)\s]+)\)")


class BundleError(Exception):
    """The folder cannot be read as a bundle at all (no document.md)."""


@dataclass(frozen=True)
class Problem:
    """One thing that is wrong with a bundle."""

    severity: str  # "error" or "warning"
    code: str  # short, stable name, e.g. "parent-child-mismatch"
    file: str  # path inside the bundle, e.g. "sections/0003-1-introduction.md"
    message: str
    line: int | None = None

    def to_dict(self) -> dict[str, Any]:
        return {
            "severity": self.severity,
            "code": self.code,
            "file": self.file,
            "line": self.line,
            "message": self.message,
        }

    def __str__(self) -> str:
        where = self.file if self.line is None else f"{self.file}:{self.line}"
        return f"{self.severity}: {where}: {self.message} [{self.code}]"


@dataclass(frozen=True)
class Block:
    """One piece of the text of a section.

    kind:  "paragraph", "table" (rows with " | " between cells) or "code"
           (monospace text, line by line).
    text:  the exact text. For a paragraph, after the backslash rule.
    page:  page number (from 1) of the nearest page mark above, or None when
           the block stands before the first page mark (a format error).
    line:  line of the file where the block starts (from 1).
    """

    kind: str
    text: str
    page: int | None
    line: int

    def rows(self) -> list[list[str]]:
        """The cells of a table block. Empty cells are kept as ""."""
        if self.kind != "table":
            raise ValueError("only a table block has rows")
        return [row.split(" | ") for row in self.text.split("\n")]

    def to_dict(self) -> dict[str, Any]:
        return {"kind": self.kind, "page": self.page, "line": self.line, "text": self.text}


@dataclass
class Node:
    """The document or one section, with its fields and its body."""

    id: str
    path: str  # path inside the bundle, e.g. "sections/0008-2-1-1-limits.md"
    fields: dict[str, Any]  # everything between the two lines of ---
    location: str = ""  # the path line of # Location ("" for the document)
    blocks: list[Block] = field(default_factory=list)
    links: list[tuple[int, str]] = field(default_factory=list)  # (line, target) outside # Text
    body: str = ""

    # --- the fields, read the way FORMAT.md defines them ---

    @property
    def htpd(self) -> dict[str, Any]:
        value = self.fields.get("htpd")
        return value if isinstance(value, dict) else {}

    @property
    def kind(self) -> str:
        return str(self.htpd.get("node", ""))

    @property
    def title(self) -> str:
        return str(self.fields.get("title", ""))

    @property
    def description(self) -> str:
        return str(self.fields.get("description", ""))

    @property
    def number(self) -> str | None:
        value = self.htpd.get("number")
        return None if value is None else str(value)

    @property
    def order(self) -> int | None:
        return _int_or_none(self.htpd.get("order"))

    @property
    def level(self) -> int | None:
        return _int_or_none(self.htpd.get("level"))

    @property
    def heading_from(self) -> str | None:
        value = self.htpd.get("heading_from")
        return None if value is None else str(value)

    @property
    def page_start(self) -> int | None:
        return _int_or_none(self.htpd.get("page_start"))

    @property
    def page_end(self) -> int | None:
        return _int_or_none(self.htpd.get("page_end"))

    @property
    def span_page_end(self) -> int | None:
        return _int_or_none(self.htpd.get("span_page_end"))

    @property
    def parent(self) -> str | None:
        value = self.htpd.get("parent")
        return None if value is None else str(value)

    @property
    def children(self) -> list[str]:
        value = self.htpd.get("children")
        return [str(c) for c in value] if isinstance(value, list) else []

    @property
    def previous(self) -> str | None:
        value = self.htpd.get("previous")
        return None if value is None else str(value)

    @property
    def next(self) -> str | None:
        value = self.htpd.get("next")
        return None if value is None else str(value)

    @property
    def pages(self) -> list[int]:
        """The pages that hold text of this node, in order, without repeats."""
        seen: list[int] = []
        for b in self.blocks:
            if b.page is not None and b.page not in seen:
                seen.append(b.page)
        return seen

    @property
    def text(self) -> str:
        """All blocks, one empty line between them. Exact text, no page marks."""
        return "\n\n".join(b.text for b in self.blocks)

    def summary(self) -> dict[str, Any]:
        return {
            "id": self.id,
            "title": self.title,
            "number": self.number,
            "level": self.level,
            "order": self.order,
            "page_start": self.page_start,
            "page_end": self.page_end,
            "span_page_end": self.span_page_end,
            "parent": self.parent,
            "children": self.children,
            "previous": self.previous,
            "next": self.next,
        }


def _int_or_none(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    return value if isinstance(value, int) else None


# --------------------------------------------------------------------------
# Parsing one file


def split_front_matter(content: str) -> tuple[str | None, str, int]:
    """Return (yaml text, body, line of the first body line).

    The fields are the lines between the first line (---) and the next line
    that is exactly ---. Without them, yaml text is None and body is all.
    """
    lines = content.split("\n")
    if not lines or lines[0] != "---":
        return None, content, 1
    for i in range(1, len(lines)):
        if lines[i] == "---":
            return "\n".join(lines[1:i]), "\n".join(lines[i + 1 :]), i + 2
    return None, content, 1


def unescape_paragraph(text: str) -> str:
    """FORMAT.md 4.3: a backslash at the very start is not part of the text."""
    return text[1:] if text.startswith("\\") else text


def parse_body(body: str, first_line: int, file: str) -> tuple[str, list[Block], list[tuple[int, str]], list[Problem]]:
    """Cut a body into its parts. Returns (location, blocks, links, problems)."""
    problems: list[Problem] = []
    lines = body.split("\n")
    part: str | None = None
    location_lines: list[str] = []
    blocks: list[Block] = []
    links: list[tuple[int, str]] = []
    page: int | None = None
    paragraph: list[str] = []
    paragraph_line = 0
    seen_parts: list[str] = []
    no_text_marker = False

    def flush_paragraph() -> None:
        nonlocal paragraph, no_text_marker
        if not paragraph:
            return
        text = "\n".join(paragraph)
        if text == NO_TEXT_LINE and page is None and not blocks:
            no_text_marker = True
        else:
            if page is None:
                problems.append(Problem("error", "text-without-page", file,
                                        "text stands before the first page mark", paragraph_line))
            blocks.append(Block("paragraph", unescape_paragraph(text), page, paragraph_line))
        paragraph = []

    i = 0
    while i < len(lines):
        line = lines[i]
        lineno = first_line + i
        fence = _FENCE_OPEN.match(line)
        if fence:
            # A block: nothing inside is structure, not even # Text or a page mark.
            flush_paragraph()
            ticks, info = fence.group(1), fence.group(2)
            j = i + 1
            while j < len(lines) and lines[j] != ticks:
                j += 1
            inner = "\n".join(lines[i + 1 : j])
            if j >= len(lines):
                problems.append(Problem("error", "unclosed-block", file,
                                        f"block opened with {ticks}{info} is never closed", lineno))
            if part == "# Text":
                kind = "table" if info == "text" else "code"
                if info not in ("", "text"):
                    problems.append(Problem("warning", "unknown-block-mark", file,
                                            f"block mark {info!r} is not 'text' or nothing", lineno))
                if page is None:
                    problems.append(Problem("error", "text-without-page", file,
                                            "text stands before the first page mark", lineno))
                blocks.append(Block(kind, inner, page, lineno))
            i = j + 1
            continue
        if line in PARTS:
            flush_paragraph()
            if line in seen_parts:
                problems.append(Problem("error", "repeated-part", file, f"{line} appears twice", lineno))
            seen_parts.append(line)
            part = line
            i += 1
            continue
        if part == "# Text":
            mark = _PAGE_MARK.match(line)
            if mark:
                flush_paragraph()
                page = int(mark.group(1))
            elif line == "":
                flush_paragraph()
            else:
                if not paragraph:
                    paragraph_line = lineno
                paragraph.append(line)
        else:
            if part == "# Location" and line and not line.startswith("Source:"):
                location_lines.append(line)
            for m in _LINK.finditer(line):
                links.append((lineno, m.group(1)))
        i += 1
    flush_paragraph()

    if "# Text" in seen_parts and not blocks and not no_text_marker:
        problems.append(Problem("warning", "empty-text", file,
                                f"# Text is empty and does not say {NO_TEXT_LINE!r}"))
    return "\n".join(location_lines), blocks, links, problems


def node_id_for(path: str) -> str:
    return path[:-3] if path.endswith(".md") else path


def read_node(root: Path, path: str) -> tuple[Node | None, list[Problem]]:
    """Read one file of the bundle. `path` is relative to the bundle root."""
    problems: list[Problem] = []
    try:
        raw = (root / path).read_bytes()
    except OSError as e:
        return None, [Problem("error", "unreadable", path, f"cannot read file: {e.strerror or e}")]
    try:
        content = raw.decode("utf-8")
    except UnicodeDecodeError as e:
        return None, [Problem("error", "not-utf8", path, f"not UTF-8: {e.reason} at byte {e.start}")]
    if "\r" in content:
        problems.append(Problem("warning", "crlf", path, "line ends are not \\n"))
    yaml_text, body, first_line = split_front_matter(content)
    if yaml_text is None:
        return None, problems + [Problem("error", "no-fields", path, "the file does not start with fields between --- lines")]
    try:
        fields = load_fields(yaml_text)
    except _FieldError as e:
        return None, problems + [Problem("error", "bad-fields", path,
                                         f"fields are not valid YAML: {getattr(e, 'problem', e)}",
                                         _field_error_line(e))]
    if not isinstance(fields, dict):
        return None, problems + [Problem("error", "bad-fields", path, "fields are not a mapping")]
    location, blocks, links, body_problems = parse_body(body, first_line, path)
    node = Node(id=node_id_for(path), path=path, fields=fields, location=location,
                blocks=blocks, links=links, body=body)
    return node, problems + body_problems


# --------------------------------------------------------------------------
# The bundle


class Bundle:
    """An opened bundle: the document node, the section nodes, the problems."""

    def __init__(self, root: Path, document: Node, sections: list[Node], problems: list[Problem]):
        self.root = root
        self.document = document
        self.sections = sections  # in the order of their file names
        self.problems = problems  # found while reading; check() adds more
        self.nodes: dict[str, Node] = {document.id: document}
        for s in sections:
            if s.id in self.nodes:
                problems.append(Problem("error", "duplicate-id", s.path, f"ID {s.id!r} is used twice"))
            self.nodes.setdefault(s.id, s)
        self._by_number: dict[str, Node] | None = None

    # --- opening ---

    single: bool = False  # True when read from one .htpd file (FORMAT.md section 11)

    @classmethod
    def open(cls, path: str | Path) -> "Bundle":
        root = Path(path)
        if root.is_file():
            return read_single_file(root)
        if not root.is_dir():
            raise BundleError(f"{root} is not a folder or an HTPD file")
        if not (root / "document.md").is_file():
            raise BundleError(f"{root} has no document.md, so it is not an HTPD bundle")
        problems: list[Problem] = []
        document, p = read_node(root, "document.md")
        problems += p
        if document is None:
            raise BundleError(f"{root}/document.md cannot be read: " + "; ".join(x.message for x in p))
        sections: list[Node] = []
        section_dir = root / "sections"
        if section_dir.is_dir():
            for f in sorted(section_dir.iterdir(), key=_file_order):
                if f.suffix != ".md" or f.name == "index.md" or not f.is_file():
                    continue
                node, p = read_node(root, f"sections/{f.name}")
                problems += p
                if node is not None:
                    sections.append(node)
        else:
            problems.append(Problem("error", "no-sections", "sections/", "the folder sections/ is missing"))
        return cls(root, document, sections, problems)

    # --- finding nodes ---

    def __contains__(self, node_id: str) -> bool:
        return node_id in self.nodes

    def __iter__(self) -> Iterator[Node]:
        return iter(self.sections)

    def __len__(self) -> int:
        return len(self.sections)

    def node(self, node_id: str) -> Node:
        try:
            return self.nodes[node_id]
        except KeyError:
            raise KeyError(f"no node {node_id!r} in the bundle") from None

    def find(self, ref: str) -> Node | None:
        """Find a node by ID, file name, file path, section number or the
        short label of the single file ("S14" or "[S14]")."""
        ref = ref.strip()
        short = re.fullmatch(r"\[?[Ss](\d+)\]?", ref)
        if short and 1 <= int(short.group(1)) <= len(self.sections):
            return self.sections[int(short.group(1)) - 1]
        for candidate in (ref, node_id_for(ref), f"sections/{node_id_for(ref)}"):
            if candidate in self.nodes:
                return self.nodes[candidate]
        if self._by_number is None:
            self._by_number = {}
            for s in self.sections:
                if s.number is not None:
                    self._by_number.setdefault(s.number.lower(), s)
        return self._by_number.get(ref.lower().removeprefix("section ").strip())

    def parent(self, node: Node) -> Node | None:
        return self.nodes.get(node.parent) if node.parent else None

    def children(self, node: Node) -> list[Node]:
        return [self.nodes[c] for c in node.children if c in self.nodes]

    def previous(self, node: Node) -> Node | None:
        return self.nodes.get(node.previous) if node.previous else None

    def next(self, node: Node) -> Node | None:
        return self.nodes.get(node.next) if node.next else None

    def ancestors(self, node: Node) -> list[Node]:
        """From the top-level section down to the parent of `node` (no document)."""
        chain: list[Node] = []
        seen = {node.id}
        current = self.parent(node)
        while current is not None and current.id != DOCUMENT_ID and current.id not in seen:
            seen.add(current.id)
            chain.append(current)
            current = self.parent(current)
        return list(reversed(chain))

    def path_titles(self, node: Node) -> list[str]:
        """Document title, then the titles down to this node."""
        return [self.document.title] + [a.title for a in self.ancestors(node)] + [node.title]

    def walk(self, start: Node | None = None, depth: int = 0) -> Iterator[tuple[Node, int]]:
        """The tree in document order: (node, depth). Depth 0 = top level."""
        start = start or self.document
        seen: set[str] = set()
        stack = [(c, depth) for c in reversed(self.children(start))]
        while stack:
            n, d = stack.pop()
            if n.id in seen:  # a broken bundle can have cycles
                continue
            seen.add(n.id)
            yield n, d
            stack.extend((c, d + 1) for c in reversed(self.children(n)))

    # --- facts of the document ---

    @property
    def title(self) -> str:
        return self.document.title

    @property
    def page_count(self) -> int | None:
        return _int_or_none(self.document.htpd.get("pages"))

    @property
    def source_file(self) -> str | None:
        value = self.document.htpd.get("source_file")
        return None if value is None else str(value)

    @property
    def source_sha256(self) -> str | None:
        value = self.document.htpd.get("source_sha256")
        return None if value is None else str(value)

    @property
    def name(self) -> str:
        return self.root.name.removesuffix(".htpd") if self.single else self.root.name

    def contents(self) -> list[dict[str, Any]]:
        """The full tree as a flat list in document order, with depth."""
        return [dict(n.summary(), depth=d) for n, d in self.walk()]


def _file_order(f: Path) -> tuple[int, str]:
    """NNNN has at least four digits: 10000 comes after 9999, not before."""
    digits = f.name.split("-", 1)[0]
    return (int(digits) if digits.isdigit() else 1 << 62, f.name)


SINGLE_FILE_MAGIC = "---\nhtpd_file: "
_SECTION_MARK = re.compile(r"^<!-- htpd section (\{.*\}) -->$")
_SECTION_HEADING = re.compile(r"^## \[S(\d+)\] (.*)$")
_FOOTNOTE = re.compile(r"^\[\^source-pdf\]: ")


def read_single_file(path: Path) -> Bundle:
    """Read the single-file form of a bundle (one .htpd file) into the same
    nodes as a bundle folder: the IDs, the tree, the order and the text with
    its pages are the same."""
    import json

    try:
        content = path.read_bytes().decode("utf-8")
    except (OSError, UnicodeDecodeError) as e:
        raise BundleError(f"{path} cannot be read: {e}") from None
    if not content.startswith(SINGLE_FILE_MAGIC):
        raise BundleError(f"{path} is not an HTPD file (it does not start with 'htpd_file:')")
    yaml_text, body, first_line = split_front_matter(content)
    try:
        head = load_fields(yaml_text or "")
    except _FieldError as e:
        raise BundleError(f"{path}: the fields at the top are not valid YAML: {e}") from None
    if not isinstance(head, dict):
        raise BundleError(f"{path}: the fields at the top are not a mapping")
    fname = path.name
    problems: list[Problem] = []
    lines = body.split("\n")

    # Cut the body at the section marks; inside a code block nothing is a mark.
    marks: list[tuple[int, dict[str, Any]]] = []
    fence: str | None = None
    for i, line in enumerate(lines):
        if fence is not None:
            if line == fence:
                fence = None
            continue
        m = _FENCE_OPEN.match(line)
        if m:
            fence = m.group(1)
            continue
        m = _SECTION_MARK.match(line)
        if m:
            try:
                fields = json.loads(m.group(1))
                if not isinstance(fields, dict):
                    raise ValueError("not an object")
            except ValueError as e:
                problems.append(Problem("error", "bad-fields", fname, f"section fields are not valid JSON: {e}",
                                        first_line + i))
                continue
            marks.append((i, fields))

    source = {"source_file": head.get("source_file"), "source_sha256": head.get("source_sha256")}
    sections: list[Node] = []
    for k, (i, f) in enumerate(marks):
        end = marks[k + 1][0] if k + 1 < len(marks) else len(lines)
        chunk = lines[i + 1:end]
        title = ""
        j = 0
        if chunk and (h := _SECTION_HEADING.match(chunk[0])):
            title = h.group(2)
            j = 1
        else:
            problems.append(Problem("error", "missing-part", fname, "a section mark is not followed by its "
                                    "'## [S<n>] title' line", first_line + i + 1))
        while j < len(chunk) and (chunk[j] == "" or chunk[j].startswith("Path: ")):
            j += 1
        text_lines = chunk[j:]
        if k + 1 == len(marks):  # the file ends with the footnote of the source
            while text_lines and (text_lines[-1] == "" or _FOOTNOTE.match(text_lines[-1])):
                text_lines.pop()
        node_id = str(f.get("id", f"sections/{k + 1:04d}"))
        pages = f.get("pages") if isinstance(f.get("pages"), list) and len(f.get("pages")) == 3 else [None] * 3
        htpd = {"version": head.get("htpd_file"), "node": "section", "id": node_id, "order": f.get("n"),
                "level": f.get("level"), "page_start": pages[0], "page_end": pages[1],
                "span_page_end": pages[2], "parent": f.get("parent"), "children": [], **source}
        if f.get("number") is not None:
            htpd["number"] = f.get("number")
        text_body = "# Text\n\n" + "\n".join(text_lines)
        _, blocks, _, p = parse_body(text_body, first_line + i + 1 + j - 2, fname)
        problems += p
        sections.append(Node(id=node_id, path=node_id + ".md", fields={"type": "Section", "title": title,
                                                                       "htpd": htpd}, blocks=blocks,
                             body=text_body))
    # The order and the tree, as a bundle folder writes them.
    by_id = {s.id: s for s in sections}
    top: list[str] = []
    for n, s in enumerate(sections):
        h = s.htpd
        if n > 0:
            h["previous"] = sections[n - 1].id
        if n + 1 < len(sections):
            h["next"] = sections[n + 1].id
        parent = h.get("parent")
        if parent == DOCUMENT_ID:
            top.append(s.id)
        elif parent in by_id:
            by_id[parent].htpd["children"].append(s.id)
    doc_fields = {"type": "Document", "title": head.get("title", path.stem),
                  "htpd": {"version": head.get("htpd_file"), "node": "document", "id": DOCUMENT_ID,
                           "pages": head.get("pages"), "sections": head.get("sections"), "children": top,
                           **source}}
    document = Node(id=DOCUMENT_ID, path=fname, fields=doc_fields)
    b = Bundle(path, document, sections, problems)
    b.single = True
    return b


def open_bundle(path: str | Path) -> Bundle:
    return Bundle.open(path)
