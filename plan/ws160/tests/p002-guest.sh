#!/bin/sh
# ws160-p002: the Users page of Settings changes the password through the desktop (Settings -> libkeiland's
# kl_system_account_set_password -> the compositor's system extension -> libkeiland-backend -> passwd -s).  On the
# Venus guest of the Settings image built with plan/ws160/tests/config-amd64-settings-users.mk; zdesktop --glass at
# 1280x800.  The guest's harness runs the desktop as root, so the password changed is root's (root's current password
# is read by passwd but not checked); kei's own path (the current password checked) is p001's (passwd -s as kei).
# The compositor and Settings under test (BUILD/bin/wayland, BUILD/bin/settings, BUILD/dynamic/libkeiland.so) are
# copied in; /etc/shadow is saved first and put back at the end.
#  1. Settings on Users: "ZSETTINGS USERS account name=root" (users.png).
#  2. Typed (the current, then Tab, the new one twice: "newroot123"), Enter: "ZSETTINGS USERS change request=N", the
#     compositor's "KWL SYSTEM account set-password" and "KWL SYSTEM account result ... error=0", Settings'
#     "ZSETTINGS USERS result request=N errno=0" (users-changed.png); root's shadow line has a new
#     $6$rounds=65536$ hash; no log has the password.
#  3. /etc/shadow back as it was; no ERROR in zdesktop's log.
#
#   plan/ws089/tests/settings-guest.sh start     (the guest must be up)
#   plan/ws160/tests/p002-guest.sh BUILD [OUTDIR]   (default build/ws160-p002-guest)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws089-run}"
export GUEST_RUNTIME
build=${1:?usage: p002-guest.sh BUILD [OUTDIR]}
out=${2:-build/ws160-p002-guest}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[s]ettings" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[s]ettings" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
start_desktop='export XDG_RUNTIME_DIR=/tmp HOME=/root; rm -f /tmp/wayland-0
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass --wallpaper=/usr/share/keiland/wallpaper.png > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started'
status=0
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }
. plan/ws089/tests/settings-wait.sh
shot() {
	pointer move 1270 790 sleep 500 >/dev/null
	check "$out/$1" >/dev/null
	echo "shot: $out/$1"
}
expect_log() {
	tries=0
	while [ $tries -lt 15 ]; do
		if guest "grep -E '$3' $2" | grep -Eq "$3"; then pass "$1"; return; fi
		tries=$((tries + 1))
		sleep 1
	done
	fail "$1"
}

# The programs under test; the shadow saved.
wait_guest
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
put "$build/bin/settings" /bin/settings
put "$build/dynamic/libkeiland.so" /lib/libkeiland.so
guest 'chmod 755 /bin/wayland /bin/settings; cp /etc/shadow /tmp/shadow.p002; ls -l /bin/passwd' > "$out/passwd-mode.txt"
if grep -q '^-rwsr-xr-x' "$out/passwd-mode.txt"; then pass passwd-setuid; else fail passwd-setuid; fi
guest "$start_desktop" >/dev/null
wait_desktop

# 1. Settings on Users.
guest 'export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/settings --timeout-s=300 users > /tmp/s.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
expect_log users-page /tmp/s.log 'ZSETTINGS USERS account name=root'
find_window
shot users.png

# 2. The change: the current, Tab, the new one, Tab, the new one again, Enter.
keys 'x' '<tab>' 'newroot123' '<tab>' 'newroot123' '<ret>'
expect_log change-asked /tmp/s.log 'ZSETTINGS USERS change request=[0-9]+'
expect_log compositor-asked /tmp/zdesktop.log 'KWL SYSTEM account set-password client=[0-9]+ number=[0-9]+'
expect_log compositor-result /tmp/zdesktop.log 'KWL SYSTEM account result client=[0-9]+ number=[0-9]+ error=0'
expect_log settings-result /tmp/s.log 'ZSETTINGS USERS result request=[0-9]+ errno=0'
shot users-changed.png
guest 'grep "^root:" /etc/shadow | cut -c1-21; grep "^root:" /tmp/shadow.p002 | cut -c1-21' > "$out/shadow.txt"
if head -1 "$out/shadow.txt" | grep -q '^root:\$6\$rounds=65536\$' && [ "$(head -1 "$out/shadow.txt")" != "$(tail -1 "$out/shadow.txt")" ]; then pass shadow-changed; else fail shadow-changed; fi
if guest 'grep -c newroot123 /tmp/s.log /tmp/zdesktop.log /var/log/messages' | grep -q ':[1-9]'; then fail no-password-in-logs; else pass no-password-in-logs; fi

# 3. The shadow back; no ERROR.
guest 'cp /tmp/shadow.p002 /etc/shadow; chmod 400 /etc/shadow; pkill -x settings' >/dev/null
if guest 'grep -c ERROR /tmp/zdesktop.log' | tail -1 | grep -qx 0; then pass no-error; else fail no-error; fi

echo "p002-guest: status $status (pictures in $out)"
exit $status
