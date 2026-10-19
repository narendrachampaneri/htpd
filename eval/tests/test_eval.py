"""Tests of phase 2: tokenizers, BM25, question checks, baselines, harness, report."""

from __future__ import annotations

import hashlib
import json
import math
import tempfile
import unittest
from pathlib import Path

from htpd_reader import open_bundle
from htpd_reader.bm25 import BM25
from htpd_reader.tokens import count_tokens, plain_words, technical_tokens

from eval import harness
from eval.questions import Question, load, save, validate
from eval.systems import REGISTRY, Context, chunk_units, section_units, whole_text

ROOT = Path(__file__).resolve().parents[2]
EXAMPLES = ROOT / "examples"
SAMPLE = EXAMPLES / "sample.htpd"
LIMITS = "sections/0008-2-1-1-limits"
DERATE = "sections/0011-3-1-thermal-derating"
SUPPLY = "sections/0007-2-1-supply"
STATUS = "sections/0014-a-1-status-register"


def q(id_: str, kind: int, question: str, answer, gold, pages, bundle: str = "sample") -> Question:
    return Question(id_, bundle, kind, question, answer, gold, pages)


QUESTIONS = [
    q("s1", 1, "At which temperature must the controller reduce the charging current?",
      "If the temperature exceeds 85 °C", [DERATE], [3]),
    q("s2", 2, "What is the maximum bus voltage?", "Bus voltage | 200 | 400 | 480 | V", [LIMITS], [2]),
    q("s3", 2, "What is the reset value of the READY bit?", "0 | READY | R | 0", [STATUS], [5]),
    q("s4", 3, "What are the logic supply voltage and its maximum?",
      ["The logic supply is 3.3 V.", "Logic supply voltage | 3.0 | 3.3 | 3.6 | V"], [SUPPLY, LIMITS], [2]),
    q("s5", 4, "What is the baud rate of the RS-485 port?", "", [], []),
]


class FakeEmbedder:
    """Bag of hashed words: enough to test the plumbing of meaning search."""

    name = "fake"

    def encode(self, texts, query=False):
        out = []
        for t in texts:
            v = [0.0] * 64
            for w in plain_words(t):
                v[int(hashlib.md5(w.encode()).hexdigest(), 16) % 64] += 1.0
            n = math.sqrt(sum(x * x for x in v)) or 1.0
            out.append([x / n for x in v])
        return out


class Tokens(unittest.TestCase):
    def test_technical(self) -> None:
        t = technical_tokens("Write 0x7A1 to dmcontrol.ndmreset (see 3.14.2) and __arm_sme_state, DebugModule")
        for want in ("0x7a1", "dmcontrol", "ndmreset", "3.14.2", "__arm_sme_state", "arm", "sme", "state",
                     "debugmodule", "debug", "module"):
            self.assertIn(want, t)
        self.assertNotIn("3", t)
        self.assertNotIn("the", technical_tokens("the reset value", drop_stopwords=True))
        self.assertEqual(technical_tokens("Maximum"), ["maximum", "max"])

    def test_plain_and_count(self) -> None:
        self.assertEqual(plain_words("Bus voltage | 200"), ["bus", "voltage", "200"])
        self.assertEqual(count_tokens("a, b."), 4)


class Bm25(unittest.TestCase):
    def test_ranking(self) -> None:
        idx = BM25([["reset", "value"], ["bus", "voltage", "bus"], ["voltage"]])
        self.assertEqual([i for i, _ in idx.top(["bus"])], [1])
        top = idx.top(["voltage", "bus"])
        self.assertEqual(top[0][0], 1)
        self.assertEqual(idx.top(["nothing"]), [])
        # a rare word weighs more than a common one
        idx = BM25([["a", "x"], ["a", "y"], ["a", "x"]])
        self.assertGreater(idx.scores(["y"])[1], idx.scores(["a"])[1])


