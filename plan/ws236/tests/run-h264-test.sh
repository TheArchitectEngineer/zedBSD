#!/bin/sh
# ws236-p003: access units and the no-reorder SPS rewrite (rpi4-h264.c) on the host.  The ws083 streams are split into
# access units (one per frame ffprobe counts); every SPS is rewritten, ffmpeg's trace_headers must read the new VUI
# (max_num_reorder_frames 0, max_dec_frame_buffering between max_num_ref_frames and 16: the level's MaxDpbFrames), every SPS field before the VUI must read
# the same, and a stream without reordering must decode to the same frames.  New folder of build/tmp per run (Q1 removes old ones).  Last line "h264: all PASS".
# usage: sh plan/ws236/tests/run-h264-test.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws236-h264.XXXXXX")
flags="-std=c11 -Wall -Wextra -Werror -I$repo/src -I$repo/include"
cc $flags -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
	"$repo/plan/ws236/tests/h264-tool.c" "$repo/src/drivers/platform/rpi4/rpi4-h264.c" -o "$work/h264-tool"
export ASAN_OPTIONS=detect_leaks=0
fail=0
for name in pb-high-352-pyramid i-high-352-cqm p-baseline-64; do
	in="$repo/plan/ws236/tests/$name.h264"
	units=$(UBSAN_OPTIONS=halt_on_error=1 "$work/h264-tool" units "$in" | tail -1 | awk '{print $2}')
	frames=$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 "$in")
	if [ "$units" != "$frames" ]; then echo "FAIL $name: $units units, $frames frames"; fail=1; fi
	UBSAN_OPTIONS=halt_on_error=1 "$work/h264-tool" rewrite "$in" "$work/$name.h264"
	ffmpeg -hide_banner -i "$work/$name.h264" -c copy -bsf:v trace_headers -f null - 2>"$work/$name.trace" || true
	ffmpeg -hide_banner -i "$in" -c copy -bsf:v trace_headers -f null - 2>"$work/$name.orig" || true
	sed -n "/Sequence Parameter Set/,/vui_parameters_present_flag/p" "$work/$name.orig" | head -200 | sed "s/^\[[^]]*\] //" | grep -v vui_parameters_present >"$work/$name.p1"
	sed -n "/Sequence Parameter Set/,/vui_parameters_present_flag/p" "$work/$name.trace" | head -200 | sed "s/^\[[^]]*\] //" | grep -v vui_parameters_present >"$work/$name.p2"
	if ! cmp -s "$work/$name.p1" "$work/$name.p2"; then echo "FAIL $name: SPS fields before the VUI changed"; fail=1; fi
	refs=$(grep -m1 'max_num_ref_frames' "$work/$name.trace" | awk '{print $NF}')
	reorder=$(grep -m1 'max_num_reorder_frames' "$work/$name.trace" | awk '{print $NF}')
	buffering=$(grep -m1 'max_dec_frame_buffering' "$work/$name.trace" | awk '{print $NF}')
	[ "$refs" = 0 ] && refs=1
	if [ "$reorder" != 0 ] || [ "$buffering" -lt "$refs" ] || [ "$buffering" -gt 16 ]; then echo "FAIL $name: reorder=$reorder buffering=$buffering refs=$refs"; fail=1; fi
	# A stream without reordering decodes to the same frames; ffmpeg drops the late frames of a reordered one.
	if [ "$name" != pb-high-352-pyramid ]; then
		ffmpeg -v error -i "$in" -f framemd5 - | grep -v '^#' | awk -F, '{print $6}' >"$work/$name.a"
		ffmpeg -v error -i "$work/$name.h264" -f framemd5 - | grep -v '^#' | awk -F, '{print $6}' >"$work/$name.b"
		if ! cmp -s "$work/$name.a" "$work/$name.b"; then echo "FAIL $name: decoded frames differ"; fail=1; fi
	fi
	echo "$name: units=$units frames=$frames refs=$refs reorder=$reorder buffering=$buffering"
done
[ "$fail" = 0 ] && echo "h264: all PASS"
# ws236-p004: the SPS/PPS writers against libmedia's parser: every set written back from its StdVideo fields reads
# the same in ffmpeg's trace_headers (the SPS up to the VUI, the PPS whole) and the streams decode to the same frames.
mkdir -p "$work/vk"
ln -s "$repo/include/libc/vulkan" "$work/vk/vulkan"
san="-O1 -g -fsanitize=address,undefined"
# shellcheck disable=SC2086
cc -std=c11 -Wall -Wextra -Werror -I"$repo/src" -I"$repo/include" $san -c "$repo/src/drivers/platform/rpi4/rpi4-h264.c" -o "$work/rpi4-h264.o"
for part in h264 bits; do
	# shellcheck disable=SC2086
	cc -std=c11 -Wall -Wextra -I"$work/vk" $san -c "$repo/userland/desktop/libmedia/$part.c" -o "$work/libmedia-$part.o"
done
# shellcheck disable=SC2086
cc -std=c11 -Wall -Wextra -I"$repo/src" -I"$work/vk" -idirafter "$repo/include" -I"$repo/userland/desktop/libmedia" $san \
	"$repo/plan/ws236/tests/h264-write-test.c" "$work/rpi4-h264.o" "$work/libmedia-h264.o" "$work/libmedia-bits.o" -o "$work/h264-write"
fail=0
for name in pb-high-352-pyramid i-high-352-cqm p-baseline-64; do
	in="$repo/plan/ws236/tests/$name.h264"
	UBSAN_OPTIONS=halt_on_error=1 "$work/h264-write" rewrite "$in" "$work/$name.w.h264" 0
	for kind in sps pps; do
		if [ $kind = sps ]; then range='/Sequence Parameter Set/,/vui_parameters_present_flag/'; else range='/Picture Parameter Set/,/rbsp_stop_one_bit/'; fi
		ffmpeg -hide_banner -i "$in" -c copy -bsf:v trace_headers -f null - 2>&1 | sed -n "${range}p" | sed 's/^\[[^]]*\] //' | grep -v 'vui_parameters_present\|rbsp_stop\|rbsp_alignment' | sed 's/^[0-9]* *//' >"$work/$name.$kind.1"
		ffmpeg -hide_banner -i "$work/$name.w.h264" -c copy -bsf:v trace_headers -f null - 2>&1 | sed -n "${range}p" | sed 's/^\[[^]]*\] //' | grep -v 'vui_parameters_present\|rbsp_stop\|rbsp_alignment' | sed 's/^[0-9]* *//' >"$work/$name.$kind.2"
		if ! cmp -s "$work/$name.$kind.1" "$work/$name.$kind.2"; then echo "FAIL $name: written $kind differs"; diff "$work/$name.$kind.1" "$work/$name.$kind.2" | head -5; fail=1; fi
	done
	ffmpeg -v error -i "$in" -f framemd5 - | grep -v '^#' | awk -F, '{print $6}' | sort >"$work/$name.w.a"
	ffmpeg -v error -i "$work/$name.w.h264" -f framemd5 - | grep -v '^#' | awk -F, '{print $6}' | sort >"$work/$name.w.b"
	if ! cmp -s "$work/$name.w.a" "$work/$name.w.b"; then echo "FAIL $name: written sets decode differently"; fail=1; fi
	echo "$name: sets written back $(wc -l <"$work/$name.sps.2") SPS lines $(wc -l <"$work/$name.pps.2") PPS lines"
done
[ "$fail" = 0 ] && echo "h264-write: all PASS"
