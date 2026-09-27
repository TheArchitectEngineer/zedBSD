#!/bin/sh
# ws035-p094: zsessiond with the test greeter (greeter-probe, no picture) on the Venus guest of the login image
# (plan/ws035/tests/build-login-image.sh).  A user alice (uid 1001, password "secret word") is made; then:
#  1. zsessiond --graphical --greeter=/bin/greeter-probe --session=/tmp/p094-session: the greeter runs as _greeter
#     (uid 78) with /dev/gpu0 and the input devices _greeter's (0600); a wrong password and an unknown user are
#     answered FAIL after the delay (2 s), a bad request ERROR; the right password OK.
#  2. The session runs as alice with XDG_RUNTIME_DIR=/run/user/1001 (0700, alice's), HOME, and the seat alice's;
#     utmpx has alice on seat0 (who).
#  3. When the session ends, the greeter is started again; it now fails (EXIT 1) three times in a row, and
#     zsessiond gives the console back (CONSOLE reason=greeter-failed, exit 0) with the devices root's again.
#  4. SIGTERM stops zsessiond while its greeter waits: the greeter is ended, the devices go back to root.
#  5. zsessiond without --graphical keeps to the console (no login= boot parameter): CONSOLE reason=boot-parameters.
#
#   plan/tools/files/files-guest.sh start     (the guest must be up; GUEST_RUNTIME as for the files tests)
#   plan/ws035/tests/zdesktop-p094.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
status=0

# Fails the run unless a guest file has a line matching a pattern (within some seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt "${3:-10}" ]; do
		found=$(guest "grep -cE '$2' $1" | tail -1)
		[ "${found:-0}" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
		echo "log: $2 ok"
	else
		echo "log: $2 MISSING"
		status=1
	fi
}

# Fails the run unless a command's output matches a pattern.
expect_run() {
	output=$(guest "$1")
	if printf '%s\n' "$output" | grep -qE "$2"; then
		echo "run: $2 ok"
	else
		echo "run: $2 MISSING ($output)"
		status=1
	fi
}

hash=$(openssl passwd -6 -salt p094salt 'secret word')

# The user, the session script, and clean logs.
guest "grep -q '^alice:' /etc/passwd || echo 'alice:x:1001:1001:Alice:/home/alice:/bin/sh' >> /etc/passwd
grep -q '^alice:' /etc/group || echo 'alice:x:1001:' >> /etc/group
grep -v '^alice:' /etc/shadow > /tmp/shadow.new; echo 'alice:$hash:0:0:99999:7:::' >> /tmp/shadow.new; cat /tmp/shadow.new > /etc/shadow; rm -f /tmp/shadow.new
mkdir -p /home/alice; chown 1001:1001 /home/alice
printf '%s\n' '#!/bin/sh' 'echo SESSION uid=\$(id -u) runtime=\$XDG_RUNTIME_DIR home=\$HOME user=\$USER' 'ls -ld \$XDG_RUNTIME_DIR' 'ls -l /dev/gpu0' 'who' 'sleep 4' 'echo SESSION end' > /tmp/p094-session
chmod 0755 /tmp/p094-session
printf '%s\n' 'AUTH alice wrong' 'AUTH nobody x' 'HELLO' 'AUTH alice secret word' > /tmp/greeter-probe
chmod 0644 /tmp/greeter-probe
rm -f /var/log/zsessiond.log /var/log/greeter.log" >/dev/null

# 1-3. The greeter, the session, the greeter again that fails.
guest "/sbin/zsessiond --graphical --greeter=/bin/greeter-probe --session=/tmp/p094-session </dev/null >/dev/null 2>&1 & sleep 1; echo started" >/dev/null
expect_log /var/log/greeter.log 'PROBE start uid=78'
expect_log /var/log/zsessiond.log 'SEAT path=/dev/gpu0 uid=78 mode=0600'
expect_log /var/log/greeter.log 'PROBE send=AUTH alice reply=FAIL' 15
expect_log /var/log/zsessiond.log 'AUTH fail user=alice wrong=1 delay=2'
expect_log /var/log/greeter.log 'PROBE send=AUTH nobody reply=FAIL' 15
expect_log /var/log/greeter.log 'PROBE send=HELLO reply=ERROR' 15
expect_log /var/log/greeter.log 'PROBE send=AUTH alice reply=OK' 15
expect_log /var/log/zsessiond.log 'AUTH ok user=alice uid=1001'
expect_log /var/log/zsessiond.log 'SESSION start user=alice uid=1001 .* runtime=/run/user/1001'
guest "echo EXIT 1 > /tmp/greeter-probe" >/dev/null
expect_log /run/user/1001/session.log 'SESSION uid=1001 runtime=/run/user/1001 home=/home/alice user=alice'
expect_log /run/user/1001/session.log '^drwx------ .* alice .* /run/user/1001'
expect_log /run/user/1001/session.log '^c.w------- .* alice .* /dev/gpu0'
expect_log /run/user/1001/session.log '^alice +seat0'
expect_log /var/log/zsessiond.log 'SESSION end user=alice' 15
expect_log /var/log/zsessiond.log 'CONSOLE reason=greeter-failed failures=3' 30
expect_run 'ls -l /dev/gpu0' '^crw-rw-rw- .* root '
guest "cat /var/log/zsessiond.log; cat /var/log/greeter.log" > build/ws035-p094-logs.txt

# 4. SIGTERM while the greeter waits.
guest "echo SLEEP 60 > /tmp/greeter-probe; rm -f /var/log/zsessiond.log
/sbin/zsessiond --graphical --greeter=/bin/greeter-probe --session=/tmp/p094-session </dev/null >/dev/null 2>&1 & echo \$! > /tmp/p094.pid; sleep 3; echo started" >/dev/null
expect_log /var/log/zsessiond.log 'GREETER start'
guest 'kill -TERM $(cat /tmp/p094.pid)' >/dev/null
expect_log /var/log/zsessiond.log 'ZSESSIOND STOP'
expect_run 'ps -A -o args | grep -c "[g]reeter-probe"; ls -l /dev/gpu0' '^0$'
expect_run 'ls -l /dev/gpu0' '^crw-rw-rw- .* root '

# 5. Without --graphical and without login=graphical: the console.
guest "rm -f /var/log/zsessiond.log; /sbin/zsessiond; echo exit=\$?" | tail -1
expect_log /var/log/zsessiond.log 'CONSOLE reason=boot-parameters'

echo "zdesktop-p094: status=$status"
exit $status
