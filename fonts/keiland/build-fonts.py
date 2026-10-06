#!/usr/bin/env python3
"""Build Mahora Mono and proportional Mahora Regular/Bold from font1."""

import argparse
from copy import deepcopy
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET

import cairo
from fontTools.feaLib.builder import addOpenTypeFeaturesFromString
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.cairoPen import CairoPen
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.recordingPen import RecordingPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.svgLib.path import SVGPath
from fontTools.ttLib import TTFont
import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT.parent / "font1" / "regular"
OLD_BOLD = ROOT.parent / "font1" / "bold"
VERSION = "0.301"
SIDEBEARING = 78
SPACE_ADVANCE = 614
SVG_NS = "http://www.w3.org/2000/svg"
FONT_TRANSFORM = (2048 / 768, 0, 0, -2048 / 768,
                  -282 * 2048 / 768, 745 * 2048 / 768)
KERN_PAIRS = {
    "AV": -60, "AW": -40, "AY": -60, "AT": -40,
    "VA": -60, "WA": -40, "YA": -60, "TA": -50,
    "LT": -40, "LV": -40, "LW": -30, "LY": -50,
    "FA": -35, "PA": -35, "Ta": -60, "Te": -60,
    "To": -70, "Tr": -35, "Tu": -35, "Va": -40,
    "Ve": -40, "Vo": -50, "Wa": -30, "We": -30,
    "Wo": -35, "Ya": -60, "Ye": -60, "Yo": -65,
    "T.": -60, "T,": -60, "V.": -50, "V,": -50,
    "Y.": -60, "Y,": -60,
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def h_measurement(path):
    """Use the same representative H-stem measure as the saved weight draft."""
    alpha = np.asarray(Image.open(path).getchannel("A")) > 128
    ys, xs = np.where(alpha)
    height = int(ys.max() + 1 - ys.min())
    row = alpha[ys.min() + round(height * 0.22)]
    starts = np.where(np.diff(np.r_[False, row, False].astype(int)) == 1)[0]
    ends = np.where(np.diff(np.r_[False, row, False].astype(int)) == -1)[0]
    widths = [int(end - start) for start, end in zip(starts, ends)]
    if len(widths) != 2:
        raise ValueError(f"Expected two H stems: {path} {widths}")
    return {"height_pixels": height, "stem_widths_pixels": widths,
            "mean_stem_height_ratio": float(np.mean(widths) / height)}


def weight_settings():
    regular = h_measurement(SOURCE / "glyphs/U0048.png")
    bold = h_measurement(OLD_BOLD / "glyphs/U0048.png")
    ratio = bold["mean_stem_height_ratio"]
    height = regular["height_pixels"]
    stem = float(np.mean(regular["stem_widths_pixels"]))
    # Grow on both sides, then normalize the whole glyph to the original cap
    # height. This preserves weight relative to the saved Bold cap height.
    growth = (ratio * height - stem) / (1 - ratio)
    radius = growth / 2
    scale = height / (height + growth)
    alpha = np.asarray(Image.open(SOURCE / "glyphs/U0048.png").getchannel("A")) > 128
    bottom = int(np.where(alpha)[0].max() + 1)
    return {"reference_regular_H": regular, "reference_saved_bold_H": bold,
            "radius_pixels": radius, "normalization_scale": scale,
            "normalization_baseline_pixels": bottom}


def write_svg(path, title, commands):
    ET.register_namespace("", SVG_NS)
    root = ET.Element(f"{{{SVG_NS}}}svg", {
        "width": "1024", "height": "1024", "viewBox": "0 0 1024 1024",
    })
    ET.SubElement(root, f"{{{SVG_NS}}}title").text = title
    if commands:
        ET.SubElement(root, f"{{{SVG_NS}}}path", {
            "d": commands, "fill": "#000000", "fill-rule": "nonzero",
        })
    ET.indent(root)
    ET.ElementTree(root).write(path, encoding="utf-8", xml_declaration=True)


def generate_bold(settings):
    """Expand the existing filled outlines; retain counters and the baseline."""
    directory = ROOT / "bold/svg"
    directory.mkdir(parents=True, exist_ok=True)
    radius = settings["radius_pixels"]
    scale = settings["normalization_scale"]
    baseline = settings["normalization_baseline_pixels"]
    with tempfile.TemporaryDirectory(prefix="keiland-bold-") as temporary:
        bitmap_path = Path(temporary) / "glyph.pbm"
        trace_path = Path(temporary) / "glyph.svg"
        for codepoint in [None, *range(32, 127)]:
            filename = ".notdef.svg" if codepoint is None else f"U{codepoint:04X}.svg"
            surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, 2048, 2048)
            context = cairo.Context(surface)
            context.scale(2, 2)
            center = 512.4375
            context.translate(center, baseline)
            context.scale(scale, scale)
            context.translate(-center, -baseline - radius)
            source_path = SOURCE / "svg" / filename
            if codepoint == ord("%"):
                # The two circles need more room after the stroke grows.
                # Keep their shapes and weight, separating them from the slash.
                recording = RecordingPen()
                SVGPath(str(source_path)).draw(recording)
                contour = []
                for operation, points in recording.value:
                    contour.append((operation, points))
                    if operation not in ["closePath", "endPath"]:
                        continue
                    bounds_pen = BoundsPen(None)
                    for method, arguments in contour:
                        getattr(bounds_pen, method)(*arguments)
                    x0, y0, x1, y1 = bounds_pen.bounds
                    offset = 0
                    if y1 - y0 < 300:
                        offset = -radius * 2 if (y0 + y1) / 2 < 500 else radius * 2
                    target = TransformPen(CairoPen(None, context), (1, 0, 0, 1, offset, 0))
                    for method, arguments in contour:
                        getattr(target, method)(*arguments)
                    contour = []
            else:
                SVGPath(str(source_path)).draw(CairoPen(None, context))
            context.set_source_rgba(0, 0, 0, 1)
            context.fill_preserve()
            context.set_line_width(radius * 2)
            context.set_line_join(cairo.LINE_JOIN_ROUND)
            context.stroke()
            buffer = io.BytesIO()
            surface.write_to_png(buffer)
            buffer.seek(0)
            alpha_image = Image.open(buffer).getchannel("A").resize(
                (1024, 1024), Image.Resampling.LANCZOS,
            )
            alpha = np.asarray(alpha_image)
            commands = ""
            if codepoint != 32:
                bitmap = Image.fromarray(np.where(alpha >= 128, 0, 255).astype("uint8"))
                bitmap.convert("1").save(bitmap_path)
                subprocess.run([
                    "potrace", str(bitmap_path), "--svg", "--flat", "--unit", "10",
                    "--turdsize", "2", "--alphamax", "1.0", "--opttolerance", "0.2",
                    "--output", str(trace_path),
                ], check=True)
                pen = SVGPathPen(None)
                SVGPath(str(trace_path), transform=(0.1, 0, 0, -0.1, 0, 1024)).draw(pen)
                commands = pen.getCommands()
                if not commands:
                    raise ValueError(f"Empty bold glyph: {filename}")
            write_svg(directory / filename, f"Mahora Bold {filename}", commands)


