#!/bin/sh
# Host check of ws269-p010a and p010c: the stage lines and hw.gpu.powervr.stages
# text (stage.c), gpu-selftest's run of its stages against stand-in programs,
# the image formats, layouts and state words (image-state.c), and the
# descriptor set bytes and data sequencer programs (descriptor-set.c,
# data-program.c, p010h).
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
sh "$here/selftest-host.sh" "$out"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/descriptor-set.c" "$root/src/drivers/gpu/powervr/data-program.c" \
	"$root/src/drivers/gpu/powervr/pds-encode.c" "$here/data-host-test.c" -o "$out/data-host-test"
"$out/data-host-test"
