"""Scores the reading order of an .htpd file against the known order of a generated PDF (make_layouts.py).

    score_layout(truth, units, tokens, align) -> dict of numbers

`units` are the blocks of the text of the file in file order (`Htpd.units`: paragraphs, blocks of
rows or code, headings), each a list of (page, line). `truth` is the <case>.truth.json of the PDF:
the paragraphs of every page in the order a person reads them. `tokens` and `align` are the word
rule and the longest-common-subsequence of run_bench.py (passed in, so there is no import cycle).

    lay_words / lay_order    words of the truth, and how many of them are in the file on their page in the
                             same order (a longest common subsequence)
    lay_extra                words of the file on a page that are not part of that match (a word out of order,
                             a word that is not in the truth)
    lay_pages / lay_pages_ok pages of the truth, and pages whose words are all in order with no extra word
    lay_paras / lay_paras_ok paragraphs of the truth, and those found whole: all their words one after the
                             other in one block of the file
    lay_split                paragraphs whose words stand in two runs or more (in two blocks, or cut by other text)
    lay_joined               blocks of the file that hold words of two paragraphs or more

A heading made up by the converter for a document without headings ("Page 3", "Front matter") is not
text of the document and is not counted.
"""

from __future__ import annotations

import re
from collections import Counter

SYNTHETIC = re.compile(r"^(?:Front matter|Pages? \d+(?:\s*[-–]\s*\d+)?)$")


def contiguous(small: list[str], big: list[str]) -> bool:
    n = len(small)
    if n == 0:
        return True
    first = small[0]
    for i in range(len(big) - n + 1):
        if big[i] == first and big[i:i + n] == small:
            return True
    return False


def pieces_of(words: list[str], blocks: list[list[str]]) -> int:
    """In how many runs the words of a paragraph stand in the blocks of the file: from each word the longest
    run that is one after the other in a block (a single stray word is not a run)."""
    pieces = 0
    i = 0
    while i < len(words):
        best = 0
        for b in blocks:
            for j, w in enumerate(b):
                if w != words[i]:
                    continue
                k = 0
                while i + k < len(words) and j + k < len(b) and b[j + k] == words[i + k]:
                    k += 1
                best = max(best, k)
        if best >= 2:
            pieces += 1
        i += max(best, 1)
    return pieces


def score_layout(truth: dict, units, tokens, align) -> dict:
    s: Counter = Counter()
    for page in truth["pages"]:
        number = page["page"]
        paras = page["paragraphs"]
        T: list[str] = []
        tp: list[int] = []
        for k, para in enumerate(paras):
            words = tokens(para["text"])
            T += words
            tp += [k] * len(words)
        O: list[str] = []
        ou: list[int] = []
        blocks: list[list[str]] = []   # the words of each unit on this page
        for kind, lines in units:
            if kind == "t" and SYNTHETIC.match(lines[0][1]):
                continue
            words = [w for p, line in lines if p == number for w in tokens(line)]
            if not words:
                continue
            O += words
            ou += [len(blocks)] * len(words)
            blocks.append(words)
        in_t, in_o = align(T, O)
        s["lay_words"] += len(T)
        s["lay_order"] += sum(in_t)
        s["lay_extra"] += len(O) - sum(in_o)
        s["lay_pages"] += 1
        s["lay_pages_ok"] += int(all(in_t) and all(in_o))
        pairs = [(i, j) for i, j in zip((i for i, m in enumerate(in_t) if m), (j for j, m in enumerate(in_o) if m))]
        by_para: dict[int, set[int]] = {}
        by_unit: dict[int, set[int]] = {}
        for i, j in pairs:
            by_para.setdefault(tp[i], set()).add(ou[j])
            by_unit.setdefault(ou[j], set()).add(tp[i])
        s["lay_paras"] += len(paras)
        for k, para in enumerate(paras):
            words = tokens(para["text"])
            s["lay_paras_ok"] += int(any(contiguous(words, b) for b in blocks))
            s["lay_split"] += int(pieces_of(words, blocks) > 1)
        s["lay_joined"] += sum(1 for v in by_unit.values() if len(v) > 1)
    return dict(s)
