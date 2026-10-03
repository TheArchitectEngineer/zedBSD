#!/bin/sh
# ws035-p095: the graphical login from end to end on the Venus guest of the login image
# (plan/ws035/tests/build-login-image.sh): sessiond --graphical starts zdesktop --greeter as _greeter at the
# display's preferred size.  Users alice (uid 1001, "secret word") and bob (uid 1002) are made (zdesktop-p094.sh
# makes alice; this makes both).  ws136-p002: since 2026-09-29 the image has kei (uid 1000) in its base passwd and
# sessiond logs kei in at boot (/etc/keiland/autologin); the run empties the autologin file (restored at the end), so
# the sessiond it starts brings the greeter, which offers kei, alice and bob in passwd's order.
#  1. greeter.png: the login screen: the time and date, the card with the three users, Down selects alice.
#  2. wrong.png: a wrong password: "Wrong password" (SESSIOND AUTH fail).
#  3. The right password (typed with Shift for nothing, a space in it): the greeter ends, the session runs as alice
#     (zdesktop --session, pid owned by alice, its socket in /run/user/1001), session.png.
#  4. home.png: App Home has Log Out as its last icon; a click on it ends the session (ZWL SESSION logout).
#  5. again.png: the login screen again.  sessiond is then stopped (SIGTERM).
#
#   plan/tools/files/files-guest.sh start     (the guest must be up; GUEST_RUNTIME as for the files tests)
#   plan/ws035/tests/zdesktop-p095.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws035-p095}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.8; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[s]essiond|[w]ayland( |$)" | awk "{print \$1}"); do kill $p; done; sleep 2'
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

# Moves the pointer on an output of the size zdesktop opened.
pointer() { python3 plan/ws035/tests/qmp-pointer.py --width "$width" --height "$height" "$GUEST_RUNTIME/qmp.sock" "$@"; }

hash=$(openssl passwd -6 -salt p095salt 'secret word')
hash_bob=$(openssl passwd -6 -salt p095bob 'bob')

