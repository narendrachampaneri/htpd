"""Tests of the re-ranker: its weights load, it only re-orders, and it never
overrides a section the question names or the "not found" rule."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from htpd_reader import open_bundle
from htpd_reader.rerank import FEATURES, WEIGHTS, Features, Reranker
from htpd_reader.retriever import Retriever, Settings

ROOT = Path(__file__).resolve().parents[2]
SAMPLE = ROOT / "examples" / "sample.htpd"


class RerankTest(unittest.TestCase):
    def setUp(self) -> None:
        self.b = open_bundle(SAMPLE)
        self.on = Retriever(self.b, Settings())
        self.off = self.on.with_settings(rerank=False)

    def test_weights_ship_with_the_reader(self) -> None:
        data = json.loads(WEIGHTS.read_text(encoding="utf-8"))
        self.assertEqual(data["features"], list(FEATURES))
        self.assertTrue(data["nets"])
        for net in data["nets"]:
            self.assertEqual(len(net["w1"]), len(FEATURES))
        self.assertIsNotNone(Reranker.load())
        self.assertTrue(Settings().rerank)

    def test_one_number_per_feature(self) -> None:
        q = "What is the reset value of the READY bit?"
        cands = self.off.retrieve(q).candidates
        rows = Features(self.off).of(q, cands)
        self.assertEqual(len(rows), len(cands))
        self.assertTrue(all(len(r) == len(FEATURES) for r in rows))

    def test_only_reorders(self) -> None:
        for q in ("What is the reset value of the READY bit?", "How is the charger started?",
                  "Which register holds the error code?"):
            a = self.on.retrieve(q)
            b = self.off.retrieve(q)
            self.assertEqual(sorted(c.node.id for c in a.candidates), sorted(c.node.id for c in b.candidates))
            self.assertEqual((a.coverage, a.not_found, a.missing), (b.coverage, b.not_found, b.missing))

    def test_a_named_section_stays_first(self) -> None:
        numbered = [s for s in self.b.sections if s.number]
        self.assertTrue(numbered)
        s = numbered[-1]
        r = self.on.retrieve(f"What does section {s.number} say?")
        self.assertEqual(r.candidates[0].node.id, s.id)

    def test_not_used_without_word_search(self) -> None:
        tree_only = self.on.with_settings(words=False, numbers=False)
        r = tree_only.with_settings()
        r._reranker = Reranker({"features": list(FEATURES), "mean": [0.0] * len(FEATURES),
                                "scale": [1.0] * len(FEATURES),
                                "weights": [-1.0] * len(FEATURES), "bias": 0.0})  # would turn the order around
        q = "status register bits"
        self.assertGreater(len(tree_only.with_settings(rerank=False).retrieve(q).candidates), 1)
        self.assertEqual([c.node.id for c in r.retrieve(q).candidates],
                         [c.node.id for c in tree_only.with_settings(rerank=False).retrieve(q).candidates])

    def test_networks_and_logistic_weights_score_alike(self) -> None:
        n = len(FEATURES)
        base = {"features": list(FEATURES), "mean": [0.0] * n, "scale": [1.0] * n}
        logistic = Reranker({**base, "weights": [1.0] + [0.0] * (n - 1), "bias": 0.5})
        # one hidden unit that passes the first feature on (ReLU of a positive value)
        net = Reranker({**base, "nets": [{"w1": [[1.0]] + [[0.0]] * (n - 1), "b1": [0.5], "w2": [1.0]}]})
        rows = [[2.0] + [0.0] * (n - 1), [1.0] + [0.0] * (n - 1)]
        self.assertEqual(logistic.score(rows), [2.5, 1.5])
        self.assertEqual(net.score(rows), [2.5, 1.5])
        # the hidden unit is a ReLU: below zero it gives nothing
        self.assertEqual(net.score([[-3.0] + [0.0] * (n - 1)]), [0.0])
        with self.assertRaises(ValueError):
            Reranker({**base, "nets": [{"w1": [[1.0, 2.0]] * n, "b1": [0.0], "w2": [1.0]}]})

    def test_without_weights_the_fused_order_stays(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            self.assertIsNone(Reranker.load(Path(d) / "missing.json"))
            bad = Path(d) / "bad.json"
            bad.write_text(json.dumps({"features": ["other"], "weights": [1], "bias": 0, "mean": [0], "scale": [1]}))
            self.assertIsNone(Reranker.load(bad))
        r = self.off.with_settings(rerank=True)
        r._reranker = None  # as when rerank.json is missing
        q = "How is the charger started?"
        self.assertEqual([c.node.id for c in r.retrieve(q).candidates],
                         [c.node.id for c in self.off.retrieve(q).candidates])


if __name__ == "__main__":
    unittest.main()
