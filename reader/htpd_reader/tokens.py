"""Tokenizers and the one token count used for every number in the report.

* `plain_words`: lower-case runs of letters, digits and `_` - what a
  simple search uses (the baselines).
* `technical_tokens`: keeps the words of technical manuals whole and also
  gives their parts: register and field names (`dmcontrol`,
  `__arm_sme_state` -> also `arm`, `sme`, `state`), hex numbers (`0x7a1`),
  section numbers (`3.14.2`), CamelCase (`DebugModule` -> also `debug`,
  `module`), the short forms of tables (`maximum` -> also `max`), and
  words broken at a hyphen (`inter-rupt` -> also `interrupt`).
* `count_tokens`: the number of words and punctuation marks. It is not the
  tokenizer of any one AI model; it is one fixed measure, so the numbers of
  all systems can be compared with each other.
"""

from __future__ import annotations

import re
import unicodedata

_PLAIN = re.compile(r"\w+", re.UNICODE)
_TECH = re.compile(
    r"0[xX][0-9A-Fa-f_]+"  # hex number
    r"|\d+(?:\.\d+)+"  # 3.14.2, 1.0
    r"|[A-Za-z_][A-Za-z0-9_]*"  # identifiers and words
    r"|\d+"
)
_CAMEL = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z]+|\d+")
_COUNT = re.compile(r"\w+|[^\w\s]", re.UNICODE)

# Words that tables of technical manuals shorten: the long form also gives the short one.
SHORT_FORMS = {"maximum": "max", "minimum": "min", "typical": "typ", "registers": "register",
               "address": "addr", "addresses": "addr"}

STOPWORDS = frozenset(
    "a an and are as at be by can do does for from has have how i if in is it its of on or "
    "that the this to was what when where which who why will with".split()
)


_HYPHENATED = re.compile(r"\b([A-Za-z]+)-([A-Za-z0-9]+)\b")


def fold(text: str) -> str:
    """The text as search sees it: compatibility forms made plain (NFKC), so a
    raised or lowered number of the file (I²C, 2³¹, x₀) matches the plain one of
    a question (I2C, 2^31, x0), and a ligature matches its letters."""
    return text if text.isascii() else unicodedata.normalize("NFKC", text)


def main_tokens(text: str) -> list[str]:
    """The whole technical words of a text, lower case, without their parts."""
    return [m.group(0).lower() for m in _TECH.finditer(fold(text))]


def plain_words(text: str) -> list[str]:
    return [w.lower() for w in _PLAIN.findall(fold(text))]


def technical_tokens(text: str, drop_stopwords: bool = False) -> list[str]:
    text = fold(text)
    out: list[str] = []
    for m in _TECH.finditer(text):
        raw = m.group(0)
        tok = raw.lower()
        if drop_stopwords and tok in STOPWORDS:
            continue
        out.append(tok)
        if tok in SHORT_FORMS:
            out.append(SHORT_FORMS[tok])
        if tok.startswith("0x"):
            continue
        if "_" not in raw and (raw.islower() or raw.isupper() or raw[1:].islower()):
            continue  # most words: no parts (fast path; same result)
        parts: list[str] = []
        if "_" in raw.strip("_"):
            parts = [p.lower() for p in raw.split("_") if p]
        elif any(c.isupper() for c in raw[1:]) and any(c.islower() for c in raw):
            parts = [p.lower() for p in _CAMEL.findall(raw)]
        if len(parts) > 1:
            out.extend(p for p in parts if p != tok)
    # A word the PDF broke at a line end keeps its hyphen in the text
    # ("inter-rupt", "RS-485"); the joined form is searched as well.
    for m in _HYPHENATED.finditer(text):
        out.append((m.group(1) + m.group(2)).lower())
    return out


def count_tokens(text: str) -> int:
    return len(_COUNT.findall(text))
