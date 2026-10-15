#!/bin/sh
# The source audit of the amdrdna driver: no identifier of the cited Linux and Mesa headers in the code, and a
# "Value(s) referenced:" comment above every hexadecimal block (audit-symbols.py says the rules).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws039/tests/audit-symbols.sh [LINUX-TREE [OUTPUT-DIRECTORY]]
#   LINUX-TREE defaults to plan/ws039/temp/linux; the Mesa tree plan/ws039/temp/mesa is used when it exists.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
linux=${1:-$repo/plan/ws039/temp/linux}
mesa=$repo/plan/ws039/temp/mesa
if [ ! -d "$mesa" ]; then
	mesa=-
fi
python3 -I "$repo/plan/ws039/tests/audit-symbols.py" "$linux" "$mesa" "$repo/src/drivers/gpu/amdrdna" \
	"$repo/plan/ws039/tests/audit-allow.txt"
