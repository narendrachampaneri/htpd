# htpd_reader - the HTPD reader

Python 3.10+, standard library only. Reads HTPD files exactly as `docs/FORMAT.md` describes them.

## Install

From the root of the repository:

```sh
python3 -m pip install -e .        # gives the command htpd-read
python3 -m pip install pytest      # only to run the tests
python3 -m pytest                  # all Python tests
```

It needs nothing but Python; `pip install -e ".[yaml]"` adds PyYAML, which
reads the fields a little faster.

Without installing: `PYTHONPATH=reader python3 -m htpd_reader ...`.
Without pytest: `PYTHONPATH=reader:. python3 -m unittest discover -s reader/tests -t .`

## Command line

```sh
htpd-read contents manual.htpd [--depth 2]   # the tree with pages
htpd-read section  manual.htpd 3.1           # one section: ID, file name or number
htpd-read search   manual.htpd temperature exceeds
htpd-read ask      manual.htpd "What is the maximum NPU frequency?" [--context]
htpd-read check    manual.htpd [--pdf manual.pdf]
```

Every command takes `--json`. `check` lists every problem and ends with
exit code 1 when there is an error.

`ask` answers only from the file: it prints the exact lines (a table row with
its header row, a sentence, a code line) with their page and section, and
`--context` adds the sections they come from. When the file cannot answer it
prints `Not found in this file.` and why - for example when the question
names a term the file never uses ("the file never mentions PCIe"). Nothing
is downloaded and no GPU is used. How it works: `docs/READER.md`.

## Library

```python
from htpd_reader import open_bundle, check

b = open_bundle("manual.htpd")
s = b.find("2.1.1")                    # by number, ID or file name
for block in s.blocks:                 # kind: paragraph, table or code
    print(block.page, block.kind, block.text)
print(b.ancestors(s), b.next(s), b.children(s))
print(check(b))                        # [] for a good bundle
```

The text of a block is byte for byte the text of the bundle. The only change
is the rule of FORMAT.md 4.3: a backslash at the very start of a paragraph is
not part of the text. The tests prove this by writing every block back and
comparing the bytes.
