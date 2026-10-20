# HTPD part B - evaluation report

Written by `python -m eval report` from `eval/results/`. Never edit by hand.

Recall@k: a gold section is among the first k. MRR: mean of 1/rank of the first gold section.
Page: the first page the system points to is a gold page. Not found: share of questions
about things the document does not hold that get "not found". False not found: share of
questions the document does answer that still get "not found" (lower is better). Answer in text:
share of answerable questions whose exact answer stands in the text handed to the AI. Tokens: words and
punctuation marks handed to the AI (one measure for all systems); "of whole" is the
share of the whole document.

## Split: test

### Phase 2 - baselines

| System | Questions | R@1 | R@5 | R@10 | MRR | Page | Not found | False not found | Answer in text | Tokens | Of whole | ms/question |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| B0 whole document | 133 | - | - | - | - | - | 0.000 | 0.000 | 1.000 | 45556 | 100.0 % | 0.0 |
| B1 chunks, BM25 | 133 | 0.360 | 0.712 | 0.829 | 0.512 | 0.505 | 0.000 | 0.000 | 0.829 | 2610 | 6.4 % | 0.6 |
| B3 HTPD sections, BM25 | 133 | 0.649 | 0.874 | 0.928 | 0.748 | 0.541 | 0.000 | 0.000 | 0.838 | 2194 | 5.4 % | 0.7 |

Bundles: aapcs64, riscv-debug. Run on 2026-10-20, Linux x86_64, Python 3.13.16.

### Phase 3 - retriever and packer

| System | Questions | R@1 | R@5 | R@10 | MRR | Page | Not found | False not found | Answer in text | Tokens | Of whole | ms/question |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| HTPD: words + tree (no model) | 133 | 0.667 | 0.892 | 0.928 | 0.763 | 0.568 | 0.955 | 0.009 | 0.856 | 2457 | 6.1 % | 17.2 |

Bundles: aapcs64, riscv-debug. Run on 2026-10-20, Linux x86_64, Python 3.13.16.

## Split: tune

### Phase 2 - baselines

| System | Questions | R@1 | R@5 | R@10 | MRR | Page | Not found | False not found | Answer in text | Tokens | Of whole | ms/question |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| B0 whole document | 121 | - | - | - | - | - | 0.000 | 0.000 | 1.000 | 64510 | 100.0 % | 0.0 |
| B1 chunks, BM25 | 121 | 0.340 | 0.845 | 0.938 | 0.543 | 0.670 | 0.000 | 0.000 | 0.887 | 2712 | 7.1 % | 0.6 |
| B3 HTPD sections, BM25 | 121 | 0.763 | 0.938 | 0.979 | 0.839 | 0.691 | 0.000 | 0.000 | 0.907 | 2795 | 5.8 % | 0.7 |

Bundles: aaelf64, riscv-priv. Run on 2026-10-20, Linux x86_64, Python 3.13.16.

### Phase 3 - retriever and packer

| System | Questions | R@1 | R@5 | R@10 | MRR | Page | Not found | False not found | Answer in text | Tokens | Of whole | ms/question |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| HTPD: words + tree (no model) | 121 | 0.804 | 0.918 | 0.969 | 0.862 | 0.763 | 0.958 | 0.000 | 0.918 | 2324 | 6.1 % | 18.2 |

Bundles: aaelf64, riscv-priv. Run on 2026-10-20, Linux x86_64, Python 3.13.16.
