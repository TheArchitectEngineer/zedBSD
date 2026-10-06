#!/bin/sh
# ws161-p004: fidoctl built for Linux on the host (libpasskey with os-linux.c), with the host's OpenSSL: list answers
# (no key is needed: "devices N"), a wrong use exits 2, verify takes an assertion made by fidoctl-assertion.py and
# refuses one whose signature is over other client data.  A real key is the UAT's (p006).
#   sh plan/ws161/tests/fidoctl-host-test.sh   (from the repository's top; OUT= to choose the build folder)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
OUT=${OUT:-build/ws161-fidoctl-host}
mkdir -p "$OUT"
LIB=userland/base/libpasskey
cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I. \
	-o "$OUT/fidoctl" userland/base/fidoctl/main.c $LIB/cbor.c $LIB/crypto-openssl.c $LIB/ctap2.c $LIB/descriptor.c \
	$LIB/hid.c $LIB/os-posix.c $LIB/os-linux.c $LIB/pin.c $LIB/verify.c -lcrypto
status=0
"$OUT/fidoctl" list > "$OUT/list.txt" && grep -q '^devices [0-9]' "$OUT/list.txt" && echo "ok list" || { echo "FAIL list"; status=1; }
set +e
"$OUT/fidoctl" 2> /dev/null; code=$?
set -e
[ $code -eq 2 ] && echo "ok usage" || { echo "FAIL usage $code"; status=1; }
# shellcheck disable=SC2046
"$OUT/fidoctl" verify $(python3 plan/ws161/tests/fidoctl-assertion.py) > "$OUT/verify.txt" && grep -q '^verified sign-count 5$' "$OUT/verify.txt" && echo "ok verify" || { echo "FAIL verify"; status=1; }
set +e
# shellcheck disable=SC2046
"$OUT/fidoctl" verify $(python3 plan/ws161/tests/fidoctl-assertion.py --tamper) > /dev/null 2> "$OUT/tamper.txt"; code=$?
set -e
[ $code -eq 1 ] && grep -q 'verify' "$OUT/tamper.txt" && echo "ok a wrong signature is refused" || { echo "FAIL tamper $code"; status=1; }
[ $status -eq 0 ] && echo "fidoctl-host-test: PASS" || echo "fidoctl-host-test: FAIL"
exit $status
