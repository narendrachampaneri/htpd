#!/usr/bin/env python3
"""Makes PDFs with tables whose cells are known, for the benchmark (run_bench.py --set tables).

    python3 tests/bench/make_tables.py build/tables

For every case it writes <case>.pdf and <case>.truth.json: the cells of the table as a person
reads them, row by row, with the page of each row. run_bench.py converts the PDF and scores the
result cell by cell. Needs reportlab only; the PDFs are the same on every run.

The truth of a cell that wraps over several lines is the whole text (a name broken inside itself
- "LCD_P2" over "4" - is one name: "LCD_P24"). A cell that spans columns or rows is written as in the
format (FORMAT.md 4.6): its text, a space and `[>]` (one more column), `[>>]` (two more) or `[=]`
(one more row); the cells it covers are empty strings.

The cases:

    grid               ruled grid
    booktabs           horizontal rules only (top, below the header, bottom)
    stream             no rules at all
    merged             cells over columns and over rows (ruled)
    header2            a header of two levels (ruled)
    wrapped            cells of several lines
    stacked            a label cell with four lines and four stacked values beside it
    stacked_stream     the same without rules
    conditions         conditions stacked in a cell as list items, a value on each line
    wrapped_ids        identifiers broken inside themselves (LCD_P24, US-GAAP-1010)
    wrapped_mono       the same in a monospace font (CAN_FD_REG_0)
    wrapped_money      money values broken before the closing bracket ("($1,433,600.00)")
    empty              empty cells
    multipage          3 pages, the header repeats (horizontal rules)
    multipage_stream   3 pages, the header repeats, no rules
    multipage_plain    3 pages, the header does not repeat
    registers          register map: offset, name, acronym, access, reset, description
    ledger             financial ledger (ruled)
    datasheet          electrical characteristics (no rules)
    registers_narrow   the register map in columns so narrow that a header cell breaks inside itself
    registers_stream   the register map without rules
    mono_names         names and numbers in a monospace font, the description in the text font
    mono_table         a table all in a monospace font (no rules)
    wrapped_middle     cells set in the middle of tall rows (ruled)
    wrapped_middle_stream   the same without rules
    rowrules           a hairline under every row
    tight              numbers in narrow columns (a LaTeX-like table)
    wrapped_first      the first column wraps too
    chart              a chart with grid lines: NOT a table
"""

from __future__ import annotations

import argparse
import json
import random
import sys
from pathlib import Path

from reportlab import rl_config
from reportlab.lib import colors
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfgen.canvas import Canvas
from reportlab.graphics.shapes import Drawing, Line, Rect, String
from reportlab.platypus import Paragraph, SimpleDocTemplate, Spacer, Table, TableStyle

rl_config.invariant = 1

W, H = letter
BODY = ParagraphStyle("body", fontName="Helvetica", fontSize=10, leading=13, spaceAfter=8)
TITLE = ParagraphStyle("title", fontName="Helvetica-Bold", fontSize=14, leading=18, spaceAfter=8)
CELL = ParagraphStyle("cell", fontName="Helvetica", fontSize=8.5, leading=10.5)
CELLB = ParagraphStyle("cellb", parent=CELL, fontName="Helvetica-Bold")
MONO = ParagraphStyle("mono", parent=CELL, fontName="Courier", fontSize=8)
RIGHT = ParagraphStyle("right", parent=CELL, alignment=2)


def esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def P(text: str, style: ParagraphStyle = CELL) -> Paragraph:
    return Paragraph(esc(text), style)


def table(data, widths, style, repeat=0, align="LEFT", cell_style=CELL, bold_rows=(0,)):
    """A reportlab table. Strings become paragraphs (so they wrap); '' stays empty."""
    rows = []
    for r, row in enumerate(data):
        rows.append([P(c, CELLB if r in bold_rows else cell_style) if isinstance(c, str) and c else c
                     for c in row])
    t = Table(rows, colWidths=widths, repeatRows=repeat, hAlign=align)
    t.setStyle(TableStyle(style))
    return t


GRID = [("GRID", (0, 0), (-1, -1), 0.5, colors.black), ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3)]
BOOK = [("LINEABOVE", (0, 0), (-1, 0), 1.0, colors.black), ("LINEBELOW", (0, 0), (-1, 0), 0.6, colors.black),
        ("LINEBELOW", (0, -1), (-1, -1), 1.0, colors.black), ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3)]
