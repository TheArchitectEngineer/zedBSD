#!/bin/sh
# ws236: makes the Video Player's test films: 10 s of ffmpeg's synthetic testsrc2 at 1920x1080 30 fps, H.264 High with
# two B frames, and Baseline (no reordering), in mp4 (no audio).  Kept in plan/ws236/tests/ for the test image.
# usage: sh plan/ws236/tests/make-test-mp4.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ffmpeg -y -v error -f lavfi -i testsrc2=size=1920x1080:rate=30 -t 10 -c:v libx264 -profile:v high -level 4.1 -bf 2 \
	-g 60 -crf 42 -pix_fmt yuv420p -movflags +faststart "$here/high-1080p30-10s.mp4"
ffmpeg -y -v error -f lavfi -i testsrc2=size=1920x1080:rate=30 -t 10 -c:v libx264 -profile:v baseline -level 4.1 \
	-g 60 -crf 42 -pix_fmt yuv420p -movflags +faststart "$here/baseline-1080p30-10s.mp4"
