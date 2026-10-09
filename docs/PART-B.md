# Part B: the HTPD reader model - plan

Part A (the converter) turns a PDF into a bundle. Part B is "the strength
and power": given a question, it finds the few places in one or more
bundles that hold the answer, and hands exactly that text, with its pages,
to any AI model.

This file is the plan for B. Coding agents follow it phase by phase.
`docs/FORMAT.md` is the contract B reads; `AGENTS.md` has the working rules.

## 1. What "strong" means

B is strong when it is **measured** to be better than the usual ways on
the same questions (section 4):

* it finds the right sections (recall),
* it gives the right pages,
* it says "not found" instead of guessing when the answer is not there,
* it hands the AI far fewer tokens than the whole document,
* it is fast on a normal PC, without internet.

The strength does not come from training a big language model. It comes
from three things together: the structure of the bundle (tree, order,
pages), a good search, and a small **trained decision model** that is
specialised in one job - picking the right parts of an HTPD bundle.

## 2. Rules for B

1. **Measure first.** Every part of B must show in numbers that it helps.
   A part that does not help is removed.
2. **Offline first.** B runs on a normal PC (CPU is enough) without
   internet, after the models are downloaded once. A cloud model may be
   used as an option, never as a must.
3. **Free and open.** Only libraries and models with open licences
   (MIT, Apache-2.0, BSD and the like).
4. **Exact text with pages.** B never changes, shortens or rewrites text
   from a bundle. Everything it returns carries the section ID and the
   page.
5. **The format is the contract.** B only reads bundles. If B needs more
   in a bundle, write a proposal for format 0.2 into `docs/`; do not
   change the converter during a phase of B.
6. **Test data stays apart.** Questions for testing and data for training
   come from different documents. Nothing from the test documents is ever
   used for training.
7. **Private documents stay private.** They live in `private/` (ignored by
   git) and are never committed, uploaded or put into a prompt for a cloud
   model.
8. **Everywhere.** B is Python 3.10+ and works on Linux, Windows and macOS.
   It does not need PDFium or the C++ code.

## 3. How B works

```
question
   │
   ▼
1 Reader ──► 2 Retriever ──► 3 Decider (trained model) ──► 4 Packer ──► text + pages ──► any AI
```

1. **Reader** (`reader/htpd_reader`): reads a bundle exactly as
   `docs/FORMAT.md` says: fields, blocks, page marks, the backslash rule,
   the tree (parent, children) and the order (previous, next). It can
   check a bundle and list every problem.
2. **Retriever**: finds candidate sections in three ways and merges the
   lists (reciprocal rank fusion):
   * word search (BM25) with a tokenizer that keeps technical words whole:
     register and field names (`dmcontrol`, `__arm_sme_state`), hex numbers
     (`0x7a1`), section numbers (`3.14.2`);
   * meaning search with a small open embedding model that runs on CPU
     (for example `bge-small-en-v1.5`);
   * the tree: the parent of a hit gives context, its neighbours continue
     a table or a list.
3. **Decider** - the trained model, the core of B's strength. It looks at
   the question and a short "card" of a section (its path of titles, its
   description and its first lines) and gives calibrated probabilities
   for three decisions:
   * **relevant?** (yes/no) - to sort the candidates;
   * **which child?** (a choice among the subsections) - to walk down the
     tree from the contents, as a person reads a manual;
   * **enough?** (yes/no) - to stop when the collected text answers the
     question.

   First choice: **Laya**, an open (Apache-2.0) decision model of about
   421M parameters that runs on CPU, returns probabilities for typed
   questions (choice, score, yes/no) and answers in tens of milliseconds.
   It does not write text, which suits this job. It is fine-tuned on B's
   own training data (phase 5). To compare: an open cross-encoder
   reranker. The numbers decide which one stays.
4. **Packer**: puts together what the AI gets, within a token budget: the
   chosen sections with their exact text, the location line, the pages,
   and the parent titles for context. It says "not found" when nothing
   passes the decider.

