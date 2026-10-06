#!/usr/bin/env python3
"""Trace the Regular PNGs to SVG, or build the font from editable SVGs."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET

from fontTools import __version__ as fonttools_version
from fontTools.agl import UV2AGL
from fontTools.fontBuilder import FontBuilder, buildCmapSubTable
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.transformPen import TransformPen
from fontTools.svgLib.path import SVGPath
from fontTools.ttLib import newTable
import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parent
SVG_DIRECTORY = ROOT / "svg"
FONT_PATH = ROOT / "Font1-Regular.ttf"
CODEPOINTS = range(0x20, 0x7F)
UNITS_PER_EM = 2048
ADVANCE = 1229
ASCENT = 1638
DESCENT = -410
PIXEL_SCALE = UNITS_PER_EM / 768
SVG_NAMESPACE = "http://www.w3.org/2000/svg"
FONT_TRANSFORM = (
    PIXEL_SCALE, 0, 0, -PIXEL_SCALE,
    -282 * PIXEL_SCALE, 745 * PIXEL_SCALE,
)


def svg_filename(codepoint):
    """Keep the Unicode identity in the editable filename."""
    return SVG_DIRECTORY / f"U{codepoint:04X}.svg"


def write_svg(path, title, commands):
    """Write actual vector paths in the original PNG coordinate system."""
    ET.register_namespace("", SVG_NAMESPACE)
    root = ET.Element(
        f"{{{SVG_NAMESPACE}}}svg",
        {"width": "1024", "height": "1024", "viewBox": "0 0 1024 1024"},
    )
    ET.SubElement(root, f"{{{SVG_NAMESPACE}}}title").text = title
    if commands:
        ET.SubElement(
            root,
            f"{{{SVG_NAMESPACE}}}path",
            {"d": commands, "fill": "#000000", "fill-rule": "nonzero"},
        )
    ET.indent(root)
    ET.ElementTree(root).write(path, encoding="utf-8", xml_declaration=True)


def trace_images():
    """Convert opaque ink to smooth cubic curves without embedding PNGs."""
    SVG_DIRECTORY.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="font1-trace-") as temporary:
        temporary = Path(temporary)
        for codepoint in CODEPOINTS:
            image_path = ROOT / "glyphs" / f"U{codepoint:04X}.png"
            with Image.open(image_path) as image:
                alpha = np.asarray(image.getchannel("A"))
            commands = ""
            if codepoint != 0x20:
                bitmap_path = temporary / "glyph.pbm"
                trace_path = temporary / "glyph.svg"
                bitmap = Image.fromarray(np.where(alpha >= 128, 0, 255).astype("uint8"))
                bitmap.convert("1").save(bitmap_path)
                subprocess.run(
                    [
                        "potrace", str(bitmap_path), "--svg", "--flat",
                        "--unit", "10",
                        "--turdsize", "2", "--alphamax", "1.0",
                        "--opttolerance", "0.5", "--output", str(trace_path),
                    ],
                    check=True,
                )
                pen = SVGPathPen(None)
                # Potrace uses ten units per pixel and an SVG group that
                # flips Y. Apply it explicitly before writing flat paths;
                # SVGPath does not inherit transforms from ancestor groups.
                trace_transform = (0.1, 0, 0, -0.1, 0, 1024)
                SVGPath(str(trace_path), transform=trace_transform).draw(pen)
                commands = pen.getCommands()
                if not commands:
                    raise ValueError(f"Empty traced glyph U+{codepoint:04X}")
            write_svg(
                svg_filename(codepoint),
                f"Font1 Regular U+{codepoint:04X} {chr(codepoint)}",
                commands,
            )

    # Glyph zero has a visible missing-character box, separate from Unicode.
    write_svg(
        SVG_DIRECTORY / ".notdef.svg",
        "Font1 Regular missing character",
        "M342 240H682V745H342Z M380 278V707H644V278Z",
    )


def svg_to_glyph(path):
    """Translate SVG cubic curves into integer TrueType quadratic contours."""
    pen = TTGlyphPen(None)
    quadratic_pen = Cu2QuPen(pen, max_err=1.0)
    transformed_pen = TransformPen(quadratic_pen, FONT_TRANSFORM)
    SVGPath(str(path)).draw(transformed_pen)
    return pen.glyph()


def build_font():
    """Use Unicode cmaps and one advance for every glyph, including space."""
    mapping = {codepoint: UV2AGL.get(codepoint, f"uni{codepoint:04X}")
               for codepoint in CODEPOINTS}
    glyph_order = [".notdef", *mapping.values()]
    glyphs = {".notdef": svg_to_glyph(SVG_DIRECTORY / ".notdef.svg")}
    for codepoint, name in mapping.items():
        glyphs[name] = svg_to_glyph(svg_filename(codepoint))

    builder = FontBuilder(UNITS_PER_EM, isTTF=True)
    builder.setupGlyphOrder(glyph_order)
    builder.setupCharacterMap(mapping)

    # BMP compatibility and full Unicode encoding both map this ASCII subset.
    # Separate dictionaries prevent fontTools from sharing a format-4 binary
    # with the format-12 records just because their mapping objects match.
    builder.font["cmap"].tables.extend([
        buildCmapSubTable(dict(mapping), 12, 0, 4),
        buildCmapSubTable(dict(mapping), 12, 3, 10),
    ])
    builder.setupGlyf(glyphs)
    metrics = {}
    for name, glyph in glyphs.items():
        glyph.recalcBounds(builder.font["glyf"])
        left = getattr(glyph, "xMin", 0)
        right = getattr(glyph, "xMax", 0)
        if left < 0 or right > ADVANCE:
            raise ValueError(f"Glyph exceeds monospace cell: {name} {left}..{right}")
        metrics[name] = (ADVANCE, left)
    builder.setupHorizontalMetrics(metrics)
    builder.setupHorizontalHeader(ascent=ASCENT, descent=DESCENT, lineGap=0)
    builder.setupNameTable({
        "familyName": "Font1",
        "styleName": "Regular",
        "uniqueFontIdentifier": "Font1-Regular-0.201",
        "fullName": "Font1 Regular",
        "psName": "Font1-Regular",
        "version": "Version 0.201",
        "typographicFamily": "Font1",
        "typographicSubfamily": "Regular",
        "description": "Font1 Regular. Unicode-encoded ASCII design prototype.",
    })
    builder.setupOS2(
        version=4,
        sTypoAscender=ASCENT,
        sTypoDescender=DESCENT,
        sTypoLineGap=0,
        usWinAscent=ASCENT,
        usWinDescent=-DESCENT,
        usWeightClass=400,
        usWidthClass=5,
        fsSelection=0xC0,
        sxHeight=glyphs["x"].yMax,
        sCapHeight=glyphs["H"].yMax,
        usDefaultChar=0,
        usBreakChar=0x20,
    )
    builder.font["OS/2"].panose.bFamilyType = 2
    builder.font["OS/2"].panose.bProportion = 9
    builder.setupPost(
        isFixedPitch=1, underlinePosition=-180, underlineThickness=70,
    )
    builder.setupMaxp()
    builder.setupHead(unitsPerEm=UNITS_PER_EM, fontRevision=0.201, macStyle=0)
    builder.font["gasp"] = newTable("gasp")
    builder.font["gasp"].version = 1
    builder.font["gasp"].gaspRange = {65535: 10}

    # Fix timestamps so unchanged SVGs produce the same binary on rebuilding.
    builder.font.recalcTimestamp = False
    builder.font["head"].created = 3874089600  # 2026-10-06, TrueType epoch.
    builder.font["head"].modified = builder.font["head"].created
    builder.save(FONT_PATH)

    report = {
        "family": "Font1", "style": "Regular", "version": "0.201",
        "font": FONT_PATH.name,
        "font_sha256": hashlib.sha256(FONT_PATH.read_bytes()).hexdigest(),
        "units_per_em": UNITS_PER_EM, "advance_units": ADVANCE,
        "ascent_units": ASCENT, "descent_units": DESCENT,
        "encoding": "Unicode / ISO 10646 semantics",
        "coverage": "U+0020-U+007E (95 printable ASCII characters)",
        "glyph_count": len(glyph_order), "missing_glyph": ".notdef",
        "cmap_subtables": [
            {"platform": table.platformID, "encoding": table.platEncID,
             "format": table.format}
            for table in sorted(builder.font["cmap"].tables,
                                key=lambda table: (table.platformID, table.platEncID))
        ],
        "trace": {"alpha_threshold": 128, "turdsize": 2,
                  "alphamax": 1.0, "opttolerance_pixels": 0.5},
        "cubic_to_quadratic_error_units": 1.0,
        "hinting": "No manually authored TrueType hint instructions",
        "fonttools_version": fonttools_version,
        "svg_sources": [
            {"codepoint": f"U+{codepoint:04X}", "glyph": name,
             "file": str(svg_filename(codepoint).relative_to(ROOT)),
             "sha256": hashlib.sha256(svg_filename(codepoint).read_bytes()).hexdigest()}
            for codepoint, name in mapping.items()
        ],
    }
    lowercase_path = ROOT / "lowercase-strokes.json"
    if lowercase_path.exists():
        report["lowercase_construction_reference"] = {
            "file": lowercase_path.name,
            "sha256": hashlib.sha256(lowercase_path.read_bytes()).hexdigest(),
            "note": "Centerline source; editable filled SVGs remain the direct TTF inputs",
            "outline_trace_tolerance_pixels": 0.2,
        }
    (ROOT / "font-build.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Built {FONT_PATH}: 95 Unicode mappings, 96 glyphs, fixed advance {ADVANCE}.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", action="store_true",
                        help="Regenerate SVGs from PNGs before building (overwrites SVGs).")
    arguments = parser.parse_args()
    if arguments.trace:
        trace_images()
    build_font()


if __name__ == "__main__":
    main()
