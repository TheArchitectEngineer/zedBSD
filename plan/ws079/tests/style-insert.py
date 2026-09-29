#!/usr/bin/env python3
"""Inserts paragraph comments before lines of a C file (ws079-p009, the blank-after-brace fixes).

  python3 plan/ws079/tests/style-insert.py FILE EDITS

EDITS is a text file of blocks, one a line to fix:

  LINE|START OF THE LINE|COMMENT

LINE is the line number as style-check.py printed it (before any insertion), START OF THE LINE the start of that
line's text (without its indentation) as a check that the right line is changed, and COMMENT the text of the comment.
Each such line gets a blank line and the comment, at its own indentation, above it.  An empty COMMENT inserts only
the blank line (for a line that already starts with a comment).  The edits are applied from the bottom up, so the
numbers stay those of the original file.
"""

import sys


def main():
	path, edits_path = sys.argv[1], sys.argv[2]
	lines = open(path).read().split("\n")
	edits = []
	for raw in open(edits_path).read().split("\n"):
		if not raw.strip() or raw.startswith("#"):
			continue
		number, start, comment = raw.split("|", 2)
		edits.append((int(number), start, comment))
	for number, start, comment in sorted(edits, reverse=True):
		target = lines[number - 1]
		if not target.strip().startswith(start):
			sys.exit("%s:%d: expected %r, found %r" % (path, number, start, target.strip()))
		indent = target[:len(target) - len(target.lstrip())]
		insertion = [""]
		if comment:
			insertion.append("%s/* %s */" % (indent, comment))
		lines[number - 1:number - 1] = insertion
	open(path, "w").write("\n".join(lines))
	print("%s: %d paragraphs" % (path, len(edits)))


if __name__ == "__main__":
	main()
