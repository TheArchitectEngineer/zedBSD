#!/usr/bin/env python3
"""Verify the deliverable names, spacing, retained shapes and Bold weight."""

import hashlib
import json
from pathlib import Path
import subprocess

from fontTools.ttLib import TTFont
import numpy as np
from PIL import Image, ImageDraw, ImageFont, features
from scipy import ndimage


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT.parent / "font1/regular/Font1-Regular.ttf"


def ink(face, character):
    canvas = Image.new("L", (1300, 1100))
    ImageDraw.Draw(canvas).text((40, 820), character, font=face, fill=255, anchor="ls")
    return np.asarray(canvas) >= 128


def topology(mask):
    return ndimage.label(mask)[1], ndimage.label(~mask)[1] - 1


def h_measurement(mask):
    ys, xs = np.where(mask)
    height = int(ys.max() + 1 - ys.min())
    row = mask[ys.min() + round(height * 0.22)]
    starts = np.where(np.diff(np.r_[False, row, False].astype(int)) == 1)[0]
    ends = np.where(np.diff(np.r_[False, row, False].astype(int)) == -1)[0]
    widths = [int(b - a) for a, b in zip(starts, ends)]
    assert len(widths) == 2, widths
    return {"height_pixels": height, "stem_widths_pixels": widths,
            "mean_stem_height_ratio": float(np.mean(widths) / height)}


def main():
    source = TTFont(SOURCE)
    fonts = {}
    results = {}
    for filename, family, style, full_name, mono in [
        ("Keiland-Mono.ttf", "Keiland Mono", "Regular", "Keiland Mono", True),
        ("Keiland-Regular.ttf", "Keiland", "Regular", "Keiland Regular", False),
        ("Keiland-Bold.ttf", "Keiland", "Bold", "Keiland Bold", False),
    ]:
        path = ROOT / filename
        font = TTFont(path, checkChecksums=2)
        for tag in font.keys():
            if tag != "GlyphOrder":
                font[tag]
        cmap = font.getBestCmap()
        assert set(cmap) == set(range(32, 127))
        assert len(font.getGlyphOrder()) == 96
        assert font["head"].unitsPerEm == 2048
        assert font["name"].getDebugName(1) == family
        assert font["name"].getDebugName(2) == style
        assert font["name"].getDebugName(4) == full_name
        assert font["name"].getDebugName(5) == "Version 0.300"
        assert bool(font["post"].isFixedPitch) == mono
        assert font["OS/2"].usWeightClass == (700 if style == "Bold" else 400)
        assert bool(font["OS/2"].fsSelection & 0x20) == (style == "Bold")
        assert bool(font["head"].macStyle & 1) == (style == "Bold")
        for table in font["cmap"].tables:
            assert table.cmap == cmap
        widths = {advance for advance, _ in font["hmtx"].metrics.values()}
        assert (widths == {1229}) if mono else len(widths) > 1
        assert ("GPOS" in font) == (not mono)
        assert font["glyf"]["space"].numberOfContours == 0
        for name in font.getGlyphOrder():
            glyph = font["glyf"][name]
            if not glyph.numberOfContours:
                continue
            advance, left = font["hmtx"][name]
            assert left == glyph.xMin
            assert 0 <= glyph.xMin <= glyph.xMax <= advance, name
            assert glyph.yMin >= -410 and glyph.yMax <= 1638, name
        face = ImageFont.truetype(str(path), 768)
        assert not ink(face, " ").any()
        for codepoint in range(33, 127):
            assert ink(face, chr(codepoint)).any()
        fc = subprocess.check_output([
            "fc-scan", "--format", "%{family}|%{style}|%{fontformat}|%{spacing}|%{charset}",
            str(path),
        ], text=True)
        fc_family, fc_style, fc_format, fc_spacing, fc_charset = fc.split("|")
        assert (fc_family, fc_style, fc_format, fc_charset) == (family, style, "TrueType", "20-7e"), fc
        assert fc_spacing == "100" if mono else fc_spacing in ["", "0"], fc
        fonts[filename] = font
        results[filename] = {
            "full_name": full_name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "unicode_mappings": 95, "glyphs": 96, "advance_count": len(widths),
            "fontconfig": fc, "H_measurement_768px": h_measurement(ink(face, "H")),
        }

    mono = fonts["Keiland-Mono.ttf"]
    regular = fonts["Keiland-Regular.ttf"]
    bold = fonts["Keiland-Bold.ttf"]
    for name in source.getGlyphOrder():
        assert source["glyf"][name].compile(source["glyf"]) == mono["glyf"][name].compile(mono["glyf"])
        assert source["hmtx"][name] == mono["hmtx"][name]
        a, ae, af = mono["glyf"][name].getCoordinates(mono["glyf"])
        b, be, bf = regular["glyf"][name].getCoordinates(regular["glyf"])
        assert ae == be and af == bf and len(a) == len(b)
        offsets = {(x2 - x1, y2 - y1) for (x1, y1), (x2, y2) in zip(a, b)}
        assert not offsets or (len(offsets) == 1 and next(iter(offsets))[1] == 0)
        assert regular["hmtx"][name][0] == bold["hmtx"][name][0]
    regular_face = ImageFont.truetype(str(ROOT / "Keiland-Regular.ttf"), 768)
    bold_face = ImageFont.truetype(str(ROOT / "Keiland-Bold.ttf"), 768)
    for codepoint in range(33, 127):
        character = chr(codepoint)
        assert topology(ink(regular_face, character)) == topology(ink(bold_face, character)), character
    build = json.loads((ROOT / "font-build.json").read_text())
    target = build["bold"]["reference_saved_bold_H"]["mean_stem_height_ratio"]
    measured = results["Keiland-Bold.ttf"]["H_measurement_768px"]
    assert abs(measured["mean_stem_height_ratio"] - target) < 0.004
    assert abs(measured["height_pixels"] - results["Keiland-Regular.ttf"]["H_measurement_768px"]["height_pixels"]) <= 2
    if features.check("raqm"):
        mono_face = ImageFont.truetype(str(ROOT / "Keiland-Mono.ttf"), 64)
        reg_face = ImageFont.truetype(str(ROOT / "Keiland-Regular.ttf"), 64)
        bld_face = ImageFont.truetype(str(ROOT / "Keiland-Bold.ttf"), 64)
        assert mono_face.getlength("iii") == mono_face.getlength("MMM")
        assert reg_face.getlength("iii") < reg_face.getlength("MMM")
        assert reg_face.getlength("AV") < reg_face.getlength("AV", features=["-kern"])
        sample = "AVATAR To Wa Yo  align minimum little passphrase 0123456789"
        assert reg_face.getlength(sample) == bld_face.getlength(sample)
    report = {
        "fonts": results, "mono_outlines_and_metrics_match_approved_source": True,
        "proportional_regular_shapes_preserved_by_x_translation": True,
        "regular_bold_advances_match": True, "bold_components_and_counters_preserved": True,
        "bold_H_target_ratio": target, "raqm_layout_checks_performed": features.check("raqm"),
    }
    (ROOT / "font-verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: names, Unicode, pitch, approved shapes, Bold counters and target weight.")


if __name__ == "__main__":
    main()
