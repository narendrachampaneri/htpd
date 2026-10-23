#!/usr/bin/env python3
"""Draws the animations of the README (docs/assets/*.gif).

    python3 docs/assets/make_demo.py [folder with IMX93AEC.htpd, F2.png]

The terminals show real commands of htpd 0.1.2 on the NXP i.MX 93 data sheet
(IMX93AEC.pdf) with their real output, shortened where it is long. The figure
animation shows the picture `htpd-read figure` wrote (F2.png) when the folder
holds it. how-it-works.gif is a drawing of the idea.
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
FONTS = Path("/usr/share/fonts/truetype/dejavu")
MONO = ImageFont.truetype(str(FONTS / "DejaVuSansMono.ttf"), 15)
MONO_B = ImageFont.truetype(str(FONTS / "DejaVuSansMono-Bold.ttf"), 15)
SANS = ImageFont.truetype(str(FONTS / "DejaVuSans.ttf"), 13)

BG, BAR, FG, DIM = (255, 255, 255), (238, 242, 247), (22, 30, 46), (100, 116, 139)
BLUE, GREEN, TEAL, BORDER = (29, 78, 216), (21, 128, 61), (15, 118, 110), (203, 213, 225)
W, LINE, LEFT, TOP = 1000, 23, 24, 58


def terminal(lines, height, cursor=True, title="htpd - terminal"):
    """lines: (text, colour, is_command)."""
    im = Image.new("RGB", (W, height), BG)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, W - 1, height - 1], 10, fill=BG, outline=BORDER)
    d.rounded_rectangle([1, 1, W - 2, 40], 10, fill=BAR)
    d.rectangle([1, 30, W - 2, 40], fill=BAR)
    for i, c in enumerate([(237, 106, 94), (245, 191, 79), (98, 197, 84)]):
        d.ellipse([18 + i * 22, 14, 30 + i * 22, 26], fill=c)
    d.text((W / 2, 21), title, font=SANS, fill=DIM, anchor="mm")
    y = TOP
    for text, colour, command in lines:
        if command:
            d.text((LEFT, y), "$", font=MONO_B, fill=BLUE)
            d.text((LEFT + 20, y), text, font=MONO, fill=FG)
        else:
            d.text((LEFT, y), text, font=MONO, fill=colour)
        y += LINE
    if cursor:
        text, _, command = lines[-1]
        x = LEFT + 20 + MONO.getlength(text) + 2
        d.rectangle([x, y - LINE + 2, x + 9, y - LINE + 19], fill=BLUE)
    return im


def play(script, height):
    """script: (command, output lines, pause after). Returns frames and durations."""
    frames, durations, shown = [], [], []
    for cmd, output, pause in script:
        for n in range(0, len(cmd) + 1, 3):
            frames.append(terminal(shown + [(cmd[:n], FG, True)], height))
            durations.append(40)
        shown.append((cmd, FG, True))
        frames.append(terminal(shown, height))
        durations.append(400)
        shown += [(t, c, False) for t, c in output]
        frames.append(terminal(shown + [("", FG, True)], height))
        durations.append(pause)
        shown.append(("", FG, False))
    return frames, durations, shown


def save(name, frames, durations):
    out = HERE / name
    pal = [f.convert("P", palette=Image.Palette.ADAPTIVE, colors=96) for f in frames]
    pal[0].save(out, save_all=True, append_images=pal[1:], duration=durations, loop=0, optimize=True, disposal=1)
    print(f"wrote {out.name}: {out.stat().st_size // 1024} kB, {len(frames)} frames")


def convert_gif():
    script = [("htpd convert IMX93AEC.pdf --single", [
        ('Converted "i.MX 93 Applications Processors Data Sheet for Automotive Products"', FG),
        ("  output file     IMX93AEC.htpd", FG),
        ("  pages           109", FG),
        ("  sections        114  (headings from: bookmark)", FG),
        ("  paragraphs      1729", FG),
        ("  figures         53 stored as pictures (1.6 MB)", FG),
        ("  files written   1 (2.5 MB; the PDF is 1.7 MB)", FG),
        ("  time            1.17 s", GREEN),
        ("Left out of the text:", DIM),
        ('  header on 107 pages: "NXP Semiconductors IMX93AEC"', DIM),
        ('  footer on 101 pages: "2 / 109"', DIM),
        ("Give IMX93AEC.htpd to any AI model and ask your questions about the document.", TEAL),
    ], 5000)]
    frames, durations, _ = play(script, 400)
    save("demo-convert.gif", frames, durations)


def ask_gif():
    script = [
        ("htpd-read contents IMX93AEC.htpd --depth 1", [
            ("i.MX 93 Applications Processors Data Sheet - 109 pages, 114 sections", FG),
            ("1 i.MX 93 introduction (pages 2-5)", FG),
            ("2 Block diagram (pages 9-10)", FG),
            ("4 Electrical characteristics (page 12)", FG),
            ("6 Package information and contact assignments (page 82)", FG),
        ], 1800),
        ('htpd-read ask IMX93AEC.htpd "What is the maximum NPU frequency?"', [
            ("[page 18, 4.1.5 Maximum frequency of main modules]", BLUE),
            ("  EdgeLock® Secure Enclave | 133 MHz | 200 MHz | 250 MHz", DIM),
            ("  NPU | 500 MHz | 800 MHz | 1000 MHz", GREEN),
        ], 2200),
        ('htpd-read ask IMX93AEC.htpd "Which Cortex-M core is in the i.MX 93?"', [
            ("[page 5, 1 i.MX 93 introduction]", BLUE),
            ("  Unified trace capability for dual core Cortex®-A55 and Cortex®-M33 CPUs", GREEN),
        ], 4500),
    ]
    frames, durations, _ = play(script, 470)
    save("demo-ask.gif", frames, durations)


def figure_gif(folder: Path):
    script = [("htpd-read figure IMX93AEC.htpd F2 -o F2.png", [
        ("F2: Figure 2. i.MX 93 system block diagram", FG),
        ("page 10, i.MX 93 Applications Processors Data Sheet", DIM),
        ("picture written to F2.png", TEAL),
    ], 900)]
    height = 520
    frames, durations, shown = play(script, height)
    pic = folder / "F2.png"
    if pic.is_file():
        img = Image.open(pic).convert("RGB")
        scale = (height - 190) / img.height
        img = img.resize((int(img.width * scale), int(img.height * scale)), Image.Resampling.LANCZOS)
        base = terminal(shown[:-1] + [("", FG, True)], height)
        x, y = W - img.width - 40, 60
        for step in range(1, 9):   # the picture appears
            f = base.copy()
            layer = Image.new("RGB", img.size, BG)
            f.paste(Image.blend(layer, img, step / 8), (x, y))
            ImageDraw.Draw(f).rectangle([x - 1, y - 1, x + img.width, y + img.height], outline=BORDER)
            frames.append(f)
            durations.append(60)
        durations[-1] = 5000
    save("demo-figure.gif", frames, durations)


def how_it_works_gif():
    """PDF -> HTPD sections -> the few sections a question needs -> the answer with its page."""
    H = 300
    title_font = ImageFont.truetype(str(FONTS / "DejaVuSans-Bold.ttf"), 17)
    small = ImageFont.truetype(str(FONTS / "DejaVuSans.ttf"), 13)
    boxes = [
        (40, "PDF", "5000 pages", (241, 245, 249), (148, 163, 184)),
        (285, "HTPD file", "sections + pages", (239, 246, 255), (96, 165, 250)),
        (530, "Your question", "the few sections it needs", (240, 253, 250), (45, 212, 191)),
        (775, "Answer", "exact text + page", (240, 253, 244), (74, 222, 128)),
    ]
    frames, durations = [], []

    def draw(stage, pulse=0):
        im = Image.new("RGB", (W, H), BG)
        d = ImageDraw.Draw(im)
        d.rounded_rectangle([0, 0, W - 1, H - 1], 12, fill=BG, outline=BORDER)
        for i, (x, name, sub, fill, edge) in enumerate(boxes):
            on = i <= stage
            d.rounded_rectangle([x, 70, x + 190, 230], 14, fill=fill if on else BG, outline=edge if on else BORDER, width=2)
            d.text((x + 95, 98), name, font=title_font, fill=FG if on else BORDER, anchor="mm")
            d.text((x + 95, 122), sub, font=small, fill=DIM if on else BORDER, anchor="mm")
            if not on:
                continue
            if i == 0:   # pages
                for k in range(4):
                    d.rectangle([x + 40 + k * 10, 145 + k * 6, x + 110 + k * 10, 215], fill=BG, outline=(148, 163, 184))
            elif i == 1:   # sections
                for k in range(5):
                    d.rounded_rectangle([x + 30, 142 + k * 16, x + 160, 154 + k * 16], 4, fill=(219, 234, 254))
                    d.text((x + 36, 148 + k * 16), f"[S{k + 1}] p.{3 + k * 4}", font=small, fill=BLUE, anchor="lm")
            elif i == 2:   # the chosen sections light up
                for k in range(5):
                    hit = k in (1, 3)
                    d.rounded_rectangle([x + 30, 142 + k * 16, x + 160, 154 + k * 16], 4,
                                        fill=(153, 246, 228) if hit else (241, 245, 249))
            elif i == 3:
                d.text((x + 95, 160), "NPU | 1000 MHz", font=small, fill=GREEN, anchor="mm")
                d.rounded_rectangle([x + 55, 180, x + 135, 202], 11, fill=(220, 252, 231))
                d.text((x + 95, 191), "page 18", font=small, fill=GREEN, anchor="mm")
            if i < stage or (i == stage and i < 3):
                if i < 3:
                    ax = x + 200
                    reach = 1.0 if i < stage else pulse
                    end = ax + int(30 * reach)
                    d.line([ax, 150, end, 150], fill=edge, width=3)
                    if reach >= 1:
                        d.polygon([(ax + 36, 150), (ax + 28, 144), (ax + 28, 156)], fill=edge)
        d.text((W / 2, 262), "Convert once - then any AI answers from the exact text, with the page.",
               font=small, fill=DIM, anchor="mm")
        return im

    for stage in range(4):
        for p in (0.25, 0.5, 0.75, 1.0):
            frames.append(draw(stage, p))
            durations.append(90)
        durations[-1] = 1100
    durations[-1] = 3500
    save("how-it-works.gif", frames, durations)


if __name__ == "__main__":
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.cwd()
    convert_gif()
    ask_gif()
    figure_gif(folder)
    how_it_works_gif()
