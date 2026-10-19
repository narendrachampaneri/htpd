"""A small YAML reader for the fields HTPD writes, with the standard library only.

PyYAML is used when it is installed. Where it is not - the code sandbox of an
AI chat, a bare Python - this reads the part of YAML that the converter writes
(docs/FORMAT.md section 3): mappings by indentation, lists of scalars or of
mappings, double-quoted strings, plain words and numbers, and the flow forms
`[a, b]`, `[]` and `{ key: value, ... }`. Anything else is an error, never a
guess.
"""

from __future__ import annotations

import re
from typing import Any


class MiniYAMLError(ValueError):
    def __init__(self, message: str, line: int | None = None):
        super().__init__(message)
        self.problem = message
        self.line = line


_INT = re.compile(r"^[-+]?\d+$")
_FLOAT = re.compile(r"^[-+]?(\d+\.\d*|\.\d+|\d+)([eE][-+]?\d+)?$")
_ESCAPES = {'"': '"', "\\": "\\", "/": "/", "n": "\n", "r": "\r", "t": "\t", "0": "\0", "b": "\b", "f": "\f",
            "e": "\x1b", " ": " ", "N": "\x85", "_": "\xa0", "L": " ", "P": " "}


def _quoted(s: str, line: int) -> tuple[str, str]:
    """A double-quoted string at the start of s: (value, rest after it)."""
    out: list[str] = []
    i = 1
    while i < len(s):
        c = s[i]
        if c == '"':
            return "".join(out), s[i + 1:]
        if c == "\\":
            if i + 1 >= len(s):
                break
            e = s[i + 1]
            if e in _ESCAPES:
                out.append(_ESCAPES[e])
                i += 2
                continue
            width = {"x": 2, "u": 4, "U": 8}.get(e)
            if width is None:
                raise MiniYAMLError(f"unknown escape \\{e}", line)
            digits = s[i + 2:i + 2 + width]
            if len(digits) != width or not all(ch in "0123456789abcdefABCDEF" for ch in digits):
                raise MiniYAMLError(f"bad escape \\{e}{digits}", line)
            out.append(chr(int(digits, 16)))
            i += 2 + width
            continue
        out.append(c)
        i += 1
    raise MiniYAMLError("a quoted string is not closed", line)


def _plain(s: str) -> Any:
    if s in ("null", "~", ""):
        return None
    if s in ("true", "True"):
        return True
    if s in ("false", "False"):
        return False
    if _INT.match(s):
        return int(s)
    if _FLOAT.match(s):
        return float(s)
    return s


def _flow_items(s: str, line: int) -> list[str]:
    """Split the inside of [..] or {..} at top-level commas."""
    items, depth, cur, i = [], 0, [], 0
    while i < len(s):
        c = s[i]
        if c == '"':
            _, rest = _quoted(s[i:], line)
            j = len(s) - len(rest)
            cur.append(s[i:j])
            i = j
            continue
        if c in "[{":
            depth += 1
        elif c in "]}":
            depth -= 1
        if c == "," and depth == 0:
            items.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    if "".join(cur).strip():
        items.append("".join(cur).strip())
    return items


def _value(s: str, line: int) -> Any:
    s = s.strip()
    if s.startswith('"'):
        v, rest = _quoted(s, line)
        if rest.strip():
            raise MiniYAMLError(f"text after a quoted string: {rest.strip()!r}", line)
        return v
    if s.startswith("["):
        if not s.endswith("]"):
            raise MiniYAMLError("a [ list ] is not closed", line)
        return [_value(x, line) for x in _flow_items(s[1:-1], line)]
    if s.startswith("{"):
        if not s.endswith("}"):
            raise MiniYAMLError("a { mapping } is not closed", line)
        out: dict[str, Any] = {}
        for item in _flow_items(s[1:-1], line):
            k, sep, v = item.partition(":")
            if not sep:
                raise MiniYAMLError(f"no ':' in {item!r}", line)
            out[k.strip()] = _value(v, line)
        return out
    if s.startswith(("'", "&", "*", "!", "|", ">")):
        raise MiniYAMLError(f"this YAML form is not used by HTPD: {s[:20]!r}", line)
    return _plain(s)