PLAIN = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("TOPPADDING", (0, 0), (-1, -1), 3),
         ("BOTTOMPADDING", (0, 0), (-1, -1), 3), ("LEFTPADDING", (0, 0), (-1, -1), 6),
         ("RIGHTPADDING", (0, 0), (-1, -1), 12)]

INTRO = ("The values below were measured on the reference board at 25 degrees C unless a condition says "
         "otherwise. They are given as a guide; the limits of the data sheet apply.")
OUTRO = "All values are typical for the production lot. Contact support for the full test report."


def story(case: str, flow, extra_after=True):
    out = [Paragraph(f"Table report: {case.replace('_', ' ')}", TITLE), Paragraph(INTRO, BODY), Spacer(1, 6)]
    out += flow if isinstance(flow, list) else [flow]
    if extra_after:
        out += [Spacer(1, 10), Paragraph(OUTRO, BODY)]
    return out


# ---------------------------------------------------------------------------------------------
# The cases. Each returns (flowables, truth) where truth = {"rows": [[cell, ...], ...], "pages": [page of each row],
# "header_rows": n}. "pages" is filled in by page_of_rows() when a table runs over pages.


def case_grid():
    data = [["Parameter", "Symbol", "Min", "Typ", "Max", "Unit"],
            ["Supply voltage", "VDD", "3.0", "3.3", "3.6", "V"],
            ["Supply current", "IDD", "12", "18", "25", "mA"],
            ["Standby current", "ISB", "0.4", "0.9", "2.0", "uA"],
            ["Input high voltage", "VIH", "2.0", "1.8", "5.5", "V"],
            ["Input low voltage", "VIL", "-0.3", "0.0", "0.8", "V"],
            ["Output current", "IO", "0", "100", "200", "mA"],
            ["Junction temperature", "TJ", "-40", "25", "125", "C"],
            ["Clock frequency", "fCK", "1", "48", "72", "MHz"]]
    return [table(data, [110, 55, 50, 50, 50, 50], GRID)], {"rows": data, "header_rows": 1}


def case_booktabs():
    data = [["Method", "Accuracy", "F1", "Params", "Latency"],
            ["Baseline", "71.2", "68.4", "12M", "14 ms"],
            ["Ours small", "74.9", "72.0", "9M", "11 ms"],
            ["Ours base", "78.3", "76.1", "31M", "19 ms"],
            ["Ours large", "80.7", "78.8", "110M", "42 ms"],
            ["Ensemble", "82.1", "80.2", "152M", "75 ms"]]
    return [table(data, [90, 70, 60, 60, 70], BOOK)], {"rows": data, "header_rows": 1}


def case_stream():
    data = [["Item", "Quantity", "Weight", "Colour", "Price"],
            ["Bracket", "120", "0.4 kg", "grey", "3.20"],
            ["Housing", "40", "1.9 kg", "black", "18.50"],
            ["Cable set", "75", "0.7 kg", "red", "7.10"],
            ["Fan unit", "30", "0.9 kg", "white", "12.00"],
            ["Filter", "200", "0.1 kg", "blue", "1.45"],
            ["Mount plate", "55", "2.2 kg", "grey", "9.80"]]
    return [table(data, [90, 70, 70, 70, 60], PLAIN)], {"rows": data, "header_rows": 1}


def case_merged():
    data = [["Region [=]", "Area [=]", "Sales (k$) [>>]", "", ""],
            ["", "", "Q1", "Q2", "Q3"],
            ["North [=]", "Urban", "10", "11", "12"],
            ["", "Rural", "5", "6", "7"],
            ["South [=]", "Urban", "20", "22", "21"],
            ["", "Rural", "8", "9", "9"],
            ["East", "All", "14", "15", "17"]]
    style = GRID + [("SPAN", (0, 0), (0, 1)), ("SPAN", (1, 0), (1, 1)), ("SPAN", (2, 0), (4, 0)),
                    ("SPAN", (0, 2), (0, 3)), ("SPAN", (0, 4), (0, 5))]
    shown = [["Region", "Area", "Sales (k$)", "", ""], ["", "", "Q1", "Q2", "Q3"],
             ["North", "Urban", "10", "11", "12"], ["", "Rural", "5", "6", "7"],
             ["South", "Urban", "20", "22", "21"], ["", "Rural", "8", "9", "9"],
             ["East", "All", "14", "15", "17"]]
    return [table(shown, [70, 60, 55, 55, 55], style)], {"rows": data, "header_rows": 2}


