#!/bin/sh
# ws159-p001: records, on the 5330's Linux, both the I2C-HID touchpad's evdev
# node and the PS/2 mouse node for SECONDS while the user touches the pad, so
# that the design knows whether the PS/2 emulation still sends packets while
# the I2C-HID touchpad is driven (Linux's i2c_hid + hid-multitouch).
#   sudo sh touch-record.sh [SECONDS]   (writes /tmp/ws159-touch-*.txt)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
seconds=${1:-20}
pad=$(grep -l 'Touchpad' /sys/class/input/event*/device/name | head -1 | sed 's|/sys/class/input/\(event[0-9]*\)/.*|\1|')
ps2=$(grep -l 'PS/2 Generic Mouse' /sys/class/input/event*/device/name | head -1 | sed 's|/sys/class/input/\(event[0-9]*\)/.*|\1|')
echo "touchpad=/dev/input/$pad ps2=/dev/input/$ps2 seconds=$seconds"
timeout "$seconds" od -A d -t x1 -v -w24 "/dev/input/$pad" > /tmp/ws159-touch-pad.txt &
timeout "$seconds" od -A d -t x1 -v -w24 "/dev/input/$ps2" > /tmp/ws159-touch-ps2.txt &
wait
echo "pad events: $(wc -l < /tmp/ws159-touch-pad.txt)  ps2 events: $(wc -l < /tmp/ws159-touch-ps2.txt)"
