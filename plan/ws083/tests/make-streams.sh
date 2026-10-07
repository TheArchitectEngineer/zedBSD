#!/bin/sh
# ws083-p004: makes the small H.264 test streams of plan/ws083/tests/streams/ and their reference
# frame hashes (design.md §8.4, HD4 (a), H5).  Every stream is encoded here from ffmpeg's synthetic
# test sources (testsrc, mandelbrot) by the host's libx264; no outside video goes in.  The bitstreams
# are this project's own output (Zlib, as the tree).
#
#   sh plan/ws083/tests/make-streams.sh [directory]
#
# The directory defaults to plan/ws083/tests/streams.  The script writes new files there and does
# not remove any.  Versions used for the committed set (2026-10-08): ffmpeg 7.1.5-0+deb13u1
# (Debian 13), libx264 0.164.3108+git31e19f9 (Debian libx264-164 2:0.164.3108+git31e19f9-2+b1).
# Another encoder version may make other bytes; the committed streams and hashes are the reference.
#
# Each stream comes with <name>.sha256: one line a frame in display order, the SHA-256 of the frame
# as ffmpeg's own decoder outputs it in NV12 (the cropped Y plane, then the interleaved UV plane),
# which is what vkvideo-probe prints for the frames it decodes.
#
# p004 makes the intra streams (every picture an IDR I picture):
#   i-baseline-64      64x64,   Baseline, CAVLC, one slice,   3 frames
#   i-main-352-slices  352x288, Main, CABAC, four slices,     3 frames
#   i-high-352-cqm     352x288, High, CABAC, 8x8 transform, a non-symmetric scaling matrix
#                      (i-high-352-cqm.cqm: the coefficients of a list all differ), 3 frames
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=${1:-$repo/plan/ws083/tests/streams}
mkdir -p "$out"

ffmpeg=${FFMPEG:-ffmpeg}
common="-hide_banner -loglevel error -nostdin -y"

# A scaling matrix file in x264's (JM) format, kept with the stream it made: the values of a list
# all differ, and no two lists are the same, so a list read in the wrong order or into the wrong
# place decodes to a different picture.
cqm=$out/i-high-352-cqm.cqm
exec 3>"$cqm"
list=0
for name in INTRA4X4_LUMA INTRA4X4_CHROMAU INTRA4X4_CHROMAV INTER4X4_LUMA INTER4X4_CHROMAU INTER4X4_CHROMAV; do
	values=""
	index=0
	while [ $index -lt 16 ]; do
		values="$values$(( 8 + list * 3 + index * 2 )),"
		index=$((index + 1))
	done
	printf '%s =\n%s\n' "$name" "${values%,}" >&3
	list=$((list + 1))
done
for name in INTRA8X8_LUMA INTER8X8_LUMA; do
	values=""
	index=0
	while [ $index -lt 64 ]; do
		values="$values$(( 6 + list + index )),"
		index=$((index + 1))
	done
	printf '%s =\n%s\n' "$name" "${values%,}" >&3
	list=$((list + 1))
done
exec 3>&-

# Encodes one stream as an Annex B elementary stream, frame order kept.
encode() {
	name=$1
	shift
	"$ffmpeg" $common "$@" -fps_mode passthrough -an -bsf:v h264_mp4toannexb -f h264 "$out/$name.h264"
}

# Writes the reference hashes of one stream: ffmpeg's decode, NV12, one SHA-256 a frame.
reference() {
	name=$1
	"$ffmpeg" $common -i "$out/$name.h264" -pix_fmt nv12 -f framehash -hash sha256 - |
		sed -n 's/^[0-9].*, \([0-9a-f]\{64\}\)$/\1/p' >"$out/$name.sha256"
}

encode i-baseline-64 -f lavfi -i "testsrc=size=64x64:rate=25:duration=0.12" \
	-c:v libx264 -profile:v baseline -pix_fmt yuv420p -g 1 -crf 26 -x264-params "keyint=1:slices=1"
reference i-baseline-64

encode i-main-352-slices -f lavfi -i "testsrc=size=352x288:rate=25:duration=0.12" \
	-c:v libx264 -profile:v main -pix_fmt yuv420p -g 1 -crf 30 -x264-params "keyint=1:slices=4"
reference i-main-352-slices

encode i-high-352-cqm -f lavfi -i "mandelbrot=size=352x288:rate=25" -frames:v 3 \
	-c:v libx264 -profile:v high -pix_fmt yuv420p -g 1 -crf 30 -x264-params "keyint=1:8x8dct=1:cqmfile=$cqm"
reference i-high-352-cqm

echo "ws083 streams written to $out"
ls -l "$out"
