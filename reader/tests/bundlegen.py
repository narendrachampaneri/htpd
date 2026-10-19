"""Make small hand-made bundles for the tests, in the form the converter writes.

    make_bundle(folder, [
        Sec("1 Intro", level=1, blocks=[P(1, "Hello.")]),
        Sec("1.1 Scope", level=2, blocks=[T(1, "a | b | c")]),
    ])

The parent of a section is the nearest section above it with a smaller level.
The tests change single lines of the written files to make broken bundles.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

from htpd_reader.bundle import Block
from htpd_reader.render import render_text_part

SHA = "ab" * 32


def P(page: int, text: str) -> Block:
    return Block("paragraph", text, page, 0)


def T(page: int, text: str) -> Block:
    return Block("table", text, page, 0)


def C(page: int, text: str) -> Block:
    return Block("code", text, page, 0)


@dataclass
class Sec:
    title: str
    level: int = 1
    blocks: list[Block] = field(default_factory=list)
    number: str | None = None


def _slug(title: str) -> str:
    s = re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")[:60].strip("-")
    return s or "section"


def _q(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def make_bundle(folder: Path, sections: list[Sec], title: str = "Test Manual", pages: int | None = None) -> Path:
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "sections").mkdir(exist_ok=True)
    ids = [f"sections/{i + 1:04d}-{_slug(s.title)}" for i, s in enumerate(sections)]
    parents: list[str] = []
    for i, s in enumerate(sections):
        parent = "document"
        for j in range(i - 1, -1, -1):
            if sections[j].level < s.level:
                parent = ids[j]
                break
        parents.append(parent)
    children = {pid: [ids[i] for i, p in enumerate(parents) if p == pid] for pid in ["document", *ids]}

    def own_pages(s: Sec) -> tuple[int, int]:
        ps = [b.page for b in s.blocks if b.page is not None]
        return (min(ps), max(ps)) if ps else (1, 1)

    spans: dict[str, int] = {}
    for i in range(len(sections) - 1, -1, -1):
        end = own_pages(sections[i])[1]
        for c in children[ids[i]]:
            end = max(end, spans[c])
        spans[ids[i]] = end
    total_pages = pages or max([spans[i] for i in ids] + [1])

    for i, s in enumerate(sections):
        ps, pe = own_pages(s)
        nid = ids[i]
        lines = ["---", "type: Section", f"title: {_q(s.title)}", 'description: "x"',
                 f'resource: "t.pdf#page={ps}"', "tags: [htpd, section]",
                 "generated: { by: htpd/0.1.0, at: 2026-10-07T00:00:00Z }",
                 "htpd:", '  version: "0.1"', "  node: section", f'  id: "{nid}"', f"  order: {i + 1}"]
        if s.number:
            lines.append(f'  number: "{s.number}"')
        lines += [f"  level: {s.level}", "  heading_from: bookmark", f"  page_start: {ps}",
                  f"  page_end: {pe}", f"  span_page_end: {spans[nid]}", f'  parent: "{parents[i]}"']
        kids = children[nid]
        lines.append("  children: []" if not kids else "  children:")
        lines += [f'    - "{k}"' for k in kids]
        if i > 0:
            lines.append(f'  previous: "{ids[i - 1]}"')
        if i + 1 < len(ids):
            lines.append(f'  next: "{ids[i + 1]}"')
        lines += ['  source_file: "t.pdf"', f'  source_sha256: "{SHA}"', "---", ""]
        body = "# Location\n\n[" + title + "](../document.md) > **" + s.title + "**\n\n"
        body += f"Source: `t.pdf`, page {ps}.[^source-pdf]\n\n"
        body += render_text_part(s.blocks)
        if kids:
            body += "# Subsections\n\n" + "".join(
                f"* [{sections[ids.index(k)].title}]({k.split('/')[1]}.md) - page 1\n" for k in kids) + "\n"
        body += "# Neighbours\n\n"
        if i > 0:
            body += f"* Previous: [{sections[i - 1].title}]({ids[i - 1].split('/')[1]}.md)\n"
        if i + 1 < len(ids):
            body += f"* Next: [{sections[i + 1].title}]({ids[i + 1].split('/')[1]}.md)\n"
        body += f"* Document: [{title}](../document.md)\n\n[^source-pdf]: t.pdf, page {ps}\n"
        (folder / f"{nid}.md").write_text("\n".join(lines) + "\n" + body, encoding="utf-8")

    doc = ["---", "type: Document", f"title: {_q(title)}", 'description: "test"', 'resource: "t.pdf"',
           "tags: [htpd, document]", "htpd:", '  version: "0.1"', "  node: document", '  id: "document"',
           '  source_file: "t.pdf"', f'  source_sha256: "{SHA}"', "  source_bytes: 1",
           f"  pages: {total_pages}", f"  sections: {len(ids)}", "  headings_from: bookmark",
           "  body_font_size: 10.0"]
    doc.append("  children: []" if not children["document"] else "  children:")
    doc += [f'    - "{k}"' for k in children["document"]]
    doc += ["---", "", "# Contents", ""]
    doc += [f"* [{s.title}]({ids[i]}.md) - page 1" for i, s in enumerate(sections)]
    doc += ["", "# Conversion notes", "", "* test", ""]
    (folder / "document.md").write_text("\n".join(doc), encoding="utf-8")
    (folder / "sections" / "index.md").write_text(
        "# Sections\n\n" + "".join(f"* [{s.title}]({ids[i].split('/')[1]}.md) - page 1\n"
                                   for i, s in enumerate(sections)), encoding="utf-8")
    (folder / "index.md").write_text('---\nokf_version: "0.2"\n---\n\n# Document\n\n* [T](document.md)\n',
                                     encoding="utf-8")
    (folder / "log.md").write_text("# Update Log\n", encoding="utf-8")
    return folder


def edit(path: Path, old: str, new: str) -> None:
    """Change one exact piece of a file; fail loudly when it is not there once."""
    text = path.read_text(encoding="utf-8")
    assert text.count(old) == 1, f"{old!r} occurs {text.count(old)} times in {path}"
    path.write_text(text.replace(old, new), encoding="utf-8")
