#!/bin/sh
# The host test of the compositor's PIN store (ws163-p002): builds pin-store.c with the host's
# compiler and libcrypt, runs it on a scratch home, and removes the scratch.
# usage: plan/ws163/tests/pin-store-host-test.sh   (from the repository's top; OUT= to choose the build folder)
set -eu
OUT=${OUT:-build/ws163-pin-host}
mkdir -p "$OUT"
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -I. -o "$OUT/pin-store-host-test" \
	plan/ws163/tests/pin-store-host-test.c userland/desktop/wayland/pin-store.c -lcrypt
SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/ws163-pin.XXXXXX")
trap 'rm -rf "$SCRATCH"' EXIT
timeout 120 "$OUT/pin-store-host-test" "$SCRATCH"
