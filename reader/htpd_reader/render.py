"""Write blocks back as the `# Text` part of a section file.

This follows the rules of FORMAT.md section 4 (the same rules the converter
uses). The reader's tests use it to prove that reading loses nothing: reading
a file and writing its blocks again gives the same bytes.
"""

from __future__ import annotations

from .bundle import NO_TEXT_LINE, Block


def starts_markdown_block(text: str) -> bool:
    """True when a paragraph that starts like `text` gets one backslash (4.3)."""
    if not text:
        return False
    first = text[0]
    if first in "#>\\":
        return True
    if text.startswith("```") or text.startswith("~~~"):
        return True
    if first == "<" and len(text) > 1:
        nxt = text[1]
        return nxt.isascii() and nxt.isalpha() or nxt in "/!?"
    if first == "[":
        close = text.find("]")
        return close != -1 and close + 1 < len(text) and text[close + 1] == ":"
    if first in "-*_":
        marks = 0
        for c in text:
            if c == first:
                marks += 1
            elif c != " ":
                return False
        return marks >= 3
    return False


def longest_backtick_run(text: str) -> int:
    longest = run = 0
    for c in text:
        run = run + 1 if c == "`" else 0
        longest = max(longest, run)
    return longest


def fence_for(text: str) -> str:
    return "`" * max(3, longest_backtick_run(text) + 1)


def render_text_part(blocks: list[Block]) -> str:
    """The `# Text` part, from its heading line up to the next part."""
    out = ["# Text\n\n"]
    if not blocks:
        out.append(NO_TEXT_LINE + "\n\n")
    page = None
    for b in blocks:
        if b.page != page:
            page = b.page
            out.append(f"<!-- page {page} -->\n\n")
        if b.kind == "paragraph":
            out.append(("\\" if starts_markdown_block(b.text) else "") + b.text + "\n\n")
        else:
            fence = fence_for(b.text)
            mark = "text" if b.kind == "table" else ""
            out.append(f"{fence}{mark}\n{b.text}\n{fence}\n\n")
    return "".join(out)
