#!/bin/sh
# ws056-p002: boots IMAGE, installs ELF as /bin/posix-r2 (and, with AS_SH=1,
# as /bin/sh), logs in on the console and runs each further argument there
# as a command, printing its output and status.
#   [AS_SH=1] sh plan/ws056/tests/console-probe.sh IMAGE ELF COMMAND...
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
image=$1
elf=$2
shift 2
guest="python3 plan/tools/guest/guest.py"
export GUEST_RUNTIME="$(pwd)/build/ws056/probe-run"
serial="python3 plan/tools/guest/serial.py --socket $GUEST_RUNTIME/serial.sock --timeout ${PROBE_TIMEOUT:-60}"

$guest stop >/dev/null 2>&1 || true
$guest start --disk nvme "$image" >/dev/null
$guest wait >/dev/null
$guest put "$elf" /bin/posix-r2
$guest run 'chmod 755 /bin/posix-r2'
$serial login >/dev/null
if [ -n "${AS_SH-}" ]; then
	$guest run 'cp /bin/posix-r2 /bin/sh.new && mv /bin/sh.new /bin/sh'
fi
for command in "$@"; do
	echo "=== $command"
	$serial run "$command" 2>&1 | tail -${PROBE_LINES:-5}
	echo "=== status $?"
done
$guest stop >/dev/null