def case_header2():
    data = [["Signal [=]", "Setup time (ns) [>]", "", "Hold time (ns) [>]", ""],
            ["", "Min", "Max", "Min", "Max"],
            ["DATA0", "1.2", "3.4", "0.5", "1.1"],
            ["DATA1", "1.3", "3.5", "0.5", "1.2"],
            ["ADDR", "2.0", "4.1", "0.8", "1.6"],
            ["CLK", "0.4", "0.9", "0.2", "0.4"]]
    shown = [["Signal", "Setup time (ns)", "", "Hold time (ns)", ""], ["", "Min", "Max", "Min", "Max"]] + data[2:]
    style = GRID + [("SPAN", (0, 0), (0, 1)), ("SPAN", (1, 0), (2, 0)), ("SPAN", (3, 0), (4, 0))]
    return [table(shown, [70, 60, 60, 60, 60], style)], {"rows": data, "header_rows": 2}


def case_wrapped():
    data = [["Name", "Description", "Notes"],
            ["Watchdog", "The watchdog timer resets the controller when the main loop does not "
                         "service it within the configured timeout period.", "Enable after boot"],
            ["Brown-out", "Supervises the supply voltage and holds the controller in reset while "
                          "it is below the threshold.", "Threshold is set in the fuse bits"],
            ["Sleep", "Stops the CPU clock and keeps the RAM content.", "Wake by interrupt"],
            ["Calibration", "Stored in the last page of flash. It is read at start and written "
                            "only by the service tool.", "Do not erase"]]
    return [table(data, [70, 210, 100], GRID)], {"rows": data, "header_rows": 1}


def case_stacked():
    head = ["Parameter", "Level", "Min", "Typ", "Max", "Unit"]
    levels = ["Level 1 falling", "Level 2 falling", "Level 3 falling", "Level 4 falling"]
    vals = [("2.62", "2.70", "2.78"), ("2.35", "2.41", "2.47"), ("2.12", "2.20", "2.26"), ("1.85", "1.91", "1.97")]
    rows = [head] + [[levels[i], *vals[i], "V"] for i in range(4)]
    # One tall row: the label cell has four lines, each value cell four lines, the unit is repeated
    # on every line so that every sub-row stands alone.
    label = "\n".join(levels)
    tall = [label] + ["\n".join(v[k] for v in vals) for k in range(3)] + ["\n".join(["V"] * 4)]
    tab_rows = [[P(c, CELLB) for c in ["Threshold", "Min", "Typ", "Max", "Unit"]],
                [Paragraph("<br/>".join(esc(x) for x in tall[0].split("\n")), CELL)] +
                [Paragraph("<br/>".join(esc(x) for x in t.split("\n")), CELL) for t in tall[1:]]]
    t = Table(tab_rows, colWidths=[110, 55, 55, 55, 40])
    t.setStyle(TableStyle(GRID))
    truth_rows = [["Threshold", "Min", "Typ", "Max", "Unit"]] + [[levels[i], *vals[i], "V"] for i in range(4)]
    return [t], {"rows": truth_rows, "header_rows": 1}


def case_stacked_stream():
    levels = ["Level 1 falling", "Level 2 falling", "Level 3 falling", "Level 4 falling"]
    vals = [("2.62", "2.70", "2.78"), ("2.35", "2.41", "2.47"), ("2.12", "2.20", "2.26"), ("1.85", "1.91", "1.97")]
    tall = ["<br/>".join(levels)] + ["<br/>".join(v[k] for v in vals) for k in range(3)] + ["<br/>".join(["V"] * 4)]
    head = [P(c, CELLB) for c in ["Threshold", "Min", "Typ", "Max", "Unit"]]
    body = [Paragraph(t, CELL) for t in tall]
    t = Table([head, body], colWidths=[110, 55, 55, 55, 40])
    t.setStyle(TableStyle(PLAIN))
    truth_rows = [["Threshold", "Min", "Typ", "Max", "Unit"]] + [[levels[i], *vals[i], "V"] for i in range(4)]
    return [t], {"rows": truth_rows, "header_rows": 1}


