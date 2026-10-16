#!/bin/sh
# The source audit of the amdrdna shader compiler (src/drivers/gpu/amdrdna/compiler/): no identifier of the Mesa files
# it cites in its code, every hexadecimal block's source named before it, no source named in a file's head
# (audit-compiler.py, with audit-allow.txt and audit-allow-compiler.txt).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws039/tests/audit-compiler.sh [MESA-TREE]
#   MESA-TREE is an unpacked Mesa 25.0.7 (default build/mesa/mesa-25.0.7, from the tarball of
#   plan/ws039/phase007/phase.md).
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
tests="$repo/plan/ws039/tests"
mesa=${1:-$repo/build/mesa/mesa-25.0.7}
python3 -I "$tests/audit-compiler.py" "$mesa" "$repo/src/drivers/gpu/amdrdna/compiler" "$tests/audit-allow.txt" \
	"$tests/audit-allow-compiler.txt"
