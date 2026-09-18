#!/bin/sh
# WS031: host build + run of the LCD-A first-slice test (ASan/UBSan), from the repository root.
set -eu
D=src/drivers/gpu/i915/parity/lcd
OUT=${TMPDIR:-/tmp}/ws031-lcd-host-test
# shift-base is off: the reference text has `(1 << 31)` register bits (the kernel is built with wrapping semantics)
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-missing-field-initializers -fsanitize=address,undefined -fno-sanitize=alignment -fno-sanitize=shift-base \
   -I"$D" -o "$OUT" plan/ws031/tests/lcd-host-test.c \
   "$D"/*_port.c "$D/parity_lcd_calc.c" "$D/parity_lcd_modeset.c"
"$OUT" plan/ws031/display-ref "$@"
