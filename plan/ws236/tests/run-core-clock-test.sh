#!/bin/sh
# ws236-p011: the core clock's floors in rpi4-firmware.c (host, ASan/UBSan).
# Usage: sh plan/ws236/tests/run-core-clock-test.sh [output-directory]
set -eu
out=${1:-build/ws236-core-clock}
mkdir -p "$out"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Iinclude -Isrc -I. \
    plan/ws236/tests/core-clock-host-test.c src/drivers/platform/rpi4/rpi4-firmware.c \
    -o "$out/core-clock-host-test"
"$out/core-clock-host-test"
