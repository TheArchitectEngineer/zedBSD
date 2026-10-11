#!/bin/sh
# ws205-p002: libmedia's SBC against FFmpeg's (the independent side, a host tool only, never in the product).
# Builds plan/ws205/tests/sbc-tool.c with userland/desktop/libmedia/sbc.c (normally and with ASan/UBSan) in a new
# folder of the build (old folders are Q1's to remove), then for each setting: our encoder decoded by FFmpeg against
# the input (the least SNR given), our decoder against FFmpeg's on the same frames (77 dB expected, 50 at least),
# FFmpeg's encoder decoded by ours, and mSBC both ways.  The last line is "sbc: all PASS".
# usage: sh plan/ws205/tests/run-sbc-host-test.sh   (needs ffmpeg with its sbc encoder and decoder)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws205-sbc.XXXXXX")
flags="-std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -I$repo"
# shellcheck disable=SC2086
cc $flags -O2 "$repo/plan/ws205/tests/sbc-tool.c" "$repo/userland/desktop/libmedia/sbc.c" -lm -o "$work/sbc-tool"
# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer "$repo/plan/ws205/tests/sbc-tool.c" \
    "$repo/userland/desktop/libmedia/sbc.c" -lm -o "$work/sbc-sanitized"
tool="$work/sbc-tool"
ff() { ffmpeg -hide_banner -loglevel error -y "$@"; }
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$work/sbc-sanitized" self
# rate channels mode subbands blocks bitpool least-SNR-of-FFmpeg's-decoding
while read -r rate channels mode subbands blocks bitpool least; do
	echo "== $rate $channels $mode $subbands/$blocks bitpool $bitpool"
	"$tool" signal "$rate" "$channels" 2 "$work/in.raw"
	"$tool" encode "$rate" "$channels" "$mode" "$subbands" "$blocks" "$bitpool" "$work/in.raw" "$work/ours.sbc"
	ff -f sbc -i "$work/ours.sbc" -f s16le "$work/ours-ff.raw"
	"$tool" compare "$work/in.raw" "$work/ours-ff.raw" "$channels" "$least"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$work/sbc-sanitized" decode "$work/ours.sbc" "$work/ours-ours.raw"
	"$tool" compare "$work/ours-ff.raw" "$work/ours-ours.raw" "$channels" 50
done <<LIST
16000 1 mono 4 8 16 45
32000 1 mono 8 16 31 65
44100 2 dual 8 16 32 60
48000 2 stereo 4 4 20 50
44100 2 joint-snr 8 12 35 60
48000 2 joint 8 16 53 60
44100 2 joint 8 16 250 60
48000 2 joint 8 16 2 0
LIST
echo "== FFmpeg's encoder, our decoder"
"$tool" signal 48000 2 4 "$work/in.raw"
ff -f s16le -ar 48000 -ac 2 -i "$work/in.raw" -c:a sbc -b:a 328k -f sbc "$work/ff.sbc"
"$tool" decode "$work/ff.sbc" "$work/ff-ours.raw"
ff -f sbc -i "$work/ff.sbc" -f s16le "$work/ff-ff.raw"
"$tool" compare "$work/ff-ff.raw" "$work/ff-ours.raw" 2 50
echo "== mSBC both ways"
"$tool" signal 16000 1 4 "$work/in16.raw"
"$tool" encode-msbc "$work/in16.raw" "$work/ours.msbc"
ff -f sbc -i "$work/ours.msbc" -f s16le "$work/ours-msbc-ff.raw"
"$tool" compare "$work/in16.raw" "$work/ours-msbc-ff.raw" 1 45
ff -f s16le -ar 16000 -ac 1 -i "$work/in16.raw" -c:a sbc -msbc 1 -f sbc "$work/ff.msbc"
"$tool" decode "$work/ff.msbc" "$work/ff-msbc-ours.raw"
ff -f sbc -i "$work/ff.msbc" -f s16le "$work/ff-msbc-ff.raw"
"$tool" compare "$work/ff-msbc-ff.raw" "$work/ff-msbc-ours.raw" 1 50
echo "sbc: all PASS"
