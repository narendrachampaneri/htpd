# The HTPD reader

The converter turns a document into an HTPD file. The reader takes a
question, finds the few places in the file that hold the answer, and hands
exactly that text, with its pages, to any AI model - or says
**"Not found in this file."** with the reason.

The reader is Python 3.10+ with the standard library only (PyYAML is used
when it is installed). It runs on any PC, offline, without a GPU and
without any download. `docs/FORMAT.md` is the contract it reads.

## Rules

1. **Exact text with pages.** The reader never changes, shortens or
   rewrites text of a file. Everything it returns carries its section and
   page.
2. **Measured.** Every part of the reader shows in `eval/report.md` that it
   helps; a part that does not help is removed.
3. **Light.** No GPU, no internet, no model download. The one learned part
   (the re-ranker) is 5 small networks of 16 units in a JSON file.
4. **Free and open.** Apache-2.0, like the rest of HTPD.
5. **Test data stays apart.** Questions for testing and data for training
   come from different documents (section 4).
6. **Private documents stay private.** They live in `private/` (ignored by
   git) and are never committed, uploaded or put into a prompt for a cloud
   model.

## How it works

```
question
   │
   ▼
1 Read ──► 2 Retrieve ──► 3 Re-rank ──► 4 Not found? ──► 5 Pack ──► text + pages ──► any AI
                                                                 └► 6 Answer lines (htpd-read ask)
```

1. **Read** (`bundle.py`, `check.py`): the folder form and the single-file
   form, exactly as FORMAT.md says - fields, blocks, page marks, the
   backslash rule, the tree, the order, the links, the OCR pages. `check`
   lists every problem of a file.
2. **Retrieve** (`retriever.py`): two ranked lists merged by reciprocal
   rank fusion:
   * **words** - BM25 over the title (counted twice) and the text of each
     section, with a tokenizer that keeps technical words whole: register
     and field names (`dmcontrol`, `__arm_sme_state`), hex numbers
     (`0x7a1`), section numbers (`3.14.2`), and raised numbers as typed
     (`2^31` finds `2³¹`);
   * **tree** - BM25 over the path of titles of each section (chapter >
     section > subsection), at a hundredth of the weight of the words: it
     only decides between sections the words rank alike.

   A section number in the question ("section 3.14.2") puts that section
   first.
3. **Re-rank** (`rerank.py`, weights in `rerank.json`): 14 numbers a person
   would look at - the ranks, how much of the question the section and its
   best single line or table row hold, the specific terms and word pairs
   of the question, the title, length and depth - go through 5 small
   neural networks; their average orders the first 20 sections. Trained by
   `model/train_reranker.py` on training documents only
   ([`model/README.md`](../model/README.md)). It never changes the "not
   found" rule, and a section the question names stays first.
4. **Not found** (`retriever.py`), without any model:
   * the question names something the document never mentions - a
     specific term (an acronym, a register or part name: PCIe, VDD_SOC) or
     a plain word (Bluetooth, price). Other forms of a word count as known
     ("started" for "start"), and so do short forms in tables ("max");
   * the best sections hold too little of the question (coverage below
     0.17, set on the tuning split).
5. **Pack** (`packer.py`): the chosen sections with their exact text, the
   path of titles and the pages, within a budget of 3000 tokens and at
   most 8 sections. A long section gives its best blocks. Text read by OCR
   is marked.
6. **Answer** (`answer.py`): the few exact lines that answer the question
   - a table row with its header row, a sentence, a code line, a figure -
   each with its page and section.

Figures stand in the text as `![F12: caption]` lines; `htpd-read figure`
writes the picture of one to a PNG file, and the MCP tool `figure` returns
it as an image, so an AI that can see pictures can look at a diagram.

The reader is used in five ways: as a Python library, as the command
`htpd-read`, as the MCP server `htpd-mcp` for AI agents, as the skill
`skills/htpd` for AI apps (the reader packed with a `SKILL.md`), and as the
Claude Code plugin `htpd` (`.claude-plugin/`: both skills and the MCP
server, started with `skills/htpd/scripts/htpd.py mcp`). A single
`.htpd` file also works without any of them: its reading rules tell any AI
how to use it.

## Evaluation

`eval/` measures the reader (`eval/README.md`); its numbers are only in
`eval/report.md`, written by the harness, never by hand.

* **Documents**, pinned by link and SHA-256 in `eval/docs.list`:
  * test: RISC-V Debug Specification, Arm AAPCS64;
  * tuning: RISC-V privileged manual, Arm ELF manual (the "not found"
    threshold and every default of the reader are chosen here);
  * training: RISC-V unprivileged manual (and documents in `private/`).
* **Questions** (`eval/questions/*.jsonl`): a fact, a value from a table, an
  answer that needs two sections, and a question the document does not
  answer. A question is kept only when its answer stands in its section on
  its page (`python -m eval validate`); a person checks them with
  `python -m eval review`.
* **Measures**: Recall@1/5/10, MRR, the page, "not found" and false "not
  found", whether the exact answer stands in the text handed to the AI,
  tokens, time.
* **Baselines** the reader must beat: B0 the whole document, B1 chunks of
  500 tokens with BM25, B3 HTPD sections with BM25.

## Folders

```
reader/htpd_reader/   the reader, the MCP server, the re-ranker's weights
reader/tests/         its tests
skills/htpd/          the skill for AI apps (scripts/build_skill.py packs it)
skills/convert/       the plugin's skill that converts PDFs
.claude-plugin/       the Claude Code plugin and its marketplace
eval/                 questions, harness, baselines, report
model/                the re-ranker's training
```
