#!/bin/sh
# ws075-p029: the glass with and without the blur of the windows under it, on the Venus guest (the running guest of
# plan/ws035/tests/zdesktop-guest.sh, the criteria image of this tree).  zdesktop --glass at 1280x800 with the wallpaper,
# Settings (its glass blurs what is under it) and Files over it, then App Home, Wiseview and the QWERTY keyboard panel.
# VARIANT names the pictures; FILES_BIN (a Files built with keiland_glass_set_blur(1), plan/ws075/phase029/
# files-blur-shot.patch) replaces /bin/files for the run, and ZDESKTOP_EXTRA (e.g. --keyboard-blur) is added to zdesktop.
#   GUEST_RUNTIME=... VARIANT=off plan/ws075/tests/glass-shots.sh OUTDIR
#   GUEST_RUNTIME=... VARIANT=on FILES_BIN=build/.../files-blur ZDESKTOP_EXTRA=--keyboard-blur plan/ws075/tests/glass-shots.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws035-sq-run}"
out=$1
variant=${VARIANT:-off}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 120 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
shot() { python3 plan/ws035/tests/zdesktop-check.py "$out/$variant-$1" --runtime "$GUEST_RUNTIME" >/dev/null; echo "shot $variant-$1"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[/]bin/files|[/]bin/settings" | awk "{print \$1}"); do kill $p; done; sleep 1'

# The Files of the run.
if [ -n "${FILES_BIN:-}" ]; then
	guest '[ -f /bin/files.orig ] || cp /bin/files /bin/files.orig' >/dev/null
	put "$FILES_BIN" /bin/files
else
	guest '[ -f /bin/files.orig ] && cp /bin/files.orig /bin/files; true' >/dev/null
fi
guest 'chmod 755 /bin/files' >/dev/null

# zdesktop, then Settings and Files over it.
guest "$stop_all" >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass \$picture ${ZDESKTOP_EXTRA:-} > /tmp/zdesktop.log 2>&1 </dev/null & i=0; while [ ! -S /tmp/wayland-0 ] && [ \$i -lt 60 ]; do sleep 0.5; i=\$((i+1)); done; sleep 1; echo started" >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/settings --timeout-s=500 > /tmp/s.log 2>&1 </dev/null & sleep 6; echo ok" >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/files --timeout-s=500 > /tmp/f.log 2>&1 </dev/null & sleep 8; echo ok" >/dev/null
pointer move 640 790 sleep 600
shot windows.png

# The QWERTY panel from the bottom-left corner over the windows.
pointer move 6 792 sleep 200 down sleep 80 move 78 721 sleep 60 move 150 650 sleep 120 up sleep 900
pointer move 700 200 sleep 500
shot keyboard.png
pointer move 6 792 sleep 200 down sleep 80 move 78 721 sleep 60 move 150 650 sleep 120 up sleep 900

# App Home (the Kei button), then closed.
pointer move 22 16 sleep 150 down sleep 60 up sleep 1500
shot home.png
pointer move 22 16 sleep 150 down sleep 60 up sleep 1200

# Wiseview (a swipe up from the middle of the bottom edge).
pointer move 640 798 sleep 200 down sleep 80 move 640 700 sleep 60 move 640 520 sleep 120 up sleep 1500
shot wiseview.png
guest 'grep -a "KWL GLASS\|KWL OSK open\|KWL WISEVIEW\|KWL HOME" /tmp/zdesktop.log | tail -20' > "$out/$variant-log.txt"
guest "$stop_all" >/dev/null
