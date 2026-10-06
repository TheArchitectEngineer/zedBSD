#!/usr/bin/env python3
"""check-subset.py: ws175-p005, reads a subset host-truetype-subset made with fontTools.

    check-subset.py ORIGINAL SUBSET TEXT

The subset must open (checksums checked), keep the glyphs' count, draw the glyphs of TEXT (and their composite parts) as
the original does, leave the other glyphs empty, keep the widths, and have no cmap, layout or variation table.
Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import sys
from fontTools.ttLib import TTFont


def main() -> int:
	original = TTFont(sys.argv[1])
	subset = TTFont(sys.argv[2], checkChecksums=2)
	text = sys.argv[3]
	bad = 0
	if subset["maxp"].numGlyphs != original["maxp"].numGlyphs:
		print("FAIL glyph count", subset["maxp"].numGlyphs, original["maxp"].numGlyphs)
		bad += 1
	for table in ("cmap", "GSUB", "GPOS", "GDEF", "fvar", "gvar", "vhea", "vmtx"):
		if table in subset:
			print("FAIL table kept:", table)
			bad += 1
	order = original.getGlyphOrder()
	cmap = original.getBestCmap()
	wanted = {cmap[ord(ch)] for ch in text if ord(ch) in cmap}
	glyf = original["glyf"]
	for name in list(wanted):
		glyph = glyf[name]
		if glyph.isComposite():
			wanted.update(component.glyphName for component in glyph.components)
	sub_glyf = subset["glyf"]
	sub_order = subset.getGlyphOrder()
	for index, name in enumerate(order):
		mine = sub_glyf[sub_order[index]]
		theirs = glyf[name]
		if name in wanted or index == 0:
			if mine.compile(sub_glyf) != theirs.compile(glyf):
				print("FAIL glyph differs:", index, name)
				bad += 1
		elif mine.numberOfContours != 0:
			print("FAIL glyph not empty:", index, name)
			bad += 1
	widths = [original["hmtx"][name][0] for name in order]
	sub_widths = [subset["hmtx"][name][0] for name in sub_order]
	if widths != sub_widths:
		print("FAIL widths differ")
		bad += 1
	print(f"check-subset: {len(wanted)} glyphs drawn, {bad} failures")
	return 1 if bad else 0


if __name__ == "__main__":
	sys.exit(main())
