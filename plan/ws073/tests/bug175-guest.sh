#!/bin/sh
# BUG-175 on the SSH guest (plan/tools/guest/guest.py; image: plan/tools/guest/test-image.sh
# plan/tools/guest/config-amd64-ssh.mk BUILD): 40 pseudo terminals at once (each held by a sleep that has
# /dev/ptmx open), where 8 were the limit and a new Terminal failed with ENOSPC (errno 8).
#  1. 40 opens of /dev/ptmx all succeed: /dev/pts lists at least 40 terminals and no "No space" error is written.
#  2. They are given back: after the sleeps end, /dev/pts lists only what the harness's own SSH session holds.
#   plan/tools/guest/guest.py start IMAGE; plan/tools/guest/guest.py wait
#   plan/ws073/tests/bug175-guest.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws073-bug175}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
status=0

# 1. Forty held at once.
guest 'before=$(ls /dev/pts | wc -l); i=0; while [ $i -lt 40 ]; do sleep 20 < /dev/ptmx 2>> /tmp/bug175.err & i=$((i+1)); done; sleep 2
echo "before=$before held=$(ls /dev/pts | wc -l)"; cat /tmp/bug175.err; rm -f /tmp/bug175.err' > "$out/held.txt"
cat "$out/held.txt"
before=$(sed -n 's/^before=\([0-9]*\) held=\([0-9]*\).*/\1/p' "$out/held.txt")
held=$(sed -n 's/^before=\([0-9]*\) held=\([0-9]*\).*/\2/p' "$out/held.txt")
if [ -n "$held" ] && [ "$held" -ge $(( ${before:-0} + 40 )) ]; then echo "forty-ptys: ok"; else echo "forty-ptys: FAILED"; status=1; fi
if grep -qi "space" "$out/held.txt"; then echo "no-enospc: FAILED"; status=1; else echo "no-enospc: ok"; fi

# 2. Given back.
sleep 22
after=$(guest 'ls /dev/pts | wc -l' | tail -1)
echo "after=$after"
if [ -n "$after" ] && [ "$after" -le "${before:-1}" ]; then echo "given-back: ok"; else echo "given-back: FAILED"; status=1; fi

echo "bug175-guest: status $status (outputs in $out)"
exit $status