def case_conditions():
    """Datasheet rows whose conditions are stacked in one cell, a value on each line."""
    conds = ["25 C", "85 C", "105 C"]
    vals = ["406", "386", "792"]
    head = [P(c, CELLB) for c in ["Parameter", "Conditions", "Typ", "Unit"]]
    t = Table([head, [P("Leakage"), Paragraph("<br/>".join("\u2022 " + c for c in conds), CELL),
                      Paragraph("<br/>".join(vals), CELL), P("nA")]], colWidths=[80, 70, 50, 40])
    t.setStyle(TableStyle(PLAIN))
    rows = [["Parameter", "Conditions", "Typ", "Unit"]] + [["Leakage" if i == 0 else "", "\u2022 " + c, v, "nA" if i == 0 else ""]
                                                          for i, (c, v) in enumerate(zip(conds, vals))]
    return [t], {"rows": rows, "header_rows": 1}


def case_wrapped_ids():
    data = [["Pin", "Signal", "Group", "Function"],
            ["12", "LCD_P24", "LCD", "Segment drive 24"],
            ["13", "LCD_P25", "LCD", "Segment drive 25"],
            ["14", "UART2_RX", "UART", "Receive data"],
            ["15", "US-GAAP-1010", "Ledger", "Revenue account code"],
            ["16", "US-GAAP-1020", "Ledger", "Cost account code"],
            ["17", "PTA4/LLWU_P15", "GPIO", "Wake-up pin 15"]]
    return [table(data, [30, 52, 50, 140], GRID)], {"rows": data, "header_rows": 1}


def case_wrapped_mono():
    data = [["Index", "Register", "Reset", "Meaning"],
            ["0", "CAN_FD_REG_0", "0x0000", "Control register"],
            ["1", "CAN_FD_REG_1", "0x0001", "Status register"],
            ["2", "CAN_FD_TIMING_A", "0x00FF", "Bit timing A"],
            ["3", "CAN_FD_TIMING_B", "0x00FF", "Bit timing B"]]
    rows = [[P(c, CELLB) for c in data[0]]]
    for r in data[1:]:
        rows.append([P(r[0]), P(r[1], MONO), P(r[2]), P(r[3])])
    t = Table(rows, colWidths=[36, 62, 44, 100])
    t.setStyle(TableStyle(GRID))
    return [t], {"rows": data, "header_rows": 1}


def case_wrapped_money():
    data = [["Account", "Opening", "Change", "Closing"],
            ["Equipment", "$2,500,100.00", "($1,433,600.00)", "$1,066,500.00"],
            ["Inventory", "$410,250.00", "($95,125.50)", "$315,124.50"],
            ["Receivables", "$1,020,000.00", "$204,300.00", "$1,224,300.00"],
            ["Payables", "($788,400.00)", "($1,203,700.25)", "($1,992,100.25)"]]
    return [table(data, [70, 62, 62, 62], GRID, cell_style=RIGHT)], {"rows": data, "header_rows": 1}


def case_empty():
    data = [["Mode", "Voltage", "Current", "Power", "Note"],
            ["Run", "3.3", "", "0.4", "full speed"],
            ["Idle", "3.3", "12", "", ""],
            ["Sleep", "", "0.4", "0.001", "RAM kept"],
            ["Off", "0", "", "", ""],
            ["Boot", "3.3", "40", "0.13", ""]]
    return [table(data, [60, 60, 60, 60, 90], GRID)], {"rows": data, "header_rows": 1}


def part_rows(count: int):
    rnd = random.Random(7)
    return [[f"P{i:04d}", rnd.choice(["Bolt", "Nut", "Washer", "Clip", "Rivet", "Pin", "Spring"]) + f" {rnd.randint(2, 40)}",
             str(rnd.randint(1, 900)), f"{rnd.randint(1, 99)}.{rnd.randint(0, 99):02d}"] for i in range(1, count + 1)]


def case_multipage():
    data = [["Part", "Name", "Qty", "Price"]] + part_rows(125)
    return [table(data, [60, 120, 50, 60], BOOK, repeat=1)], \
           {"rows": data, "header_rows": 1, "repeat_header": True}


def case_multipage_stream():
    data = [["Part", "Name", "Qty", "Price"]] + part_rows(125)
    return [table(data, [60, 120, 50, 60], PLAIN, repeat=1)], {"rows": data, "header_rows": 1, "repeat_header": True}


def case_multipage_plain():
    data = [["Part", "Name", "Qty", "Price"]] + part_rows(125)
    return [table(data, [60, 120, 50, 60], PLAIN, repeat=0)], {"rows": data, "header_rows": 1, "repeat_header": False}


