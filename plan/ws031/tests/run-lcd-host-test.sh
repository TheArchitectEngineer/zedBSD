#!/bin/sh
# E-122: intel_opregion_port.c / intel_acpi_port.c are kernel-only (kern locks, workqueue): tested by the ktest
# WS031: host build + run of the LCD-A first-slice test (ASan/UBSan), from the repository root.
set -eu
D=src/drivers/gpu/i915/parity/lcd
OUT=${TMPDIR:-/tmp}/ws031-lcd-host-test
# shift-base is off: the reference text has `(1 << 31)` register bits (the kernel is built with wrapping semantics)
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-missing-field-initializers -fsanitize=address,undefined -fno-sanitize=alignment -fno-sanitize=shift-base \
   -I"$D" -o "$OUT" plan/ws031/tests/lcd-host-test.c \
   $(ls "$D"/*_port.c | grep -v -e intel_opregion_port.c -e intel_acpi_port.c -e hotplug -e intel_gmbus_port.c -e intel_dp_connected_port.c -e intel_hdmi_detect_port.c -e drm_probe_detect_port.c -e drm_connector_status_port.c) "$D/parity_lcd_calc.c" "$D/parity_lcd_modeset.c"
"$OUT" plan/ws031/display-ref "$@"
