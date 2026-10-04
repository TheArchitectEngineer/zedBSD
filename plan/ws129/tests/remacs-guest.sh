#!/bin/sh
# ws129 q669: REmacs, now built from userland/base/emacs, starts in Terminal on the Venus guest (an image with the terminal,
# noct and remacs, e.g. the CI configuration; start the guest first).  zdesktop at 1280x800.
#  1. /usr/bin/remacs.nap and /usr/share/remacs/skkjisyo.dic are there.
#  2. In a terminal, `noct /usr/bin/remacs.nap /tmp/remacs-test.txt`: the editor runs (a noct process with remacs.nap),
#     "hello" typed and C-x C-s saves the file with it; remacs.png.  C-x C-c quits (no noct process left).
#   sh plan/ws129/tests/remacs-guest.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws129-remacs}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[t]erminal|[n]oct" | awk "{print \$1}"); do kill $p; done; sleep 1'
status=0
expect_guest() {
	if guest "$1 && echo YES" | grep -q YES; then echo "guest: $2 ok"; else echo "guest: $2 FAILED"; status=1; fi
}

timeout 260 python3 plan/tools/guest/guest.py wait --timeout 240 >/dev/null 2>&1 || { echo "guest: SSH not up FAILED"; echo "remacs-guest: FAIL"; exit 1; }
guest "$stop_all" >/dev/null

# 1. The files.
expect_guest '[ -s /usr/bin/remacs.nap ] && [ -s /usr/share/remacs/skkjisyo.dic ]' 'remacs.nap and the dictionary are installed'

# 2. The editor in a terminal.
guest 'rm -f /tmp/remacs-test.txt; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
/bin/wayland --timeout=600 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4
/bin/terminal --token=r1 --timeout-s=500 > /tmp/t.log 2>&1 </dev/null & sleep 6; echo started' >/dev/null
pointer move 640 400 sleep 300 down sleep 60 up sleep 500
keys 'noct /usr/bin/remacs.nap /tmp/remacs-test.txt' '\n'
sleep 5
expect_guest 'ps -A -o args | grep -q "[n]oct /usr/bin/remacs.nap"' 'remacs runs'
keys 'hello'
sleep 1
keys '<ctrl-x>' '<ctrl-s>'
sleep 2
pointer move 1270 790 sleep 400
check "$out/remacs.png" >/dev/null
expect_guest 'grep -q hello /tmp/remacs-test.txt' 'C-x C-s saved the file'
keys '<ctrl-x>' '<ctrl-c>'
sleep 2
expect_guest '! ps -A -o args | grep -q "[n]oct /usr/bin/remacs.nap"' 'C-x C-c quit remacs'

guest "$stop_all" >/dev/null
[ $status = 0 ] && echo "remacs-guest: PASS" || echo "remacs-guest: FAIL"
exit $status