def case_registers_narrow():
    data = regs_data()
    return [table(data, REGS_NARROW, BOOK)], {"rows": data, "header_rows": 1}


def case_registers():
    data = [["Offset", "Name", "Acronym", "Access", "Reset", "Description"],
            ["0x000", "Control register", "CTRL", "RW", "0x0000", "Enables the peripheral and selects the clock source and the mode."],
            ["0x004", "Status register", "STAT", "RO", "0x0001", "Shows the busy flag, the error flags and the FIFO level."],
            ["0x008", "Interrupt enable", "IE", "RW", "0x0000", "One bit per source. A set bit lets the source raise the interrupt line."],
            ["0x00C", "Baud rate divider", "BRD", "RW", "0x0068", "Divides the peripheral clock."],
            ["0x010", "Transmit data", "TXD", "WO", "0x0000", "A write puts one byte into the transmit FIFO."],
            ["0x014", "Receive data", "RXD", "RO", "0x0000", "A read takes one byte out of the receive FIFO."],
            ["0x018", "CAN_FD_CONTROL_STATUS", "CFCS", "RW1C", "0x8000", "Write one to clear the sticky bits of the CAN FD controller."]]
    return [table(data, REGS, BOOK)], {"rows": data, "header_rows": 1}



REGS = [44, 84, 56, 50, 46, 200]      # wide enough for the header cells
REGS_NARROW = [38, 78, 44, 36, 40, 190]   # the header cells "Acronym" and "Access" break inside themselves


def regs_data():
    return [["Offset", "Name", "Acronym", "Access", "Reset", "Description"],
            ["0x000", "Control register", "CTRL", "RW", "0x0000", "Enables the peripheral and selects the clock source and the mode of operation."],
            ["0x004", "Status register", "STAT", "RO", "0x0001", "Shows the busy flag, the error flags and the FIFO level."],
            ["0x008", "Interrupt enable", "IE", "RW", "0x0000", "One bit per source. A set bit lets the source raise the interrupt line to the core."],
            ["0x00C", "Baud rate divider", "BRD", "RW", "0x0068", "Divides the peripheral clock."],
            ["0x010", "Transmit data", "TXD", "WO", "0x0000", "A write puts one byte into the transmit FIFO."],
            ["0x018", "CAN_FD_CONTROL_STATUS", "CFCS", "RW1C", "0x8000", "Write one to clear the sticky bits of the CAN FD controller."],
            ["0x01C", "Receive data", "RXD", "RO", "0x0000", "A read takes one byte out of the receive FIFO and moves the read pointer."]]


def case_registers_stream():
    data = regs_data()
    return [table(data, REGS, PLAIN[:2] + [PLAIN[2], PLAIN[3]])], {"rows": data, "header_rows": 1}


def case_wrapped_middle():
    data = regs_data()
    style = GRID[:1] + [("VALIGN", (0, 0), (-1, -1), "MIDDLE"), GRID[2], GRID[3]]
    return [table(data, REGS, style)], {"rows": data, "header_rows": 1}


def case_wrapped_middle_stream():
    data = regs_data()
    style = [("VALIGN", (0, 0), (-1, -1), "MIDDLE"), ("TOPPADDING", (0, 0), (-1, -1), 4),
             ("BOTTOMPADDING", (0, 0), (-1, -1), 4)]
    return [table(data, REGS, style)], {"rows": data, "header_rows": 1}


def case_rowrules():
    data = regs_data()
    style = [("LINEBELOW", (0, 0), (-1, -1), 0.3, colors.grey), ("VALIGN", (0, 0), (-1, -1), "TOP"),
             ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3)]
    return [table(data, REGS, style)], {"rows": data, "header_rows": 1}


def case_tight():
    data = [["Run", "n", "mean", "sd", "min", "max", "p95"],
            ["A1", "120", "4.31", "0.52", "3.10", "5.92", "5.11"],
            ["A2", "118", "4.28", "0.49", "3.22", "5.71", "5.02"],
            ["B1", "240", "5.07", "0.77", "3.40", "7.20", "6.35"],
            ["B2", "236", "5.11", "0.80", "3.35", "7.44", "6.48"],
            ["C1", "64", "6.90", "1.12", "4.20", "9.80", "8.77"]]
    style = [("TOPPADDING", (0, 0), (-1, -1), 2), ("BOTTOMPADDING", (0, 0), (-1, -1), 2),
             ("LEFTPADDING", (0, 0), (-1, -1), 5), ("RIGHTPADDING", (0, 0), (-1, -1), 5),
             ("LINEABOVE", (0, 0), (-1, 0), 1.0, colors.black), ("LINEBELOW", (0, 0), (-1, 0), 0.6, colors.black),
             ("LINEBELOW", (0, -1), (-1, -1), 1.0, colors.black)]
    return [table(data, [30, 34, 40, 38, 38, 38, 38], style, cell_style=RIGHT)], {"rows": data, "header_rows": 1}


