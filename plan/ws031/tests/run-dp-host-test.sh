#!/bin/sh
# WS031: host build + run of the eDP first-stage test (ASan/UBSan), from the repository root.
set -eu
D=src/drivers/gpu/i915/parity/dp
V=src/drivers/gpu/i915/parity/vbt
OUT=${TMPDIR:-/tmp}/ws031-dp-host-test
cc -std=gnu11 -O1 -g -Wall -Wextra -DPARITY_VBT_HOST -fsanitize=address,undefined -fno-sanitize=alignment \
   -I"$D" -I"$V" -o "$OUT" plan/ws031/tests/dp-host-test.c \
   "$D/parity_edp.c" "$D/intel_pps_port.c" "$D/intel_dp_aux_port.c" "$D/drm_dp_helper_port.c" \
   "$D/drm_edid_port.c" "$D/dp_fake_hw.c"
"$OUT" plan/ws031/display-ref "$@"
