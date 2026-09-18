#!/bin/sh
# WS031: host build + run of the LCD-A first-slice test (ASan/UBSan), from the repository root.
set -eu
D=src/drivers/gpu/i915/parity/lcd
OUT=${TMPDIR:-/tmp}/ws031-lcd-host-test
# shift-base is off: the reference text has `(1 << 31)` register bits (the kernel is built with wrapping semantics)
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-missing-field-initializers -fsanitize=address,undefined -fno-sanitize=alignment -fno-sanitize=shift-base \
   -I"$D" -o "$OUT" plan/ws031/tests/lcd-host-test.c \
   "$D/parity_lcd_calc.c" "$D/drm_edid_mode_port.c" "$D/intel_link_port.c" "$D/intel_dpll_port.c" \
   "$D/intel_display_port.c" "$D/intel_ddi_port.c" "$D/intel_vrr_port.c" "$D/skl_plane_port.c" "$D/drm_dp_bw_port.c" "$D/drm_modes_port.c"
"$OUT" plan/ws031/display-ref "$@"