B is used in three ways: as a Python library, as a command-line tool
(`htpd-read`), and as an **MCP server**, so that Claude Code, Antigravity
and any other MCP tool can call it. A `SKILL.md` ("How to read an HTPD
bundle") tells an AI agent how to use the tools.

## 4. Evaluation - the test for B

* **Documents**: `tests/data/sample.pdf`, the RISC-V Debug Specification,
  the Arm AAPCS64, the RISC-V Instruction Set Manual (all public), and
  your own documents in `private/`. A script `eval/get_docs.sh` downloads
  the public PDFs into `eval/docs/` (not committed) and checks their
  SHA-256, so every run and every agent tests on the same files.
* **Split by document**:
  * test: RISC-V Debug Specification and Arm AAPCS64;
  * training and tuning: the RISC-V Instruction Set Manual and the others.
* **Kinds of questions**:
  1. a fact ("What is the reset value of the field `ndmreset`?"),
  2. a value from a table,
  3. an answer that needs two sections,
  4. a question that the document does not answer (correct answer: not
     found).
* **Making the questions**: an AI writes candidate questions from one
  section, with the answer and the page. A script keeps a question only
  when the answer text really stands in that section. A person checks at
  least 100 test questions by hand. Any AI may be used for this one-time
  step; B itself must stay offline.
* **File**: `eval/questions/*.jsonl`, one question per line:
  `id, bundle, kind, question, answer, gold_sections, gold_pages`.
* **Measures** (always on the test split):
  * Recall@1, @5, @10: is a gold section among the first 1, 5, 10?
  * MRR (how high the first gold section stands);
  * page accuracy of what B returns;
  * "not found" accuracy on kind 4;
  * tokens handed to the AI (one tokenizer for all numbers);
  * time per question on CPU.
* **Baselines** that B must beat:
  * B0: the whole document text (where it fits);
  * B1: fixed chunks of about 500 tokens with BM25;
  * B2: the same chunks with embeddings;
  * B3: HTPD sections with BM25 only.
* **Report**: `eval/report.md`, one table per phase, written by the
  harness, never typed by hand.
* **First targets** (to be set again after the baselines are measured):
  Recall@5 ≥ 0.90, page accuracy ≥ 0.95, "not found" accuracy ≥ 0.80,
  at most 10 % of the tokens of B0, under 1 s per question on CPU.

## 5. Phases

One phase at a time, each on its own git branch. A phase is done only when
its "done when" lines are true and the other agent has reviewed it.

### Phase 1 - Reader

`reader/htpd_reader`: open a bundle; nodes with all fields; parent,
children, previous, next; the text of a section as blocks (paragraph,
table rows, code) with pages; `check`; a simple word search; command line
`python -m htpd_reader contents|section|search|check <bundle>` with
`--json`. Allowed libraries: PyYAML, pytest.

Done when:
* tests pass, including hand-made bundles for the hard cases: a block of
  four backticks that holds a line of three backticks, a paragraph that
  starts with a backslash, a `# Text` line inside a block, an empty table
  cell, a broken link, children and parent that disagree;
* `check` finds no problem in `examples/sample.htpd` and in the bundles of
  the three public manuals, and finds every problem in the broken ones;
* the text it returns is byte for byte the text of the bundle (after the
  backslash rule).

### Phase 2 - Evaluation set and harness

The question files (section 4), the harness (`python -m eval run <system>`)
and the four baselines.

Done when: at least 300 questions, of which at least 100 test questions
are checked by hand; all four baselines are measured; `eval/report.md`
has the first table.

### Phase 3 - Retriever and packer

Word search with the technical tokenizer, embeddings, the tree, fusion,
and the packer with a token budget and "not found".

Done when: it beats the best baseline in Recall@5 on the test split, and
the report shows what each part adds (one row per part switched off).

### Phase 4 - Training data

A generator that makes training pairs from the training documents only:
(question, card, label) for "relevant?", (question, list of children,
right child) for "which child?", (question, collected text, label) for
"enough?". Hard negatives: sibling sections, neighbours, sections with the
same words.

Done when: at least 20 000 examples, no question from the test documents,
duplicates removed, 200 examples checked by a person.

### Phase 5 - Decider model

(a) An open cross-encoder reranker, untrained, as the first comparison.
(b) Laya, fine-tuned on the phase 4 data for the three decisions. Laya
provides a notebook for fine-tuning; it ran on two free Kaggle T4 GPUs in
about four hours. Thresholds are set on the tuning split, never on test.

Done when: the decider improves Recall@5, page accuracy or tokens over
phase 3 on the test split, within the time target. The model card
(`model/README.md`) states the data, the licence and the numbers.

### Phase 6 - MCP server and skill

MCP tools: `contents`, `open_section`, `search`, `ask` (retriever +
decider + packer), `check`. A `SKILL.md` that teaches an agent to read a
bundle: start at the contents, open what the decider suggests, follow
parent and neighbours, quote with pages, say "not found" when needed.

Done when: Claude Code and Antigravity each answer 20 test questions
through the MCP server with the right pages, and the report has the
end-to-end numbers.

### Phase 7 - Learning loop (later)

With the user's consent, B keeps the questions it could not answer well
(locally), turns checked ones into new training data and is fine-tuned
again: B gets better with use. Then a UI.

## 6. Folders

```
reader/   Python package htpd_reader: reader, retriever, packer, cli, mcp
model/    training data generator, fine-tuning, model card
eval/     questions, harness, report
private/  your own documents and their bundles (never committed)
docs/     FORMAT.md (the contract), PART-B.md (this plan), BACKGROUND.md
```

## 7. Open questions (the numbers decide)

* Which embedding model.
* Whether a card fits Laya's input length (its multilingual checkpoint
  reads 1 024 tokens by default and up to 8 192).
* Whether walking down the tree beats sorting all candidates at once, or
  whether the two work best together.