# The users, clean logs, nothing of zdesktop running.
guest "$stop_all
for u in alice:1001:Alice bob:1002:Bob; do n=\${u%%:*}; rest=\${u#*:}; id=\${rest%%:*}; full=\${rest#*:}
 grep -q \"^\$n:\" /etc/passwd || echo \"\$n:x:\$id:\$id:\$full:/home/\$n:/bin/sh\" >> /etc/passwd
 grep -q \"^\$n:\" /etc/group || echo \"\$n:x:\$id:\" >> /etc/group
 mkdir -p /home/\$n; chown \$id:\$id /home/\$n; done
grep -v -e '^alice:' -e '^bob:' /etc/shadow > /tmp/shadow.new; echo 'alice:$hash:0:0:99999:7:::' >> /tmp/shadow.new; echo 'bob:$hash_bob:0:0:99999:7:::' >> /tmp/shadow.new; cat /tmp/shadow.new > /etc/shadow; rm -f /tmp/shadow.new
rm -f /var/log/sessiond.log /var/log/greeter.log
[ -f /tmp/p095-autologin.saved ] || cp /etc/keiland/autologin /tmp/p095-autologin.saved; : > /etc/keiland/autologin" >/dev/null

# 0. The GPU admits root and the device's owner (gpu_open): alice owns it 0600, bob is refused by the mode; with
#    0666 bob is still refused by the GPU (logged); owned by root again, alice is refused as before.
expect_run() {
	output=$(guest "$1")
	if printf '%s\n' "$output" | grep -qE "$2"; then
		echo "run: $2 ok"
	else
		echo "run: $2 MISSING ($output)"
		status=1
	fi
}
expect_run 'chown 1001:1001 /dev/gpu0; chmod 0600 /dev/gpu0; /bin/greeter-probe --open-as=1001 /dev/gpu0' 'uid=1001 path=/dev/gpu0 ok'
expect_run '/bin/greeter-probe --open-as=1002 /dev/gpu0' 'uid=1002 path=/dev/gpu0 errno=25'
expect_run 'chmod 0666 /dev/gpu0; /bin/greeter-probe --open-as=1002 /dev/gpu0' 'uid=1002 path=/dev/gpu0 errno=25'
expect_run 'dmesg | tail -5' 'gpu: open refused: uid 1002 is neither root nor the device.s owner'
expect_run 'chown 0:0 /dev/gpu0; /bin/greeter-probe --open-as=1001 /dev/gpu0' 'uid=1001 path=/dev/gpu0 errno=25'
expect_run 'chmod 0666 /dev/gpu0; ls -l /dev/gpu0' '^crw-rw-rw- .* root '

# 1. The login screen.
guest "/sbin/sessiond --graphical </dev/null >/dev/null 2>&1 & sleep 1; echo started" >/dev/null
expect_log /var/log/greeter.log 'ZWL GREETER open users=3 selected=kei' 20
expect_log /var/log/greeter.log 'ZWL OUTPUT open' 20
set -- $(guest "grep 'ZWL OUTPUT open' /var/log/greeter.log | tail -1" | sed -n 's/.*width=\([0-9]*\) height=\([0-9]*\).*/\1 \2/p')
width=${1:-1280}; height=${2:-800}
echo "output ${width}x$height"
sleep 3
pointer move $((width - 4)) $((height / 3)) sleep 400
keys '<down>'
expect_log /var/log/greeter.log 'ZWL GREETER select user=alice' 10
check "$out/greeter.png" >/dev/null

# 2. A wrong password.
keys 'nope' '\n'
expect_log /var/log/sessiond.log 'AUTH fail user=alice wrong=1' 10
expect_log /var/log/greeter.log 'ZWL GREETER answer=FAIL' 10
sleep 1
check "$out/wrong.png" >/dev/null
keys 'abc'
sleep 1
check "$out/typing.png" >/dev/null
keys '<esc>'

# 3. The right password; the session.
keys 'secret word' '\n'
expect_log /var/log/sessiond.log 'AUTH ok user=alice uid=1001' 10
expect_log /var/log/sessiond.log 'SESSION start user=alice' 15
expect_log /run/user/1001/session.log 'ZWL READY socket=/run/user/1001/wayland-0' 20
guest "ps -A -o user,pid,args" | grep -E 'zdesktop|sessiond|greeter'
owner=$(guest "ps -A -o user,args" | awk '$2 == "/bin/wayland" {print $1}' | tail -1)
echo "session zdesktop user: $owner"
[ "$owner" = alice ] || [ "$owner" = 1001 ] || status=1
sleep 3
check "$out/session.png" >/dev/null

# 4. App Home's Log Out.
pointer move 23 17 sleep 300 down sleep 60 up sleep 1500
check "$out/home.png" >/dev/null
set -- $(guest "grep 'ZWL HOME icon name=\"Log Out\"' /run/user/1001/session.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p')
echo "Log Out at ${1:-?},${2:-?}"
if [ -n "${1:-}" ]; then
	pointer move "$1" "$2" sleep 400 down sleep 60 up sleep 500
else
	status=1
fi
expect_log /run/user/1001/session.log 'ZWL SESSION logout' 10
expect_log /var/log/sessiond.log 'SESSION end user=alice' 20

# 5. The login screen again.
expect_log /var/log/sessiond.log 'GREETER start .*' 10
sleep 6
pointer move $((width - 4)) $((height / 3)) sleep 400
check "$out/again.png" >/dev/null
guest "$stop_all" >/dev/null
guest "cat /var/log/sessiond.log; cat /var/log/greeter.log" > "$out/logs.txt"
guest "[ -f /tmp/p095-autologin.saved ] && cat /tmp/p095-autologin.saved > /etc/keiland/autologin && rm -f /tmp/p095-autologin.saved" >/dev/null

echo "zdesktop-p095: status=$status"
exit $status
