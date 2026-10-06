#!/bin/sh
# ws173-p001, p002: the AAT's mouse, keyboard and screen capture on the Venus guest of plan/ws173/tests/config-amd64-aat.mk.
#  1. zdesktop (1280x800, glass) and seat-probe's window; aat-input start --width 1280 --height 800: the compositor takes the
#     three devices (KWL INPUT ... kind=pointer abs=0, kind=pointer abs=1, kind=keyboard).
#  2. keiland-shot from root: a PNG of 1280x800 (shot-a.png).
#  3. aat-input move-to 100 100, shot; move-to 900 600, shot: the two pictures differ only near those two places (the
#     cursor moved; the check is on the host with python).
#  4. aat-input click 640 400 (seat-probe's window) then key a, key shift+b, type "c1": seat-probe hears keys 30, 48, 46
#     and 2 (SEATPROBE key N state=1).  aat-input wheel 1 and drag work (ok).
#  5. aat-input stop: the devices go (KWL INPUT_CLOSED); no ERROR in zdesktop's log.
# PASS: every "ok" line and the last line aat-p002: PASS.  The pictures are left in OUTDIR.
#
#   plan/tools/titlebar/menu-guest.sh start BUILD/hdd-image.img   (the image of config-amd64-aat.mk)
#   plan/ws173/tests/aat-p002.sh [OUTDIR]   (default build/ws173-aat)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws070-run}"
export GUEST_RUNTIME
out=${1:-build/ws173-aat}
mkdir -p "$out"
guest() { timeout 60 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null | tr -d '\r'; }
get() { timeout 120 python3 plan/tools/guest/guest.py get "$1" "$2" >/dev/null 2>&1 </dev/null; }
status=0
ok() { echo "ok: $1"; }
fail() { echo "FAIL: $1"; status=1; }
expect_log() {
	tries=0
	while [ $tries -lt 8 ]; do
		n=$(guest "grep -cE '$2' $1" | tail -1)
		[ "${n:-0}" -ge "${3:-1}" ] 2>/dev/null && { ok "log $2"; return; }
		tries=$((tries + 1)); sleep 1
	done
	fail "log $2 (found ${n:-0})"
}
aat() { r=$(guest "/bin/aat-input $*" | tail -1); [ "$r" = ok ] && ok "aat-input $*" || fail "aat-input $* ($r)"; }
shot() {
	r=$(guest "/bin/keiland-shot /tmp/$1" | tail -1)
	echo "$r" | grep -q "^KEILAND-SHOT /tmp/$1 1280x800$" && ok "keiland-shot $1" || fail "keiland-shot $1 ($r)"
	get "/tmp/$1" "$out/$1"
}

# 1. The desktop, a window that takes the keyboard, the devices.
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[s]eat-probe" | awk "{print \$1}"); do kill $p; done; /bin/aat-input stop >/dev/null 2>&1; sleep 1'
guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0 /tmp/keiland-shot.sock; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=300 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & for w in $(seq 1 40); do grep -q KWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 1
/bin/seat-probe --timeout-s=200 --token=a > /tmp/a.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
expect_log /tmp/zdesktop.log 'KWL SHOT listening path=/tmp/keiland-shot.sock'
expect_log /tmp/a.log 'SEATPROBE ready run=a'
r=$(guest '/bin/aat-input start --width 1280 --height 800' | tail -1)
echo "$r" | grep -q '^AAT-INPUT ready width=1280 height=800' && ok "aat-input start" || fail "aat-input start ($r)"
expect_log /tmp/zdesktop.log 'KWL INPUT device=.* kind=pointer abs=0'
expect_log /tmp/zdesktop.log 'KWL INPUT device=.* kind=pointer abs=1'
expect_log /tmp/zdesktop.log 'KWL INPUT device=.* kind=keyboard'

# 2-3. The shots, and the cursor between them.
shot shot-a.png
aat move-to 100 100
sleep 1
shot shot-b.png
aat move-to 900 600
sleep 1
shot shot-c.png
if python3 - "$out/shot-b.png" "$out/shot-c.png" <<'PY'
import struct, sys, zlib
def load(path):
    data = open(path, 'rb').read(); pos = 8; idat = b''
    while pos < len(data):
        n = struct.unpack('>I', data[pos:pos+4])[0]; t = data[pos+4:pos+8]
        if t == b'IHDR': w, h = struct.unpack('>II', data[pos+8:pos+16])
        if t == b'IDAT': idat += data[pos+8:pos+8+n]
        pos += 12 + n
    return w, h, zlib.decompress(idat)
w, h, a = load(sys.argv[1]); _, _, b = load(sys.argv[2])
stride = 1 + 3 * w
near = far = 0
for y in range(h):
    for x in range(w):
        o = y * stride + 1 + 3 * x
        if a[o:o+3] != b[o:o+3]:
            if (abs(x - 110) < 60 and abs(y - 110) < 60) or (abs(x - 910) < 60 and abs(y - 610) < 60): near += 1
            else: far += 1
print('changed near the cursor: %d, elsewhere: %d' % (near, far))
sys.exit(0 if near > 20 and far < near else 1)
PY
then ok "the cursor moved between the shots"; else fail "the cursor moved between the shots"; fi

# 4. A click on the window, then keys.
aat click 640 400
aat key a
aat key shift+b
aat type c1
aat wheel 1
aat drag 200 200 300 300 5
expect_log /tmp/a.log 'SEATPROBE key 30 state=1'
expect_log /tmp/a.log 'SEATPROBE key 48 state=1'
expect_log /tmp/a.log 'SEATPROBE key 46 state=1'
expect_log /tmp/a.log 'SEATPROBE key 2 state=1'
expect_log /tmp/a.log 'SEATPROBE modifiers depressed=1'

# 5. The devices go; no error.
aat stop
expect_log /tmp/zdesktop.log 'KWL INPUT_CLOSED' 3
guest 'grep -E "ERROR|protocol error" /tmp/zdesktop.log' > "$out/errors.txt"
[ -s "$out/errors.txt" ] && fail "errors in zdesktop's log (see $out/errors.txt)" || ok "no error"
guest 'cat /tmp/zdesktop.log' > "$out/zdesktop.log"
guest 'cat /tmp/a.log' > "$out/probe.log"
guest "$stop_all" >/dev/null
[ $status -eq 0 ] && echo "aat-p002: PASS" || echo "aat-p002: FAIL"
exit $status
