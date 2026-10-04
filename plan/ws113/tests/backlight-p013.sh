#!/bin/sh
# ws113-p013: /dev/backlight on a running zedBSD guest (any image with this kernel; the Venus guest has no panel).
#  1. /dev/backlight is a directory in the root's listing; on a guest without an eDP panel it is empty and
#     /dev/backlight/backlight0 does not exist.
#  2. The namespaces stay apart: /dev/input still lists the event devices, an event device is not found under
#     /dev/backlight and no backlight is found in the root.
# PASS: every "ok" line and the last line backlight-p013: PASS.
#
#   (a guest up through plan/tools/guest/guest.py, e.g. plan/tools/files/files-guest.sh start IMAGE)
#   plan/ws113/tests/backlight-p013.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
guest() { timeout 60 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
status=0
expect() {
	if [ "$2" = "$3" ]; then echo "ok: $1"; else echo "FAIL: $1 (got '$2', want '$3')"; status=1; fi
}

# 1. The directory, empty without a panel.
expect "/dev/backlight is listed in /dev" "$(guest 'ls /dev | grep -c "^backlight$"' | tail -1)" 1
expect "/dev/backlight is a directory" "$(guest 'test -d /dev/backlight && echo yes' | tail -1)" yes
expect "/dev/backlight is empty without a panel" "$(guest 'ls /dev/backlight | wc -l' | tail -1 | tr -d ' ')" 0
expect "/dev/backlight/backlight0 does not exist" "$(guest 'test -e /dev/backlight/backlight0 || echo absent' | tail -1)" absent

# 2. The namespaces.
events=$(guest 'ls /dev/input | grep -c "^event"' | tail -1)
[ "${events:-0}" -ge 1 ] 2>/dev/null && echo "ok: /dev/input lists $events event devices" || { echo "FAIL: /dev/input lists no event device"; status=1; }
expect "an event device is not under /dev/backlight" "$(guest 'test -e /dev/backlight/event0 || echo absent' | tail -1)" absent
expect "no backlight in the root" "$(guest 'ls /dev | grep -c "^backlight[0-9]"' | tail -1)" 0

[ $status = 0 ] && echo "backlight-p013: PASS" || echo "backlight-p013: FAIL"
exit $status
