#!/bin/sh
# Host test of the amdrdna driver's table readers and register tables: the block table, the boot database, the
# video BIOS readers, the VRAM layout, the firmware picture's shape (whole, placed, scaled), the device IDs
# (synthetic cases, and a hardware dump when one is given), the display register addresses against the Linux
# DCN 2.0 header, the memory and interrupt words of stage A2 (pools, table entries, hub words against the board's
# Linux values, interrupt entries) and their register addresses against the Linux headers, the firmware headers, the
# security processor's frames and commands and the thermal words of stages A3t to A4 (the real navi10 files of the
# amd-navi-firmware cache when it exists; AMDRDNA_FIRMWARE overrides it), the power tables of stage A4's full tier
# (against tables laid out by the Linux structures, the board's video BIOS and the real smc file), the packets, queue images, settings and
# shape words of stage A5 and its first dispatch's program on the GFX10 interpreter (their image positions, register
# offsets and settings against the Linux sources), the hub contexts, address-space identifiers and ring commands of
# stage A5s, the display's words of stages A6 to A8, the display paths of the board's video BIOS and the EDID decode of
# stage A10, and the source audit.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws039/tests/amdrdna-host-test.sh [DUMP-DIRECTORY]
#   DUMP-DIRECTORY holds the files plan/ws039/tests/hw-dump.py read on the board (default: plan/ws039/temp/dump/cold
#   when it exists).  The Linux tree for the comparison and the audit is plan/ws039/temp/linux (AMDRDNA_LINUX
#   overrides it); without it those two parts are skipped and say so.
# Needs cc, glslc and python3.  Everything goes in a new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
tests="$repo/plan/ws039/tests"
dump=${1:-$repo/plan/ws039/temp/dump/cold}
linux=${AMDRDNA_LINUX:-$repo/plan/ws039/temp/linux}
firmware=${AMDRDNA_FIRMWARE:-$repo/build/sources/firmware/amd-navi/dc85ccedc9c973682fbcf4d628ca61174bcc3120}
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/amdrdna-host.XXXXXX")
flags="-std=gnu11 -O1 -g -DKERN_USER_ABI_LP64 -DKERN_UAPI_NATIVE -I$repo/include -I$repo/src -I$repo \
-fsanitize=address,undefined -fno-sanitize=alignment -Wall -Wextra -Werror -Wdeclaration-after-statement"
driver="$repo/src/drivers/gpu/amdrdna"
cc $flags -include uapi/errno.h -o "$out/amdrdna-tables-test" "$tests/amdrdna-tables-test.c" "$driver/bytes.c" \
	"$driver/block-table.c" "$driver/boot-database.c" "$driver/vbios-table.c" "$driver/layout.c" \
	"$driver/products.c" "$driver/dcn-registers.c" "$driver/picture.c"
cc $flags -include uapi/errno.h -o "$out/amdrdna-memory-test" "$tests/amdrdna-memory-test.c" "$driver/span-pool.c" \
	"$driver/hub-registers.c" "$driver/vm-format.c" "$driver/interrupt-format.c"
cc $flags -include uapi/errno.h -o "$out/amdrdna-sessions-test" "$tests/amdrdna-sessions-test.c" "$driver/context-pool.c" \
	"$driver/command-format.c"
cc $flags -include uapi/errno.h -o "$out/amdrdna-display-test" "$tests/amdrdna-display-test.c" "$driver/display-format.c"
cc $flags -include uapi/errno.h -o "$out/amdrdna-psp-test" "$tests/amdrdna-psp-test.c" "$driver/bytes.c" \
	"$driver/firmware-format.c" "$driver/psp-format.c" "$driver/thermal-format.c"
cc $flags -include uapi/errno.h -o "$out/amdrdna-sink-test" "$tests/amdrdna-sink-test.c" "$driver/bytes.c" \
	"$driver/vbios-table.c" "$driver/edid.c"
cc $flags -include uapi/errno.h -o "$out/amdrdna-power-test" "$tests/amdrdna-power-test.c" "$driver/bytes.c" \
	"$driver/vbios-table.c" "$driver/thermal-format.c"
compiler="$driver/compiler"
cc $flags -include uapi/errno.h -o "$out/amdrdna-engines-test" "$tests/amdrdna-engines-test.c" "$driver/command-format.c" \
	"$driver/queue-format.c" "$driver/settings.c" "$driver/check-program.c" "$tests/gfx10-machine.c" \
	"$tests/gfx10-texture.c" "$compiler/gfx10-program.c" "$compiler/gfx10-compile.c" "$compiler/gfx10-wait.c" \
	"$compiler/gfx10-encode.c" "$repo/src/drivers/gpu/compiler/spirv.c" "$repo/src/drivers/gpu/compiler/optimize.c" -lm
glslc --target-env=vulkan1.0 -fshader-stage=compute -o "$out/ids.spv" "$tests/shaders/ids.comp"
status=0
"$out/amdrdna-engines-test" "$out/ids.spv" || status=1
"$out/amdrdna-memory-test" || status=1
"$out/amdrdna-sessions-test" || status=1
"$out/amdrdna-display-test" || status=1
if [ -d "$firmware" ]; then
	"$out/amdrdna-psp-test" "$firmware" || status=1
else
	echo "SKIP firmware files: no $firmware"
	"$out/amdrdna-psp-test" || status=1
fi
power_args=
if [ -d "$firmware" ]; then
	power_args="--firmware $firmware"
fi
if [ -d "$dump" ]; then
	power_args="$power_args --dump $dump"
fi
if [ -d "$linux/drivers/gpu/drm/amd" ]; then
	amd="$linux/drivers/gpu/drm/amd"
	cc -std=gnu11 -O1 -I"$amd/include" -I"$amd/pm/swsmu/inc" -I"$amd/pm/swsmu/inc/pmfw_if" \
		-o "$out/amdrdna-power-linux" "$tests/amdrdna-power-linux.c"
	mkdir "$out/power-linux"
	"$out/amdrdna-power-linux" "$out/power-linux"
	power_args="$power_args --linux $out/power-linux"
fi
# shellcheck disable=SC2086
"$out/amdrdna-power-test" $power_args || status=1
"$out/amdrdna-sink-test" "$dump/rom.bin" || status=1
if [ -d "$dump" ]; then
	"$out/amdrdna-tables-test" --dump "$dump" || status=1
else
	echo "SKIP dump cases: no $dump"
	"$out/amdrdna-tables-test" || status=1
fi
if [ -d "$linux/drivers/gpu/drm/amd" ]; then
	"$out/amdrdna-tables-test" --dcn > "$out/dcn.txt"
	python3 -I "$tests/amdrdna-dcn-compare.py" "$linux/drivers/gpu/drm/amd" < "$out/dcn.txt" || status=1
	"$out/amdrdna-memory-test" --hub > "$out/hub.txt"
	python3 -I "$tests/amdrdna-hub-compare.py" "$linux/drivers/gpu/drm/amd" < "$out/hub.txt" || status=1
	"$out/amdrdna-engines-test" --layout > "$out/engines.txt"
	python3 -I "$tests/amdrdna-engines-compare.py" "$linux/drivers/gpu/drm/amd" < "$out/engines.txt" || status=1
	"$tests/audit-symbols.sh" "$linux" "$out" || status=1
else
	echo "SKIP the DCN comparison and the audit: no Linux tree at $linux"
fi
if [ "$status" -ne 0 ]; then
	echo "FAIL amdrdna-host-test ($out)"
	exit 1
fi
echo "PASS amdrdna-host-test ($out)"
