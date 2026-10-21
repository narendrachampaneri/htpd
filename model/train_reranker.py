"""Train the re-ranker (reader/htpd_reader/rerank.py) on TRAINING documents only.

    python -m model.train_reranker eval/bundles/riscv-isa.htpd private/bundles/more.htpd ...
                                   [--out reader/htpd_reader/rerank.json]

For each question made by rules from a training bundle (model/make_data.py:
section titles, table cells, rare terms), the retriever finds its 20
candidates, and each candidate gets the features of rerank.py. A few small
neural networks (numpy only; one hidden layer of 16 units each, started from
different seeds) learn to give the right section the highest score among the
candidates of its question (a softmax over the candidates: the "listwise"
loss). Their scores are averaged, which keeps the result steady.

Questions whose right section is not among the 20 teach nothing and are left
out. Title questions count less: their answer is in the title, which makes
them too easy and would teach the model to trust titles too much.

Test and tuning documents are refused by name and by the SHA-256 of their PDF
(AGENTS.md rule 10). The settings (network size, which training manuals) were
chosen on the tuning split; the test split is measured once.
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
HIDDEN = 16
NETS = 5
EPOCHS = 60


class Data:
    def __init__(self) -> None:
        self.x: list[np.ndarray] = []  # one (candidates x features) array per question
        self.y: list[np.ndarray] = []  # 1 for the right section
        self.w: list[float] = []  # weight of the question


def examples(paths: list[Path]) -> Data:
    bundles = {p.name.removesuffix(".htpd"): open_bundle(p) for p in paths}
    refuse_test_documents(bundles)
    d = Data()
    for name, b in bundles.items():
        r = Retriever(b, Settings(meaning=False, rerank=False))
        feats = Features(r)
        qs = list(title_questions(b)) + list(table_questions(b)) + list(term_questions(b))
        used = 0
        for q in qs:
            cands = r.retrieve(q.question, k=KEEP).candidates
            labels = np.array([1.0 if c.node.id == q.gold else 0.0 for c in cands])
            if not labels.any() or len(cands) < 2:
                continue
            d.x.append(np.array(feats.of(q.question, cands)))
            d.y.append(labels)
            d.w.append(SOURCE_WEIGHT.get(q.source, 1.0))
            used += 1
        print(f"{name}: {used} of {len(qs)} questions have the right section among {KEEP}", file=sys.stderr)
    return d


def train_net(d: Data, idx: list[int], mean: np.ndarray, scale: np.ndarray, seed: int,
              hidden: int = HIDDEN, epochs: int = EPOCHS, lr: float = 3e-3, l2: float = 1e-4,
              batch: int = 64) -> dict[str, list]:
    rng = np.random.default_rng(seed)
    n = len(FEATURES)
    params = [rng.normal(0, 1 / np.sqrt(n), (n, hidden)), np.zeros(hidden), rng.normal(0, 1 / np.sqrt(hidden), hidden)]
    m1 = [np.zeros_like(p) for p in params]
    m2 = [np.zeros_like(p) for p in params]
    t = 0
    order = np.array(idx)
    for _ in range(epochs):
        rng.shuffle(order)
        for start in range(0, len(order), batch):
            qs = order[start:start + batch]
            grads = [np.zeros_like(p) for p in params]
            for q in qs:
                z = (d.x[q] - mean) / scale
                pre = z @ params[0] + params[1]
                h = np.maximum(pre, 0.0)
                o = h @ params[2]
                e = np.exp(o - o.max())
                go = (e / e.sum() - d.y[q] / d.y[q].sum()) * d.w[q] / len(qs)
                grads[2] += h.T @ go
                gh = np.outer(go, params[2]) * (pre > 0)
                grads[0] += z.T @ gh
                grads[1] += gh.sum(axis=0)
            t += 1
            for i, g in enumerate(grads):
                if i != 1:
                    g = g + l2 * params[i]
                m1[i] = 0.9 * m1[i] + 0.1 * g
                m2[i] = 0.999 * m2[i] + 0.001 * g * g
                params[i] -= lr * (m1[i] / (1 - 0.9 ** t)) / (np.sqrt(m2[i] / (1 - 0.999 ** t)) + 1e-8)
    return {"w1": [[round(float(v), 6) for v in row] for row in params[0]],
            "b1": [round(float(v), 6) for v in params[1]], "w2": [round(float(v), 6) for v in params[2]]}


def scores(nets: list[dict[str, list]], x: np.ndarray, mean: np.ndarray, scale: np.ndarray) -> np.ndarray:
    z = (x - mean) / scale
    return sum(np.maximum(z @ np.array(n["w1"]) + np.array(n["b1"]), 0) @ np.array(n["w2"]) for n in nets) / len(nets)


def top1(d: Data, idx: list[int], score) -> float:
    return sum(float(d.y[q][int(np.argmax(score(d.x[q])))] == 1) for q in idx) / max(len(idx), 1)


def train(d: Data, idx: list[int], nets: int = NETS) -> tuple[list[dict[str, list]], np.ndarray, np.ndarray]:
    allx = np.vstack([d.x[q] for q in idx])
    mean = allx.mean(axis=0)
    scale = allx.std(axis=0)
    scale[scale == 0] = 1.0
    return [train_net(d, idx, mean, scale, seed) for seed in range(nets)], mean, scale


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("bundles", nargs="+", type=Path)
    ap.add_argument("--out", type=Path, default=WEIGHTS)
    ap.add_argument("--nets", type=int, default=NETS)
    a = ap.parse_args(argv)
    d = examples(a.bundles)
    every = list(range(len(d.x)))
    # hold out one question in five to see that the model learned something general
    held = [q for q in every if q % 5 == 0]
    rest = [q for q in every if q % 5]
    nets, mean, scale = train(d, rest, a.nets)
    fused = FEATURES.index("fused_rank")
    print(f"held-out top-1: retriever {top1(d, held, lambda x: x[:, fused]):.3f}"
          f"  re-ranker {top1(d, held, lambda x: scores(nets, x, mean, scale)):.3f}", file=sys.stderr)
    nets, mean, scale = train(d, every, a.nets)
    out = {"features": list(FEATURES), "mean": [round(float(v), 6) for v in mean],
           "scale": [round(float(v), 6) for v in scale], "nets": nets,
           "trained_on": [p.name for p in a.bundles], "questions": len(every)}
    a.out.write_text(json.dumps(out) + "\n", encoding="utf-8")
    print(f"wrote {a.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
