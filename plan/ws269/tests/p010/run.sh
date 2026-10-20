#!/bin/sh
# Host check of ws269-p010a and p010c: the stage lines and hw.gpu.powervr.stages
# text (stage.c), gpu-selftest's run of its stages against stand-in programs,
# the image formats, layouts and state words (image-state.c), and (p010d) the
# render targets' sizes, records and free list frames (render-target-layout.c),
# and (p010n) the kick commands, render context state and reset notices
# (render-kick-layout.c).
#   plan/ws269/tests/p010/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws269/tests/p010
out=$1
mkdir -p "$out"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/stage.c" "$here/stage-host-test.c" -o "$out/stage-host-test"
"$out/stage-host-test"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/image-state.c" "$here/image-host-test.c" -o "$out/image-host-test"
"$out/image-host-test"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/render-target-layout.c" "$here/target-host-test.c" -o "$out/target-host-test"
"$out/target-host-test"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/render-kick-layout.c" "$root/src/drivers/gpu/powervr/stage.c" \
	"$here/kick-host-test.c" -o "$out/kick-host-test"
"$out/kick-host-test"
sh "$here/selftest-host.sh" "$out"
