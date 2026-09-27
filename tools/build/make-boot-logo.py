#!/usr/bin/env python3
"""Draws the Kei boot logo as a binary PPM (ws035-p096, redrawn for Kei in ws078).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

The logo is made here from shapes, so no picture from elsewhere is in the
tree (plan/ws035/kei-identity-design.md describes it in words): the mark is
two overlapping translucent shapes in blue gradients, a tall rounded bar and
a leaf leaning to the upper right; under it the word Kei in thin strokes of
a small geometric font of its own, and under that "powered by zedBSD" in
small spaced letters.  The background is one pale colour; the UEFI loader
fills the screen with the colour of the top-left pixel and draws the logo in
the middle.  The output is the same for the same script.

    make-boot-logo.py OUTPUT.ppm
"""
import math
import sys

WIDTH = 520
HEIGHT = 400
BACKGROUND = (236, 243, 250)
SLATE = (46, 58, 76)
GREY = (120, 132, 146)

# The mark: the bar (a rounded rectangle) and the leaf (a lens between two points).
MARK_TOP = 24
BAR_LEFT = WIDTH / 2 - 78
BAR_WIDTH = 62
BAR_HEIGHT = 150
BAR_RADIUS = 30
BAR_LIGHT = (196, 214, 250)
BAR_DEEP = (120, 158, 238)
LEAF_FROM = (WIDTH / 2 - 50, MARK_TOP + 150)
LEAF_TO = (WIDTH / 2 + 84, MARK_TOP + 62)
LEAF_BULGE = 0.36
LEAF_DEEP = (36, 108, 236)
LEAF_LIGHT = (176, 222, 250)
MARK_ALPHA = 0.86

# The word Kei and the line under it: height of capitals and of lower case, stroke, baseline, letter spacing.
WORD = (70.0, 48.0, 6.0, 282.0, 10.0)
LINE = (15.0, 10.5, 1.9, 330.0, 5.0)


def arc(cx, cy, rx, ry, start, end, steps=40):
	"""A polyline along an ellipse from start to end degrees (0 is right, 90 is up)."""
	points = []
	for index in range(steps + 1):
		angle = math.radians(start + (end - start) * index / steps)
		points.append((cx + rx * math.cos(angle), cy - ry * math.sin(angle)))
	return points


def letter(name, x, metrics):
	"""The strokes (polylines) of one letter at x in a size, and its width."""
	cap, xheight, stroke, baseline, _ = metrics
	unit = cap / 80.0
	half = stroke / 2
	top = baseline - cap
	xtop = baseline - xheight
	r = xheight / 2 - half
	cx = x + r + half
	cy = xtop + xheight / 2
	descender = xheight * 0.45
	if name == ' ':
		return [], xheight * 0.55
	if name == 'K':
		w = 46.0 * unit
		joint = top + cap * 0.58
		return [[(x + half, top + half), (x + half, baseline - half)],
		        [(x + w - half, top + half), (x + half, joint)],
		        [(x + half + cap * 0.2, top + cap * 0.44), (x + w - half, baseline - half)]], w
	if name == 'e':
		return [[(cx - r, cy), (cx + r, cy)] + arc(cx, cy, r, r, 0, 318)], 2 * r + stroke
	if name == 'i':
		dot = xtop - stroke * 2.4
		return [[(x + half, xtop + half), (x + half, baseline - half)], [(x + half, dot), (x + half, dot)]], stroke
	if name == 'o':
		return [arc(cx, cy, r, r, 0, 360, 60)], 2 * r + stroke
	if name == 'd':
		stem = cx + r
		return [arc(cx, cy, r, r, 0, 360, 60), [(stem, top + half), (stem, baseline - half)]], 2 * r + stroke
	if name == 'b':
		stem = x + half
		return [arc(stem + r, cy, r, r, 0, 360, 60), [(stem, top + half), (stem, baseline - half)]], 2 * r + stroke
	if name == 'p':
		stem = x + half
		return [arc(stem + r, cy, r, r, 0, 360, 60), [(stem, xtop + half), (stem, baseline + descender)]], 2 * r + stroke
	if name == 'r':
		stem = x + half
		return [[(stem, xtop + half), (stem, baseline - half)], arc(stem + r, xtop + half + r, r, r, 180, 70, 20)], r * 1.4 + stroke
	if name == 'w':
		w = xheight * 1.3
		return [[(x + half, xtop + half), (x + w * 0.25, baseline - half), (x + w * 0.5, xtop + xheight * 0.3),
		         (x + w * 0.75, baseline - half), (x + w - half, xtop + half)]], w
	if name == 'y':
		w = xheight * 0.9
		return [[(x + half, xtop + half), (x + w / 2, baseline - half)],
		        [(x + w - half, xtop + half), (x + w * 0.28, baseline + descender)]], w
	if name == 'z':
		w = xheight * 0.8
		return [[(x + half, xtop + half), (x + w - half, xtop + half), (x + half, baseline - half),
		         (x + w - half, baseline - half)]], w
	if name == 'B':
		w = 45.0 * unit
		mid = top + cap * 0.47
		upper = (mid - top - half) / 2
		lower = (baseline - half - mid) / 2
		line = [(x + half, baseline - half), (x + half, top + half), (x + w - upper - half, top + half)]
		line += arc(x + w - upper - half, top + half + upper, upper, upper, 90, -90)[1:]
		line += [(x + half, mid), (x + w - lower - half, mid)]
		line += arc(x + w - lower - half, mid + lower, lower, lower, 90, -90)[1:]
		line += [(x + half, baseline - half)]
		return [line], w
	if name == 'S':
		w = 47.0 * unit
		radius = (cap - stroke) / 4
		middle = x + w / 2
		upper = arc(middle, top + half + radius, w / 2 - half, radius, 20, 270, 36)
		lower = arc(middle, baseline - half - radius, w / 2 - half, radius, 90, -160, 36)
		return [upper + lower], w
	if name == 'D':
		w = 52.0 * unit
		radius = cap / 2 - half
		line = [(x + half, top + half), (x + w - radius - half, top + half)]
		line += arc(x + w - radius - half, top + cap / 2, radius, radius, 90, -90)[1:]
		line += [(x + half, baseline - half), (x + half, top + half)]
		return [line], w
	raise ValueError(name)


