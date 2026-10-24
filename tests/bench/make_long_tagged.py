#!/usr/bin/env python3
"""Makes long tagged PDFs without bookmarks, to measure what the tag gate (`--tags auto`) costs and gives.

    python3 tests/bench/make_long_tagged.py build/bench/long [--pages 320]
    python3 tests/bench/run_bench.py build/bench/long --tag long

Two documents of the same text (every page: a heading, two paragraphs, a table of 4 x 4 cells, so about 40
tagged elements a page; a chapter heading every 8 pages), tagged like a Word export, no bookmarks:

  long-tagged-flat.pdf    the headings are set like body text (the tags alone say what a heading is)
                          -> the headings found without the tags are poor, the gate reads the tags
  long-tagged-styled.pdf  the headings are bold and larger -> good without the tags, the gate skips them

Needs nothing but the Python standard library: the PDF is written by hand (tests/data/make_test_pdfs.py).
"""
import argparse
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('make_test_pdfs', os.path.join(HERE, '..', 'data', 'make_test_pdfs.py'))
maker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(maker)

PARA = ('The controller supervises the power stage and the vehicle interface, samples the bus voltage',
        'and the output current every ten milliseconds and reports the values over the internal bus.')


def build(path, pages, styled):
    t = maker.Tagged(pages)
    body = []
    for p in range(pages):
        y = 690
        if p % 8 == 0:
            body.append(t.elem('H1', t.text(p, 72, y, 'Part %d' % (p // 8 + 1), size=16 if styled else 11,
                                            font='F2' if styled else 'F1', tag='H1')))
            y -= 30
        body.append(t.elem('H2', t.text(p, 72, y, 'Section %d.%d Limits and ratings' % (p // 8 + 1, p % 8 + 1),
                                        size=13 if styled else 11, font='F2' if styled else 'F1', tag='H2')))
        y -= 26
        for k in range(2):
            body.append(t.elem('P', *[t.text(p, 72, y - 14 * i, line) for i, line in enumerate(PARA)]))
            y -= 14 * len(PARA) + 10
        xs = (72, 190, 330, 450)
        rows = []
        for r in range(4):
            cells = []
            for c, x in enumerate(xs):
                text = ('Name', 'Description', 'Min', 'Max')[c] if r == 0 else (
                    'P%d' % (r * 7 + p % 13), 'Supply of stage %d' % r, str(r), str(r * 10 + c))[c]
                cells.append(t.elem('TH' if r == 0 else 'TD', t.elem('P', t.text(p, x, y, text))))
            rows.append(t.elem('TR', *cells))
            y -= 16
        body.append(t.elem('Table', *rows))
    t.write(path, t.elem('Document', *body))


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out', help='folder for the PDFs')
    ap.add_argument('--pages', type=int, default=320)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    build(os.path.join(args.out, 'long-tagged-flat.pdf'), args.pages, styled=False)
    build(os.path.join(args.out, 'long-tagged-styled.pdf'), args.pages, styled=True)
    print('wrote long-tagged-flat.pdf and long-tagged-styled.pdf (%d pages) to %s' % (args.pages, args.out))
