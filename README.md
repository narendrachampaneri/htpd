<p align="center">
  <img src="docs/assets/banner.svg" alt="HTPD - Hyper Text Portable Document" width="100%">
</p>

<p align="center">
  <a href="https://github.com/narendrachampaneri/htpd/actions/workflows/tests.yml"><img src="https://img.shields.io/github/actions/workflow/status/narendrachampaneri/htpd/tests.yml?branch=main&label=tests&style=for-the-badge" alt="tests"></a>
  <img src="https://img.shields.io/badge/converter-C%2B%2B23-1d4ed8?style=for-the-badge" alt="C++23">
  <img src="https://img.shields.io/badge/reader-Python%203.10%2B-0f766e?style=for-the-badge" alt="Python 3.10+">
  <img src="https://img.shields.io/badge/license-Apache--2.0-475569?style=for-the-badge" alt="Apache-2.0">
  <img src="https://img.shields.io/badge/status-0.1%20alpha-b45309?style=for-the-badge" alt="0.1 alpha">
</p>

<p align="center">
  <a href="#what-is-htpd"><b>What it is</b></a> &nbsp;·&nbsp;
  <a href="https://github.com/narendrachampaneri/htpd/releases"><b>Download</b></a> &nbsp;·&nbsp;
  <a href="#quick-start"><b>Quick start</b></a> &nbsp;·&nbsp;
  <a href="#use-it-with-your-ai"><b>Use with your AI</b></a> &nbsp;·&nbsp;
  <a href="#the-format"><b>Format</b></a> &nbsp;·&nbsp;
  <a href="#results"><b>Results</b></a> &nbsp;·&nbsp;
  <a href="#roadmap"><b>Roadmap</b></a>
</p>

<p align="center">
  <img src="docs/assets/demo.gif" alt="Converting a data sheet with htpd and asking questions with htpd-read" width="88%">
  <br><sub>Real commands on the NXP i.MX 93 data sheet (answer output shortened).</sub>
</p>

---

## What is HTPD?

You have a 300-page datasheet or a 5000-page reference manual and a question
about it. You upload the PDF to an AI, and it answers slowly, misses details,
mixes up tables, cannot tell you the page - or invents an answer that is not
in the document at all.

**HTPD fixes the input instead of the model.** It has two parts:

| | Part | What it does |
|---|---|---|
| **A** | **Converter** (`htpd`, C++) | Turns a PDF into HTPD: the exact text, cut into sections at the document's own headings, every paragraph with its page, tables kept as rows, code kept as code, headers and footers removed. |
| **B** | **Reader** (`htpd-read`, skill, MCP server, Python) | Lets any AI use an HTPD document: it finds the few sections that answer a question and hands over only that text - with pages - or says **"Not found in this file."** |

```mermaid
flowchart LR
    PDF["📄 PDF<br/>56 MB, 5652 pages"] -->|htpd convert| H["HTPD<br/>sections · pages · tree"]
    H -->|upload| CHAT["AI chat<br/>(file + skill)"]
    H -->|htpd-read / MCP| AGENT["AI agent<br/>(Claude Code, Antigravity, Codex)"]
    CHAT --> ANS["Answer from the file<br/>with page numbers"]
    AGENT --> ANS
```

<p align="center">
  <img src="docs/assets/features.svg" alt="Exact text - Every fact has a page - Says not found - 100 % local" width="100%">
</p>

<p align="center">
  <img src="docs/assets/before-after.svg" alt="A PDF page becomes a clean HTPD section with its place in the document and page marks" width="100%">
</p>

---

## Quick start

### Download (no build needed)

