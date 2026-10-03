#!/usr/bin/env python3
# ws101-p016: splits the time of Noct's GPU run of mix.nct (KEI_GLES_COMPUTE_TRACE=2: libEGL's and libGLESv2's
# "time step=" lines on stderr, and mix.nct's "MIX call=" lines) into its parts: the one-time start (EGL, the
# shader's compile and link) and, for each call, the steps of libGLESv2 and what is left outside it (Noct: the
# interpreter, its own copies).  The CPU run's log (without --gpu) gives its first and median call.
#   plan/ws101/tests/time-split.py GPU-LOG [CPU-LOG]
# Prints a table per call and the medians of the calls after the first (the times are the guest's clock's, in
# milliseconds; zedBSD's monotonic clock counts whole milliseconds).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import re
import statistics
import sys

STEP = re.compile(r'^(?:gles|egl): time step=(\S+) us=(\d+)')
CALL = re.compile(r'^MIX call=(\d+) ms=(\d+)')
ONCE = ('egl-initialize', 'egl-create-context', 'egl-create-pbuffer', 'egl-make-current', 'compile-shader', 'link-program')
# The parts of a call, in order: what each sums (steps inside others are not added twice).
PARTS = (
	('buffer-data', ('buffer-data',), 'copy into libGLESv2 (malloc, clear, copy of the input and output)'),
	('device-buffer', ('device-buffer', 'data-device-buffer'), 'device buffer made (vkCreateBuffer, vkAllocateMemory, map) or a spare taken (ws101-p017)'),
	('upload', ('upload', 'upload-in-place'), 'copy into the device buffer'),
	('record', (), 'dispatch recorded (dispatch-record minus device-buffer and upload)'),
	('gpu-wait', (), 'submit and wait for the GPU (frame-wait minus collect, which runs inside it)'),
	('readback', ('readback',), 'copy of the output from the device buffer'),
	('map-other', (), 'rest of glMapBufferRange'),
	('delete', ('delete-buffers',), 'glDeleteBuffers'),
	('collect', ('collect',), 'memory of the finished frame freed (the device buffers of the call before), inside frame-wait'),
)


def read(path):
	# The steps in order, and the calls' times.
	steps = []
	calls = {}
	for line in open(path, errors='replace'):
		match = STEP.match(line)
		if match:
			steps.append((match.group(1), int(match.group(2)) / 1000.0))
			continue
		match = CALL.match(line)
		if match:
			calls[int(match.group(1))] = int(match.group(2))
	return steps, calls


def split(steps):
	# The one-time steps; the others cut into calls: a call starts at the first buffer-data after a delete-buffers.
	once = {}
	segments = [[]]
	deleted = False
	for name, ms in steps:
		if name in ONCE:
			once[name] = once.get(name, 0.0) + ms
			continue
		if name == 'buffer-data' and deleted:
			segments.append([])
			deleted = False
		if name == 'delete-buffers':
			deleted = True
		segments[-1].append((name, ms))
	return once, [segment for segment in segments if any(name == 'dispatch-record' for name, _ in segment)]


def parts(segment):
	# The sums of a call's steps into its parts.
	total = {}
	waited = False
	for name, ms in segment:
		# A collect after the call's wait is the end of the program's (eglTerminate), not the call's.
		if name == 'collect' and waited:
			name = 'teardown'
		if name == 'frame-wait':
			waited = True
		total[name] = total.get(name, 0.0) + ms
	result = {}
	for part, names, _ in PARTS:
		result[part] = sum(total.get(name, 0.0) for name in names)
	result['buffer-data'] = total.get('buffer-data', 0.0) - total.get('data-device-buffer', 0.0)
	result['record'] = total.get('dispatch-record', 0.0) - total.get('device-buffer', 0.0) - result['upload']
	result['gpu-wait'] = total.get('frame-wait', 0.0) - result['collect']
	result['map-other'] = total.get('map-buffer', 0.0) - total.get('frame-wait', 0.0) - result['readback']
	result['libgles'] = sum(result[part] for part, _, _ in PARTS)
	return result


def main():
	if len(sys.argv) < 2:
		print('usage: time-split.py GPU-LOG [CPU-LOG]')
		return 2
	steps, calls = read(sys.argv[1])
	once, segments = split(steps)
	print('one-time: ' + ' '.join('%s=%.0f' % (name, once.get(name, 0.0)) for name in ONCE))
	rows = []
	for index, segment in enumerate(segments):
		row = parts(segment)
		row['call'] = calls.get(index, -1)
		row['outside'] = row['call'] - row['libgles'] if row['call'] >= 0 else -1
		rows.append(row)
		print('call=%d total=%d libgles=%.0f outside=%.0f ' % (index, row['call'], row['libgles'], row['outside']) +
		      ' '.join('%s=%.0f' % (part, row[part]) for part, _, _ in PARTS))
	later = rows[1:] or rows
	if later:
		print('MEDIAN (calls after the first, ms): total=%.0f libgles=%.0f outside=%.0f ' % (
			statistics.median(r['call'] for r in later), statistics.median(r['libgles'] for r in later),
			statistics.median(r['outside'] for r in later)) +
			' '.join('%s=%.0f' % (part, statistics.median(r[part] for r in later)) for part, _, _ in PARTS))
		for part, _, meaning in PARTS:
			print('  %-13s %s' % (part, meaning))
	if len(sys.argv) > 2:
		_, cpu = read(sys.argv[2])
		values = [cpu[k] for k in sorted(cpu)]
		if values:
			print('CPU: first=%d median_after_first=%.0f calls=%d' % (values[0], statistics.median(values[1:] or values), len(values)))
	return 0


sys.exit(main())
