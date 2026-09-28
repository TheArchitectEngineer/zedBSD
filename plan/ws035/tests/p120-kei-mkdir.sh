#!/bin/sh
# ws035-p120 (BUG-097): mkdir as the demonstration's non-root user kei, on the Venus guest of the demo image
# (plan/ws035/tests/build-demo-venus-image.sh).  root gives kei the harness's SSH key, then, as kei:
#   1. mkdir of a name that is there, in a directory kei may not write (/home, /etc): EEXIST ("File exists");
#   2. mkdir of a name that is not there, in such a directory (/p118-new, /etc/p118-new): EACCES ("Permission denied");
#   3. mkdir -p "$HOME/Documents/p118/a/b" (which passes /home): made;
#   4. id: the group network (/etc/group of the demo image).
#
#   GUEST_RUNTIME=... plan/ws035/tests/zdesktop-guest.sh start build/amd64/hdd-image.img
#   plan/ws035/tests/p120-kei-mkdir.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
port=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["ssh_port"])' "$GUEST_RUNTIME/session.json")
as_kei() {
	timeout 60 ssh -i plan/tmp/guest/id_ed25519 -p "$port" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	    -o LogLevel=ERROR -o ConnectTimeout=5 -o BatchMode=yes kei@127.0.0.1 -- "$1" 2>&1 </dev/null
}
status=0

# Fails the run unless the output of a step has the expected text.
expect() {
	if printf '%s\n' "$2" | grep -q -- "$3"; then
		echo "$1: ok ($(printf '%s' "$2" | tr '\n' ' '))"
	else
		echo "$1: FAIL ($(printf '%s' "$2" | tr '\n' ' '))"
		status=1
	fi
}

# kei's key (the home is made by root here when no login has made it yet).
guest 'mkdir -p /home/kei/.ssh && cp /root/.ssh/authorized_keys /home/kei/.ssh/ && chown 1000:1000 /home/kei /home/kei/.ssh /home/kei/.ssh/authorized_keys && chmod 0700 /home/kei /home/kei/.ssh && chmod 0600 /home/kei/.ssh/authorized_keys && echo key' | tail -1

expect "whoami" "$(as_kei 'id')" 'uid=1000(kei)'
expect "group network" "$(as_kei 'id')" 'network'
expect "mkdir /home (there, not writable)" "$(as_kei 'mkdir /home; echo rc=$?')" 'File exists'
expect "mkdir /etc (there, not writable)" "$(as_kei 'mkdir /etc; echo rc=$?')" 'File exists'
expect "mkdir /p118-new (not there, not writable)" "$(as_kei 'mkdir /p118-new; echo rc=$?')" 'Permission denied'
expect "mkdir /etc/p118-new (not there, not writable)" "$(as_kei 'mkdir /etc/p118-new; echo rc=$?')" 'Permission denied'
expect "mkdir -p in the home" "$(as_kei 'mkdir -p "$HOME/Documents/p118/a/b" && ls -ld "$HOME/Documents/p118/a/b"')" 'kei'
expect "mkdir -p again (all there)" "$(as_kei 'mkdir -p "$HOME/Documents/p118/a/b"; echo rc=$?')" 'rc=0'
guest 'ls -ld /p118-new /etc/p118-new 2>&1; rm -rf /home/kei/Documents/p118' | sed 's/^/left: /'

[ $status = 0 ] && echo "p120-kei-mkdir: PASS" || echo "p120-kei-mkdir: FAIL"
exit $status
