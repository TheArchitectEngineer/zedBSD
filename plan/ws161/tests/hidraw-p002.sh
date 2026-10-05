#!/bin/sh
# ws161-p002: the raw HID node on a running zedBSD guest of plan/ws161/tests/config-amd64-hidraw.mk (the test kernel's
# loopback security key).
#  1. /dev/input/hidraw0 is there, mode 0600 root (devfs's own, no seat given), and not in the root of /dev.
#  2. hidraw-probe: the requests, two opens, a read that does not wait, CTAPHID INIT, a PING over 6 packets, WINK, the
#     grab (the other open cannot write, EBUSY, and hears nothing), a
#     write of the wrong length refused, an unknown command answered ERROR ("HIDRAW PASS").
#  3. The probe as a user that is not root (kei, else nobody) cannot open it (EACCES).
#  4. The system's events: the kernel's log names the node (hidraw: /dev/input/hidraw0 ... usage=f1d0:0001).
# PASS: every "ok" line and the last line hidraw-p002: PASS.
#
#   (a guest up through plan/tools/guest/guest.py, e.g. plan/tools/files/files-guest.sh start IMAGE)
#   plan/ws161/tests/hidraw-p002.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
guest() { timeout 60 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
status=0
expect() {
	if [ "$2" = "$3" ]; then echo "ok: $1"; else echo "FAIL: $1 (got '$2', want '$3')"; status=1; fi
}

# 1. The node.
expect "/dev/input/hidraw0 is a character device" "$(guest 'test -c /dev/input/hidraw0 && echo yes' | tail -1)" yes
expect "it is root's alone" "$(guest 'ls -l /dev/input/hidraw0' | tail -1 | cut -c1-10)" "crw-------"
expect "no hidraw in the root of /dev" "$(guest 'ls /dev | grep -c "^hidraw"' | tail -1)" 0

# 2. The probe.
guest '/bin/hidraw-probe' | tee /dev/stderr | grep -q '^HIDRAW PASS$' && echo "ok: hidraw-probe" || { echo "FAIL: hidraw-probe"; status=1; }

# 3. Not for another user (the probe run as kei, or nobody, through the tests' runas, fails to open it).
user=$(guest 'grep -q "^kei:" /etc/passwd && echo kei || echo nobody' | tail -1)
guest "runas $user /bin/hidraw-probe -f /dev/input/hidraw0 2>&1; true" | grep -q 'FAIL step=open' && echo "ok: $user cannot open it" || { echo "FAIL: $user could open it (or runas is missing)"; status=1; }

# 4. The kernel's line.
expect "the kernel published it" "$(guest 'dmesg | grep -c "hidraw: /dev/input/hidraw0.*usage=f1d0:0001"' | tail -1)" 1

[ $status = 0 ] && echo "hidraw-p002: PASS" || echo "hidraw-p002: FAIL"
exit $status
