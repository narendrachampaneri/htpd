# htpd_reader - part B of HTPD

Python 3.10+. Reads HTPD bundles exactly as `docs/FORMAT.md` describes them.

## Install

From the root of the repository:

```sh
python3 -m pip install -e .        # gives the command htpd-read
python3 -m pip install pytest      # only to run the tests
python3 -m pytest                  # all Python tests
```

Without installing: `PYTHONPATH=reader python3 -m htpd_reader ...`.
Without pytest: `PYTHONPATH=reader:. python3 -m unittest discover -s reader/tests -t .`

## Command line

```sh
htpd-read contents manual.htpd [--depth 2]   # the tree with pages
htpd-read section  manual.htpd 3.1           # one section: ID, file name or number
htpd-read search   manual.htpd temperature exceeds
htpd-read check    manual.htpd [--pdf manual.pdf]
```

Every command takes `--json`. `check` lists every problem and ends with
exit code 1 when there is an error.

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
