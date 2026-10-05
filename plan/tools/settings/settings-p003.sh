#!/bin/sh
# ws135-p002/p003: the compositor's settings and libkeiland's kl_settings_* on the Venus guest of the Settings image
# with the probe (SETTINGS_CONFIG=plan/tools/settings/config-amd64-settings.mk plan/ws089/tests/build-settings-image.sh BUILD).
# zdesktop --glass at 1280x800 as root (HOME=/root).  Judged by the probe's and zdesktop's logs and desktop.conf:
#  1. desktop.conf seeded (pointer.speed=50, my.note=hello): the probe's dump has pointer.speed=50 flags=0 and
#     window.opacity flags=1 (the default); terminal.ambiguous-wide without --app is ENOTSUP (error=ENOTSUP).
#  2. A watching probe (all keys) runs while a second probe sets pointer.speed 120 (result error=0), 500 (EINVAL, the
#     range), sound.available 1 (EPERM, read only), no.key (ENOENT), keyboard.repeat.rate 40 (error=0), a FIFO as
#     the wallpaper (result EINVAL) and a generated wallpaper (result error=0).  The watcher hears pointer.speed 120,
#     keyboard.repeat.rate 40 and the wallpaper; zdesktop applies them (ZWL PREFERENCES key=... applied).
#  3. desktop.conf is not written during the session (its checksum unchanged).
#  4. zdesktop stopped by SIGTERM: desktop.conf has pointer.speed=120, keyboard.repeat.rate=40, the wallpaper, and
#     my.note=hello still.
#  5. zdesktop started again: the probe reads pointer.speed=120.
#  6. --app=terminal: terminal.ambiguous-wide set 1 (result error=0) and read back; /root/.config/keiland/terminal.conf
#     has ambiguous-wide=1.
#  7. No ZWL ERROR in zdesktop's logs.
#
#   plan/ws089/tests/settings-guest.sh start IMAGE    (the guest must be up)
#   plan/tools/settings/settings-p003.sh [OUTDIR]        (default build/ws135-shots/p003)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws089-run}"
export GUEST_RUNTIME
out=${1:-build/ws135-shots/p003}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
env='export XDG_RUNTIME_DIR=/tmp HOME=/root'
conf=/root/.config/keiland/desktop.conf
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[k]eiland-settings" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
start_desktop="$env; rm -f /tmp/wayland-0; /bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started"
status=0

# Checks that a guest file has a line matching a pattern.
expect() {
	found=$(guest "grep -cE '$2' $1" | tail -1)
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
		echo "ok: $3"
	else
		echo "FAIL: $3 ($2 not in $1)"
		status=1
	fi
}

# 0. A fresh desktop with a seeded file.
guest "$stop_all; mkdir -p /root/.config/keiland; rm -f /root/.config/keiland/terminal.conf; printf 'pointer.speed=50\nmy.note=hello\n' > $conf; mkfifo /tmp/settings-fifo 2>/dev/null; echo ready" >/dev/null
guest "$start_desktop" >/dev/null
expect /tmp/zdesktop.log 'ZWL SETTINGS open present=1' 'zdesktop opened the settings'

# 1. The first state.
guest "$env; /bin/keiland-settings dump > /tmp/p1.log 2>&1; echo dumped" >/dev/null
guest 'cat /tmp/p1.log' > "$out/dump.log"
expect /tmp/p1.log 'value key=pointer.speed value=50 flags=0' 'dump: pointer.speed 50 from the file'
expect /tmp/p1.log 'value key=window.opacity value=[0-9]+ flags=1' 'dump: window.opacity at its default'
expect /tmp/p1.log 'value key=terminal.ambiguous-wide error=ENOTSUP' 'dump: an application key without --app is ENOTSUP'
sum_before=$(guest "cksum $conf" | tail -1)

