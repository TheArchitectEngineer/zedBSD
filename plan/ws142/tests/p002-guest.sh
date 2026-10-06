#!/bin/sh
# ws142-p002: the Windows key pressed alone opens and closes App Home.  On the desktop test guest
# (plan/ws079/tests/config-amd64-pen.mk, started with plan/ws079/tests/pen-guest.sh start IMAGE); the compositor under
# test (BUILD/bin/wayland) is copied in, 1280x800, with one wltest window.  The keys come through QMP.
#  1. Super (meta_l) pressed and released: "KWL SUPER home" and "KWL HOME open via=super" (home-open.png).
#  2. Again: "KWL HOME close via=super".
#  3. The right Super (meta_r): opens; Esc closes it ("KWL HOME close via=escape").
#  4. Super+Tab: Wiseview opens from the keyboard ("KWL WISEVIEW opening key"), Home does not; Esc closes Wiseview.
#  5. Super held while the mouse clicks the window: Home does not open.
#  6. Super held 1.5 s: Home does not open.
#  7. Shift+Super: Home does not open.
#  8. The Windows key reaches no client (the user's decision D9): /bin/seat-probe focused hears neither Super's key
#     (125, 126), while a plain key (a, 30) and Super's bit in the modifiers (depressed=64 with Super+a) still come.
#  9. The compositor is still up, with no ERROR in its log.
#
#   plan/ws079/tests/pen-guest.sh start IMAGE
#   plan/ws142/tests/p002-guest.sh BUILD [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws079-run}"
build=${1:?usage: p002-guest.sh BUILD [OUTDIR]}
out=${2:-build/ws142-p002}
mkdir -p "$out"
qmp="$GUEST_RUNTIME/qmp.sock"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1; }
send() { timeout 40 python3 plan/ws049/tests/qmp-send.py "$qmp" "$@" >> "$out/qmp.txt" 2>&1; }
# A picture for the eye, read from the Venus head through QEMU's VNC (QMP screendump shows the text console instead).
shot() { timeout 60 python3 plan/ws035/tests/zdesktop-check.py "$out/$1.png" --runtime "$GUEST_RUNTIME" >> "$out/qmp.txt" 2>&1; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$qmp" "$@"; }
key() { send input-send-event "{\"events\":[{\"type\":\"key\",\"data\":{\"down\":$2,\"key\":{\"type\":\"qcode\",\"data\":\"$1\"}}}]}"; }
tap() { key "$1" true; sleep 0.15; key "$1" false; sleep 1; }
count() { guest "grep -c '$1' /tmp/zdesktop.log" | tail -1; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]ltest|[w]lshm" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }
expect_count() { n=$(count "$2"); if [ "${n:-0}" -eq "$3" ] 2>/dev/null; then pass "$1"; else fail "$1 ($2: ${n:-?}, expected $3)"; fi; }
: > "$out/qmp.txt"

# The compositor under test and one window.
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
guest 'chmod 755 /bin/wayland; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
/bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q KWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 2; echo started' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0; /bin/wltest --windowed --size=420x300 --frames=3600 --delay-ms=250 > /tmp/w.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null

# 1 and 2. The left Super opens Home, then closes it.
tap meta_l
expect_count super-opens 'KWL HOME open via=super' 1
expect_count super-logged 'KWL SUPER home' 1
pointer move 700 500 sleep 300 >/dev/null
shot home-open
tap meta_l
expect_count super-closes 'KWL HOME close via=super' 1

# 3. The right Super opens it; Esc closes it.
tap meta_r
expect_count right-super-opens 'KWL HOME open via=super' 2
tap esc
expect_count esc-closes 'KWL HOME close via=escape' 1

# 4. Super+Tab: Wiseview, not Home.
key meta_l true; sleep 0.1; key tab true; sleep 0.05; key tab false; sleep 0.1; key meta_l false; sleep 1
expect_count super-tab-wiseview 'KWL WISEVIEW opening key' 1
expect_count super-tab-no-home 'KWL HOME open via=super' 2
tap esc

# 5. Super held while the mouse clicks.
key meta_l true; sleep 0.1
pointer move 640 400 sleep 100 down sleep 60 up sleep 200 >/dev/null
key meta_l false; sleep 1
expect_count click-no-home 'KWL HOME open via=super' 2

# 6. Super held 1.5 s.
key meta_l true; sleep 1.5; key meta_l false; sleep 1
expect_count long-hold-no-home 'KWL HOME open via=super' 2

# 7. Shift+Super.
key shift true; sleep 0.1; key meta_l true; sleep 0.1; key meta_l false; sleep 0.1; key shift false; sleep 1
expect_count shift-no-home 'KWL HOME open via=super' 2

# 8. Super reaches no client: seat-probe has the focus.
guest 'export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0; /bin/seat-probe --timeout-s=40 --token=k > /tmp/k.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
tap meta_l
tap esc
tap meta_r
tap esc
key meta_l true; sleep 0.1; key a true; sleep 0.05; key a false; sleep 0.1; key meta_l false; sleep 0.5
tap a
guest 'cat /tmp/k.log' > "$out/seat-probe.log"
if grep -q 'SEATPROBE focus' "$out/seat-probe.log"; then pass probe-focused; else fail probe-focused; fi
if grep -Eq 'SEATPROBE key (125|126) ' "$out/seat-probe.log"; then fail super-not-delivered; else pass super-not-delivered; fi
if grep -q 'SEATPROBE key 30 state=1' "$out/seat-probe.log"; then pass plain-key-delivered; else fail plain-key-delivered; fi
if grep -q 'SEATPROBE modifiers depressed=64 ' "$out/seat-probe.log"; then pass super-modifier-delivered; else fail super-modifier-delivered; fi

# 9. Still up, no ERROR.
guest 'cat /tmp/zdesktop.log' > "$out/zdesktop.log"
if guest 'ps -A -o args' | grep -qE '[w]ayland( |$)'; then pass alive; else fail alive; fi
if grep -q ERROR "$out/zdesktop.log"; then fail no-error; else pass no-error; fi

echo "p002-guest: status $status (outputs in $out; home-open.png for the eye)"
exit $status
