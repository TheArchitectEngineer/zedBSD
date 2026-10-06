#!/usr/bin/env python3
"""Refresh the PNG and TrueType previews without modifying glyph sources."""

import argparse
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parent
LABEL_FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
BASELINE = 745
ORIGIN = 282
ADVANCE = 460.875


def label(size):
    return ImageFont.truetype(LABEL_FONT, size)


def raster_line(canvas, text, x, baseline, size, directory):
    scale = size / 768
    for character in text:
        image = Image.open(directory / f"U{ord(character):04X}.png").convert("RGBA")
        image = image.resize((round(1024 * scale),) * 2, Image.Resampling.LANCZOS)
        canvas.paste(image, (round(x - ORIGIN * scale),
                             round(baseline - BASELINE * scale)), image)
        x += ADVANCE * scale


def font_line(canvas, text, x, baseline, size, font_path):
    font = ImageFont.truetype(str(font_path), size)
    ImageDraw.Draw(canvas).text((x, baseline), text, font=font, fill="black", anchor="ls")


def overview(from_font):
    canvas = Image.new("RGB", (1400, 1720), "white")
    draw = ImageDraw.Draw(canvas)
    for index, codepoint in enumerate(range(32, 127)):
        x = index % 10 * 140
        y = index // 10 * 172
        draw.rectangle((x, y, x + 139, y + 171), outline="#d7dce2")
        draw.line((x + 12, y + 104, x + 127, y + 104), fill="#b6cfe8")
        if from_font:
            font_line(canvas, chr(codepoint), x + 42, y + 104, 92,
                      ROOT / "Font1-Regular.ttf")
        else:
            raster_line(canvas, chr(codepoint), x + 42, y + 104, 92, ROOT / "glyphs")
        draw.text((x + 12, y + 133), f"U+{codepoint:04X}", font=label(13), fill="#405269")
        if codepoint == 32:
            draw.text((x + 12, y + 150), "SPACE (blank)", font=label(13), fill="#405269")
    filename = "ttf-ascii-overview.png" if from_font else "ascii-overview.png"
    canvas.save(ROOT / filename)