def svg_to_glyph(path):
    pen = TTGlyphPen(None)
    SVGPath(str(path)).draw(TransformPen(Cu2QuPen(pen, max_err=1.0), FONT_TRANSFORM))
    return pen.glyph()


def set_names(font, mono=False, bold=False):
    family = "Mahora Mono" if mono else "Mahora"
    style = "Bold" if bold else "Regular"
    full = "Mahora Mono" if mono else f"Mahora {style}"
    ps_name = "MahoraMono" if mono else f"Mahora-{style}"
    names = {1: family, 2: style, 3: f"{ps_name}-{VERSION}", 4: full,
             5: f"Version {VERSION}", 6: ps_name, 16: family, 17: style,
             10: f"{full}. Unicode ASCII font derived from the approved font1 design."}
    font["name"].names = []
    for name_id, value in names.items():
        font["name"].setName(value, name_id, 3, 1, 0x409)
        font["name"].setName(value, name_id, 1, 0, 0)
    font["head"].fontRevision = float(VERSION)
    font["head"].macStyle = 1 if bold else 0
    font["OS/2"].usWeightClass = 700 if bold else 400
    font["OS/2"].fsSelection = 0xA0 if bold else 0xC0
    font["OS/2"].panose.bWeight = 8 if bold else 5
    font["OS/2"].panose.bProportion = 9 if mono else 0
    font["post"].isFixedPitch = 1 if mono else 0
    font.recalcTimestamp = False