# 2. A watcher, and the changes.
guest "$env; /bin/keiland-settings --timeout-ms=12000 watch '' > /tmp/watch.log 2>&1 </dev/null & sleep 2; echo watching" >/dev/null
picture=$(guest 'ls /usr/share/keiland/wallpapers/*.png | head -1' | tail -1)
guest "$env; /bin/keiland-settings --timeout-ms=6000 set pointer.speed 120 set pointer.speed 500 set sound.available 1 set no.key 1 set keyboard.repeat.rate 40 set wallpaper /tmp/settings-fifo set wallpaper $picture > /tmp/p2.log 2>&1; echo set" >/dev/null
sleep 8
guest 'cat /tmp/p2.log' > "$out/set.log"
guest 'cat /tmp/watch.log' > "$out/watch.log"
expect /tmp/p2.log 'set key=pointer.speed value=120 error=0' 'set: pointer.speed 120 asked'
expect /tmp/p2.log 'result request=1 error=0' 'set: pointer.speed 120 applied'
expect /tmp/p2.log 'set key=pointer.speed value=500 error=EINVAL' 'set: 500 is outside the range'
expect /tmp/p2.log 'set key=sound.available value=1 error=EPERM' 'set: sound.available is read only'
expect /tmp/p2.log 'set key=no.key value=1 error=ENOENT' 'set: an unknown key'
expect /tmp/p2.log 'result request=2 error=0' 'set: keyboard.repeat.rate 40 applied'
expect /tmp/p2.log 'result request=3 error=EINVAL' 'set: a FIFO wallpaper is refused'
expect /tmp/p2.log 'result request=4 error=0' 'set: a wallpaper applied'
expect /tmp/watch.log 'change key=pointer.speed value=120 flags=0' 'watch: another process hears pointer.speed'
expect /tmp/watch.log 'change key=keyboard.repeat.rate value=40 flags=0' 'watch: keyboard.repeat.rate'
expect /tmp/watch.log 'change key=wallpaper value=/usr/share/keiland/wallpapers/' 'watch: the wallpaper'
expect /tmp/zdesktop.log 'ZWL PREFERENCES key=pointer.speed applied value=120' 'zdesktop applied pointer.speed'
expect /tmp/zdesktop.log 'ZWL PREFERENCES key=keyboard.repeat.rate applied value=40' 'zdesktop applied the repeat'
expect /tmp/zdesktop.log 'ZWL GLASS wallpaper path=/usr/share/keiland/wallpapers/' 'zdesktop showed the wallpaper'

# 3. Not written during the session.
sum_during=$(guest "cksum $conf" | tail -1)
[ "$sum_before" = "$sum_during" ] && echo "ok: desktop.conf not written during the session" || { echo "FAIL: desktop.conf changed during the session"; status=1; }

# 4. Written at the end (SIGTERM).
guest "pid=\$(ps -A -o pid,args | grep -E '[w]ayland( |\$)' | awk '{print \$1}'); kill -TERM \$pid; i=0; while ps -A -o args | grep -qE '[w]ayland( |\$)' && [ \$i -lt 50 ]; do sleep 0.2; i=\$((i+1)); done; echo stopped" >/dev/null
guest "cat $conf" > "$out/desktop.conf"
expect $conf '^pointer.speed=120$' 'end: pointer.speed=120 written'
expect $conf '^keyboard.repeat.rate=40$' 'end: keyboard.repeat.rate=40 written'
expect $conf '^wallpaper=/usr/share/keiland/wallpapers/' 'end: the wallpaper written'
expect $conf '^my.note=hello$' 'end: the unknown key kept'
expect /tmp/zdesktop.log 'ZWL SETTINGS saved error=0' 'end: zdesktop saved'
guest 'cp /tmp/zdesktop.log /tmp/zdesktop-1.log' >/dev/null

# 5. The next session reads it.
guest "$start_desktop" >/dev/null
guest "$env; /bin/keiland-settings get pointer.speed > /tmp/p5.log 2>&1; echo got" >/dev/null
expect /tmp/p5.log 'value key=pointer.speed value=120 flags=0' 'next session: pointer.speed 120'

# 6. An application's own key.
guest "$env; /bin/keiland-settings --app=terminal set terminal.ambiguous-wide 1 get terminal.ambiguous-wide > /tmp/p6.log 2>&1; echo app" >/dev/null
guest 'cat /tmp/p6.log' > "$out/app.log"
expect /tmp/p6.log 'result request=1 error=0' 'app: set'
expect /tmp/p6.log 'value key=terminal.ambiguous-wide value=1 flags=0' 'app: read back'
expect /root/.config/keiland/terminal.conf '^ambiguous-wide=1$' 'app: terminal.conf written'

# 7. No error.
errors=$(guest "cat /tmp/zdesktop-1.log /tmp/zdesktop.log | grep -c 'ZWL ERROR'" | tail -1)
[ "${errors:-1}" = 0 ] && echo "ok: no ZWL ERROR" || { echo "FAIL: ZWL ERROR lines ($errors)"; status=1; }
guest 'grep -E "ZWL (SETTINGS|PREFERENCES|GLASS wallpaper)" /tmp/zdesktop-1.log' > "$out/zdesktop-settings.log"
guest "$stop_all; rm -f $conf /root/.config/keiland/terminal.conf /tmp/settings-fifo; echo clean" >/dev/null

[ $status = 0 ] && echo "settings-p003: PASS" || echo "settings-p003: FAIL"
exit $status
