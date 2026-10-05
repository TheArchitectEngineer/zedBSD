#!/bin/sh
# The host test of the raw HID devices' descriptor reading (ws161-p002): builds src/drivers/generic/hidraw-describe.c
# with the host's compiler (under ASan and UBSan) and runs it.
# usage: plan/ws161/tests/hidraw-describe-host-test.sh   (from the repository's top; OUT= to choose the build folder)
set -eu
OUT=${OUT:-build/ws161-hidraw-host}
mkdir -p "$OUT"
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Iinclude -I. \
	-o "$OUT/hidraw-describe-host-test" plan/ws161/tests/hidraw-describe-host-test.c src/drivers/generic/hidraw-describe.c
timeout 60 "$OUT/hidraw-describe-host-test"
