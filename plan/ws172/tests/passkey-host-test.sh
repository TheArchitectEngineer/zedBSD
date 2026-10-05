#!/bin/sh
# The host test of /sbin/passkey's pure parts (ws172-p002), under ASan and UBSan.
# usage: plan/ws172/tests/passkey-host-test.sh   (from the repository's top)
set -eu
OUT=${OUT:-build/ws172-passkey-host}
mkdir -p "$OUT"
cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I. \
	-o "$OUT/passkey-host-test" plan/ws172/tests/passkey-host-test.c userland/base/passkey/request.c \
	userland/base/passkey/record.c
timeout 60 "$OUT/passkey-host-test"
