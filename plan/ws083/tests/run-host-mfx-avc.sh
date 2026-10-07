#!/bin/sh
# ws083-p004: the host golden of the MFX AVC builder (design §8.1, D24).  host-mfx-avc.c builds decodes
# with render/video-mfx.c and writes each batch with the fields it expects; genxml-decode.py reads the
# batches back through Mesa's genxml (pinned by SHA-256, read as data) and compares.  Plain and
# ASan/UBSan, in a new directory under build/tmp (nothing is removed here; Q1's plan/tools/q1-clean.sh
# removes the runs build/tmp/ws083-mfx-avc does not point at).
#   sh plan/ws083/tests/run-host-mfx-avc.sh [MESA-TREE]
# The Mesa tree defaults to build/mesa-tools/mesa-25.0.7 (the Debian 13 source of Mesa 25.0.7; only
# src/intel/genxml is read).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mesa=${1:-$repo/build/mesa-tools/mesa-25.0.7}
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws083-mfx-avc"
work=$fresh_dir
compiler=${CC:-cc}
decoder="$repo/plan/ws083/tests/genxml-decode.py"
render=$repo/src/drivers/gpu/i915/render
sources="$repo/plan/ws083/tests/host-mfx-avc.c $render/video-mfx.c $render/video-h264-tables.c $render/batch.c"
base="-std=gnu89 -Wall -Wextra -Werror -Wdeclaration-after-statement -DKERN_USER_ABI_LP64 -I$repo/include -I$repo -idirafter $repo/include/libc"

# The decoder resolves the genxml imports as Gen12 needs them.
python3 -I "$decoder" --mesa "$mesa" --self-test

for kind in plain sanitized; do
	flags="-O2"
	if [ "$kind" = sanitized ]; then
		flags="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"
	fi
	mkdir "$work/$kind"
	"$compiler" $base $flags $sources -o "$work/$kind/golden"
	"$work/$kind/golden" "$work/$kind"
	for expect in "$work/$kind"/*.expect; do
		name=${expect%.expect}
		printf '%s: ' "$(basename "$name")"
		python3 -I "$decoder" --mesa "$mesa" --batch "$name.bin" --expect "$expect"
	done
done
echo "WS083 MFX AVC host golden PASS (plain and ASan/UBSan)"
