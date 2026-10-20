"""The HTPD retriever: find the sections of a bundle that answer a question.

Three ranked lists, merged by reciprocal rank fusion (RRF):

* words   - BM25 over each section's title and text, with the technical
            tokenizer (register names, hex numbers and section numbers stay
            whole). The title counts twice.
* meaning - cosine similarity of an open embedding model (optional; needs
            sentence-transformers, see embed.py).
* tree    - BM25 over the *path* of titles of each section (chapter > section
            > subsection). This is what the structure of a bundle adds: a
            question about "the abstract command in the debug module" matches
            the place in the tree, even when the section text says neither.
            It counts only a hundredth of the other lists (`tree_weight`), so
            it can only decide between candidates the words rank alike: on
            the tuning questions every weight from 0.02 up pushed the right
            section down (Recall@1 0.804 at 0.01, 0.691 at 0.1, Arm ELF
            manual 0.825 -> 0.550), because neighbouring ranks of the fusion
            differ by less than 0.001.

A section number in the question ("section 3.14.2", "see 4.1") puts that
section first.

"Not found" has two rules, both without any model:

* the question names a thing the document never mentions: a specific term
  (an acronym, a register or signal name, a part number: PCIe, HDMI,
  VDD_SOC, I2C, Cortex-A55) or a plain word (Bluetooth, price, temperature).
  A document cannot answer a question about a thing it does not name. Other
  forms of a word of the document count as known ("started" for "start"),
  and so do the words that only phrase a question ("mean", "happen",
  "describe"). The answer says which words are missing.
* the share of the question's rare words (weighted by IDF) that stand in the
  best candidates is below `not_found_below` (set on the tuning split, never
  on the test split; 0 switches this rule off).

A question that names a section of the document ("section 4.1") is never
"not found".
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field, replace
from typing import Any

from .bm25 import BM25
from .bundle import Bundle, Node
from .tokens import SHORT_FORMS, STOPWORDS, main_tokens, plain_words, technical_tokens

RRF_K = 60

# The "not found" coverage threshold of htpd-words, tuned on the tuning split by
# `python -m eval tune htpd-words` (eval/tuned.json; a test keeps both equal).
# htpd-read ask, the skill and the MCP server use it.
TUNED_NOT_FOUND_BELOW = 0.17

# Words that phrase a question but do not name what it is about. A question may
# use them even when the document never does.
QUESTION_WORDS = frozenset(
    "mean meaning stand happen consist describe explain define tell give list show find need "
    "use work kind sort type way thing exist possible ever always never also just please "
    "difference compare whether else other used say get set make take know want like "
    "many much more most less few several each every any all some only same different "
    "must should could would may might shall".split())
_WORD_IN_QUESTION = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_.+-]*")
_SECTION_REF = re.compile(r"(?:section|chapter|clause|appendix|table|§)\s*([A-Z]?\d+(?:\.\d+)*|[A-Z](?:\.\d+)*)\b",
                          re.IGNORECASE)


@dataclass(frozen=True)
class Settings:
    words: bool = True
    meaning: bool = True
    tree: bool = True
    numbers: bool = True  # a section number in the question goes first
    technical: bool = True  # technical tokenizer; False: plain words
    candidates: int = 50  # length of each ranked list
    not_found_below: float = 0.0  # 0 disables "not found"
    coverage_of: int = 3  # how many top candidates count for the coverage
    unknown_terms: bool = True  # "not found" when the question names a term or word the document never uses
    tree_weight: float = 0.01  # weight of the tree list in the fusion (words and meaning count 1)


@dataclass
class Candidate:
    node: Node
    score: float  # fused score
    ranks: dict[str, int] = field(default_factory=dict)  # rank in each list, from 1


@dataclass
class Retrieval:
    question: str
    candidates: list[Candidate]
    coverage: float  # share of the question's IDF found in the best candidates
    not_found: bool
    missing: list[str] = field(default_factory=list)  # specific terms of the question the document never uses

    @property
    def reason(self) -> str:
        if not self.not_found:
            return ""
        if self.missing:
            return "the file never mentions " + ", ".join(self.missing)
        return "no part of the file matches the question well enough"

    @property
    def ids(self) -> list[str]:
        return [c.node.id for c in self.candidates]


def specific_terms(question: str) -> list[str]:
    """Words of a question that name one specific thing: an acronym (PCIe,
    HDMI, ADC), a name with a digit or an underscore (I2C, VDD_SOC,
    Cortex-A55). Plain words and plain numbers are not specific."""
    out: list[str] = []
    for m in _WORD_IN_QUESTION.finditer(question):
        w = m.group(0).strip(".+-")
        if not w or not any(c.isalpha() for c in w):
            continue
        caps = sum(c.isupper() for c in w)
        has_digit = any(c.isdigit() for c in w)
        if caps >= 2 or "_" in w or has_digit:
            if w not in out:
                out.append(w)
    return out


def card_text(bundle: Bundle, node: Node, max_words: int = 120) -> str:
    """A short card of a section: its path of titles and its first words."""
    path = " > ".join(bundle.path_titles(node)[1:])
    words = node.text.split()
    first = " ".join(words[:max_words])
    return f"{path}\n{first}" if first else path


def stem(word: str) -> str:
    """A light stem, so that "started", "starts" and "start" are one word."""
    w = word.lower()
    for suffix in ("ing", "ed", "ly", "s"):
        if w.endswith(suffix) and len(w) - len(suffix) >= 3:
            w = w[: -len(suffix)]
            break
    if w.endswith("e") and len(w) >= 4:  # "saves", "saved", "save" -> "sav"
        w = w[:-1]
    return w


class Retriever:
    def __init__(self, bundle: Bundle, settings: Settings | None = None, embedder: Any = None,
                 cache_dir: Any = None):
        self.bundle = bundle
        self.settings = settings or Settings()
        self.sections = [s for s in bundle.sections]
        tok = self.tokens
        self.word_index = BM25([tok(s.title) * 2 + tok(s.text) for s in self.sections])
        self.tree_index = BM25([tok(" ".join(bundle.path_titles(s)[1:])) for s in self.sections])
        self.by_number = {s.number.lower(): i for i, s in enumerate(self.sections) if s.number}
        self._vocabulary: set[str] | None = None
        self._stems: set[str] | None = None
        self.embedder = embedder if self.settings.meaning else None
        self.vectors: list[list[float]] | None = None
        if self.embedder is not None:
            from .embed import cached_encode

            cards = [card_text(bundle, s) for s in self.sections]
            self.vectors = cached_encode(self.embedder, cards, cache_dir, f"{bundle.name}-cards")

    def tokens(self, text: str) -> list[str]:
        return technical_tokens(text) if self.settings.technical else plain_words(text)

    def with_settings(self, **changes: Any) -> "Retriever":
        """The same indexes with other settings (for ablations and tuning)."""
        clone = object.__new__(Retriever)
        clone.__dict__.update(self.__dict__)
        clone.settings = replace(self.settings, **changes)
        return clone

    # ------------------------------------------------------------------

    def lists(self, question: str) -> dict[str, list[int]]:
        st = self.settings
        q = self.tokens(question)
        out: dict[str, list[int]] = {}
        if st.words:
            out["words"] = [i for i, _ in self.word_index.top(q, st.candidates)]
        if st.tree:
            out["tree"] = [i for i, _ in self.tree_index.top(q, st.candidates)]
        if st.meaning and self.embedder is not None and self.vectors is not None:
            from .embed import cosine_top

            qv = self.embedder.encode([question], query=True)[0]
            out["meaning"] = [i for i, _ in cosine_top(qv, self.vectors, st.candidates)]
        return out

    def retrieve(self, question: str, k: int = 20) -> Retrieval:
        st = self.settings
        lists = self.lists(question)
        fused: dict[int, float] = {}
        ranks: dict[int, dict[str, int]] = {}
        for name, order in lists.items():
            weight = st.tree_weight if name == "tree" else 1.0
            for r, i in enumerate(order, 1):
                fused[i] = fused.get(i, 0.0) + weight / (RRF_K + r)
                ranks.setdefault(i, {})[name] = r
        named = False
        if st.numbers:
            for m in _SECTION_REF.finditer(question):
                i = self.by_number.get(m.group(1).lower())
                if i is not None:
                    named = True
                    fused[i] = fused.get(i, 0.0) + 1.0
                    ranks.setdefault(i, {})["number"] = 1
        order = sorted(fused, key=lambda i: (-fused[i], i))[:k]
        cands = [Candidate(self.sections[i], fused[i], ranks.get(i, {})) for i in order]
        coverage = self.coverage(question, [c.node for c in cands[: st.coverage_of]])
        missing = self.missing_terms(question) if st.unknown_terms and not named else []
        # a question that names a section of the document is never "not found"
        low = not named and st.not_found_below > 0 and coverage < st.not_found_below
        not_found = not cands or bool(missing) or low
        return Retrieval(question, cands, coverage, not_found, missing)

    @property
    def vocabulary(self) -> set[str]:
        """Every whole word of the document, lower case, and the joined form
        of words broken at a hyphen."""
        if self._vocabulary is None and self.settings.technical:
            # the word index already holds every technical token of every section,
            # and those include the whole words: no second pass over the text
            self._vocabulary = set(self.word_index.postings) | set(main_tokens(self.bundle.title))
        if self._vocabulary is None:
            vocab: set[str] = set(main_tokens(self.bundle.title))
            for s in self.sections:
                for text in (s.title, s.text):
                    vocab.update(main_tokens(text))
                    vocab.update(t for t in technical_tokens(text) if t not in vocab)
            self._vocabulary = vocab
        return self._vocabulary

    @property
    def stems(self) -> set[str]:
        if getattr(self, "_stems", None) is None:
            self._stems = {stem(w) for w in self.vocabulary}
        return self._stems

    def missing_terms(self, question: str) -> list[str]:
        """The specific terms and plain words of the question that the document
        never uses (as the question writes them)."""
        vocab = self.vocabulary
        missing = []
        for term in specific_terms(question):
            words = main_tokens(term)
            joined = "".join(w for w in words)

            def known(w: str) -> bool:
                return w in vocab or (w.endswith("s") and w[:-1] in vocab)

            if not (all(known(w) for w in words) or known(joined)):
                missing.append(term)
        if missing:  # a missing specific term is the clearest reason; plain words add nothing
            return missing
        taken: set[str] = set()
        question_stems = {stem(w) for w in QUESTION_WORDS}
        for m in _WORD_IN_QUESTION.finditer(question):
            word = m.group(0).strip(".+-")
            low = word.lower()
            if (len(low) < 3 or not low.isalpha() or low in STOPWORDS or low in taken
                    or stem(low) in question_stems):
                continue
            short = SHORT_FORMS.get(low)  # "maximum" is known when the tables say "max"
            if low not in vocab and stem(low) not in self.stems and not (short and short in vocab):
                missing.append(word)
                taken.add(low)
        return missing

    def coverage(self, question: str, nodes: list[Node]) -> float:
        """Share of the question's word weight (IDF) that the nodes hold."""
        terms = [t for t in dict.fromkeys(self.tokens(question)) if t not in STOPWORDS]
        idf = self.word_index.idf
        total = sum(idf.get(t, max(idf.values(), default=1.0)) for t in terms)
        if not terms or total <= 0:
            return 0.0
        have: set[str] = set()
        for n in nodes:
            have.update(self.tokens(n.title))
            have.update(self.tokens(n.text))
        return sum(idf.get(t, 0.0) for t in terms if t in have) / total