Get the files from the [**release page**](https://github.com/narendrachampaneri/htpd/releases):

| File | What it is |
|---|---|
| `htpd-0.1.0-linux-x64.tar.gz` | The converter, ready to run - Linux x64, Ubuntu 24.04 or newer, also WSL on Windows |
| `htpd-skill.zip` | The skill for AI apps ([how to add it](#2-apps-with-skills---the-htpd-skill)) |

```sh
tar xzf htpd-0.1.0-linux-x64.tar.gz
cd htpd-0.1.0-linux-x64
./htpd convert manual.pdf --single           # writes manual.htpd - give it to your AI
```

### Build from source

**Requirements:** Linux (tested on Ubuntu 24.04, also in WSL), g++ 13+, make or CMake, Python 3.10+.

```sh
git clone https://github.com/narendrachampaneri/htpd.git
cd htpd

./scripts/get_pdfium.sh        # downloads the PDF engine once (PDFium, about 4 MB)
make                           # builds build/htpd
make test                      # optional: the C++ tests
```

**Convert a PDF**

```sh
./build/htpd convert manual.pdf --single     # one file: manual.htpd  (for AI chats)
./build/htpd convert manual.pdf              # a folder: manual.htpd/ (for tools and agents)
```

**Ask a question from the command line**

```sh
python3 -m pip install -e .                  # installs htpd-read and htpd-mcp

# try it on the sample file that comes with the repository
htpd-read ask examples/single-file/sample.htpd "What is the maximum bus voltage?"
```

```
[page 2, 2.1.1 Limits]
  Parameter | Min | Typ | Max | Unit
  Bus voltage | 200 | 400 | 480 | V
...
```

Each answer line is copied word for word from the file, with its page and
section; `--context` adds the whole sections for the AI to read.

```sh
htpd-read ask examples/single-file/sample.htpd "Which OCPP version is supported?"
```

```
Not found in this file. (the file never mentions OCPP)
```

Then use your own file the same way: `htpd-read ask manual.htpd "your question"`.

---

## Use it with your AI

### 1. Any AI chat - just upload the file

`htpd convert manual.pdf --single` writes **one `.htpd` file**. It begins
with the reading rules for the model - *answer only from this file, give the
page of every fact, say "Not found in this file." when the answer is not
there* - followed by the contents and every section with its pages.

Upload it to Claude, ChatGPT or any other assistant instead of the PDF.
If an app does not accept the extension, rename it to `manual.htpd.txt` -
it is plain text.

> Best for documents that fit in the chat's context (a datasheet of a few
> hundred pages). For very large manuals, use the skill below.

### 2. Apps with skills - the `htpd` skill

Download `htpd-skill.zip` from the [release page](https://github.com/narendrachampaneri/htpd/releases),
or build it from the repository:

```sh
python3 scripts/build_skill.py               # writes dist/htpd-skill.zip
```

| App | How to add it |
|---|---|
| **Claude app** | *Customize → Skills*, upload `htpd-skill.zip`, switch it on (code execution must be on). Then upload a `.htpd` file to a chat and ask. |
| **Claude Code** | Unzip into `~/.claude/skills/` (gives `~/.claude/skills/htpd/`). |
| **Other agents** that read `SKILL.md` | Unzip into their skills folder. |

The skill runs a small script that needs **only Python** - no packages, no
model. It searches the file, returns the exact lines with `[page, section]`,
and says why when something is not in the file. Because the model reads only
what the script returns, this also works for manuals far too large for any
chat.

### 3. Agents with MCP - the `htpd-mcp` server

```sh
claude mcp add htpd -- htpd-mcp --bundles ~/manuals        # Claude Code
```

For Antigravity, Codex or any MCP client: command `htpd-mcp`, arguments
`--bundles /path/to/your/htpd/files`. Tools: `list_bundles`, `contents`,
`open_section`, `search`, `ask`, `check`. Standard library only.

---

## The format

An HTPD document is plain UTF-8 text: Markdown with a few fields. Any
model, editor or `grep` can read it.

<details>
<summary><b>See one section of an HTPD file</b> (shortened)</summary>

````markdown
<!-- htpd section {"n":14,"id":"sections/0014-4-1-5-maximum-frequency-of-main-modules","number":"4.1.5","level":3,"pages":[18,18,18],"parent":"sections/0008-4-1-chip-level-conditions"} -->
## [S14] 4.1.5 Maximum frequency of main modules

Path: 4 Electrical characteristics > 4.1 Chip-level conditions > 4.1.5 Maximum frequency of main modules - page 18

<!-- page 18 -->

```text
Cortex®-A55 cores | 0.9 GHz | 1.4 GHz | 1.7 GHz
DRAM | 933 MHz | 1400 MHz | 1866 MHz
NPU | 500 MHz | 800 MHz | 1000 MHz
```
````

</details>

- **Sections** follow the document's own headings (bookmarks, numbered headings, font sizes) and form a tree: parent, children, previous, next.
- **Page marks** `<!-- page N -->` stand above the text of every page.
- **Tables** are rows with ` | ` between cells; **code and listings** keep their spacing.
- **Two forms, same content:** a single `.htpd` file (for chats) and a bundle folder with one file per section (for tools), which is also a valid [Open Knowledge Format](https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md) bundle.

The full specification - every field and every rule - is in
[`docs/FORMAT.md`](docs/FORMAT.md). Examples:
[`examples/single-file/sample.htpd`](examples/single-file/sample.htpd) and
[`examples/sample.htpd/`](examples/sample.htpd/).

---

## Results

Measured with the converter on real public documents:

| Document | Pages | Sections | PDF → `.htpd` | Time |
|---|---:|---:|---|---:|
| NXP i.MX 93 Automotive Data Sheet | 109 | 109 | 1.7 MB → 230 KB | 0.6 s |
| NXP i.MX 93 Reference Manual | 5 652 | 5 641 | 56.3 MB → 11.6 MB | 14 s |
| RISC-V Debug Specification | 119 | 164 | – | 0.3 s |
| Arm AAPCS64 | 65 | 125 | – | 0.2 s |
| RISC-V Instruction Set Manual | 906 | 1 015 | – | 2.6 s |

The two i.MX 93 documents were converted to the single file on a 2-core
cloud machine; the other three, to bundle folders, with version 0.1.0.

**Robustness.** A stress test makes 34 difficult PDFs locally - two
columns, sideways tables, rotated pages, 1500 pages, huge tables, CJK and
Unicode text, encrypted, truncated and corrupted files, LaTeX and Word
exports - and checks that the converter never crashes or hangs, that every
bundle is valid, and that no word is lost compared with `pdftotext`. Earlier
versions were also run on 301 damaged PDFs from PDFium's test collection.

**Tests.** 70 C++ tests and 129 Python tests, run on every change. The
Python tests also run without PyYAML, as the skill does in a chat sandbox.

**Answering.** A first check on the i.MX 93 data sheet: of 12 questions
with known answers the right section came first for 7, and both questions
about things the data sheet does not contain (PCIe, HDMI) were answered
*"Not found in this file."* Measured numbers on public manuals (133 test
questions) are in [`eval/report.md`](eval/report.md), written by the
evaluation harness in [`eval/`](eval/README.md). On the test manuals the
reader finds the right section and page more often than plain word search,
puts the exact answer into the text it hands to the AI more often, and says
"Not found in this file." for almost every question the manuals do not
answer - while almost never saying it for one they do.

---

## Roadmap

HTPD is version 0.1. It works today; these are the known next steps.

- [x] Converter: sections, pages, tables, code, headers/footers, sideways pages, damaged files
- [x] Single-file `.htpd` with reading rules for AI models
- [x] Reader, word search, "not found" rule, `ask`, MCP server, skill for AI apps
- [ ] **Better search on very large manuals** - better ranking for register and table questions; a question on 5600 pages takes about 2 s from the command line (was 7 s), 0.02 s in the MCP server
- [ ] Optional meaning search (small open embedding model, CPU only) - measured, then kept only if it helps
- [x] Evaluation on public test sets, with the numbers in `eval/report.md` (questions still to be checked by a person)
- [x] **Fewer false "not found"** answers (see the report)
- [x] Rank the first section at least as well as plain BM25 over HTPD sections (see the report)
- [ ] OCR for scanned pages (Tesseract, local)
- [ ] Windows build

**Known limits:** no pictures or figures; formulas lose their layout;
scanned pages have no text yet; tables are rows of text (cells spanning
several lines are not joined); tested mainly with English technical
documents. Details: [`docs/CONVERTER.md`](docs/CONVERTER.md#limits-please-read).

---

## Project layout

<details>
<summary><b>Folders and documentation</b></summary>

```
include/ src/ cli/      the converter (C++23, PDFium is used in one file only)
reader/htpd_reader/     the reader: bundle, check, search, retriever, packer, ask, MCP server
skills/htpd/            the skill for AI apps (scripts/build_skill.py packs it)
eval/                   evaluation harness, baselines, report
model/                  training data and the optional decider model
tests/                  C++ tests, test PDFs, the stress test
docs/                   FORMAT.md · CONVERTER.md · PART-B.md · BACKGROUND.md
examples/               a converted document in both forms
```

| Document | For |
|---|---|
| [`docs/FORMAT.md`](docs/FORMAT.md) | The format: anyone can write a reader from it |
| [`docs/CONVERTER.md`](docs/CONVERTER.md) | Building, options, how the converter works, what was tested, limits |
| [`reader/README.md`](reader/README.md) | The reader, its commands and its Python API |
| [`docs/PART-B.md`](docs/PART-B.md) | The plan and measures for the reader |
| [`docs/BACKGROUND.md`](docs/BACKGROUND.md) | Why HTPD is built the way it is |
| [`AGENTS.md`](AGENTS.md) | Rules for contributors and coding agents |

</details>

---

## Contributing

Issues and pull requests are welcome. Before you change anything, read
[`AGENTS.md`](AGENTS.md); the short version:

- All tests pass before a change is merged: `make test` and `python3 -m pytest`.
- Every new rule in the code comes with a test that fails without it.
- Never lose text and never make text up.
- A change to what is written into an HTPD file updates `docs/FORMAT.md` in the same commit.

## License

HTPD is open source under the [Apache License 2.0](LICENSE): you may use,
change and share it, also in commercial products, as long as you keep the
copyright and license notices ([`NOTICE`](NOTICE)) and mark what you changed.
The license includes a patent grant from every contributor.

PDFium, which the converter uses, is not part of this repository and has its
own license (BSD-style with an Apache-2.0 part); after
`scripts/get_pdfium.sh` its license texts are in
`third_party/pdfium/licenses/`. If you give the program to others together
with `libpdfium.so`, give them those files too.

---

<div align="center">

Made by **Narendra Champaneri**

</div>
