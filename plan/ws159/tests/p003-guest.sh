#!/bin/sh
# ws159-p003 on the pen test guest (plan/ws079/tests/config-amd64-pen.mk with
# this branch's kernel and touchinject, started with
# plan/ws079/tests/pen-guest.sh start IMAGE).  QEMU has no LPSS I2C and no
# I2C-HID device; what QEMU can check is checked:
#  1. touchinject -c: the injector's refusals are unchanged (14 passed; the
#     unknown kind is now 4, since 3 is the touch pad).
#  2. The touch pad of the injector: pad-basic.touch replayed while
#     touchinject -d reads the node: its name "Test touchpad (input-inject)",
#     its properties 0x5 (a pointer and a button pad), BTN_TOOL_FINGER for
#     one finger, BTN_TOOL_DOUBLETAP for two, BTN_LEFT pressed and released,
#     and the fingers lifted (BTN_TOUCH 0).
#  3. The touch screen of the injector is unchanged apart from its new
#     property 0x2 (direct): ws079's p012-guest.sh is run as it is.
#  4. The kernel log has no lpss-i2c or i2c-hid line (nothing to bind in QEMU).
#
#   plan/ws079/tests/pen-guest.sh start IMAGE
#   plan/ws159/tests/p003-guest.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws079-run}"
out=${1:-build/ws159-p003}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1; }
status=0

# 1. The refusals.
guest '/bin/touchinject -c' > "$out/touchcheck.txt"
if grep -q 'TOUCHCHECK result=ok passed=14 failed=0' "$out/touchcheck.txt"; then
	echo "touchcheck: ok"
else
	echo "touchcheck: FAILED"
	status=1
fi

# 2. The touch pad, read back from its evdev node.
put plan/ws159/tests/pad-basic.touch /tmp/pad-basic.touch
guest '/bin/touchinject -d 4000 > /tmp/paddump.txt 2>&1 & sleep 0.3; /bin/touchinject /tmp/pad-basic.touch; echo replay=$?; sleep 4; cat /tmp/paddump.txt' > "$out/paddump.txt"
expect() {
	if grep -q "$1" "$out/paddump.txt"; then
		echo "pad: $2 ok"
	else
		echo "pad: $2 FAILED"
		status=1
	fi
}
expect '^replay=0$' "the script ran"
expect 'TOUCHDUMP props=0x5' "a pointer and a button pad"
expect 'BTN_TOOL_FINGER 1' "BTN_TOOL_FINGER for one finger"
expect 'BTN_TOOL_DOUBLETAP 1' "BTN_TOOL_DOUBLETAP for two"
expect 'BTN_LEFT 1' "BTN_LEFT pressed"
expect 'BTN_LEFT 0' "BTN_LEFT released"
expect 'BTN_TOUCH 0' "the fingers lifted"

# 3. The touch screen, as ws079 checks it.
sh plan/ws079/tests/p012-guest.sh "$out/p012" > "$out/p012.txt" 2>&1
if grep -q 'FAILED' "$out/p012.txt"; then
	echo "touch screen (p012-guest.sh): FAILED"
	status=1
else
	echo "touch screen (p012-guest.sh): ok"
fi

# 4. Nothing of LPSS or I2C-HID in QEMU.
guest 'dmesg | grep -E "lpss-i2c|i2c-hid" | head -5; echo dmesg-done' > "$out/dmesg.txt"
if grep -qE 'lpss-i2c|i2c-hid' "$out/dmesg.txt"; then
	echo "kernel log: unexpected LPSS or I2C-HID lines (see $out/dmesg.txt)"
	status=1
else
	echo "kernel log: no LPSS or I2C-HID line ok"
fi

[ $status -eq 0 ] && echo "ws159-p003: PASS" || echo "ws159-p003: FAIL"
exit $status
