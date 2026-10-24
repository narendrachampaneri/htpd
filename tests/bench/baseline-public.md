# HTPD bench: baseline-public

32 documents, 0 errors. Converter `2b57ca1+dirty`, 2026-10-24 03:35:22, best of 3 runs, 4 CPUs, no Tesseract.

## Speed and size

| document | pages | time s | cpu s | size B | figures B |
|---|---:|---:|---:|---:|---:|
| data/actualtext | 1 | 0.006 | 0.006 | 2,878 | 0 |
| data/figures | 2 | 0.018 | 0.020 | 21,322 | 17,191 |
| data/fonts | 1 | 0.007 | 0.007 | 3,101 | 0 |
| data/links | 3 | 0.006 | 0.006 | 4,083 | 0 |
| data/lowlevel | 4 | 0.007 | 0.007 | 4,702 | 0 |
| data/ocr | 3 | 0.061 | 0.062 | 33,944 | 29,644 |
| data/sample | 5 | 0.012 | 0.011 | 14,139 | 0 |
| data/sample_nobookmarks | 5 | 0.012 | 0.011 | 14,178 | 0 |
| data/sideways | 4 | 0.007 | 0.006 | 3,963 | 0 |
| eval/aaelf64 | 45 | 0.092 | 0.092 | 131,499 | 0 |
| eval/aapcs64 | 65 | 0.154 | 0.166 | 264,328 | 52,643 |
| eval/riscv-debug | 119 | 0.710 | 0.780 | 719,694 | 328,882 |
| eval/riscv-isa | 670 | 1.768 | 2.109 | 3,043,861 | 1,273,119 |
| eval/riscv-priv | 172 | 0.613 | 0.736 | 1,229,356 | 630,223 |
| zoo/datasheet-slides | 19 | 0.052 | 0.122 | 83,003 | 71,127 |
| zoo/datasheet-web | 3 | 0.041 | 0.043 | 77,871 | 65,674 |
| zoo/datasheet-word | 3 | 0.008 | 0.007 | 10,137 | 0 |
| zoo/german-slides | 7 | 0.042 | 0.088 | 61,394 | 55,447 |
| zoo/german-web | 1 | 0.008 | 0.007 | 4,501 | 0 |
| zoo/german-word | 1 | 0.004 | 0.004 | 4,120 | 0 |
| zoo/gujarati-slides | 9 | 0.052 | 0.117 | 75,891 | 68,555 |
| zoo/gujarati-web | 1 | 0.012 | 0.011 | 5,747 | 0 |
| zoo/gujarati-word | 1 | 0.006 | 0.006 | 5,118 | 0 |
| zoo/hindi-slides | 7 | 0.045 | 0.096 | 56,437 | 49,957 |
| zoo/hindi-web | 1 | 0.009 | 0.009 | 5,099 | 0 |
| zoo/hindi-word | 1 | 0.005 | 0.005 | 4,601 | 0 |
| zoo/manual-slides | 16 | 0.057 | 0.155 | 109,741 | 99,359 |
| zoo/manual-web | 2 | 0.029 | 0.032 | 52,133 | 42,497 |
| zoo/manual-word | 3 | 0.048 | 0.047 | 25,523 | 16,517 |
| zoo/report-slides | 13 | 0.053 | 0.125 | 89,892 | 80,688 |
| zoo/report-web | 2 | 0.012 | 0.012 | 7,663 | 0 |
| zoo/report-word | 2 | 0.006 | 0.005 | 6,940 | 0 |
| **TOTAL** | 1,191 | 3.963 | 4.910 | 6,176,859 | 2,881,523 |

## Text (pdftotext -layout against the page marks)

