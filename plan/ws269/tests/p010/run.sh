#!/bin/sh
# Host check of ws269-p010a and p010c: the stage lines and hw.gpu.powervr.stages
# text (stage.c), gpu-selftest's run of its stages against stand-in programs,
# the image formats, layouts and state words (image-state.c), (p010d) the
# render targets' sizes, records and free list frames (render-target-layout.c),
# (p010n) the kick commands, render context state and reset notices
# (render-kick-layout.c), and (p010h) the descriptor set bytes and data
# sequencer programs (descriptor-set.c, data-program.c), the render context's
# switch programs and task words (render-sr-program.c), the render commands'
# registers (render-command.c), the placing of a job's draws (render-job.c
# over stand-ins of the node's memory and kick), and (p010e) the fixed
# render programs and words (render-program.c), and (p010f) the transfer
# command words, copy and blit programs (transfer-program.c), and the
# attachment load background (render-load-program.c), and (p010g) the render
# passes and render jobs (render-pass.c), and (p010i) the graphics pipelines
# and draws (render-draw.c).
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
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/render-command.c" "$root/src/drivers/gpu/powervr/render-kick-layout.c" \
	"$here/command-host-test.c" -o "$out/command-host-test"
"$out/command-host-test"
p="$root/src/drivers/gpu/powervr"
cc -std=gnu11 -O1 -Wall -Wextra -Werror -Wno-maybe-uninitialized -I"$root/include" -I"$root/src" \
	"$p/render-job.c" "$p/render-command.c" "$p/render-kick-layout.c" "$p/render-program.c" "$p/render-load-program.c" \
	"$p/render-draw.c" "$p/render-pass.c" "$p/data-program.c" "$p/descriptor-set.c" "$p/pds-encode.c" \
	"$p/image-state.c" $compiler "$here/job-host-test.c" -lm -o "$out/job-host-test"
"$out/job-host-test"
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
cc -std=gnu11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/render-load-program.c" "$root/src/drivers/gpu/powervr/data-program.c" \
	"$root/src/drivers/gpu/powervr/pds-encode.c" "$root/src/drivers/gpu/powervr/usc-encode.c" \
	"$root/src/drivers/gpu/powervr/descriptor-set.c" "$root/src/drivers/gpu/powervr/image-state.c" \
	"$root/src/drivers/gpu/powervr/shader-spirv.c" "$root/src/drivers/gpu/powervr/shader-spirv-interface.c" \
	"$root/src/drivers/gpu/powervr/shader-spirv-control.c" "$root/src/drivers/gpu/powervr/shader-spirv-instruction.c" \
	"$root/src/drivers/gpu/powervr/shader-backend.c" "$here/render-load-host-test.c" -o "$out/render-load-host-test"
"$out/render-load-host-test"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/render-pass.c" "$root/src/drivers/gpu/powervr/image-state.c" \
	"$here/pass-host-test.c" -o "$out/pass-host-test"
"$out/pass-host-test"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" -I"$root" \
	"$root/src/drivers/gpu/powervr/render-draw.c" "$root/src/drivers/gpu/powervr/pds-encode.c" \
	"$root/src/drivers/gpu/powervr/image-state.c" "$root/src/drivers/gpu/powervr/usc-encode.c" \
	"$root/src/drivers/gpu/powervr/shader-spirv.c" "$root/src/drivers/gpu/powervr/shader-spirv-interface.c" \
	"$root/src/drivers/gpu/powervr/shader-spirv-control.c" "$root/src/drivers/gpu/powervr/shader-spirv-instruction.c" \
	"$root/src/drivers/gpu/powervr/shader-backend.c" "$here/draw-host-test.c" -lm -o "$out/draw-host-test"
"$out/draw-host-test"
