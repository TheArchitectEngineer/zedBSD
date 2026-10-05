#!/bin/sh
# The host test of the CCID messages and class descriptor (ws161-p003): builds src/drivers/usb/usb-ccid-proto.c with the
# host's compiler (under ASan and UBSan) and runs it.
# usage: plan/ws161/tests/ccid-proto-host-test.sh   (from the repository's top; OUT= to choose the build folder)
set -eu
OUT=${OUT:-build/ws161-ccid-host}
mkdir -p "$OUT"
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Iinclude -I. \
	-o "$OUT/ccid-proto-host-test" plan/ws161/tests/ccid-proto-host-test.c src/drivers/usb/usb-ccid-proto.c
timeout 60 "$OUT/ccid-proto-host-test"
