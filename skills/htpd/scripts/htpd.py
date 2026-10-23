#!/usr/bin/env python3
"""HTPD reader for the htpd skill: answer questions from a .htpd file.

    python3 htpd.py ask      FILE "question" [--context] [--json]
    python3 htpd.py contents FILE [--depth N]
    python3 htpd.py section  FILE S14
    python3 htpd.py search   FILE words...
    python3 htpd.py figure   FILE F12 [-o F12.png]
    python3 htpd.py check    FILE
    python3 htpd.py mcp      [--bundles DIR ...]   # the MCP server (stdio)

Needs only Python 3.10 or newer. The package `htpd_reader` lies next to this
file in the skill (scripts/htpd_reader); in the HTPD repository it is found in
reader/.
"""

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_READER = HERE.parents[2] / "reader" if len(HERE.parents) > 2 else HERE
for candidate in (HERE, REPO_READER):
    if (candidate / "htpd_reader" / "__init__.py").is_file():
        sys.path.insert(0, str(candidate))
        break

if sys.version_info < (3, 10):
    sys.exit("htpd needs Python 3.10 or newer")


def run(argv: list[str]) -> int:
    if argv[:1] == ["mcp"]:
        from htpd_reader.mcp import main as mcp_main

        return mcp_main(argv[1:])
    from htpd_reader.cli import main

    return main(argv)


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
