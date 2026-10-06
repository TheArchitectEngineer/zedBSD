#!/usr/bin/env python3
"""Render specimens directly from the three delivered TrueType fonts."""

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parent
LABEL = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONTS = ["Keiland-Mono", "Keiland-Regular", "Keiland-Bold"]


def text(canvas, content, x, baseline, size, font_path):
    face = ImageFont.truetype(str(font_path), size)
    ImageDraw.Draw(canvas).text((x, baseline), content, font=face, fill="#15202b", anchor="ls")


def q_review(before_directory):
    canvas = Image.new("RGB", (1540, 1050), "#fafbfc")
    draw = ImageDraw.Draw(canvas)
    heading = ImageFont.truetype(LABEL, 24)
    small = ImageFont.truetype(LABEL, 18)
    draw.text((50, 28), "Keiland / q height alignment", font=heading, fill="#15202b")
    draw.text((50, 73), "Before 0.300 / After 0.301. Same size and shared baseline guides.",
              font=small, fill="#617386")
    for row, name in enumerate(FONTS):
        top = 140 + row * 290
        draw.text((50, top), name.replace("-", " "), font=heading, fill="#15202b")
        for index, (title, path) in enumerate([
            ("Before", before_directory / f"{name}.ttf"),
            ("After", ROOT / f"{name}.ttf"),
        ]):
            x = 50 + index * 760
            draw.text((x, top + 44), title, font=small, fill="#617386")
            draw.line((x, top + 183, x + 650, top + 183), fill="#aac7df")
            draw.line((x, top + 99, x + 650, top + 99), fill="#d4e0ea")
            text(canvas, "anopqg", x + 15, top + 183, 170, path)
            text(canvas, "quick equal glyphs", x + 15, top + 250, 32, path)
    canvas.save(ROOT / "q-height-review.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--q-before-directory", type=Path,
                        help="Previous TTF directory for the q-height comparison")
    arguments = parser.parse_args()
    specimen = Image.new("RGB", (1600, 1650), "#fafbfc")
    draw = ImageDraw.Draw(specimen)
    heading = ImageFont.truetype(LABEL, 25)
    small = ImageFont.truetype(LABEL, 17)
    draw.text((56, 24), "Keiland / Unicode ASCII typefaces 0.301", font=heading, fill="#15202b")
    for index, name in enumerate(FONTS):
        path = ROOT / f"{name}.ttf"
        top = 85 + index * 515
        kind = "Monospace" if index == 0 else "Proportional / same advances in Regular and Bold"
        draw.text((56, top), name.replace("-", " "), font=heading, fill="#15202b")
        draw.text((56, top + 36), kind, font=small, fill="#617386")
        for line, y, size in [
            ("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 118, 60),
            ("abcdefghijklmnopqrstuvwxyz", 191, 60),
            ("0123456789  Il1 O0  agp  &@% ?! {} [] ; ~", 260, 48),
            ("The quick brown fox jumps over the lazy dog.", 330, 40),
            ('const align = { name: "Keiland", size: 16 };', 388, 35),
        ]:
            text(specimen, line, 56, top + y, size, path)
        for size, y in [(14, 432), (18, 465)]:
            draw.text((56, top + y - 18), f"{size}px", font=small, fill="#617386")
            text(specimen, "Small glyphs: align minimum little passphrase.  0123456789", 136, top + y, size, path)
        draw.line((56, top + 490, 1544, top + 490), fill="#d7dfe6")

        grid = Image.new("RGB", (1400, 1550), "white")
        grid_draw = ImageDraw.Draw(grid)
        grid_draw.text((30, 18), name.replace("-", " ") + " / ASCII 95", font=heading, fill="#15202b")
        label = ImageFont.truetype(LABEL, 13)
        for i, codepoint in enumerate(range(32, 127)):
            x = i % 10 * 140
            y = 65 + i // 10 * 148
            grid_draw.rectangle((x, y, x + 139, y + 147), outline="#d7dfe6")
            grid_draw.line((x + 10, y + 96, x + 130, y + 96), fill="#c5d9e9")
            face = ImageFont.truetype(str(path), 92)
            width = face.getlength(chr(codepoint))
            text(grid, chr(codepoint), x + (140 - width) / 2, y + 96, 92, path)
            grid_draw.text((x + 12, y + 120), f"U+{codepoint:04X}", font=label, fill="#617386")
        grid.save(ROOT / f"{name}-ascii.png")
    specimen.save(ROOT / "keiland-specimen.png")
    if arguments.q_before_directory:
        q_review(arguments.q_before_directory)
    print("Rendered the three TTF specimens and all ASCII glyphs.")


if __name__ == "__main__":
    main()
