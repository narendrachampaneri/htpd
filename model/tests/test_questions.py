"""Tests of the training questions made by rules (model/questions.py)."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from htpd_reader import open_bundle

from model.questions import (TestDocumentError, bare_title, file_questions, refuse_test_documents,
                             rule_questions, table_questions, term_questions, title_questions)

ROOT = Path(__file__).resolve().parents[2]
SAMPLE = ROOT / "examples" / "sample.htpd"


class Questions(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        cls.t = Path(cls._tmp.name)
        cls.b = open_bundle(SAMPLE)

    @classmethod
    def tearDownClass(cls) -> None:
        cls._tmp.cleanup()

    def test_refuses_test_documents_by_name_and_checksum(self) -> None:
        docs = self.t / "docs.list"
        docs.write_text("sample train - -\nother test - -\n")
        refuse_test_documents({"sample": self.b}, docs)   # a training document is fine
        with self.assertRaises(TestDocumentError):
            refuse_test_documents({"other": self.b}, docs)
        pinned = self.t / "pinned.list"
        pinned.write_text(f"renamed test {self.b.source_sha256} -\n")
        with self.assertRaises(TestDocumentError):
            refuse_test_documents({"sample": self.b}, pinned)

    def test_refuses_tuning_documents(self) -> None:
        tune = self.t / "tune.list"
        tune.write_text(f"tuned tune {self.b.source_sha256} -\n")
        with self.assertRaises(TestDocumentError):
            refuse_test_documents({"tuned": self.b}, tune)
        with self.assertRaises(TestDocumentError):
            refuse_test_documents({"sample": self.b}, tune)

    def test_rules(self) -> None:
        self.assertEqual(bare_title("2.1.1 Limits"), "Limits")
        self.assertEqual(bare_title("Appendix A Register map"), "Appendix A Register map")
        self.assertEqual(bare_title("A.1 Status register"), "Status register")
        tq = {q.question for q in table_questions(self.b)}
        self.assertTrue(any(q.startswith("What is the") and " of " in q for q in tq))
        self.assertTrue(all(q.gold != "sections/0001-front-matter" for q in title_questions(self.b)))
        terms = {q.question for q in term_questions(self.b)}
        self.assertTrue(any("READY" in q or "DERATE" in q or "EVSE" in q for q in terms), terms)
        self.assertEqual({q.source for q in rule_questions(self.b)}, {"title", "table", "term"})
        ids = {s.id for s in self.b.sections}
        self.assertTrue(all(q.gold in ids for q in rule_questions(self.b)))

    def test_questions_are_stable(self) -> None:
        self.assertEqual(rule_questions(self.b), rule_questions(open_bundle(SAMPLE)))

    def test_question_files_are_used(self) -> None:
        from eval.questions import Question, save

        qf = self.t / "q.jsonl"
        save(qf, [Question("x1", "sample", 1, "When is the current lowered?", "If the temperature exceeds 85 °C",
                           ["sections/0011-3-1-thermal-derating"], [3]),
                  Question("x2", "sample", 1, "Bad one?", "words that are not there",
                           ["sections/0011-3-1-thermal-derating"], [3])])
        got = {q.question for q in file_questions(self.b, "sample", [qf])}
        self.assertEqual(got, {"When is the current lowered?"})   # a question whose answer is not in the file is left out


if __name__ == "__main__":
    unittest.main()
