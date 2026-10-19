"""Tests of phase 5: the decider, the Laya answer reading, the systems and the exports."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from htpd_reader import open_bundle
from htpd_reader.ask import Asker
from htpd_reader.decider import Decider, LayaScorer, sigmoid
from htpd_reader.retriever import Settings
from htpd_reader.tokens import plain_words

from model.export_laya import to_laya
from model.make_data import generate
from model.train_cross_encoder import auc

ROOT = Path(__file__).resolve().parents[2]
SAMPLE = ROOT / "examples" / "sample.htpd"


class WordScorer:
    """Probability = share of question words in the card; a stand-in for a model."""

    name = "words"

    def __init__(self, favourite: str | None = None):
        self.favourite = favourite

    def score(self, question, cards):
        q = set(plain_words(question))
        out = []
        for c in cards:
            p = 0.9 * len(q & set(plain_words(c))) / max(1, len(q))
            if self.favourite and self.favourite in c:
                p = 0.99
            out.append(p)
        return out


class DeciderTest(unittest.TestCase):
    def setUp(self) -> None:
        self.b = open_bundle(SAMPLE)

    def test_reorders_by_probability(self) -> None:
        d = Decider(WordScorer(favourite="3 Charging sequence > 3.2 Shutdown"))
        r = Asker(self.b, Settings(meaning=False), decider=d).ask("controller temperature current")
        self.assertEqual(r.ranked[0].title, "3.2 Shutdown")
        self.assertAlmostEqual(d.last_best, 0.99)

    def test_not_found_below_threshold(self) -> None:
        d = Decider(WordScorer(), not_found_below=0.5)
        a = Asker(self.b, Settings(meaning=False), decider=d)
        self.assertTrue(a.ask("What is the baud rate of the RS-485 port?").not_found)
        self.assertFalse(a.ask("contactor coil supply").not_found)
        self.assertFalse(a.ask("What does section 3.2 say about baud?").not_found)

    def test_keeps_candidates_beyond_the_scored_ones(self) -> None:
        a = Asker(self.b, Settings(meaning=False))
        plain = a.ask("controller").ranked
        d = Decider(WordScorer(), candidates=2)
        r = Asker(self.b, Settings(meaning=False), decider=d).ask("controller")
        self.assertEqual({n.id for n in r.ranked}, {n.id for n in plain})
        self.assertEqual([n.id for n in r.ranked[2:]], [n.id for n in plain[2:]])

    def test_laya_answer(self) -> None:
        self.assertEqual(LayaScorer.yes_probability({"choice": "yes", "confidence": 0.8}), 0.8)
        self.assertAlmostEqual(LayaScorer.yes_probability({"choice": "no", "confidence": 0.8}), 0.2)
        self.assertEqual(LayaScorer.yes_probability({"probabilities": {"yes": 0.3, "no": 0.7}}), 0.3)

    def test_sigmoid_and_auc(self) -> None:
        self.assertAlmostEqual(sigmoid(0.0), 0.5)
        self.assertEqual(sigmoid(-1000.0), 0.0)
        self.assertEqual(auc([0.9, 0.8, 0.1], [1, 1, 0]), 1.0)
        self.assertEqual(auc([0.1, 0.9], [1, 0]), 0.0)


class Systems(unittest.TestCase):
    def test_decider_systems_run(self) -> None:
        import eval.systems_decider  # noqa: F401
        from eval import harness
        from eval.questions import save
        from eval.systems import Context
        from eval.tests.test_eval import EXAMPLES, QUESTIONS, FakeEmbedder

        with tempfile.TemporaryDirectory() as t:
            t = Path(t)
            qf = t / "q.jsonl"
            save(qf, QUESTIONS)
            docs = t / "docs.list"
            docs.write_text("sample test - -\n")
            ctx = Context(embedder_factory=FakeEmbedder, scorer_factory=lambda name: WordScorer())
            for name in ("htpd-ce", "htpd-ce-trained", "htpd-laya"):
                m = harness.evaluate(name, "test", [qf], EXAMPLES, ctx, docs_list=docs, save=False)["metrics"]
                self.assertIsNotNone(m["recall@5"], name)


class Exports(unittest.TestCase):
    def test_laya_requests(self) -> None:
        with tempfile.TemporaryDirectory() as t:
            docs = Path(t) / "docs.list"
            docs.write_text("sample train - -\n")
            ex = generate({"sample": open_bundle(SAMPLE)}, docs_list=docs)
        seen = set()
        for e in ex:
            r = to_laya(e)
            (name, spec), = r["questions"].items()
            self.assertEqual(spec["type"], "choice")
            self.assertIn(r["answers"][name], spec["criteria"])
            self.assertIn(e["question"], r["state"] + spec["instructions"])
            json.dumps(r)
            seen.add(name)
        self.assertEqual(seen, {"relevant", "enough", "child"})


if __name__ == "__main__":
    unittest.main()