def text_strokes(text, metrics):
	"""The strokes of a line of text centred on the image."""
	spacing = metrics[4]
	width = 0.0
	for name in text:
		width += letter(name, 0.0, metrics)[1] + spacing
	width -= spacing

	# Lays the letters out from the left edge that centres the line.
	x = (WIDTH - width) / 2
	strokes = []
	for name in text:
		lines, advance = letter(name, x, metrics)
		strokes += lines
		x += advance + spacing
	return strokes


def cover(layer, strokes, color, width):
	"""Paints round-capped strokes of a width, antialiased over one pixel."""
	half = width / 2
	for points in strokes:
		for (ax, ay), (bx, by) in zip(points, points[1:]):
			left = int(min(ax, bx) - half - 2)
			right = int(max(ax, bx) + half + 2)
			top = int(min(ay, by) - half - 2)
			bottom = int(max(ay, by) + half + 2)
			dx = bx - ax
			dy = by - ay
			length = dx * dx + dy * dy
			for y in range(max(top, 0), min(bottom, HEIGHT)):
				for x in range(max(left, 0), min(right, WIDTH)):
					px = x + 0.5 - ax
					py = y + 0.5 - ay
					t = 0.0
					if length > 0:
						t = max(0.0, min(1.0, (px * dx + py * dy) / length))
					distance = math.hypot(px - t * dx, py - t * dy)
					alpha = max(0.0, min(1.0, half + 0.5 - distance))
					if alpha > layer[y][x][0]:
						layer[y][x] = (alpha, color)


def in_bar(x, y):
	"""Whether a point is inside the bar."""
	cx = min(max(x, BAR_LEFT + BAR_RADIUS), BAR_LEFT + BAR_WIDTH - BAR_RADIUS)
	cy = min(max(y, MARK_TOP + BAR_RADIUS), MARK_TOP + BAR_HEIGHT - BAR_RADIUS)
	return math.hypot(x - cx, y - cy) <= BAR_RADIUS


def leaf_circles():
	"""The two circles whose intersection is the leaf."""
	(ax, ay), (bx, by) = LEAF_FROM, LEAF_TO
	mx = (ax + bx) / 2
	my = (ay + by) / 2
	chord = math.hypot(bx - ax, by - ay)
	sagitta = chord * LEAF_BULGE / 2
	radius = (chord * chord / 4 + sagitta * sagitta) / (2 * sagitta)
	nx = -(by - ay) / chord
	ny = (bx - ax) / chord
	offset = radius - sagitta
	return (mx + nx * offset, my + ny * offset, radius), (mx - nx * offset, my - ny * offset, radius)


def mix(a, b, t):
	return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def main():
	if len(sys.argv) != 2:
		sys.exit("usage: make-boot-logo.py OUTPUT.ppm")

	# The background, and a coverage layer for the letters (alpha, colour).
	pixels = [[BACKGROUND for _ in range(WIDTH)] for _ in range(HEIGHT)]
	letters = [[(0.0, SLATE) for _ in range(WIDTH)] for _ in range(HEIGHT)]
	first, second = leaf_circles()
	(ax, ay), (bx, by) = LEAF_FROM, LEAF_TO
	span = (bx - ax) * (bx - ax) + (by - ay) * (by - ay)

	# The mark: the bar, then the leaf over it, each translucent, sampled 4x4 per pixel.
	for y in range(MARK_TOP - 2, MARK_TOP + BAR_HEIGHT + 4):
		for x in range(int(BAR_LEFT) - 2, int(LEAF_TO[0]) + 8):
			color = [0.0, 0.0, 0.0]
			for sample in range(16):
				sx = x + (sample % 4 + 0.5) / 4
				sy = y + (sample // 4 + 0.5) / 4
				shade = pixels[y][x]
				if in_bar(sx, sy):
					depth = (sy - MARK_TOP) / BAR_HEIGHT
					shade = mix(shade, mix(BAR_LIGHT, BAR_DEEP, depth), MARK_ALPHA)
				inside_first = math.hypot(sx - first[0], sy - first[1]) <= first[2]
				inside_second = math.hypot(sx - second[0], sy - second[1]) <= second[2]
				if inside_first and inside_second:
					along = ((sx - ax) * (bx - ax) + (sy - ay) * (by - ay)) / span
					shade = mix(shade, mix(LEAF_DEEP, LEAF_LIGHT, max(0.0, min(1.0, along))), MARK_ALPHA)
				color = [color[i] + shade[i] / 16 for i in range(3)]
			pixels[y][x] = tuple(color)

	# The word and the line under it.
	cover(letters, text_strokes("Kei", WORD), SLATE, WORD[2])
	cover(letters, text_strokes("powered by zedBSD", LINE), GREY, LINE[2])

	# The letters over the background.
	with open(sys.argv[1], "wb") as output:
		output.write(b"P6\n%d %d\n255\n" % (WIDTH, HEIGHT))
		row = bytearray()
		for y in range(HEIGHT):
			for x in range(WIDTH):
				alpha, color = letters[y][x]
				row += bytes(round(value) for value in mix(pixels[y][x], color, alpha))
		output.write(bytes(row))


if __name__ == "__main__":
	main()
