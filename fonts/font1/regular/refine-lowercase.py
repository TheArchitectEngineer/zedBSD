#!/usr/bin/env python3
"""Expand the editable monoline centerlines into lowercase SVGs and PNGs."""

import io
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path
import subprocess
import tempfile

import cairo
from fontTools.pens.cairoPen import CairoPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.svgLib.path import SVGPath, parse_path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent
SPEC = spec_from_file_location("font1_builder", ROOT / "build-font.py")
BUILDER = module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)


def render(definition, width, size, outline=None):
    """Use one stroke width in every direction, including curved strokes."""
    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, size * 2, size * 2)
    context = cairo.Context(surface)
    context.scale(2, 2)
    context.set_source_rgba(0, 0, 0, 1)
    pen = CairoPen(None, context)
    if outline is not None:
        SVGPath(str(outline)).draw(pen)
        context.fill()
    else:
        context.translate(*definition.get("translation_pixels", (0, 0)))
        context.set_line_width(width)
        context.set_line_cap(cairo.LINE_CAP_BUTT)
        context.set_line_join(cairo.LINE_JOIN_ROUND)
        for commands in definition["paths"]:
            parse_path(commands, pen)
            context.stroke()
        for dot in definition.get("dots", []):
            context.arc(dot["cx"], dot["cy"], dot["radius"], 0, 6.283185307179586)
            context.fill()
    buffer = io.BytesIO()
    surface.write_to_png(buffer)
    buffer.seek(0)
    return Image.open(buffer).convert("RGBA").resize(
        (size, size), Image.Resampling.LANCZOS,
    )


def main():
    design = json.loads((ROOT / "lowercase-strokes.json").read_text())
    if set(design["glyphs"]) != set("abcdefghijklmnopqrstuvwxyz"):
        raise ValueError("The design must contain exactly a-z")
    width = design["stroke_width_pixels"]
    size = design["canvas_pixels"]
    if design["line_cap"] != "butt" or design["line_join"] != "round":
        raise ValueError("This construction uses butt caps and round joins")
    with tempfile.TemporaryDirectory(prefix="font1-lowercase-") as directory:
        bitmap_path = Path(directory) / "glyph.pbm"
        trace_path = Path(directory) / "glyph.svg"
        for character, definition in design["glyphs"].items():
            if definition.get("preserve_outline"):
                codepoint = ord(character)
                if not BUILDER.svg_filename(codepoint).is_file():
                    raise ValueError(f"Missing preserved SVG for {character}")
                if not (ROOT / "glyphs" / f"U{codepoint:04X}.png").is_file():
                    raise ValueError(f"Missing preserved PNG for {character}")
                continue
            image = render(definition, width, size)
            alpha = np.asarray(image.getchannel("A"))
            bitmap = Image.fromarray(np.where(alpha >= 128, 0, 255).astype("uint8"))
            bitmap.convert("1").save(bitmap_path)
            subprocess.run([
                "potrace", str(bitmap_path), "--svg", "--flat", "--unit", "10",
                "--turdsize", "2", "--alphamax", "1.0", "--opttolerance", "0.2",
                "--output", str(trace_path),
            ], check=True)
            pen = SVGPathPen(None)
            SVGPath(str(trace_path), transform=(0.1, 0, 0, -0.1, 0, size)).draw(pen)
            codepoint = ord(character)
            svg_path = BUILDER.svg_filename(codepoint)
            BUILDER.write_svg(
                svg_path, f"Font1 Regular U+{codepoint:04X} {character}",
                pen.getCommands(),
            )
            # Export PNGs from the final filled paths, so both editable assets
            # describe the same outlines rather than two separate revisions.
            render(definition, width, size, outline=svg_path).save(
                ROOT / "glyphs" / f"U{codepoint:04X}.png",
            )
    preserved = "".join(character for character, definition in design["glyphs"].items()
                        if definition.get("preserve_outline"))
    print(f"Updated lowercase: {width}px construction; preserved outlines: {preserved}.")


if __name__ == "__main__":
    main()