def _split_key(text: str, line: int) -> tuple[str, str]:
    if text.startswith('"'):
        key, rest = _quoted(text, line)
        rest = rest.lstrip()
        if not rest.startswith(":"):
            raise MiniYAMLError("no ':' after a quoted key", line)
        return key, rest[1:]
    m = re.match(r"^([^:#]+?):(\s|$)(.*)$", text)
    if not m:
        raise MiniYAMLError(f"expected 'key: value', got {text!r}", line)
    return m.group(1).strip(), m.group(3)


def load(text: str) -> Any:
    lines = [(i + 1, ln) for i, ln in enumerate(text.split("\n")) if ln.strip() and not ln.lstrip().startswith("#")]
    for n, ln in lines:
        if "\t" in ln[: len(ln) - len(ln.lstrip())]:
            raise MiniYAMLError("a tab in the indentation", n)
    if not lines:
        return None
    value, pos = _block(lines, 0, _indent(lines[0][1]))
    if pos != len(lines):
        raise MiniYAMLError("unexpected indentation", lines[pos][0])
    return value


def _indent(s: str) -> int:
    return len(s) - len(s.lstrip(" "))


def _block(lines: list[tuple[int, str]], pos: int, indent: int) -> tuple[Any, int]:
    first = lines[pos][1].strip()
    if first.startswith("- ") or first == "-":
        return _list(lines, pos, indent)
    return _mapping(lines, pos, indent)


def _mapping(lines: list[tuple[int, str]], pos: int, indent: int) -> tuple[dict[str, Any], int]:
    out: dict[str, Any] = {}
    while pos < len(lines):
        n, raw = lines[pos]
        ind = _indent(raw)
        if ind < indent:
            break
        if ind > indent:
            raise MiniYAMLError("unexpected indentation", n)
        text = raw.strip()
        if text.startswith("- "):
            raise MiniYAMLError("a list item where a key was expected", n)
        key, rest = _split_key(text, n)
        pos += 1
        if rest.strip():
            out[key] = _value(rest, n)
        elif pos < len(lines) and (_indent(lines[pos][1]) > indent or
                                   (_indent(lines[pos][1]) == indent and lines[pos][1].strip().startswith("- "))):
            out[key], pos = _block(lines, pos, _indent(lines[pos][1]))
        else:
            out[key] = None
    return out, pos


def _list(lines: list[tuple[int, str]], pos: int, indent: int) -> tuple[list[Any], int]:
    out: list[Any] = []
    while pos < len(lines):
        n, raw = lines[pos]
        ind = _indent(raw)
        text = raw.strip()
        if ind < indent or (ind == indent and not (text.startswith("- ") or text == "-")):
            break
        if ind > indent:
            raise MiniYAMLError("unexpected indentation", n)
        item = text[1:].strip()
        if not item:
            pos += 1
            value, pos = _block(lines, pos, _indent(lines[pos][1])) if pos < len(lines) else (None, pos)
            out.append(value)
            continue
        if re.match(r'^("[^"]*"|[^"\[{][^:]*):(\s|$)', item) and not item.startswith('"') or \
                (item.startswith('"') and re.match(r'^"(?:[^"\\]|\\.)*"\s*:(\s|$)', item)):
            # "- key: value" starts a mapping; its other keys follow, indented to the key
            inner = ind + (len(text) - len(item))
            sub = [(n, " " * inner + item)]
            j = pos + 1
            while j < len(lines) and _indent(lines[j][1]) >= inner and not (
                    _indent(lines[j][1]) == ind and lines[j][1].strip().startswith("- ")):
                sub.append(lines[j])
                j += 1
            value, used = _mapping(sub, 0, inner)
            if used != len(sub):
                raise MiniYAMLError("unexpected indentation", sub[used][0])
            out.append(value)
            pos = j
            continue
        out.append(_value(item, n))
        pos += 1
    return out, pos
