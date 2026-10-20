"""Train the re-ranker (reader/htpd_reader/rerank.py) on the TRAINING documents only.

    python -m model.train_reranker eval/bundles/riscv-isa.htpd [--out reader/htpd_reader/rerank.json]

For each question made by rules from a training bundle (model/make_data.py:
section titles, table cells, rare terms), the retriever finds its 20
candidates; each candidate gets the features of rerank.py and the label
"is the right section". A logistic model (numpy, no other package) learns one
weight per feature. Questions whose right section is not among the 20 teach
nothing and are left out; every question has the same total weight, and
title questions count less (their answer is in the title, which makes them
too easy and would teach the model to trust titles too much).

Test and tuning documents are refused (AGENTS.md rule 10). Whether the
weights are kept is decided on the tuning split (`python -m eval tune`) and
measured once on the test split.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

from htpd_reader import open_bundle
from htpd_reader.rerank import FEATURES, WEIGHTS, Features
from htpd_reader.retriever import Retriever, Settings

from .make_data import refuse_test_documents, table_questions, term_questions, title_questions

SOURCE_WEIGHT = {"title": 0.3, "table": 1.0, "term": 1.0}
KEEP = 20


def examples(paths: list[Path]) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    bundles = {p.name.removesuffix(".htpd"): open_bundle(p) for p in paths}
    refuse_test_documents(bundles)
    xs, ys, ws, groups = [], [], [], []
    g = 0
    for name, b in bundles.items():
        r = Retriever(b, Settings(meaning=False))
        feats = Features(r)
        qs = list(title_questions(b)) + list(table_questions(b)) + list(term_questions(b))
        used = 0
        for q in qs:
            cands = r.retrieve(q.question, k=KEEP).candidates
            labels = [1.0 if c.node.id == q.gold else 0.0 for c in cands]
            if not any(labels) or len(cands) < 2:
                continue
            rows = feats.of(q.question, cands)
            pos = sum(labels)
            for row, y in zip(rows, labels):
                xs.append(row)
                ys.append(y)
                # half the question's weight on its right section, half on the others
                ws.append(SOURCE_WEIGHT.get(q.source, 1.0) * (0.5 / pos if y else 0.5 / (len(rows) - pos)))
                groups.append(g)
            g += 1
            used += 1
        print(f"{name}: {used} of {len(qs)} questions have the right section among {KEEP}", file=sys.stderr)
    return np.array(xs), np.array(ys), np.array(ws), np.array(groups)


def fit(x: np.ndarray, y: np.ndarray, w: np.ndarray, l2: float = 0.1, steps: int = 3000,
        lr: float = 0.5) -> tuple[np.ndarray, float, np.ndarray, np.ndarray]:
    mean = x.mean(axis=0)
    scale = x.std(axis=0)
    scale[scale == 0] = 1.0
    z = (x - mean) / scale
    wn = w / w.sum()
    beta = np.zeros(z.shape[1])
    bias = 0.0
    for _ in range(steps):
        p = 1.0 / (1.0 + np.exp(-(z @ beta + bias)))
        err = (p - y) * wn
        beta -= lr * (z.T @ err + l2 * beta)
        bias -= lr * err.sum()
    return beta, bias, mean, scale


def top1(x: np.ndarray, y: np.ndarray, groups: np.ndarray, beta: np.ndarray, bias: float,
         mean: np.ndarray, scale: np.ndarray) -> float:
    s = ((x - mean) / scale) @ beta + bias
    hits, n = 0, 0
    for g in np.unique(groups):
        m = groups == g
        hits += int(y[m][int(np.argmax(s[m]))] == 1)
        n += 1
    return hits / max(n, 1)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("bundles", nargs="+", type=Path)
    ap.add_argument("--out", type=Path, default=WEIGHTS)
    ap.add_argument("--l2", type=float, default=0.1)  # chosen on the tuning split
    a = ap.parse_args(argv)
    x, y, w, groups = examples(a.bundles)
    # hold out one question in five to see that the model learned something general
    held = groups % 5 == 0
    beta, bias, mean, scale = fit(x[~held], y[~held], w[~held], a.l2)
    base = np.zeros(len(FEATURES))
    base[FEATURES.index("fused_rank")] = 1.0
    print(f"held-out top-1: retriever {top1(x[held], y[held], groups[held], base, 0.0, 0 * mean, 1 + 0 * scale):.3f}"
          f"  re-ranker {top1(x[held], y[held], groups[held], beta, bias, mean, scale):.3f}", file=sys.stderr)
    beta, bias, mean, scale = fit(x, y, w, a.l2)
    out = {"features": list(FEATURES), "weights": [round(float(v), 6) for v in beta], "bias": round(float(bias), 6),
           "mean": [round(float(v), 6) for v in mean], "scale": [round(float(v), 6) for v in scale],
           "trained_on": [p.name for p in a.bundles], "questions": int(len(np.unique(groups)))}
    a.out.write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8")
    for name, v in sorted(zip(FEATURES, beta), key=lambda t: -abs(t[1])):
        print(f"  {name:13s} {v:+.3f}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
