#!/bin/sh
# ws161-p004: fidoctl on a running zedBSD guest of plan/ws161/tests/config-amd64-fidoctl.mk (the test kernel's loopback
# security key, which speaks CTAPHID but not CTAP2).
#  1. fidoctl list finds the loopback key: "device /dev/input/hidraw0 ..." and "devices 1".
#  2. fidoctl info opens it (the os layer, the grab, CTAPHID INIT) and fails only at GetInfo, which the loopback answers
#     with ERROR: the exit status 1 and "fidoctl: GetInfo:" on standard error.
#  3. fidoctl verify takes an assertion made on the host (plan/ws161/tests/fidoctl-assertion.py): libpasskey's verify
#     and OpenSSL's libcrypto on the guest ("verified sign-count 5"); a wrong signature is refused (status 1).
# PASS: every "ok" line and the last line fidoctl-p004: PASS.  As root (the node is root's while no seat has it).
#
#   (a guest up through plan/tools/guest/guest.py, e.g. plan/tools/files/files-guest.sh start IMAGE)
#   plan/ws161/tests/fidoctl-p004.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
guest() { timeout 60 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
status=0
check() {
	if [ "$2" = "$3" ]; then echo "ok: $1"; else echo "FAIL: $1 (got '$2', want '$3')"; status=1; fi
}

# 1. The list.
listed=$(guest '/bin/fidoctl list')
echo "$listed"
check "the loopback key is listed" "$(echo "$listed" | grep -c '^device /dev/input/hidraw0 ')" 1
check "one key" "$(echo "$listed" | grep -c '^devices 1$')" 1

# 2. Opened; GetInfo is what the loopback cannot answer.
info=$(guest '/bin/fidoctl info; echo status=$?')
echo "$info"
check "info ends at GetInfo" "$(echo "$info" | grep -c '^fidoctl: GetInfo:')" 1
check "info's status" "$(echo "$info" | grep -c '^status=1$')" 1

# 3. Verify, with libcrypto on the guest.
good=$(python3 plan/ws161/tests/fidoctl-assertion.py)
bad=$(python3 plan/ws161/tests/fidoctl-assertion.py --tamper)
check "a good assertion verifies" "$(guest "/bin/fidoctl verify $good" | grep -c '^verified sign-count 5$')" 1
check "a wrong signature is refused" "$(guest "/bin/fidoctl verify $bad; echo status=\$?" | grep -c '^status=1$')" 1

[ $status = 0 ] && echo "fidoctl-p004: PASS" || echo "fidoctl-p004: FAIL"
exit $status
