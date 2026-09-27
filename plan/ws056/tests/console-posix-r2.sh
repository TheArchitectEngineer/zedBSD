#!/bin/sh
# ws056-p002: runs POSIX-R2.ELF on the guest's console (serial mirror) N
# times with standard input /dev/null (the test spawns and execs /bin/sh,
# which would otherwise read the console), and prints the status and the
# last result line of each run (BUG-046 is about the timer tests).
#   [AS_SH=1] sh plan/ws056/tests/console-posix-r2.sh IMAGE ELF [N]
# With AS_SH=1 the ELF is also installed as /bin/sh (the test spawns and
# execs /bin/sh expecting itself: R2_SPAWN_CHILD and R2_EXEC_FINAL), so
# that the whole test can reach status 0; the login shell already running
# on the console is not replaced.
# The image's kernel must be built with CONFIG_PCAT_SERIAL_MIRROR=y
# (config-amd64-serial.mk) and have sshd (plan/tools/guest/hybrid-image.sh).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
image=$1
elf=$2
count=${3:-5}
guest="python3 plan/tools/guest/guest.py"
export GUEST_RUNTIME="$(pwd)/build/ws056/console-run"
serial="python3 plan/tools/guest/serial.py --socket $GUEST_RUNTIME/serial.sock"

$guest stop >/dev/null 2>&1 || true
$guest start --disk nvme "$image" >/dev/null
$guest wait >/dev/null
$guest put "$elf" /bin/posix-r2
$guest run 'chmod 755 /bin/posix-r2'
$serial login >/dev/null
if [ -n "${AS_SH-}" ]; then
	$guest run 'cp /bin/posix-r2 /bin/sh.new && mv /bin/sh.new /bin/sh'
fi
n=0
while [ "$n" -lt "$count" ]; do
	n=$((n + 1))
	status=0
	$serial run '/bin/posix-r2 </dev/null' > "build/ws056/console-run/out.$n" 2>&1 || status=$?
	echo "run $n: status $status: $(grep -E 'FAIL|PASS|fail' "build/ws056/console-run/out.$n" | tail -1)"
done
$guest stop >/dev/null
