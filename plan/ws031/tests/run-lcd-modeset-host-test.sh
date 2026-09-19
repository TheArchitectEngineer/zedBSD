#!/bin/sh
# WS031: host integration test of the one-screen LCD modeset (reference callers + callees, resident eDP, register / sink models).
set -eu
cd "$(dirname "$0")/../../.."
D=src/drivers/gpu/i915/parity/dp
L=src/drivers/gpu/i915/parity/lcd
V=src/drivers/gpu/i915/parity/vbt
OUT=${TMPDIR:-/tmp}/ws031-lcd-modeset-host-test
# shift-base is off: the reference text has `(1 << 31)` register bits (the kernel is built with wrapping semantics)
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-missing-field-initializers -DPARITY_VBT_HOST -fsanitize=address,undefined \
   -fno-sanitize=alignment -fno-sanitize=shift-base -I"$D" -I"$L" -I"$V" -o "$OUT" plan/ws031/tests/lcd-modeset-host-test.c \
   "$D/parity_edp.c" "$D/intel_pps_port.c" "$D/intel_dp_aux_port.c" "$D/drm_dp_helper_port.c" "$D/drm_edid_port.c" "$D/dp_fake_hw.c" \
   "$L"/*_port.c "$L/parity_lcd_calc.c" "$L/parity_lcd_modeset.c" "$L/parity_lcd_trace.c" "$L/parity_lcd_observe.c" "$L/lcd_fake_hw.c"
"$OUT" plan/ws031/display-ref "$@"
