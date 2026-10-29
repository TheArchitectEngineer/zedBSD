#!/bin/sh
# The ODROID-C5's boot test on the board: power it (through its SwitchBot
# plug when one is named), follow the boot on the live debug UART stage by
# stage, log in and run each block's check, and print the summary.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
#   c5-night-test.sh [--device /dev/ttyUSBn] [--plug SWITCHBOT_ID]
#                    [--login USER:PASSWORD] [--scale N]
#
# The SD card must already carry the image (README.md, "媒体").  Without
# --plug the board is powered by hand when the script says "listening".
# Each command has a time limit; the run never deletes anything.

set -eu

here=$(cd "$(dirname -- "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
device=/dev/ttyUSB0
plug=
login=kei:kei
scale=1

while [ $# -gt 0 ]; do
	case $1 in
	--device) device=$2; shift 2 ;;
	--plug) plug=$2; shift 2 ;;
	--login) login=$2; shift 2 ;;
	--scale) scale=$2; shift 2 ;;
	*) echo "c5-night-test: unknown argument $1" >&2; exit 2 ;;
	esac
done

if [ ! -c "$device" ]; then
	echo "c5-night-test: no serial device $device (the USB-UART adapter)" >&2
	exit 2
fi

# The board off first, so that the line is open before the first byte.
if [ -n "$plug" ]; then
	timeout 60 python3 -I "$top/plan/tools/switchbot.py" off "$plug"
	sleep 10
fi

# The live line is followed in the background while the board comes on.
timeout 900 python3 -I "$here/c5-serial.py" --device "$device" --login "$login" \
	--commands --scale "$scale" &
watcher=$!
sleep 2
if [ -n "$plug" ]; then
	timeout 60 python3 -I "$top/plan/tools/switchbot.py" on "$plug"
fi

status=0
wait "$watcher" || status=$?
echo "c5-night-test: exit $status"
exit "$status"