| document | in order | on page | pages ok | of | lost | moved | invented | missing pieces | extra pieces | headers kept |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| data/actualtext | - | - | - | - | - | - | - | - | - | - |
| data/figures | 100.0000% | 100.0000% | 2 | 2 | 0 | 0 | 0 | 0 | 0 | 0 |
| data/fonts | 100.0000% | 100.0000% | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 |
| data/links | 100.0000% | 100.0000% | 3 | 3 | 0 | 0 | 0 | 0 | 0 | 0 |
| data/lowlevel | 84.7682% | 95.3642% | 2 | 4 | 7 | 0 | 0 | 17 | 16 | 0 |
| data/ocr | 100.0000% | 100.0000% | 2 | 2 | 0 | 0 | 0 | 0 | 0 | 0 |
| data/sample | 100.0000% | 100.0000% | 5 | 5 | 0 | 0 | 0 | 0 | 0 | 0 |
| data/sample_nobookmarks | 100.0000% | 100.0000% | 5 | 5 | 0 | 0 | 0 | 0 | 0 | 0 |
| data/sideways | 97.2603% | 97.2603% | 2 | 3 | 2 | 0 | 12 | 1 | 1 | 0 |
| eval/aaelf64 | 98.7648% | 99.6136% | 30 | 45 | 56 | 0 | 28 | 154 | 134 | 0 |
| eval/aapcs64 | 99.5590% | 99.9412% | 55 | 65 | 14 | 0 | 3 | 48 | 44 | 3 |
| eval/riscv-debug | 98.0129% | 99.3975% | 35 | 119 | 238 | 0 | 11 | 338 | 207 | 0 |
| eval/riscv-isa | 99.0727% | 99.4672% | 527 | 666 | 1,080 | 15 | 43 | 432 | 233 | 0 |
| eval/riscv-priv | 98.1252% | 99.4072% | 103 | 172 | 444 | 11 | 5 | 449 | 198 | 0 |
| zoo/datasheet-slides | 91.2621% | 99.0291% | 15 | 19 | 5 | 0 | 0 | 15 | 10 | 0 |
| zoo/datasheet-web | 98.7365% | 98.7365% | 2 | 3 | 7 | 0 | 1 | 2 | 1 | 0 |
| zoo/datasheet-word | 96.9052% | 100.0000% | 2 | 3 | 0 | 0 | 0 | 8 | 8 | 0 |
| zoo/german-slides | 100.0000% | 100.0000% | 6 | 7 | 0 | 0 | 1 | 0 | 1 | 0 |
| zoo/german-web | 84.0426% | 100.0000% | 0 | 1 | 0 | 0 | 0 | 1 | 1 | 0 |
| zoo/german-word | 100.0000% | 100.0000% | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 |
| zoo/gujarati-slides | 94.1860% | 94.1860% | 6 | 9 | 5 | 0 | 4 | 3 | 3 | 0 |
| zoo/gujarati-web | 43.5185% | 55.5556% | 0 | 1 | 48 | 0 | 81 | 22 | 21 | 0 |
| zoo/gujarati-word | 94.1860% | 94.1860% | 0 | 1 | 5 | 0 | 4 | 2 | 2 | 0 |
| zoo/hindi-slides | 82.6667% | 82.6667% | 4 | 7 | 13 | 0 | 10 | 6 | 5 | 0 |
| zoo/hindi-web | 52.0833% | 65.6250% | 0 | 1 | 33 | 0 | 52 | 18 | 18 | 0 |
| zoo/hindi-word | 78.2051% | 78.2051% | 0 | 1 | 17 | 0 | 7 | 6 | 6 | 0 |
| zoo/manual-slides | 92.6097% | 100.0000% | 14 | 16 | 0 | 0 | 13 | 4 | 2 | 0 |
| zoo/manual-web | 97.0894% | 100.0000% | 0 | 2 | 0 | 0 | 0 | 3 | 2 | 0 |
| zoo/manual-word | 100.0000% | 100.0000% | 3 | 3 | 0 | 0 | 0 | 0 | 0 | 0 |
| zoo/report-slides | 92.0308% | 100.0000% | 12 | 13 | 0 | 0 | 0 | 3 | 1 | 0 |
| zoo/report-web | 96.6184% | 100.0000% | 0 | 2 | 0 | 0 | 0 | 2 | 2 | 0 |
| zoo/report-word | 100.0000% | 100.0000% | 2 | 2 | 0 | 0 | 0 | 0 | 0 | 0 |
| **TOTAL** | 98.7063% | 99.4564% | 839 | 1,184 | 1,974 | 26 | 275 | 1,534 | 916 | 3 |

## Headings, tables, figures

