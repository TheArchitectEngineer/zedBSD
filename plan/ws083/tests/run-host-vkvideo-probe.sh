#!/bin/sh
# ws083-p004: the host test of vkvideo-probe's stream reader and frame hash (host-vkvideo-probe.c) over
# the test streams of plan/ws083/tests/streams/: ffmpeg decodes each stream to raw NV12, the test lays
# the frames into Y tiles and hashes them through the probe's de-tiling against the reference hashes.
# ws083-p006a: the DPB (host-vkvideo-dpb.c) over every stream, its output order against ffprobe's frames.
# Plain and ASan/UBSan, in a new directory under build/tmp (nothing is removed here; Q1's
# plan/tools/q1-clean.sh removes the runs build/tmp/ws083-vkvideo-probe does not point at).
#   sh plan/ws083/tests/run-host-vkvideo-probe.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws083-vkvideo-probe"
work=$fresh_dir
compiler=${CC:-cc}
streams=$repo/plan/ws083/tests/streams
probe=$repo/userland/tests/vkvideo-probe
sources="$repo/plan/ws083/tests/host-vkvideo-probe.c $probe/h264.c $probe/frame.c $repo/userland/base/common/sha256.c"
mkdir "$work/include"
ln -s "$repo/include/libc/vulkan" "$work/include/vulkan"
base="-std=gnu99 -Wall -Wextra -Werror -I$work/include"
"$compiler" $base -O2 $sources -o "$work/plain"
"$compiler" $base -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $sources -o "$work/sanitized"
for stream in i-baseline-64:1 i-main-352-slices:4 i-high-352-cqm:1; do
	name=${stream%%:*}
	slices=${stream##*:}
	ffmpeg -hide_banner -loglevel error -nostdin -y -i "$streams/$name.h264" -f rawvideo -pix_fmt nv12 "$work/$name.nv12"
	"$work/plain" "$streams/$name.h264" "$work/$name.nv12" "$streams/$name.sha256" "$slices"
	"$work/sanitized" "$streams/$name.h264" "$work/$name.nv12" "$streams/$name.sha256" "$slices"
done
# ws083-p006a: the DPB and the output order of the P and B streams (and the intra ones).
dpb_sources="$repo/plan/ws083/tests/host-vkvideo-dpb.c $probe/h264.c $probe/dpb.c"
"$compiler" $base -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $dpb_sources -o "$work/dpb"
for name in i-baseline-64 i-main-352-slices i-high-352-cqm p-baseline-64 pb-main-352 pb-high-352-pyramid; do
	ffprobe -v error -show_entries frame=pkt_pos -of csv=p=0 "$streams/$name.h264" | tr -d ',' >"$work/$name.positions"
	"$work/dpb" "$streams/$name.h264" "$work/$name.positions"
done
echo "WS083 vkvideo-probe host test PASS (stream reader, frame hash, DPB and output order; plain and ASan/UBSan)"
