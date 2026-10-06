#!/bin/sh
# ws172-p003: the Users page's security keys card on the host, through Settings' host renderer
# (plan/ws089/tests/host-build.sh) with two keys listed (HOST_KEYS, plan/ws089/tests/host-kl-system.c): the card lists
# them, and a key's Remove asks with its reference once the password is typed (its length printed, not it).
#   sh plan/ws172/tests/run-host-settings-keys.sh [OUTPUT]   (default build/ws172-settings-keys)
# Each run gets a new directory behind OUTPUT (plan/tools/fresh-out.sh); nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws172-settings-keys}
. plan/tools/fresh-out.sh
fresh_out "$out"
sh plan/ws089/tests/host-build.sh >/dev/null
render=build/ws089-host/settings-render
status=0
check() {
	if grep -q "$2" "$out/log"; then echo "ok $1"; else echo "FAIL $1"; status=1; fi
}
# The renderer types one character into a field (its keys are pressed and never let go), so the addition, which needs a
# PIN of four, is the QEMU test's (T1); here the password of one character enables Remove.
HOST_ACCOUNT_RESULT=1 HOST_KEYS=1 timeout 60 "$render" --size=1180x2400 --page=users draw="$out/keys.ppm" \
	control=300 text=k control=320 draw="$out/asked.ppm" > "$out/log" 2>&1 || status=1
check "the removal asked with the first key's reference" "HOST key remove ref=0123456789abcdef password=1"
check "the removal logged" "USERS key request=78 remove=1"
for picture in keys asked; do convert "$out/$picture.ppm" "$out/$picture.png"; done
[ $status -eq 0 ] && echo "run-host-settings-keys: PASS" || echo "run-host-settings-keys: FAIL"
exit $status
