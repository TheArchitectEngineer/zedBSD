#!/bin/sh
# WS031 E-120: host test of the N0 decision rules.
set -e
cd "$(dirname "$0")/../../.."
OUT=${TMPDIR:-/tmp}/ws031-native-decide-host-test
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src/drivers/gpu/i915/parity -o "$OUT" plan/ws031/tests/native-decide-host-test.c src/drivers/gpu/i915/parity/native_decide.c
"$OUT"
