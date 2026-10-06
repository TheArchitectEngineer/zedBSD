#!/bin/sh
# The host tests of passkey-fido2 (ws172-p003), under ASan and UBSan with the host's OpenSSL: its pure parts
# (fido2-wire-host-test.c), and the whole program built for Linux (it must refuse to run when not root).
# usage: plan/ws172/tests/fido2-host-test.sh   (from the repository's top; OUT= to choose the build folder)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
OUT=${OUT:-build/ws172-fido2-host}
mkdir -p "$OUT"
LIB=userland/base/libpasskey
FLAGS="-std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I."
status=0
cc $FLAGS -o "$OUT/fido2-wire-host-test" plan/ws172/tests/fido2-wire-host-test.c userland/base/passkey-fido2/wire.c \
	$LIB/crypto-openssl.c -lcrypto
timeout 60 "$OUT/fido2-wire-host-test" || status=1
cc $FLAGS -o "$OUT/passkey-fido2" userland/base/passkey-fido2/*.c $LIB/cbor.c $LIB/crypto-openssl.c $LIB/ctap2.c \
	$LIB/descriptor.c $LIB/hid.c $LIB/os-posix.c $LIB/os-linux.c $LIB/pin.c $LIB/verify.c userland/base/passkey/record.c \
	userland/base/passkey/request.c userland/base/common/account.c userland/base/login/verify.c -lcrypto -lcrypt
set +e
printf 'auth\nkei\nfido2\n123456\n' | "$OUT/passkey-fido2" > "$OUT/not-root.txt"; code=$?
set -e
if [ "$(id -u)" != 0 ] && [ $code -eq 2 ] && [ ! -s "$OUT/not-root.txt" ]; then echo "ok passkey-fido2 refuses to run when not root"; else echo "FAIL not root ($code)"; status=1; fi
[ $status -eq 0 ] && echo "fido2-host-test: PASS" || echo "fido2-host-test: FAIL"
exit $status
