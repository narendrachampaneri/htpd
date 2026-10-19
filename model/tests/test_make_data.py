"""Tests of phase 4: the training data generator."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from htpd_reader import open_bundle
from htpd_reader.retriever import card_text

from model.make_data import (TestDocumentError, bare_title, generate, table_questions, term_questions,
                             title_questions, write, write_check_sheet)

ROOT = Path(__file__).resolve().parents[2]
SAMPLE = ROOT / "examples" / "sample.htpd"


class Generator(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        cls.t = Path(cls._tmp.name)
        cls.docs = cls.t / "docs.list"
        cls.docs.write_text("sample train - -\nother test - -\n")
        cls.b = open_bundle(SAMPLE)
        cls.ex = generate({"sample": cls.b}, docs_list=cls.docs)

    @classmethod
    def tearDownClass(cls) -> None:
        cls._tmp.cleanup()

    def test_refuses_test_documents_by_name_and_checksum(self) -> None:
        with self.assertRaises(TestDocumentError):
            generate({"other": self.b}, docs_list=self.docs)
        pinned = self.t / "pinned.list"
        pinned.write_text(f"renamed test {self.b.source_sha256} -\n")
        with self.assertRaises(TestDocumentError):
            generate({"sample": self.b}, docs_list=pinned)

    def test_refuses_tuning_documents(self) -> None:
        tune = self.t / "tune.list"
        tune.write_text(f"tuned tune {self.b.source_sha256} -\n")
        with self.assertRaises(TestDocumentError):
            generate({"tuned": self.b}, docs_list=tune)
        with self.assertRaises(TestDocumentError):
            generate({"sample": self.b}, docs_list=tune)

    def test_all_three_tasks(self) -> None:
        tasks = {e["task"] for e in self.ex}
        self.assertEqual(tasks, {"relevant", "child", "enough"})
        self.assertEqual({e["source"] for e in self.ex}, {"title", "table", "term"})

    def test_relevant_labels(self) -> None:
        for e in self.ex:
            if e["task"] != "relevant":
                continue
            gold = self.b.node(e["gold"])
            if e["label"] == 1:
                self.assertEqual(e["card"], card_text(self.b, gold))
            else:
                neg = self.b.node(e["negative"])
                self.assertNotEqual(neg.id, gold.id)
                self.assertNotIn(neg, self.b.ancestors(gold))
                self.assertNotIn(neg.id, gold.children)

    def test_child_answer_is_on_the_path(self) -> None:
        for e in self.ex:
            if e["task"] != "child":
                continue
            self.assertLessEqual(len(e["options"]), 8)
            gold = self.b.node(e["gold"])
            path = {n.id for n in self.b.ancestors(gold)} | {gold.id}
            right = e["options"][e["answer"]].split("\n")[0]
            self.assertTrue(any(right.endswith(self.b.node(i).title) for i in path), right)

    def test_enough_labels(self) -> None:
        for e in self.ex:
            if e["task"] == "enough":
                node = self.b.node(e["gold"] if e["label"] == 1 else e["negative"])
                self.assertTrue(" ".join(node.text.split()).startswith(e["text"][:50]))

    def test_no_duplicates_and_stable(self) -> None:
        keys = [(e["task"], e["question"], e.get("card"), str(e.get("options")), e.get("text"), e.get("label"))
                for e in self.ex]
        self.assertEqual(len(keys), len(set(keys)))
        again = generate({"sample": open_bundle(SAMPLE)}, docs_list=self.docs)
        self.assertEqual([e["id"] for e in again], [e["id"] for e in self.ex])

    def test_split_by_section(self) -> None:
        split_of: dict[str, set[str]] = {}
        for e in self.ex:
            split_of.setdefault(e["gold"], set()).add(e["split"])
        self.assertTrue(all(len(s) == 1 for s in split_of.values()))

    def test_rules(self) -> None:
        self.assertEqual(bare_title("2.1.1 Limits"), "Limits")
        self.assertEqual(bare_title("Appendix A Register map"), "Appendix A Register map")
        self.assertEqual(bare_title("A.1 Status register"), "Status register")
        tq = {q.question for q in table_questions(self.b)}
        self.assertTrue(any(q.startswith("What is the") and " of " in q for q in tq))
        self.assertTrue(all(q.gold != "sections/0001-front-matter" for q in title_questions(self.b)))
        terms = {q.question for q in term_questions(self.b)}
        self.assertTrue(any("READY" in q or "DERATE" in q or "EVSE" in q for q in terms), terms)

    def test_question_files_are_used(self) -> None:
        from eval.questions import Question, save

        qf = self.t / "q.jsonl"
        save(qf, [Question("x1", "sample", 1, "When is the current lowered?", "If the temperature exceeds 85 °C",
                           ["sections/0011-3-1-thermal-derating"], [3]),
                  Question("x2", "sample", 1, "Bad one?", "words that are not there",
                           ["sections/0011-3-1-thermal-derating"], [3])])
        ex = generate({"sample": self.b}, [qf], docs_list=self.docs)
        from_file = {e["question"] for e in ex if e["source"] == "file"}
        self.assertEqual(from_file, {"When is the current lowered?"})

    def test_write_and_check_sheet(self) -> None:
        out = self.t / "data"
        counts = write(self.ex, out)
        lines = (out / "train.jsonl").read_text().splitlines() + (out / "dev.jsonl").read_text().splitlines()
        self.assertEqual(len(lines), len(self.ex))
        self.assertEqual(sum(counts.values()), len(self.ex))
        json.loads(lines[0])
        write_check_sheet(self.ex, 20, out / "check.md")
        self.assertEqual((out / "check.md").read_text().count("## [ ]"), 20)


if __name__ == "__main__":
    unittest.main()
