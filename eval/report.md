# HTPD part B - evaluation report

Written by `python -m eval report` from `eval/results/`. Never edit by hand.

Recall@k: a gold section is among the first k. MRR: mean of 1/rank of the first gold section.
Page: the first page the system points to is a gold page. Not found: share of questions
about things the document does not hold that get "not found". False not found: share of
questions the document does answer that still get "not found" (lower is better). Tokens: words and
punctuation marks handed to the AI (one measure for all systems); "of whole" is the
share of the whole document.

## Split: test

### Phase 2 - baselines

| System | Questions | R@1 | R@5 | R@10 | MRR | Page | Not found | False not found | Tokens | Of whole | ms/question |
|---|---|---|---|---|---|---|---|---|---|---|---|
| B0 whole document | 133 | - | - | - | - | - | 0.000 | 0.000 | 45556 | 100.0 % | 0.0 |
| B1 chunks, BM25 | 133 | 0.360 | 0.712 | 0.829 | 0.512 | 0.505 | 0.000 | 0.000 | 2610 | 6.4 % | 0.3 |
| B3 HTPD sections, BM25 | 133 | 0.649 | 0.874 | 0.928 | 0.748 | 0.541 | 0.000 | 0.000 | 2194 | 5.4 % | 0.3 |

Bundles: aapcs64, riscv-debug. Run on 2026-10-20, Linux x86_64, Python 3.13.16.

### Phase 3 - retriever and packer

| System | Questions | R@1 | R@5 | R@10 | MRR | Page | Not found | False not found | Tokens | Of whole | ms/question |
|---|---|---|---|---|---|---|---|---|---|---|---|
| HTPD: words + tree (no model) | 133 | 0.423 | 0.658 | 0.748 | 0.539 | 0.297 | 1.000 | 0.369 | 774 | 1.9 % | 5.6 |

Bundles: aapcs64, riscv-debug. Run on 2026-10-20, Linux x86_64, Python 3.13.16.
