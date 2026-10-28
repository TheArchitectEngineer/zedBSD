#!/bin/sh
# ws075-p013 (H4): holds the 5330's lock (/tmp/i915-hw.lock) for a run of hdmi-h4-hw.sh: OUTDIR/.locked exists while
# it is held, and it is given back when OUTDIR/.running is removed (hdmi-h4-hw.sh stop).
#
#   h4-lock.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
out=$1
exec 9> /tmp/i915-hw.lock
flock 9
echo locked > "$out/.locked"
while [ -e "$out/.running" ]; do
	sleep 2
done
rm -f "$out/.locked"
flock -u 9
