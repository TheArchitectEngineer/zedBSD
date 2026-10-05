#!/bin/sh
# ws163-p002: the lock screen's PIN (the mock of plan/ws163/phase001 section 9) on the Venus guest of the graphical
# login image (plan/ws035/tests/build-login-image.sh BUILD graphical), as plan/ws035/tests/zdesktop-p102.sh runs it:
# the boot's autologin session is stopped, the autologin emptied (restored at the end) and sessiond started, so the
# greeter comes up with kei (password "kei") selected.  kei's PIN file is put in place by the test (the PIN 246810,
# its hash made on the host with openssl passwd -6), as Settings' Users page would have the compositor write it.
#  1. kei logs in; Super+L locks with the PIN offered (ZWL LOCK locked ... pin=1): locked.png ("PIN or password").
#  2. The PIN unlocks without sessiond (ZWL LOCK unlocked pin; no new SESSIOND UNLOCK line).
#  3. Five wrong PINs in a row turn it off (ZWL LOCK pin fail ... usable=0): off.png ("Too many wrong PINs.");
#     the right PIN is then taken for a password (sessiond says FAIL after its delay), and the count is in the file.
#  4. The password unlocks (SESSIOND UNLOCK ok) and forgives them (failures 0); the next lock takes the PIN again.
#  5. No log has the PIN; the file is kei's, mode 0600.
#
#   plan/tools/files/files-guest.sh start build/<BUILD>/hdd-graphical.img   (or the image the build made)
#   plan/ws163/tests/pin-lock-guest.sh [OUTDIR]       (default build/ws163-pin-lock)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws163-pin-lock}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.8; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py --width 1280 --height 800 "$GUEST_RUNTIME/qmp.sock" "$@"; }
status=0
session=/run/user/1000/session.log

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

# Counts a guest file's lines matching a pattern.
count_log() {
	guest "grep -cE '$2' $1" | tail -1
}

# The PIN's hash, made here (SHA-512 crypt, as pin-store.c's).
hash=$(openssl passwd -6 -salt ws163pinlock0001 246810)
case $hash in
'$6$'*) echo "hash: ok" ;;
*) echo "hash: openssl made none"; exit 1 ;;
esac

# The boot's session stopped, the autologin emptied and clean logs; kei's PIN file in place.
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[s]essiond|[w]ayland( |$)" | awk "{print \$1}"); do kill $p; done; sleep 2'
guest "$stop_all
[ -f /tmp/pin-autologin.saved ] || cp /etc/keiland/autologin /tmp/pin-autologin.saved; : > /etc/keiland/autologin
rm -f /var/log/sessiond.log /var/log/greeter.log $session" >/dev/null
home=$(guest 'grep "^kei:" /etc/passwd | cut -d: -f6' | tail -1)
guest "mkdir -p $home/.config/keiland && printf 'hash %s\\nfailures 0\\n' '$hash' > $home/.config/keiland/pin && chown -R kei $home/.config && chmod 700 $home/.config $home/.config/keiland && chmod 600 $home/.config/keiland/pin && ls -l $home/.config/keiland/pin" > "$out/pin-file.txt"
cat "$out/pin-file.txt"

# 1. Login, and Super+L.
guest "/sbin/sessiond --graphical </dev/null >/dev/null 2>&1 & sleep 1; echo started" >/dev/null
expect_log /var/log/greeter.log 'ZWL GREETER open .*selected=kei' 60
sleep 2
keys 'kei' '\n'
expect_log /var/log/sessiond.log 'AUTH ok user=kei uid=1000' 10
expect_log $session 'ZWL HANDOFF go=1' 20
keys '<super-l>'
expect_log $session 'ZWL LOCK locked reason=key user=kei pin=1' 5
pointer move 1270 790 sleep 300
check "$out/locked.png" >/dev/null

# 2. The PIN unlocks, and sessiond is not asked.
before=$(count_log /var/log/sessiond.log 'SESSIOND UNLOCK')
keys '246810' '\n'
expect_log $session 'ZWL LOCK unlocked pin' 5
after=$(count_log /var/log/sessiond.log 'SESSIOND UNLOCK')
[ "${before:-x}" = "${after:-y}" ] && echo "sessiond not asked: ok" || { echo "sessiond not asked: FAIL ($before $after)"; status=1; }

# 3. Five wrong PINs turn it off; the right one is then refused.
keys '<super-l>'
expect_log $session 'ZWL LOCK locked reason=key user=kei pin=1' 5
for wrong in 1 2 3 4 5; do
	keys '000000' '\n'
done
expect_log $session 'ZWL LOCK pin fail error=[0-9]+ usable=0' 5
pointer move 1270 790 sleep 300
check "$out/off.png" >/dev/null
keys '246810' '\n'
sleep 4
pins=$(count_log $session 'ZWL LOCK unlocked pin')
[ "${pins:-0}" -eq 1 ] && echo "PIN refused while off: ok" || { echo "PIN refused while off: FAIL ($pins)"; status=1; }
guest "grep failures $home/.config/keiland/pin" | tail -1 | sed 's/^/file: /'

# 4. The password unlocks and gives the PIN back.
keys 'kei' '\n'
expect_log /var/log/sessiond.log 'SESSIOND UNLOCK ok user=kei' 8
expect_log $session 'ZWL LOCK unlocked$' 5
guest "grep failures $home/.config/keiland/pin" | tail -1 | sed 's/^/file after the password: /'
keys '<super-l>'
expect_log $session 'ZWL LOCK locked reason=key user=kei pin=1' 5
keys '246810' '\n'
sleep 2
pins=$(count_log $session 'ZWL LOCK unlocked pin')
[ "${pins:-0}" -eq 2 ] && echo "PIN after the password: ok" || { echo "PIN after the password: FAIL ($pins)"; status=1; }
check "$out/unlocked.png" >/dev/null

# 5. No log has the PIN; the file is kei's alone.
leaks=$(guest "cat /var/log/sessiond.log /var/log/greeter.log $session /var/log/messages 2>/dev/null | grep -c 246810" | tail -1)
[ "${leaks:-1}" = 0 ] && echo "no PIN in the logs: ok" || { echo "no PIN in the logs: FAIL ($leaks)"; status=1; }
guest "ls -l $home/.config/keiland/pin" | tail -1 | grep -q '^-rw------- .* kei ' && echo "file mode: ok" || { echo "file mode: FAIL"; status=1; }

# The PIN file removed and the autologin as it was (kei's session keeps running).
guest "rm -f $home/.config/keiland/pin; [ -f /tmp/pin-autologin.saved ] && cat /tmp/pin-autologin.saved > /etc/keiland/autologin && rm -f /tmp/pin-autologin.saved" >/dev/null
guest "cat /var/log/sessiond.log" > "$out/sessiond.log"
guest "cat $session" > "$out/session.log"
echo "pin-lock-guest: status=$status"
exit $status
