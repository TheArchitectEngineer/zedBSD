#!/bin/sh
# ws205-p003: libmedia's AAC-LC encoder against two decoders: FFmpeg's (the independent side, a host tool only, never in
# the product) and libmedia's own (plan/ws202/tests/host-aac-native.c).  Builds the tool normally and with ASan/UBSan in a
# new folder of the build (old folders are Q1's to remove); for each case: the bit rate reached (within 3 %), the SNR
# of FFmpeg's decoding against the input (the case's least), libmedia's decoding against FFmpeg's (90 dB), silence
# stays silent.  1.5 s a case (the native harness reads 64 KiB).  The last line is "aac: all PASS".
# usage: sh plan/ws205/tests/run-aac-host-test.sh   (needs ffmpeg and python3 with numpy)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws205-aac.XXXXXX")
encoder="$repo/userland/desktop/libmedia/aac-encode.c $repo/userland/desktop/libmedia/aac-bands.c $repo/userland/desktop/libmedia/aac-huffman.c"
decoder="$repo/userland/desktop/libmedia/bits.c $repo/userland/desktop/libmedia/aac-input.c $repo/userland/desktop/libmedia/aac-codec.c \
	$repo/userland/desktop/libmedia/aac-huffman.c $repo/userland/desktop/libmedia/aac-bands.c $repo/userland/desktop/libmedia/aac-frame.c \
	$repo/userland/desktop/libmedia/aac-synth.c $repo/userland/desktop/libmedia/aac.c $repo/userland/desktop/libmedia/sound.c"
# shellcheck disable=SC2086
cc -std=c11 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -Werror -I"$repo" "$repo/plan/ws205/tests/aac-tool.c" $encoder -lm -o "$work/aac-tool"
# shellcheck disable=SC2086
cc -std=c11 -D_DEFAULT_SOURCE -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I"$repo" \
	"$repo/plan/ws205/tests/aac-tool.c" $encoder -lm -o "$work/aac-sanitized"
# shellcheck disable=SC2086
cc -std=c89 -Wall -Wextra -Werror -Wno-long-long -pedantic -O1 -g -pthread -I"$repo" "$repo/plan/ws202/tests/host-aac-native.c" \
	$decoder -lm -o "$work/native"
signal="python3 -I $repo/plan/ws205/tests/aac-signal.py"
ff() { ffmpeg -hide_banner -loglevel error -y "$@"; }
# name rate channels bitrate least-SNR
while read -r name rate channels bitrate least; do
	echo "== $name $rate Hz $channels ch $bitrate"
	$signal make "$name" "$rate" "$channels" 1.5 "$work/in.raw"
	line=$(ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$work/aac-sanitized" encode "$rate" "$channels" "$bitrate" "$work/in.raw" "$work/out.aac")
	echo "$line"
	kbps=$(echo "$line" | sed 's/.*kbps=//')
	python3 -I -c "import sys; r=float(sys.argv[1])*1000/float(sys.argv[2]); sys.exit(0 if 0.97 <= r <= 1.03 or sys.argv[3]=='silence' else 1)" "$kbps" "$bitrate" "$name" \
		|| { echo "FAIL bit rate $kbps of $bitrate"; exit 1; }
	ff -i "$work/out.aac" -f s16le -ac "$channels" "$work/ff.raw"
	ff -i "$work/out.aac" -f f32le -ac "$channels" "$work/ff.f32"
	"$work/native" "$work/out.aac" "$work/native.f32"
	if [ "$name" = silence ]; then
		$signal silent "$work/ff.raw"
	else
		$signal snr "$work/in.raw" s16 "$work/ff.raw" s16 "$channels" 1024 "$least"
	fi
	$signal snr "$work/ff.f32" f32 "$work/native.f32" f32 "$channels" 0 90
done <<LIST
chord 48000 2 256000 70
chord 48000 2 128000 65
chord 44100 2 192000 70
chord 32000 1 64000 60
music 48000 2 256000 20
music 44100 1 96000 16
square 32000 1 64000 20
silence 48000 2 128000 0
LIST
echo "aac: all PASS"
