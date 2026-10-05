#!/bin/sh
# ws089-p016: one Settings.  A second start of Settings hands its page to the Settings that runs, which opens the page
# and brings its window to the front through zdesktop's xdg_activation_v1.  On the Venus guest of the Settings image
# (build-settings-image.sh BUILD, built from the commit under test: zdesktop, libwayland-client, libkeiland and
# Settings come with it), zdesktop --glass at 1280x800 with a private runtime directory (/tmp/kr, 0700: the one copy's
# socket needs a directory others cannot enter; /tmp would leave every start on its own).
#  1. Settings started (Home); then Files over it (its window on top).
#  2. "settings sharing": the second start ends with "ZSETTINGS DONE reason=handed-over page=sharing"; the first logs
#     "ZSETTINGS INSTANCE request page=sharing known=1 activate=0" and "ZSETTINGS PAGE sharing"; zdesktop logs a granted
#     token ("ZWL ACTIVATION token ... granted=1 reason=new-program"), "ZWL ACTIVATION activate ... result=activated"
#     and "ZWL APPS raise ... via=activation"; one settings process runs; sharing.png shows Settings in front on Sharing.
#  3. A second Files window over Settings, then "settings" without a page: handed over with an empty page
#     ("INSTANCE request page= known=0"), activated, raised; front.png.
#  4. "settings about" while Settings is in front: handed over, the page changes (PAGE about); one process; about.png.
#  5. No ERROR line in zdesktop's log.
#
#   plan/ws089/tests/settings-guest.sh start IMAGE      (the guest must be up)
#   plan/ws089/tests/settings-p016.sh [OUTDIR]           (default build/ws089-shots/p016)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws089-run}"
export GUEST_RUNTIME
out=${1:-build/ws089-shots/p016}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[s]ettings|[f]iles" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[s]ettings|[f]iles" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
env_line='export XDG_RUNTIME_DIR=/tmp/kr HOME=/root'
start_desktop="mkdir -p /tmp/kr; chmod 700 /tmp/kr; rm -f /tmp/kr/wayland-0 /tmp/kr/keiland-settings.instance; $env_line
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass --socket=/tmp/kr/wayland-0 --wallpaper=/usr/share/keiland/wallpaper.png > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started"
status=0
. plan/ws089/tests/settings-wait.sh
shot() {
	pointer move 1270 790 sleep 500 >/dev/null
	check "$out/$1" >/dev/null
	echo "shot: $out/$1"
}
expect() {
	# expect NAME FILE PATTERN: the guest's FILE has a line matching PATTERN (grep -E).
	if guest "grep -cE '$3' $2" | tail -1 | grep -qv '^0$'; then echo "$1: ok"; else echo "$1: FAILED"; status=1; fi
}
second() {
	# second ARGS: a second start of Settings, which must end at once (handed over), its log in /tmp/s2.log.
	guest "$env_line; timeout 20 /bin/settings $1 > /tmp/s2.log 2>&1 </dev/null; echo exit=\$?" | tail -1
}
one_process() {
	count=$(guest "ps -A -o args | grep -cE '^/bin/settings'" | tail -1)
	if [ "$count" = 1 ]; then echo "$1: ok (one settings)"; else echo "$1: FAILED (settings processes: $count)"; status=1; fi
}

# The desktop.
wait_guest
guest "$stop_all" >/dev/null
guest "$start_desktop" >/dev/null
wait_desktop

# 1. Settings, then Files over it.
guest "$env_line; /bin/settings --timeout-s=600 > /tmp/s.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
expect settings-ready /tmp/s.log 'ZSETTINGS READY'
guest "$env_line; /bin/files /root > /tmp/files.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
shot covered.png

# 2. A second start on Sharing.
result=$(second sharing)
echo "second sharing: $result"
[ "$result" = exit=0 ] || { echo "second-exit: FAILED"; status=1; }
sleep 2
expect handed-over /tmp/s2.log 'ZSETTINGS DONE reason=handed-over page=sharing'
expect request /tmp/s.log 'ZSETTINGS INSTANCE request page=sharing known=1 activate=0'
expect page /tmp/s.log 'ZSETTINGS PAGE sharing'
expect token /tmp/zdesktop.log 'ZWL ACTIVATION token client=[0-9]+ app=settings granted=1 reason=new-program'
expect activated /tmp/zdesktop.log 'ZWL ACTIVATION activate client=[0-9]+ surface=[0-9]+ app=settings result=activated'
expect raised /tmp/zdesktop.log 'ZWL APPS raise surface=[0-9]+ via=activation'
one_process one-settings
shot sharing.png

# 3. Another Files window over Settings, then a start without a page.
guest "$env_line; /bin/files /tmp > /tmp/files2.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
before=$(guest "grep -c 'via=activation' /tmp/zdesktop.log" | tail -1)
result=$(second "")
echo "second (no page): $result"
[ "$result" = exit=0 ] || { echo "second-exit-nopage: FAILED"; status=1; }
sleep 2
expect handed-over-nopage /tmp/s2.log 'ZSETTINGS DONE reason=handed-over page=$'
expect request-nopage /tmp/s.log 'ZSETTINGS INSTANCE request page= known=0 activate=0'
after=$(guest "grep -c 'via=activation' /tmp/zdesktop.log" | tail -1)
if [ "${after:-0}" -gt "${before:-0}" ] 2>/dev/null; then echo "raised-nopage: ok"; else echo "raised-nopage: FAILED"; status=1; fi
shot front.png

# 4. A start on About while Settings is in front.
result=$(second about)
echo "second about: $result"
sleep 2
expect page-about /tmp/s.log 'ZSETTINGS PAGE about'
one_process one-settings-about
shot about.png

# 5. No ERROR.
if guest 'grep -c ERROR /tmp/zdesktop.log' | tail -1 | grep -qx 0; then echo "no-error: ok"; else echo "no-error: FAILED"; status=1; fi
guest 'grep "ZWL ACTIVATION\|via=activation" /tmp/zdesktop.log; grep "INSTANCE\|PAGE\|DONE" /tmp/s.log' | tail -20

echo "settings-p016: status $status (pictures in $out)"
exit $status
