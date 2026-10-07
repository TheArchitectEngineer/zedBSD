#!/bin/sh
# ws051-p002: builds and runs the host test of the VBT DVO port mapping and ADL-P's TC PLL enable registers
# (host-vbt-pll.c with the constants, drv_i915_dvo_port_to_port() and the adlp_plls rows taken out of
# src/drivers/gpu/i915/display/takeover.c with sed).
#   sh plan/ws051/tests/host-vbt-pll.sh [OUTPUT]   (default build/ws051-host/host-vbt-pll)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws051-host/host-vbt-pll}
dir=$(dirname "$out")
mkdir -p "$dir"
source=src/drivers/gpu/i915/display/takeover.c
{
	grep -E '^#define I915_(DVO_PORT_|DPLL[01]_ENABLE|TBT_PLL_ENABLE|PORTTC|DPLL_ID_ICL_)' "$source"
	sed -n '/^drv_i915_dvo_port_to_port($/,/^}$/{s/^drv_i915_dvo_port_to_port(/int drv_i915_dvo_port_to_port(/;p;}' "$source"
	echo 'static const struct i915_adlp_pll_desc adlp_plls[] = {'
	sed -n '/static const struct i915_adlp_pll_desc adlp_plls\[\] = {/,/^	};$/{/{ "/p;}' "$source"
	echo '};'
} > "$dir/vbt-pll-functions.inc"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -I"$dir" plan/ws051/tests/host-vbt-pll.c -o "$out"
"$out"
