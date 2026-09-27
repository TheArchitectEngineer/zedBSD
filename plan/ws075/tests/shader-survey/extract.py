#!/usr/bin/env python3
# ws075-p001: takes the GLSL shaders out of C sources (static const char NAME[] = "..." "...";, and each element of
# static const char *const NAME[...] = { "..." "...", ... };) and writes each as OUTDIR/<file>-<NAME>[-<index>].<stage>:
# a geometry shader (EmitVertex) is geom, a vertex shader (writes gl_Position) vert, any other frag.  Prints the paths.
#
#   extract.py OUTDIR FILE.c ...
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os
import re
import sys

STRING = r'"(?:[^"\\]|\\.)*"'
LITERAL = re.compile(r'static\s+const\s+char\s+(\w+)\s*\[\]\s*=\s*((?:' + STRING + r'\s*)+);')
ARRAY = re.compile(r'static\s+const\s+char\s*\*\s*const\s+(\w+)\s*\[[^\]]*\]\s*=\s*\{((?:\s*(?:' + STRING + r'\s*)+,?)+)\s*\};')
ELEMENT = re.compile(r'((?:' + STRING + r'\s*)+),?')
PIECE = re.compile(r'"((?:[^"\\]|\\.)*)"')


def unescape(text):
	"""Returns a C string literal's text with its escapes (\\n, \\t, \\", \\\\) replaced."""
	return text.encode('latin-1').decode('unicode_escape')


def main():
	out = sys.argv[1]
	os.makedirs(out, exist_ok=True)
	for path in sys.argv[2:]:
		source = open(path, encoding='utf-8').read()
		source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
		base = os.path.splitext(os.path.basename(path))[0]

		# The single literals, then each element of the arrays of them.
		found = []
		for match in LITERAL.finditer(source):
			found.append((match.group(1), match.group(2)))
		for match in ARRAY.finditer(source):
			for index, element in enumerate(ELEMENT.findall(match.group(2))):
				found.append(('%s-%d' % (match.group(1), index), element))

		# Each shader (a string with a main), by its stage.
		for name, literal in found:
			text = ''.join(unescape(piece) for piece in PIECE.findall(literal))
			if 'main' not in text:
				continue
			stage = 'frag'
			if 'EmitVertex' in text:
				stage = 'geom'
			elif 'gl_Position' in text:
				stage = 'vert'
			name = os.path.join(out, '%s-%s.%s' % (base, name, stage))
			with open(name, 'w', encoding='utf-8') as file:
				file.write(text)
			print(name)


if __name__ == '__main__':
	main()
