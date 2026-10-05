#!/bin/sh
# The host tests of libpasskey (ws161-p004): each test of plan/ws161/tests/libpasskey-*-host-test.c, built with the
# host's compiler and OpenSSL's libcrypto under ASan and UBSan, run in turn.
# usage: plan/ws161/tests/libpasskey-host-test.sh   (from the repository's top; OUT= to choose the build folder)
set -eu
OUT=${OUT:-build/ws161-libpasskey-host}
mkdir -p "$OUT"
LIB="userland/base/libpasskey"
status=0
for test in plan/ws161/tests/libpasskey-*-host-test.c; do
	name=$(basename "$test" .c)
	cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
		-I. -o "$OUT/$name" "$test" $(ls $LIB/*.c | grep -v -e '/os-' -e '/transport-nfc-os') -lcrypto
	timeout 120 "$OUT/$name" || status=1
done
exit $status
