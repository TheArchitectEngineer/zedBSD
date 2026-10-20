#!/bin/sh
# Host check of ws269-p010a and p010c: the stage lines and hw.gpu.powervr.stages
# text (stage.c), gpu-selftest's run of its stages against stand-in programs,
# the image formats, layouts and state words (image-state.c), (p010d) the
# render targets' sizes, records and free list frames (render-target-layout.c),
# (p010n) the kick commands, render context state and reset notices
# (render-kick-layout.c), and (p010h) the descriptor set bytes and data
# sequencer programs (descriptor-set.c, data-program.c), the render context's
# switch programs and task words (render-sr-program.c), and (p010e) the fixed
# render programs and words (render-program.c), and (p010f) the transfer
# command words and copy program (transfer-program.c).
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
compiler="$root/src/drivers/gpu/powervr/shader-spirv.c $root/src/drivers/gpu/powervr/shader-spirv-interface.c $root/src/drivers/gpu/powervr/shader-spirv-control.c $root/src/drivers/gpu/powervr/shader-spirv-instruction.c $root/src/drivers/gpu/powervr/shader-backend.c $root/src/drivers/gpu/powervr/usc-encode.c"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	$compiler "$root/src/drivers/gpu/powervr/pds-encode.c" "$root/src/drivers/gpu/powervr/render-sr-program.c" \
	"$here/program-host-test.c" -o "$out/program-host-test"
"$out/program-host-test"
sh "$here/selftest-host.sh" "$out"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/descriptor-set.c" "$root/src/drivers/gpu/powervr/data-program.c" \
	"$root/src/drivers/gpu/powervr/pds-encode.c" "$here/data-host-test.c" -o "$out/data-host-test"
"$out/data-host-test"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/render-program.c" "$root/src/drivers/gpu/powervr/data-program.c" \
	"$root/src/drivers/gpu/powervr/pds-encode.c" "$root/src/drivers/gpu/powervr/usc-encode.c" \
	"$root/src/drivers/gpu/powervr/descriptor-set.c" "$root/src/drivers/gpu/powervr/image-state.c" \
	"$root/src/drivers/gpu/powervr/shader-spirv.c" "$root/src/drivers/gpu/powervr/shader-spirv-interface.c" \
	"$root/src/drivers/gpu/powervr/shader-spirv-control.c" "$root/src/drivers/gpu/powervr/shader-spirv-instruction.c" \
	"$root/src/drivers/gpu/powervr/shader-backend.c" "$here/render-host-test.c" -o "$out/render-host-test"
"$out/render-host-test"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/transfer-program.c" "$root/src/drivers/gpu/powervr/render-program.c" \
	"$root/src/drivers/gpu/powervr/data-program.c" "$root/src/drivers/gpu/powervr/pds-encode.c" \
	"$root/src/drivers/gpu/powervr/usc-encode.c" "$root/src/drivers/gpu/powervr/descriptor-set.c" \
	"$root/src/drivers/gpu/powervr/image-state.c" "$root/src/drivers/gpu/powervr/shader-spirv.c" \
	"$root/src/drivers/gpu/powervr/shader-spirv-interface.c" "$root/src/drivers/gpu/powervr/shader-spirv-control.c" \
	"$root/src/drivers/gpu/powervr/shader-spirv-instruction.c" "$root/src/drivers/gpu/powervr/shader-backend.c" \
	"$here/transfer-host-test.c" -o "$out/transfer-host-test"
"$out/transfer-host-test"
