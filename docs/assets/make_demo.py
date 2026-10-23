#!/usr/bin/env python3
"""Draws docs/assets/demo.gif: a terminal that types the commands and shows
their real output (taken from htpd 0.1.2 on the NXP i.MX 93 data sheet,
IMX93AEC.pdf; long outputs shortened).

    python3 docs/assets/make_demo.py
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent / "demo.gif"
W, H = 1000, 470
BG, BAR, FG, DIM = (255, 255, 255), (238, 242, 247), (22, 30, 46), (100, 116, 139)
BLUE, GREEN, ORANGE, BORDER = (37, 84, 199), (21, 128, 61), (194, 65, 12), (203, 213, 225)
FONT = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 15)
BOLD = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 15)
TITLE = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 13)

# (command, [(text, colour), ...])
SCRIPT = [
    ("htpd convert IMX93AEC.pdf --single", [
        ('Converted "i.MX 93 Applications Processors Data Sheet for Automotive Products"', FG),
        ("  pages           109", FG),
        ("  sections        114  (headings from: bookmark)", FG),
        ("  figures         53 stored as pictures (1.6 MB)", FG),
        ("  time            1.17 s", FG),
    ]),
    ('htpd-read ask IMX93AEC.htpd "What is the maximum NPU frequency?"', [
        ("[page 18, 4.1.5 Maximum frequency of main modules]", BLUE),
        ("  NPU | 500 MHz | 800 MHz | 1000 MHz", GREEN),
    ]),
    ('htpd-read ask IMX93AEC.htpd "How many PCIe lanes are there?"', [
        ("Not found in this file. (the file never mentions PCIe)", ORANGE),
    ]),
    ("htpd-read figure IMX93AEC.htpd F1 -o F1.png", [
        ("F1: Figure 1. Part number nomenclature\u2014i.MX 93 processors", FG),
        ("picture written to F1.png", DIM),
    ]),
]

LINE, LEFT, TOP = 23, 24, 58


def frame(lines: list[tuple[str, tuple, bool]], cursor: bool) -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, W - 1, H - 1], 10, fill=BG, outline=BORDER)
    d.rounded_rectangle([1, 1, W - 2, 40], 10, fill=BAR)
    d.rectangle([1, 30, W - 2, 40], fill=BAR)
    for i, c in enumerate([(237, 106, 94), (245, 191, 79), (98, 197, 84)]):
        d.ellipse([18 + i * 22, 14, 30 + i * 22, 26], fill=c)
    d.text((W / 2, 21), "htpd - terminal", font=TITLE, fill=DIM, anchor="mm")
    y = TOP
    for text, colour, prompt in lines:
        if prompt:
            d.text((LEFT, y), "$", font=BOLD, fill=BLUE)
            d.text((LEFT + 20, y), text, font=FONT, fill=FG)
        else:
            d.text((LEFT, y), text, font=FONT, fill=colour)
        y += LINE
    if cursor:
        last = lines[-1] if lines else ("", FG, True)
        x = LEFT + 20 + FONT.getlength(last[0]) + 2 if last[2] else LEFT + 20
        yy = TOP + (len(lines) - 1) * LINE if last[2] else y
        d.rectangle([x, yy + 2, x + 9, yy + 19], fill=BLUE)
    return im


def main() -> None:
    frames, durations = [], []
    shown: list[tuple[str, tuple, bool]] = []
    for cmd, output in SCRIPT:
        for n in range(0, len(cmd) + 1, 3):   # typing, three letters a step
            frames.append(frame(shown + [(cmd[:n], FG, True)], True))
            durations.append(45)
        shown.append((cmd, FG, True))
        frames.append(frame(shown, True))
        durations.append(450)
        shown += [(t, c, False) for t, c in output]
        frames.append(frame(shown + [("", FG, True)], True))
        durations.append(1700)
        shown.append(("", FG, False))
    frames.append(frame(shown[:-1] + [("", FG, True)], True))
    durations.append(4000)
    pal = [f.convert("P", palette=Image.Palette.ADAPTIVE, colors=64) for f in frames]
    pal[0].save(OUT, save_all=True, append_images=pal[1:], duration=durations, loop=0, optimize=True, disposal=1)
    print(f"wrote {OUT} ({OUT.stat().st_size // 1024} kB, {len(frames)} frames)")


if __name__ == "__main__":
    main()
