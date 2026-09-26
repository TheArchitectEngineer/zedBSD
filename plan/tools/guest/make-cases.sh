#!/bin/sh
# Runs the make difference cases (plan/ws046/tests/make-diff.py) on a
# guest image with the make under test as /usr/bin/make, in /root, and
# compares them with GNU make's results on the host.  From ws064-p003.
#
#   sh plan/tools/guest/make-cases.sh IMAGE
#
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
image=$1
guest="python3 plan/tools/guest/guest.py"
work=build/make-cases
export GUEST_RUNTIME="$(pwd)/build/make-cases-run"

rm -rf "$work"
mkdir -p "$work"
python3 plan/ws046/tests/make-diff.py --export "$work/cases" > /dev/null
tar -C "$work" -cf "$work/cases.tar" cases

$guest stop >/dev/null 2>&1 || true
$guest start --disk nvme "$image" >/dev/null
$guest wait >/dev/null
$guest put plan/ws046/tests/make-guest.sh /root/make-guest.sh
$guest put "$work/cases.tar" /root/cases.tar
$guest run 'cd /root && rm -rf cases && pax -r -f cases.tar && sh /root/make-guest.sh /root/cases; pax -w -f /root/results.tar cases'
$guest get /root/results.tar "$work/results.tar"
$guest stop >/dev/null
rm -rf "$work/cases"
tar -C "$work" -xf "$work/results.tar"
python3 plan/ws046/tests/make-diff.py --compare "$work/cases"