class Validate(unittest.TestCase):
    def setUp(self) -> None:
        self.b = open_bundle(SAMPLE)

    def test_good_questions(self) -> None:
        for x in QUESTIONS:
            self.assertEqual(validate(self.b, x), [], x.id)

    def test_answer_not_in_section(self) -> None:
        self.assertTrue(validate(self.b, q("x", 1, "?", "exceeds 95 °C", [DERATE], [3])))
        self.assertTrue(validate(self.b, q("x", 1, "?", "If the temperature exceeds 85 °C", [LIMITS], [2])))

    def test_wrong_page(self) -> None:
        why = validate(self.b, q("x", 1, "?", "If the temperature exceeds 85 °C", [DERATE], [4]))
        self.assertTrue(any("gold pages" in w for w in why))

    def test_kinds(self) -> None:
        self.assertTrue(validate(self.b, q("x", 3, "?", ["The logic supply is 3.3 V."], [SUPPLY], [2])))
        self.assertTrue(validate(self.b, q("x", 3, "?", ["The logic supply is 3.3 V.", "contactor coil supply"],
                                           [SUPPLY, LIMITS], [2])))
        self.assertTrue(validate(self.b, q("x", 2, "?", "If the temperature exceeds 85 °C", [DERATE], [3])))
        self.assertTrue(validate(self.b, q("x", 4, "?", "", [DERATE], [3])))
        self.assertTrue(validate(self.b, q("x", 1, "?", "x", ["sections/9999-gone"], [1])))
        self.assertTrue(validate(self.b, q("x", 7, "?", "x", [DERATE], [3])))

    def test_spaces_and_case_do_not_matter(self) -> None:
        self.assertEqual(validate(self.b, q("x", 1, "?", "if the  temperature\nexceeds 85 °c", [DERATE], [3])), [])

    def test_load_and_save(self) -> None:
        with tempfile.TemporaryDirectory() as t:
            p = Path(t) / "q.jsonl"
            QUESTIONS[0].extra["note"] = "kept"
            try:
                save(p, QUESTIONS)
                back = load([p])
            finally:
                QUESTIONS[0].extra.clear()
            self.assertEqual([x.id for x in back], [x.id for x in QUESTIONS])
            self.assertEqual(back[0].extra, {"note": "kept"})
            p.write_text(p.read_text() + p.read_text().split("\n")[0] + "\n")
            with self.assertRaises(ValueError):
                load([p])


class Units(unittest.TestCase):
    def setUp(self) -> None:
        self.b = open_bundle(SAMPLE)

    def test_chunks_keep_all_text_in_order(self) -> None:
        chunks = chunk_units(self.b, size=60)
        self.assertGreater(len(chunks), 3)
        # every character but spaces is there, once, in the order of the document
        want = "".join("".join(x.split()) for s in self.b.sections for x in [s.title, *(b.text for b in s.blocks)])
        got = "".join("".join(c.text.split()) for c in chunks)
        self.assertEqual(got, want)
        # a block that fits stays whole, with its line ends
        joined = "\n\n".join(c.text for c in chunks)
        self.assertIn(self.b.find("A.1").blocks[1].text, joined)
        for c in chunks[:-1]:
            self.assertGreaterEqual(count_tokens(c.text), 60)

    def test_long_block_is_split_without_loss(self) -> None:
        chunks = chunk_units(self.b, size=20)
        want = "".join("".join(x.split()) for s in self.b.sections for x in [s.title, *(b.text for b in s.blocks)])
        self.assertEqual("".join("".join(c.text.split()) for c in chunks), want)
        table = [c.text for c in chunks if "Bit | Name" in c.text][0]
        self.assertIn("Bit | Name | Access | Reset | Description\n0 | READY", table)

    def test_sections_and_whole(self) -> None:
        units = section_units(self.b)
        self.assertEqual(len(units), len(self.b))
        self.assertTrue(whole_text(self.b).startswith("Front matter"))