def proportional_metrics(font, regular_advances=None):
    cmap = font.getBestCmap()
    for name in font.getGlyphOrder():
        font["glyf"][name].recalcBounds(font["glyf"])
    digit_names = {cmap[cp] for cp in range(48, 58)}
    digit_advance = max(font["glyf"][name].xMax - font["glyf"][name].xMin
                        for name in digit_names) + SIDEBEARING * 2
    advances = {}
    for name in font.getGlyphOrder():
        glyph = font["glyf"][name]
        glyph.recalcBounds(font["glyf"])
        if not glyph.numberOfContours:
            advance = SPACE_ADVANCE
            left = 0
        else:
            width = glyph.xMax - glyph.xMin
            if regular_advances is not None:
                advance = regular_advances[name]
            else:
                advance = digit_advance if name in digit_names else width + SIDEBEARING * 2
            left = (advance - width) // 2
            if left < 0:
                raise ValueError(f"Glyph too wide for its proportional advance: {name}")
            coordinates, _, _ = glyph.getCoordinates(font["glyf"])
            coordinates.translate((left - glyph.xMin, 0))
            glyph.recalcBounds(font["glyf"])
        font["hmtx"].metrics[name] = (advance, left)
        advances[name] = advance
    features = "languagesystem DFLT dflt; languagesystem latn dflt;\nfeature kern {\n"
    for pair, adjustment in KERN_PAIRS.items():
        features += f"  pos {cmap[ord(pair[0])]} {cmap[ord(pair[1])]} {adjustment};\n"
    features += "} kern;\n"
    addOpenTypeFeaturesFromString(font, features)
    font["OS/2"].recalcAvgCharWidth(font)
    return advances


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reuse-bold-svg", action="store_true",
                        help="Build Bold from existing edited SVGs, retaining their edits")
    arguments = parser.parse_args()
    settings = weight_settings()
    if not arguments.reuse_bold_svg:
        generate_bold(settings)
    mono = TTFont(SOURCE / "Font1-Regular.ttf")
    set_names(mono, mono=True)
    mono.save(ROOT / "Mahora-Mono.ttf")
    regular = deepcopy(mono)
    set_names(regular)
    advances = proportional_metrics(regular)
    regular.save(ROOT / "Mahora-Regular.ttf")
    bold = deepcopy(mono)
    set_names(bold, bold=True)
    cmap = bold.getBestCmap()
    for name in bold.getGlyphOrder():
        codepoint = next((cp for cp, glyph in cmap.items() if glyph == name), None)
        filename = ".notdef.svg" if codepoint is None else f"U{codepoint:04X}.svg"
        bold["glyf"][name] = svg_to_glyph(ROOT / "bold/svg" / filename)
    proportional_metrics(bold, regular_advances=advances)
    bold["OS/2"].sCapHeight = bold["glyf"]["H"].yMax
    bold["OS/2"].sxHeight = bold["glyf"]["x"].yMax
    bold["post"].underlineThickness = 88
    bold.save(ROOT / "Mahora-Bold.ttf")
    report = {
        "version": VERSION, "coverage": "Unicode U+0020-U+007E / ASCII 95",
        "source_font": "../font1/regular/Font1-Regular.ttf",
        "source_font_sha256": digest(SOURCE / "Font1-Regular.ttf"),
        "sidebearing_units": SIDEBEARING, "space_advance_units": SPACE_ADVANCE,
        "digits": "Tabular numerals in both proportional weights",
        "bold": settings, "regular_and_bold_share_advances_and_kerning": True,
        "bold_optical_adjustments": {
            "percent_circle_x_offsets_pixels": [-settings["radius_pixels"] * 2,
                                                settings["radius_pixels"] * 2],
            "reason": "Keep both circles separate from the slash at the Bold weight",
        },
        "kerning_pairs_units": KERN_PAIRS,
        "fonts": [{"file": path.name, "sha256": digest(path)}
                  for path in sorted(ROOT.glob("*.ttf"))],
        "bold_svg_inputs": [{"file": str(path.relative_to(ROOT)), "sha256": digest(path)}
                            for path in sorted((ROOT / "bold/svg").glob("*.svg"))],
    }
    (ROOT / "font-build.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Built Mahora Mono / Mahora Regular / Mahora Bold, Unicode ASCII 95.")


if __name__ == "__main__":
    main()
