#!/bin/sh
# ws161-p005: fidoctl's NFC transport on a running zedBSD guest of plan/ws161/tests/config-amd64-fidoctl.mk (the test
# kernel's loopback card in /dev/smartcard0: it answers SELECT of FIDO's applet with "FIDO_2_0" but no CTAP2 command).
#  1. fidoctl list lists the card: "card /dev/smartcard0 ..." and "cards 1" (the loopback key too: "devices 1").
#  2. fidoctl -d /dev/smartcard0 info powers the card, selects the applet (libpasskey's transport-nfc) and fails only at
#     GetInfo, whose NFCCTAP_MSG the loopback answers 6D 00: the exit status 1 and "fidoctl: GetInfo:" on standard
#     error (no "NFC SELECT").
#  3. The card is powered off and the slot free after it: smartcard-probe passes again ("SMARTCARD PASS").
# PASS: every "ok" line and the last line fidoctl-p005: PASS.  As root (the node is root's while no seat has it).
#
#   (a guest up through plan/tools/guest/guest.py, e.g. plan/tools/files/files-guest.sh start IMAGE)
#   plan/ws161/tests/fidoctl-p005.sh
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
check "the loopback card is listed" "$(echo "$listed" | grep -c '^card /dev/smartcard0 ')" 1
check "one card" "$(echo "$listed" | grep -c '^cards 1$')" 1

# 2. Selected over NFC; GetInfo is what the loopback cannot answer.
info=$(guest '/bin/fidoctl -d /dev/smartcard0 info; echo status=$?')
echo "$info"
check "the exit status is 1" "$(echo "$info" | grep -c '^status=1$')" 1
check "it failed at GetInfo" "$(echo "$info" | grep -c '^fidoctl: GetInfo:')" 1
check "the applet was selected" "$(echo "$info" | grep -c 'NFC SELECT')" 0

# 3. The slot is free again.
guest '/bin/smartcard-probe' | grep -q '^SMARTCARD PASS$' && echo "ok: the slot is free after fidoctl" || { echo "FAIL: smartcard-probe after fidoctl"; status=1; }

[ $status = 0 ] && echo "fidoctl-p005: PASS" || echo "fidoctl-p005: FAIL"
exit $status