def specimen(from_font):
    canvas = Image.new("RGB", (1600, 1320), "#fafbfc")
    draw = ImageDraw.Draw(canvas)
    kind = "TrueType rendering" if from_font else "glyph PNG rendering"
    draw.text((64, 32), f"Font1 Regular 0.202 / {kind}", font=label(24), fill="#1a2735")
    draw.text((64, 74), "Refined lowercase / balanced g and p vertical positions", font=label(18), fill="#586878")

    def line(text, y, size, x=64):
        if from_font:
            font_line(canvas, text, x, y, size, ROOT / "Font1-Regular.ttf")
        else:
            raster_line(canvas, text, x, y, size, ROOT / "glyphs")

    for text, y in [
        ("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 191),
        ("abcdefghijklmnopqrstuvwxyz", 300),
        ("0123456789  O0  Il1  ag", 420),
        ("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", 542),
    ]:
        line(text, y, 68)
    for index, text in enumerate([
        'const font1 = { name: "Hello, world!", size: 16 };',
        'if (value >= 0 && value != 1) {',
        '    return array[index] + (width / 2);',
        '}',
        'The quick brown fox jumps over the lazy dog.',
        'Pack my box with five dozen liquor jugs.',
        '0O 1Il |! 2Z 5S 6G 8B  rn m  {} () []',
    ]):
        line(text, 648 + index * 62, 44)
    for index, size in enumerate([12, 14, 16, 18, 24]):
        y = 1090 + index * 39
        draw.text((64, y - 20), f"{size}px", font=label(14), fill="#586878")
        line("align little glyphs: abcdefghijklmnopqrstuvwxyz  Il1 i l  0123456789", y, size, x=140)
    filename = "ttf-specimen.png" if from_font else "specimen.png"
    canvas.save(ROOT / filename)


def comparison():
    selected = "0O1Ilag&@?{}[];~"
    canvas = Image.new("RGB", (1280, 690), "white")
    draw = ImageDraw.Draw(canvas)
    for row, (title, directory) in enumerate([
        ("Monaco", ROOT.parent.parent / "monaco-ascii"),
        ("font1 Regular 0.202", ROOT),
        ("Droid Sans", ROOT.parent.parent / "droid"),
        ("JetBrains Mono", ROOT.parent.parent / "jetbrains"),
    ]):
        y = 30 + row * 166
        draw.text((28, y), title, font=label(20), fill="#1a2735")
        for column, character in enumerate(selected):
            raster_line(canvas, character, 168 + column * 67, y + 124, 81,
                        directory / "glyphs")
    canvas.save(ROOT / "comparison.png")


def lowercase_review(before_font):
    canvas = Image.new("RGB", (1600, 1220), "white")
    draw = ImageDraw.Draw(canvas)
    draw.text((64, 30), "Font1 Regular / lowercase refinement", font=label(28), fill="#1a2735")
    draw.text((64, 79), "Same size and advance. Before 0.100 / After 0.202", font=label(18), fill="#586878")
    for group, characters in enumerate(["abcdefghijklm", "nopqrstuvwxyz"]):
        for revision, (title, font) in enumerate([
            ("Before", before_font), ("After", ROOT / "Font1-Regular.ttf"),
        ]):
            y = 130 + group * 380 + revision * 175
            draw.text((64, y), title, font=label(18), fill="#586878")
            for column, character in enumerate(characters):
                font_line(canvas, character, 100 + column * 112, y + 124, 148, font)
    draw.text((64, 935), "i / l detail + lowercase rhythm", font=label(22), fill="#1a2735")
    for index, (title, font) in enumerate([
        ("Before", before_font), ("After", ROOT / "Font1-Regular.ttf"),
    ]):
        baseline = 1040 + index * 110
        draw.text((64, baseline - 60), title, font=label(18), fill="#586878")
        font_line(canvas, "Il1 i l   minimum align little glyphs", 170, baseline, 62, font)
    canvas.save(ROOT / "lowercase-review.png")


def weight_comparison():
    canvas = Image.new("RGB", (1600, 990), "#fafbfc")
    draw = ImageDraw.Draw(canvas)
    draw.text((52, 30), "Font1 / Regular 0.202 and Bold", font=label(26), fill="#1a2735")
    draw.text((52, 74), "Cap height matched across all three samples", font=label(18), fill="#586878")
    for index, (title, directory) in enumerate([
        ("Monaco Regular", ROOT.parent.parent / "monaco-ascii"),
        ("font1 Regular 0.202", ROOT),
        ("font1 Bold", ROOT.parent / "bold"),
    ]):
        alpha = np.asarray(Image.open(directory / "glyphs/U0048.png").getchannel("A"))
        rows = np.where(alpha > 128)[0]
        cap_height = int(rows.max() + 1 - rows.min())
        y = 118 + index * 275
        draw.text((52, y), title, font=label(24), fill="#1a2735")
        raster_line(canvas, "HAMBURGEFONTS 0123456789", 52, y + 88,
                    56 / cap_height * 768, directory / "glyphs")
        raster_line(canvas, "ag iIl1 O0 &@? {} [] ; ~", 52, y + 168,
                    52 / cap_height * 768, directory / "glyphs")
        raster_line(canvas, "The quick brown fox jumps over the lazy dog.", 52, y + 232,
                    32 / cap_height * 768, directory / "glyphs")
        draw.line((52, y + 253, 1548, y + 253), fill="#d7dce2")
    canvas.save(ROOT.parent / "weights-comparison.png")


def position_review(before_font):
    """Compare the changed bowls against shared baseline and x-height guides."""
    canvas = Image.new("RGB", (1500, 1160), "#fafbfc")
    draw = ImageDraw.Draw(canvas)
    draw.text((56, 30), "Font1 Regular / g and p vertical positions", font=label(27), fill="#1a2735")
    draw.text((56, 78), "Same shapes and advance. Shared baseline / lowercase-height guides.",
              font=label(18), fill="#586878")
    for index, (title, font) in enumerate([
        ("Before 0.201", before_font), ("After 0.202", ROOT / "Font1-Regular.ttf"),
    ]):
        x = 56 + index * 720
        draw.text((x, 129), title, font=label(20), fill="#405269")
        draw.line((x, 176, x + 658, 176), fill="#c5d9e9")
        draw.line((x, 260, x + 658, 260), fill="#a4c2dc")
        font_line(canvas, "angpo", x + 25, 260, 170, font)

    for index, (title, font) in enumerate([
        ("Before 0.201", before_font), ("After 0.202", ROOT / "Font1-Regular.ttf"),
    ]):
        top = 374 + index * 372
        draw.rounded_rectangle((40, top, 1460, top + 337), radius=12, fill="#20242b")
        draw.text((70, top + 22), title, font=label(20), fill="#acbed0")
        for size, offset in [(32, 76), (36, 207)]:
            face = ImageFont.truetype(str(font), size)
            draw.text((70, top + offset - 17), f"{size}px", font=label(13), fill="#acbed0")
            for line_index, text in enumerate([
                "So this thing really sends me to the future?",
                "And the passphrase is...",
                "Good day, Agent M. How is 1997 treating you?",
            ]):
                draw.text((146, top + offset + line_index * 43), text,
                          font=face, fill="white", anchor="ls")
    canvas.save(ROOT / "vertical-position-review.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before-font", type=Path,
                        help="Optional previous TTF for lowercase-review.png")
    parser.add_argument("--before-position-font", type=Path,
                        help="Previous TTF for the g/p vertical-position review")
    arguments = parser.parse_args()
    for from_font in [False, True]:
        overview(from_font)
        specimen(from_font)
    comparison()
    weight_comparison()
    if arguments.before_font:
        lowercase_review(arguments.before_font)
    if arguments.before_position_font:
        position_review(arguments.before_position_font)
    print("Refreshed PNG/TTF overviews, specimens and reference comparison.")


if __name__ == "__main__":
    main()
