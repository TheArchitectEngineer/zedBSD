#!/usr/bin/env python3
"""Render specimens directly from the three delivered TrueType fonts."""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parent
LABEL = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONTS = ["Keiland-Mono", "Keiland-Regular", "Keiland-Bold"]


def text(canvas, content, x, baseline, size, font_path):
    face = ImageFont.truetype(str(font_path), size)
    ImageDraw.Draw(canvas).text((x, baseline), content, font=face, fill="#15202b", anchor="ls")


def main():
    specimen = Image.new("RGB", (1600, 1650), "#fafbfc")
    draw = ImageDraw.Draw(specimen)
    heading = ImageFont.truetype(LABEL, 25)
    small = ImageFont.truetype(LABEL, 17)
    draw.text((56, 24), "Keiland / Unicode ASCII typefaces 0.300", font=heading, fill="#15202b")
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
    print("Rendered the three TTF specimens and all ASCII glyphs.")


if __name__ == "__main__":
    main()
