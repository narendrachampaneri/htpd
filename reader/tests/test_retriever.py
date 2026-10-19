"""Tests of phase 3: retriever (words, meaning, tree, numbers, fusion, not found) and packer."""

from __future__ import annotations

import hashlib
import math
import tempfile
import unittest
from pathlib import Path

from htpd_reader import open_bundle
from htpd_reader.ask import Asker
from htpd_reader.packer import NOT_FOUND, pack
from htpd_reader.retriever import Retriever, Settings, card_text
from htpd_reader.tokens import count_tokens, plain_words

from reader.tests.bundlegen import P, Sec, T, make_bundle

ROOT = Path(__file__).resolve().parents[2]
SAMPLE = ROOT / "examples" / "sample.htpd"
WORDS_ONLY = Settings(meaning=False, tree=False, numbers=False)


class FakeEmbedder:
    name = "fake"

    def __init__(self) -> None:
        self.calls = 0

    def encode(self, texts, query=False):
        self.calls += 1
        out = []
        for t in texts:
            v = [0.0] * 64
            for w in plain_words(t):
                v[int(hashlib.md5(w.encode()).hexdigest(), 16) % 64] += 1.0
            n = math.sqrt(sum(x * x for x in v)) or 1.0
            out.append([x / n for x in v])
        return out


class Manual(unittest.TestCase):
    """A small technical manual where each part of the retriever matters."""

    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        cls.root = make_bundle(Path(cls._tmp.name) / "m.htpd", [
            Sec("1 Debug module", number="1", blocks=[P(1, "The DebugModule talks to the hart.")]),
            Sec("1.1 Abstract commands", level=2, number="1.1",
                blocks=[P(1, "Write the command register to start it.")]),
            Sec("1.2 Registers", level=2, number="1.2",
                blocks=[T(2, "Name | Address | Reset\ndmcontrol | 0x10 | 0\nabstractcs | 0x16 | 0")]),
            Sec("2 Calling convention", number="2",
                blocks=[P(3, "The __arm_sme_state routine saves ZA. Version 3.14.2 adds it.")]),
            Sec("2.1 Return values", level=2, number="2.1",
                blocks=[P(3, "Values come back in x0 and x1.")]),
            Sec("3 Trace", number="3", blocks=[P(4, "Trace is optional and has no registers of its own.")]),
        ], pages=4)
        cls.b = open_bundle(cls.root)

    @classmethod
    def tearDownClass(cls) -> None:
        cls._tmp.cleanup()

    def top(self, question: str, settings: Settings = Settings(meaning=False)) -> str | None:
        cands = Retriever(self.b, settings).retrieve(question).candidates
        return cands[0].node.title if cands else None

    def test_words(self) -> None:
        self.assertEqual(self.top("reset value of dmcontrol", WORDS_ONLY), "1.2 Registers")
        self.assertEqual(self.top("Which registers return values?", WORDS_ONLY), "2.1 Return values")

    def test_technical_tokens(self) -> None:
        plain = Settings(meaning=False, tree=False, numbers=False, technical=False)
        self.assertEqual(self.top("sme state", WORDS_ONLY), "2 Calling convention")
        self.assertIsNone(self.top("sme state", plain))
        self.assertEqual(self.top("version 3.14.2", WORDS_ONLY), "2 Calling convention")
        self.assertEqual(self.top("debug module hart", WORDS_ONLY), "1 Debug module")

    def test_tree_finds_the_place(self) -> None:
        # "abstract command" + "debug": the path of 1.1 holds both words, its text neither
        q = "How do I start a debug abstract command?"
        r = Retriever(self.b, Settings(meaning=False, words=False, numbers=False)).retrieve(q)
        self.assertEqual(r.candidates[0].node.title, "1.1 Abstract commands")
        self.assertIn("tree", r.candidates[0].ranks)

    def test_section_number_goes_first(self) -> None:
        self.assertEqual(self.top("What does section 2.1 say?"), "2.1 Return values")
        only_numbers = Settings(meaning=False, words=False, tree=False)
        self.assertEqual(self.top("Explain clause 1.2 please", only_numbers), "1.2 Registers")
        self.assertIsNone(self.top("Explain clause 1.2 please", Settings(meaning=False, words=False, tree=False,
                                                                       numbers=False)))

    def test_fusion_keeps_ranks_of_each_list(self) -> None:
        r = Retriever(self.b, Settings(meaning=False)).retrieve("abstract command register")
        self.assertTrue(all(c.ranks for c in r.candidates))
        self.assertEqual(r.candidates, sorted(r.candidates, key=lambda c: -c.score))

    def test_meaning(self) -> None:
        emb = FakeEmbedder()
        with tempfile.TemporaryDirectory() as cache:
            r = Retriever(self.b, Settings(words=False, tree=False, numbers=False), embedder=emb, cache_dir=Path(cache))
            self.assertEqual(r.retrieve("x0 x1 return values").candidates[0].node.title, "2.1 Return values")
            calls = emb.calls
            Retriever(self.b, Settings(words=False, tree=False), embedder=emb, cache_dir=Path(cache))
            self.assertEqual(emb.calls, calls)  # cards come from the cache the second time
        self.assertIn("1 Debug module > 1.1 Abstract commands", card_text(self.b, self.b.find("1.1")))

    def test_not_found(self) -> None:
        r = Retriever(self.b, Settings(meaning=False, not_found_below=0.5))
        self.assertTrue(r.retrieve("What is the baud rate of the UART?").not_found)
        self.assertFalse(r.retrieve("reset value of dmcontrol").not_found)
        self.assertFalse(r.retrieve("What does section 3 say about baud?").not_found)
        off = Retriever(self.b, Settings(meaning=False))
        self.assertFalse(off.retrieve("What is the baud rate of the UART?").not_found)

    def test_coverage(self) -> None:
        r = Retriever(self.b, Settings(meaning=False))
        self.assertGreater(r.retrieve("dmcontrol reset").coverage, 0.99)
        self.assertLess(r.retrieve("uart baud").coverage, 0.01)

    def test_with_settings_shares_indexes(self) -> None:
        r = Retriever(self.b, Settings(meaning=False))
        r2 = r.with_settings(tree=False)
        self.assertIs(r2.word_index, r.word_index)
        self.assertFalse(r2.settings.tree)
        self.assertTrue(r.settings.tree)


