#!/bin/sh
# The source audit of the amdrdna Vulkan executor (src/drivers/gpu/amdrdna/vk/): the rules of audit-symbols.py, with
# audit-allow.txt and audit-allow-vk.txt together.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws039/tests/audit-vk.sh [LINUX-TREE]
#   LINUX-TREE defaults to plan/ws039/temp/linux; the Mesa tree plan/ws039/temp/mesa is used when it exists.
# The joined allow list goes in a new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
tests="$repo/plan/ws039/tests"
linux=${1:-$repo/plan/ws039/temp/linux}
mesa=$repo/plan/ws039/temp/mesa
if [ ! -d "$mesa" ]; then
	mesa=-
fi
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/audit-vk.XXXXXX")
cat "$tests/audit-allow.txt" "$tests/audit-allow-vk.txt" > "$out/allow.txt"
python3 -I "$tests/audit-symbols.py" "$linux" "$mesa" "$repo/src/drivers/gpu/amdrdna/vk" "$out/allow.txt"
