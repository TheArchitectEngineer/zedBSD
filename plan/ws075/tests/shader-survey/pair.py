#!/usr/bin/env python3
# ws075-p001: links the shaders extract.py took out of one C source into programs with zedBSD's GLSL compiler (the host
# driver glsl-test of plan/ws068/tests/glsl-host), the way libGLESv2 links them: each fragment shader with the first
# vertex shader of the same source it links with, and each geometry shader between the first vertex and fragment
# shaders it links with.  Writes OUTDIR/<fragment or geometry name>.{vert,geom,frag}.spv and prints one line per shader
# that found no partner.
#
#   pair.py GLSL_TEST OUTDIR SHADER...
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os
import shutil
import subprocess
import sys


def link(tool, vertex, fragment, prefix, geometry=None):
	"""Links the stages (a geometry shader goes next to a copy of the vertex shader of its name); True when it links."""
	work = prefix + '.work'
	os.makedirs(work, exist_ok=True)
	shutil.copy(vertex, os.path.join(work, 'program.vert'))
	if geometry is not None:
		shutil.copy(geometry, os.path.join(work, 'program.geom'))
	result = subprocess.run([tool, 'link', os.path.join(work, 'program.vert'), fragment, prefix, '100'], capture_output=True,
	                        text=True)
	shutil.rmtree(work)
	return result.returncode == 0 and os.path.exists(prefix + '.frag.spv')


def main():
	tool = sys.argv[1]
	out = sys.argv[2]
	shaders = sys.argv[3:]
	os.makedirs(out, exist_ok=True)

	# The shaders of each source, by stage.
	sources = {}
	for path in shaders:
		name = os.path.basename(path)
		source = name.split('-')[0]
		stage = name.rsplit('.', 1)[1]
		sources.setdefault(source, {'vert': [], 'frag': [], 'geom': []})[stage].append(path)

	# Each fragment shader with a vertex shader; each geometry shader between the two.
	for source, stages in sorted(sources.items()):
		for fragment in stages['frag']:
			prefix = os.path.join(out, os.path.splitext(os.path.basename(fragment))[0])
			if not any(link(tool, vertex, fragment, prefix) for vertex in stages['vert']):
				print('%s: no vertex shader of %s links with it' % (fragment, source))
		for geometry in stages['geom']:
			prefix = os.path.join(out, os.path.splitext(os.path.basename(geometry))[0])
			found = False
			for vertex in stages['vert']:
				for fragment in stages['frag']:
					found = link(tool, vertex, fragment, prefix, geometry)
					if found:
						break
				if found:
					break
			if not found:
				print('%s: no vertex and fragment shaders of %s link with it' % (geometry, source))


if __name__ == '__main__':
	main()