def case_wrapped_first():
    data = [["Item description", "Unit", "Rate", "Remarks"],
            ["Preparation of the foundation including excavation and levelling", "m2", "14.50", "Machine work"],
            ["Reinforcement steel, bent and tied on site", "kg", "1.35", "B500"],
            ["Formwork for walls", "m2", "22.00", "Reusable panels, two uses"],
            ["Concrete C25/30 delivered and poured by pump", "m3", "118.00", "Pump hire extra"]]
    return [table(data, [130, 30, 40, 100], BOOK)], {"rows": data, "header_rows": 1}


def case_mono_names():
    data = [["Register", "Offset", "Reset", "Description"],
            ["CAN_FD_CTRL", "0x000", "0x0000", "Enables the controller and selects the clock source."],
            ["CAN_FD_STATUS", "0x004", "0x0001", "Shows the busy flag and the error flags."],
            ["CAN_FD_TIMING_A", "0x008", "0x00FF", "Bit timing of the arbitration phase."],
            ["CAN_FD_TIMING_B", "0x00C", "0x00FF", "Bit timing of the data phase."],
            ["CAN_FD_TX_FIFO", "0x010", "0x0000", "A write puts one frame into the transmit FIFO."]]
    rows = [[P(c, CELLB) for c in data[0]]]
    for r in data[1:]:
        rows.append([P(r[0], MONO), P(r[1], MONO), P(r[2], MONO), P(r[3])])
    t = Table(rows, colWidths=[90, 44, 44, 200])
    t.setStyle(TableStyle(PLAIN))
    return [t], {"rows": data, "header_rows": 1}


def case_mono_table():
    data = [["Signal", "Pin", "Mode"],
            ["TXD", "A1", "output"],
            ["RXD", "A2", "input"],
            ["RTS", "B1", "output"],
            ["CTS", "B2", "input"]]
    rows = [[P(c, ParagraphStyle("mb", parent=MONO, fontName="Courier-Bold")) for c in data[0]]]
    for r in data[1:]:
        rows.append([P(c, MONO) for c in r])
    t = Table(rows, colWidths=[70, 50, 70])
    t.setStyle(TableStyle(PLAIN))
    return [t], {"rows": data, "header_rows": 1}


def case_ledger():
    data = [["Date", "Account", "Reference", "Debit", "Credit"],
            ["2024-01-03", "Cash", "INV-1001", "$12,400.00", ""],
            ["2024-01-03", "Revenue", "INV-1001", "", "$12,400.00"],
            ["2024-01-09", "Equipment", "PO-5540", "$3,250.75", ""],
            ["2024-01-09", "Payables", "PO-5540", "", "($3,250.75)"],
            ["2024-01-15", "Salaries", "PAY-0115", "$18,000.00", ""],
            ["2024-01-15", "Cash", "PAY-0115", "", "$18,000.00"],
            ["Total", "", "", "$33,650.75", "$27,150.00"]]
    return [table(data, [58, 62, 58, 62, 62], GRID, cell_style=CELL)], {"rows": data, "header_rows": 1}


def case_datasheet():
    data = [["Symbol", "Parameter", "Conditions", "Min", "Typ", "Max", "Unit"],
            ["VDD", "Supply voltage", "", "2.7", "3.3", "3.6", "V"],
            ["IDD", "Supply current", "Run, 48 MHz", "", "14", "20", "mA"],
            ["ISTB", "Standby current", "25 C", "", "0.9", "", "uA"],
            ["VOH", "Output high voltage", "IOH = -4 mA", "2.4", "", "", "V"],
            ["VOL", "Output low voltage", "IOL = 4 mA", "", "", "0.4", "V"],
            ["tR", "Rise time", "CL = 50 pF", "", "3.5", "8", "ns"],
            ["fMAX", "Maximum frequency", "", "", "", "72", "MHz"]]
    return [table(data, [40, 100, 70, 36, 36, 36, 36], PLAIN)], {"rows": data, "header_rows": 1}