class Harness(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        t = Path(self._tmp.name)
        self.qfile = t / "q.jsonl"
        save(self.qfile, QUESTIONS)
        self.docs = t / "docs.list"
        self.docs.write_text("# test\nsample test - -\n")
        self.results = t / "results"
        self.ctx = Context(embedder_factory=FakeEmbedder)

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def ev(self, name: str) -> dict:
        return harness.evaluate(name, "test", [self.qfile], EXAMPLES, self.ctx,
                                docs_list=self.docs, results_dir=self.results)

    def test_b3_finds_the_sections(self) -> None:
        m = self.ev("B3")["metrics"]
        self.assertEqual((m["questions"], m["answerable"], m["not_in_document"]), (5, 4, 1))
        self.assertGreaterEqual(m["recall@5"], 0.75)
        self.assertEqual(m["not_found_accuracy"], 0.0)  # BM25 alone never says not found
        self.assertLess(m["tokens_vs_whole"], 1.0)

    def test_b0_is_the_whole_document(self) -> None:
        m = self.ev("B0")["metrics"]
        self.assertIsNone(m["recall@5"])
        self.assertAlmostEqual(m["tokens_vs_whole"], 1.0)

    def test_b1_and_b2_run(self) -> None:
        for name in ("B1", "B2"):
            m = self.ev(name)["metrics"]
            self.assertIsNotNone(m["recall@10"], name)
            self.assertIsNotNone(m["page_accuracy"], name)

    def test_report_is_written_from_results(self) -> None:
        for name in ("B0", "B3"):
            self.ev(name)
        out = Path(self._tmp.name) / "report.md"
        text = harness.write_report(self.results, out)
        self.assertEqual(out.read_text(), text)
        self.assertIn("## Split: test", text)
        self.assertIn("| B3 HTPD sections, BM25 | 5 |", text)
        self.assertIn("| B0 whole document | 5 | - |", text)

    def test_splits(self) -> None:
        docs = {"a": "test", "b": "train"}
        self.assertEqual(harness.split_of(q("x", 1, "?", "", [], [], bundle="a"), docs), "test")
        splits = {harness.split_of(q(f"b-{i}", 1, "?", "", [], [], bundle="b"), docs) for i in range(40)}
        self.assertEqual(splits, {"train", "tune"})
        self.assertEqual(harness.split_of(q("x", 1, "?", "", [], [], bundle="c"), docs), "unknown")

    def test_all_baselines_registered(self) -> None:
        self.assertLessEqual({"B0", "B1", "B2", "B3"}, set(REGISTRY))

    def test_results_file(self) -> None:
        self.ev("B3")
        data = json.loads((self.results / "B3-test.json").read_text())
        self.assertEqual(data["system"], "B3")
        self.assertIn("misses", data)


class CommandLine(unittest.TestCase):
    def test_systems_lists_baselines(self) -> None:
        import contextlib
        import io

        from eval.__main__ import main

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(main(["systems"]), 0)
        self.assertIn("B3", out.getvalue())


if __name__ == "__main__":
    unittest.main()


class Phase3Systems(unittest.TestCase):
    def setUp(self) -> None:
        import eval.systems_htpd  # noqa: F401  (registers the htpd systems)

        self._tmp = tempfile.TemporaryDirectory()
        t = Path(self._tmp.name)
        self.t = t
        self.ctx = Context(embedder_factory=FakeEmbedder, cache_dir=t / "cache")

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def test_htpd_systems_run_and_beat_whole_document_tokens(self) -> None:
        qfile = self.t / "q.jsonl"
        save(qfile, QUESTIONS)
        docs = self.t / "docs.list"
        docs.write_text("sample test - -\n")
        for name in ("htpd", "htpd-words", "htpd-no-tree", "htpd-plain", "htpd-no-words"):
            m = harness.evaluate(name, "test", [qfile], EXAMPLES, self.ctx, docs_list=docs,
                                 results_dir=self.t / "results")["metrics"]
            self.assertIsNotNone(m["recall@5"], name)
            self.assertLess(m["tokens_vs_whole"], 1.0, name)
        htpd = harness.evaluate("htpd-words", "test", [qfile], EXAMPLES, self.ctx, docs_list=docs, save=False)
        b3 = harness.evaluate("B3", "test", [qfile], EXAMPLES, self.ctx, docs_list=docs, save=False)
        self.assertGreaterEqual(htpd["metrics"]["mrr"], b3["metrics"]["mrr"])

    def test_tune_writes_a_threshold(self) -> None:
        docs = self.t / "docs.list"
        docs.write_text("sample train - -\n")
        ids = [f"t{i}" for i in range(400) if int(hashlib.sha256(f"t{i}".encode()).hexdigest(), 16) % 4 == 0]
        tune_qs = [Question(ids[i], "sample", x.kind, x.question, x.answer, x.gold_sections, x.gold_pages)
                   for i, x in enumerate(QUESTIONS)]
        qfile = self.t / "tune.jsonl"
        save(qfile, tune_qs)
        out = self.t / "tuned.json"
        r = harness.tune_not_found("htpd-words", [qfile], EXAMPLES, self.ctx, docs_list=docs, out=out)
        self.assertEqual(r["questions"], 5)
        self.assertEqual(r["balanced_accuracy"], 1.0)
        saved = json.loads(out.read_text())["htpd-words"]["not_found_below"]
        self.assertGreater(saved, 0.0)
        from eval.systems_htpd import tuned

        self.assertEqual(tuned("htpd-words", out), {"not_found_below": saved})
