#!/bin/sh
# ws051-p002b: builds and runs the host tests of the Type-C ports: host-tc (tc.c over fake registers, built with
# ASan/UBSan) and host-tc-tables (the AUX channels' power domains of power.c, the DKL window of dkl-phy.c and the
# Type-C long pulse of hotplug.c, taken out with sed).
#   sh plan/ws051/tests/host-tc.sh [OUT_DIR]   (default build/ws051-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
dir=${1:-build/ws051-host}
mkdir -p "$dir"
display=src/drivers/gpu/i915/display
flags="-std=c11 -O1 -g -Wall -Wextra -Werror -Wdeclaration-after-statement -Wstrict-prototypes -Wmissing-prototypes -Wshadow"
sanitize="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
cc $flags $sanitize -I"$display" "$display/tc.c" plan/ws051/tests/host-tc.c -o "$dir/host-tc"
"$dir/host-tc"
{
	sed -n '/^enum i915_power_domain {$/,/^};$/p' "$display/internal.h"
	grep -E '^#define I915_AUX_CH_' "$display/internal.h"
	for f in drv_i915_aux_legacy_power_domain drv_i915_aux_tbt_power_domain drv_i915_aux_io_power_domain; do
		sed -n "/^$f(\$/,/^}\$/{s/^$f(/static enum i915_power_domain $f(/;p;}" "$display/power.c"
	done
	grep -E '^#define I915_DKL_(WINDOW|BANK|INDEX|PORTS)' "$display/dkl-phy.c"
	sed -n '/^drv_i915_dkl_phy_window($/,/^}$/{s/^drv_i915_dkl_phy_window(/static uint32_t drv_i915_dkl_phy_window(/;p;}' "$display/dkl-phy.c"
	sed -n '/^i915_gen11_port_hotplug_long_detect($/,/^}$/{s/^i915_gen11_port_hotplug_long_detect(/static bool i915_gen11_port_hotplug_long_detect(/;p;}' "$display/hotplug.c"
} > "$dir/tc-tables.inc"
cc $flags -Wno-missing-prototypes -I"$dir" plan/ws051/tests/host-tc-tables.c -o "$dir/host-tc-tables"
"$dir/host-tc-tables"
