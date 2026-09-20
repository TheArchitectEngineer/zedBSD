#!/bin/sh
# WS031 E-120: host test of the OpRegion VBT locator against the target laptop OpRegion dump.
set -e
cd "$(dirname "$0")/../../.."
OUT=${TMPDIR:-/tmp}/ws031-opregion-host-test
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src/drivers/gpu/i915/parity -o "$OUT" plan/ws031/tests/opregion-host-test.c src/drivers/gpu/i915/parity/opregion_vbt.c
"$OUT"
