# HTPD evaluation report

Written by `python -m eval report` from `eval/results/`. Never edit by hand.

Recall@k: a gold section is among the first k. MRR: mean of 1/rank of the first gold section.
Page: the first page the system points to is a gold page. Not found: share of questions
about things the document does not hold that get "not found". False not found: share of
questions the document does answer that still get "not found" (lower is better). Answer in text:
share of answerable questions whose exact answer stands in the text handed to the AI. Tokens: words and
punctuation marks handed to the AI (one measure for all systems); "of whole" is the
share of the whole document.

## Split: test

### Baselines

| System | Questions | R@1 | R@5 | R@10 | MRR | Page | Not found | False not found | Answer in text | Tokens | Of whole | ms/question |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| B0 whole document | 133 | - | - | - | - | - | 0.000 | 0.000 | 1.000 | 46069 | 100.0 % | 0.0 |
| B1 chunks, BM25 | 133 | 0.324 | 0.721 | 0.820 | 0.491 | 0.505 | 0.000 | 0.000 | 0.829 | 2715 | 6.5 % | 0.3 |
| B3 HTPD sections, BM25 | 133 | 0.640 | 0.865 | 0.928 | 0.744 | 0.550 | 0.000 | 0.000 | 0.829 | 2224 | 5.4 % | 0.3 |

Bundles: aapcs64, riscv-debug. Run on 2026-10-23, Linux x86_64, Python 3.13.16.

### HTPD

| System | Questions | R@1 | R@5 | R@10 | MRR | Page | Not found | False not found | Answer in text | Tokens | Of whole | ms/question |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| HTPD reader (words, tree, re-ranker) | 133 | 0.784 | 0.946 | 0.955 | 0.851 | 0.640 | 0.955 | 0.009 | 0.856 | 2451 | 6.0 % | 17.0 |
| HTPD reader without the re-ranker | 133 | 0.658 | 0.892 | 0.937 | 0.757 | 0.604 | 0.955 | 0.009 | 0.856 | 2461 | 6.0 % | 7.1 |

Bundles: aapcs64, riscv-debug. Run on 2026-10-23, Linux x86_64, Python 3.13.16.

## Split: tune

### Baselines

| System | Questions | R@1 | R@5 | R@10 | MRR | Page | Not found | False not found | Answer in text | Tokens | Of whole | ms/question |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| B0 whole document | 121 | - | - | - | - | - | 0.000 | 0.000 | 1.000 | 65049 | 100.0 % | 0.0 |
| B1 chunks, BM25 | 121 | 0.289 | 0.866 | 0.959 | 0.514 | 0.691 | 0.000 | 0.000 | 0.938 | 2688 | 7.0 % | 0.3 |
| B3 HTPD sections, BM25 | 121 | 0.763 | 0.938 | 0.979 | 0.839 | 0.701 | 0.000 | 0.000 | 0.907 | 2810 | 5.8 % | 0.5 |

Bundles: aaelf64, riscv-priv. Run on 2026-10-23, Linux x86_64, Python 3.13.16.

### HTPD

| System | Questions | R@1 | R@5 | R@10 | MRR | Page | Not found | False not found | Answer in text | Tokens | Of whole | ms/question |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| HTPD reader (words, tree, re-ranker) | 121 | 0.897 | 0.979 | 0.979 | 0.932 | 0.825 | 0.958 | 0.000 | 0.959 | 2341 | 6.1 % | 21.4 |
| HTPD reader without the re-ranker | 121 | 0.804 | 0.928 | 0.969 | 0.862 | 0.773 | 0.958 | 0.000 | 0.918 | 2327 | 6.1 % | 11.6 |

Bundles: aaelf64, riscv-priv. Run on 2026-10-23, Linux x86_64, Python 3.13.16.
