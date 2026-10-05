#!/bin/sh
# ws161-p003: the smart card slot on a running zedBSD guest of plan/ws161/tests/config-amd64-hidraw.mk (the test kernel's
# loopback card; QEMU's own CCID reader is not one of whole APDUs and is not published).
#  1. /dev/smartcard0 is there, mode 0600 root, and the kernel's log names it.
#  2. smartcard-probe: the state, the claim (EBUSY, EPERM for a second open), SELECT of FIDO's applet and of another,
#     500 bytes by GET RESPONSE, EMSGSIZE, the card taken out and put back (two events, ENXIO), the close powering the
#     card off ("SMARTCARD PASS").
#  3. A user that is not root (kei, else nobody) cannot open it.
# PASS: every "ok" line and the last line smartcard-p003: PASS.
#
#   (a guest up through plan/tools/guest/guest.py, e.g. plan/tools/files/files-guest.sh start IMAGE)
#   plan/ws161/tests/smartcard-p003.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
guest() { timeout 60 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
status=0
expect() {
	if [ "$2" = "$3" ]; then echo "ok: $1"; else echo "FAIL: $1 (got '$2', want '$3')"; status=1; fi
}

# 1. The node.
expect "/dev/smartcard0 is a character device" "$(guest 'test -c /dev/smartcard0 && echo yes' | tail -1)" yes
expect "it is root's alone" "$(guest 'ls -l /dev/smartcard0' | tail -1 | cut -c1-10)" "crw-------"
expect "the kernel published it" "$(guest 'dmesg | grep -c "smartcard: /dev/smartcard0: Loopback smart card"' | tail -1)" 1

# 2. The probe.
guest '/bin/smartcard-probe' | tee /dev/stderr | grep -q '^SMARTCARD PASS$' && echo "ok: smartcard-probe" || { echo "FAIL: smartcard-probe"; status=1; }

# 3. Not for another user.
user=$(guest 'grep -q "^kei:" /etc/passwd && echo kei || echo nobody' | tail -1)
guest "runas $user /bin/smartcard-probe -i -f /dev/smartcard0 2>&1; true" | grep -q 'FAIL step=open' && echo "ok: $user cannot open it" || { echo "FAIL: $user could open it (or runas is missing)"; status=1; }

[ $status = 0 ] && echo "smartcard-p003: PASS" || echo "smartcard-p003: FAIL"
exit $status