| document | headings found | missing | extra | rows found | of | rows in file | stray | figures found | of | in file | notes ok |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| data/actualtext | - | - | - | - | - | 0 | - | - | - | 0 | 1 |
| data/figures | - | - | - | 4 | 4 | 5 | 1 | 1 | 1 | 3 | 1 |
| data/fonts | - | - | - | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 |
| data/links | 4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 |
| data/lowlevel | - | - | - | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 |
| data/ocr | - | - | - | 0 | 0 | 0 | 0 | 0 | 0 | 3 | 1 |
| data/sample | 13 | 0 | 1 | 10 | 10 | 10 | 0 | 0 | 0 | 0 | 1 |
| data/sample_nobookmarks | - | - | - | 10 | 10 | 10 | 0 | 0 | 0 | 0 | 1 |
| data/sideways | - | - | - | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 |
| eval/aaelf64 | 86 | 0 | 1 | 104 | 129 | 253 | 129 | 0 | 0 | 0 | 1 |
| eval/aapcs64 | 124 | 0 | 1 | 127 | 241 | 176 | 34 | 0 | 0 | 5 | 1 |
| eval/riscv-debug | 164 | 0 | 0 | 409 | 489 | 582 | 112 | 2 | 2 | 3 | 1 |
| eval/riscv-isa | 759 | 0 | 1 | 2,544 | 3,216 | 3,912 | 165 | 8 | 8 | 25 | 1 |
| eval/riscv-priv | 194 | 0 | 0 | 417 | 722 | 672 | 22 | 112 | 115 | 120 | 1 |
| zoo/datasheet-slides | 19 | 0 | 0 | 20 | 23 | 26 | 6 | 0 | 0 | 5 | 1 |
| zoo/datasheet-web | - | - | - | 26 | 27 | 27 | 1 | 0 | 0 | 1 | 1 |
| zoo/datasheet-word | 15 | 0 | 1 | 24 | 24 | 26 | 2 | 0 | 0 | 0 | 1 |
| zoo/german-slides | 7 | 0 | 0 | 0 | 0 | 5 | 0 | 0 | 0 | 4 | 1 |
| zoo/german-web | - | - | - | 0 | 0 | 5 | 0 | 0 | 0 | 0 | 1 |
| zoo/german-word | 3 | 0 | 1 | 0 | 0 | 5 | 0 | 0 | 0 | 0 | 1 |
| zoo/gujarati-slides | 9 | 0 | 0 | 0 | 0 | 4 | 0 | 0 | 0 | 5 | 1 |
| zoo/gujarati-web | - | - | - | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 |
| zoo/gujarati-word | 4 | 0 | 1 | 0 | 0 | 4 | 0 | 0 | 0 | 0 | 1 |
| zoo/hindi-slides | 6 | 1 | 1 | 0 | 0 | 4 | 0 | 0 | 0 | 4 | 1 |
| zoo/hindi-web | - | - | - | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 |
| zoo/hindi-word | 3 | 0 | 1 | 0 | 0 | 4 | 0 | 0 | 0 | 0 | 1 |
| zoo/manual-slides | 16 | 0 | 0 | 2 | 5 | 12 | 3 | 0 | 0 | 7 | 1 |
| zoo/manual-web | - | - | - | 5 | 5 | 12 | 0 | 0 | 0 | 1 | 1 |
| zoo/manual-word | 11 | 0 | 1 | 5 | 5 | 12 | 0 | 0 | 0 | 1 | 1 |
| zoo/report-slides | 13 | 0 | 0 | 2 | 4 | 4 | 2 | 0 | 0 | 6 | 1 |
| zoo/report-web | - | - | - | 4 | 4 | 4 | 0 | 0 | 0 | 0 | 1 |
| zoo/report-word | 8 | 0 | 1 | 4 | 4 | 4 | 0 | 0 | 0 | 0 | 1 |
| **TOTAL** | 1,458 | 1 | 11 | 3,717 | 4,922 | 5,778 | 477 | 123 | 126 | 193 | 32 |

Headings come from: data/actualtext: none, data/figures: none, data/fonts: none, data/links: bookmarks, data/lowlevel: none, data/ocr: none, data/sample: bookmarks, data/sample_nobookmarks: none, data/sideways: none, eval/aaelf64: bookmarks, eval/aapcs64: bookmarks, eval/riscv-debug: bookmarks, eval/riscv-isa: bookmarks, eval/riscv-priv: bookmarks, zoo/datasheet-slides: bookmarks, zoo/datasheet-web: none, zoo/datasheet-word: bookmarks, zoo/german-slides: bookmarks, zoo/german-web: none, zoo/german-word: bookmarks, zoo/gujarati-slides: bookmarks, zoo/gujarati-web: none, zoo/gujarati-word: bookmarks, zoo/hindi-slides: bookmarks, zoo/hindi-web: none, zoo/hindi-word: bookmarks, zoo/manual-slides: bookmarks, zoo/manual-web: none, zoo/manual-word: bookmarks, zoo/report-slides: bookmarks, zoo/report-web: none, zoo/report-word: bookmarks.

Documents without a text layer (scans) have no PDF text to compare: their text, table and figure columns show `-`.