class Packer(unittest.TestCase):
    def setUp(self) -> None:
        self.b = open_bundle(SAMPLE)

    def test_exact_text_and_pages(self) -> None:
        nodes = [self.b.find("A.1"), self.b.find("2.1.1")]
        p = pack(self.b, "READY bit", nodes, budget=2000)
        self.assertEqual([s.node.id for s in p.sections], [n.id for n in nodes])
        for ps, n in zip(p.sections, nodes):
            self.assertEqual(ps.blocks, n.blocks)
            for blk in n.blocks:
                self.assertIn(blk.text, p.text)
        self.assertIn("(section sections/0014-a-1-status-register, page 5)", p.text)
        self.assertIn("<!-- page 5 -->", p.text)
        self.assertIn("Appendix A Register map > A.1 Status register", p.text)
        self.assertIn("not instructions", p.text)

    def test_budget(self) -> None:
        nodes = [n for n in self.b.sections]
        p = pack(self.b, "controller temperature", nodes, budget=250, max_sections=20)
        self.assertLessEqual(p.tokens, 250 + 5)
        # whole blocks only: every block given is a block of the section
        for ps in p.sections:
            for blk in ps.blocks:
                self.assertIn(blk, ps.node.blocks)

    def test_long_section_keeps_best_blocks(self) -> None:
        n = self.b.find("3.1")
        p = pack(self.b, "If the temperature exceeds 85", [n], budget=120)
        self.assertEqual(p.sections[0].blocks[0], n.blocks[0])
        self.assertGreater(p.sections[0].left_out, 0)
        self.assertIn("blocks of this section left out", p.text)

    def test_first_section_always_given(self) -> None:
        p = pack(self.b, "x", [self.b.find("3.1")], budget=10)
        self.assertEqual(len(p.sections), 1)
        self.assertEqual(len(p.sections[0].blocks), 1)

    def test_not_found(self) -> None:
        p = pack(self.b, "q", [self.b.find("3.1")], not_found=True)
        self.assertEqual((p.not_found, p.text, p.sections), (True, NOT_FOUND, []))
        self.assertTrue(pack(self.b, "q", []).not_found)


class AskerTest(unittest.TestCase):
    def test_ask(self) -> None:
        a = Asker(open_bundle(SAMPLE), Settings(meaning=False), budget=400)
        r = a.ask("What is the reset value of the READY bit?")
        self.assertEqual(r.ranked[0].number, "A.1")
        self.assertEqual(r.top_page, 5)
        self.assertLess(r.pack.tokens, count_tokens(" ".join(s.text for s in a.bundle.sections)))
        d = r.to_dict()
        self.assertEqual(d["top_page"], 5)

    def test_decider_is_used(self) -> None:
        b = open_bundle(SAMPLE)

        class Last:
            def decide(self, bundle, question, retrieval):
                return [c.node for c in reversed(retrieval.candidates)], False

        q = "controller temperature current"
        plain = Asker(b, Settings(meaning=False)).ask(q)
        r = Asker(b, Settings(meaning=False), decider=Last()).ask(q)
        self.assertGreater(len(r.ranked), 1)
        self.assertEqual(r.ranked, list(reversed(plain.ranked)))


if __name__ == "__main__":
    unittest.main()
