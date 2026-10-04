#!/bin/sh
# ws113-p002: builds and runs the host test of the firmware's output (host-gop.c with the constants and the
# two functions of src/drivers/gpu/i915/display/output.c taken out with sed).
#   sh plan/ws113/tests/host-gop.sh [OUTPUT]   (default build/ws113-host/host-gop)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws113-host/host-gop}
dir=$(dirname "$out")
mkdir -p "$dir"
source=src/drivers/gpu/i915/display/output.c
{
	grep -E '^#define I915_OUTPUT_(DDI_|MODE_|PORT_|PIPES)' "$source"
	sed -n '/^drv_i915_gop_output_read($/,/^}$/{s/^drv_i915_gop_output_read(/void drv_i915_gop_output_read(/;p;}' "$source"
	sed -n '/^drv_i915_gop_output_name($/,/^}$/{s/^drv_i915_gop_output_name(/void drv_i915_gop_output_name(/;p;}' "$source"
} > "$dir/gop-functions.inc"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-format-truncation -I"$dir" plan/ws113/tests/host-gop.c -o "$out"
"$out"
