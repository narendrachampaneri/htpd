#!/usr/bin/env python3
"""Build the htpd skill as a zip that AI apps with skills can install.

    python3 scripts/build_skill.py            # writes dist/htpd-skill.zip

The zip holds one folder, `htpd/`, as its root:

    htpd/SKILL.md                    what the skill does and how the model uses it
    htpd/scripts/htpd.py             the command the model runs
    htpd/scripts/htpd_reader/*.py    the reader, standard library only
    htpd/scripts/htpd_reader/rerank.json  the weights of the re-ranker

Upload it in the Claude app (Customize > Skills; code execution must be on),
or unpack it into the skills folder of any agent that reads SKILL.md files.
"""

from __future__ import annotations

import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKILL = ROOT / "skills" / "htpd"
PACKAGE = ROOT / "reader" / "htpd_reader"
NAME_RE = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")


def front_matter(text: str) -> dict[str, str]:
    if not text.startswith("---\n"):
        raise SystemExit("SKILL.md must start with --- fields")
    head = text[4:text.index("\n---\n", 4)]
    out = {}
    for line in head.splitlines():
        key, _, value = line.partition(":")
        out[key.strip()] = value.strip()
    return out


def check_skill(skill_md: str) -> dict[str, str]:
    """The rules of the Claude app and of the Agent Skills format."""
    f = front_matter(skill_md)
    name, description = f.get("name", ""), f.get("description", "")
    problems = []
    if not NAME_RE.match(name) or len(name) > 64:
        problems.append(f"name {name!r}: lower-case letters, digits and '-', at most 64 characters")
    if name != SKILL.name:
        problems.append(f"name {name!r} must equal the folder name {SKILL.name!r}")
    if not description or len(description) > 200:
        problems.append(f"description must be 1 to 200 characters (it has {len(description)})")
    if "<" in description or ">" in description:
        problems.append("description must not contain < or >")
    if problems:
        raise SystemExit("SKILL.md: " + "; ".join(problems))
    return f


def build(out: Path) -> Path:
    skill_md = (SKILL / "SKILL.md").read_text(encoding="utf-8")
    check_skill(skill_md)
    out.parent.mkdir(parents=True, exist_ok=True)
    files: list[tuple[Path, str]] = [(SKILL / "SKILL.md", "htpd/SKILL.md"),
                                     (ROOT / "LICENSE", "htpd/LICENSE"), (ROOT / "NOTICE", "htpd/NOTICE"),
                                     (SKILL / "scripts" / "htpd.py", "htpd/scripts/htpd.py")]
    for py in sorted(PACKAGE.glob("*.py")):
        files.append((py, f"htpd/scripts/htpd_reader/{py.name}"))
    files.append((PACKAGE / "rerank.json", "htpd/scripts/htpd_reader/rerank.json"))  # the re-ranker's weights
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for src, arc in files:
            info = zipfile.ZipInfo(arc, date_time=(2026, 1, 1, 0, 0, 0))   # same bytes on every build
            info.external_attr = (0o755 if arc.endswith("htpd.py") else 0o644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, src.read_bytes())
    return out


if __name__ == "__main__":
    path = build(Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "dist" / "htpd-skill.zip")
    print(f"wrote {path} ({path.stat().st_size // 1024} kB)")