def chart_drawing() -> Drawing:
    """Bars on a grid with axis labels: lines and numbers, no table."""
    d = Drawing(380, 200)
    x0, y0, w, h = 40, 20, 320, 160
    for i in range(6):
        y = y0 + i * h / 5
        d.add(Line(x0, y, x0 + w, y, strokeColor=colors.grey, strokeWidth=0.4))
        d.add(String(x0 - 4, y - 3, str(i * 20), fontName="Helvetica", fontSize=8, textAnchor="end"))
    for i in range(7):
        x = x0 + i * w / 6
        d.add(Line(x, y0, x, y0 + h, strokeColor=colors.grey, strokeWidth=0.4))
    for i, (m, v) in enumerate(zip(["Jan", "Feb", "Mar", "Apr", "May", "Jun"], [35, 52, 61, 48, 77, 90])):
        x = x0 + i * w / 6
        d.add(Rect(x + 8, y0, w / 6 - 16, v * h / 100, fillColor=colors.HexColor("#3465a4"), strokeColor=None))
        d.add(String(x + w / 12, y0 - 10, m, fontName="Helvetica", fontSize=8, textAnchor="middle"))
    return d


def case_chart():
    return [chart_drawing()], {"rows": [], "header_rows": 0, "kind": "chart"}


CASES = {
    "grid": case_grid, "booktabs": case_booktabs, "stream": case_stream, "merged": case_merged,
    "header2": case_header2, "wrapped": case_wrapped, "stacked": case_stacked,
    "wrapped_ids": case_wrapped_ids, "wrapped_mono": case_wrapped_mono, "wrapped_money": case_wrapped_money,
    "empty": case_empty, "multipage": case_multipage, "multipage_stream": case_multipage_stream,
    "multipage_plain": case_multipage_plain, "registers": case_registers, "ledger": case_ledger,
    "datasheet": case_datasheet, "chart": case_chart, "registers_stream": case_registers_stream,
    "stacked_stream": case_stacked_stream, "conditions": case_conditions, "registers_narrow": case_registers_narrow, "mono_names": case_mono_names, "mono_table": case_mono_table, "wrapped_middle": case_wrapped_middle, "wrapped_middle_stream": case_wrapped_middle_stream,
    "rowrules": case_rowrules, "tight": case_tight, "wrapped_first": case_wrapped_first,
}


def pdf_pages(pdf: Path) -> list[str]:
    import subprocess
    text = subprocess.run(["pdftotext", "-layout", str(pdf), "-"], capture_output=True, text=True).stdout
    return text.split("\f")[:-1] or [text]


def build(case: str, folder: Path) -> None:
    flow, truth = CASES[case]()
    pdf = folder / f"{case}.pdf"
    doc = SimpleDocTemplate(str(pdf), pagesize=letter, leftMargin=60, rightMargin=60, topMargin=60,
                            bottomMargin=60, title=f"Table report {case}", author="HTPD bench")
    doc.build(story(case, flow, extra_after=not case.startswith("multipage")))
    truth["case"] = case
    truth.setdefault("kind", "table")
    pages = pdf_pages(pdf)
    rows, row_pages = [], []
    head = truth["rows"][:truth["header_rows"]]
    page_seen = 1
    for r, row in enumerate(truth["rows"]):
        if r < truth["header_rows"]:
            rows.append(row)
            row_pages.append(1)
            continue
        key = next(c for c in row if c)
        page = page_seen
        while page <= len(pages) and key not in pages[page - 1]:
            page += 1
        page = min(page, len(pages))
        if truth.get("repeat_header") and page > page_seen:
            rows.extend(head)               # the PDF prints the header again at the top of the next page
            row_pages.extend([page] * len(head))
        page_seen = page
        rows.append(row)
        row_pages.append(page)
    truth["rows"], truth["row_pages"] = rows, row_pages
    (folder / f"{case}.truth.json").write_text(json.dumps(truth, indent=1, ensure_ascii=False) + "\n",
                                               encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder", type=Path)
    ap.add_argument("--only", help="comma-separated case names")
    args = ap.parse_args()
    args.folder.mkdir(parents=True, exist_ok=True)
    names = args.only.split(",") if args.only else list(CASES)
    for name in names:
        if name not in CASES:
            sys.exit(f"unknown case {name!r}; choose from {', '.join(CASES)}")
        build(name, args.folder)
        print("made", name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
