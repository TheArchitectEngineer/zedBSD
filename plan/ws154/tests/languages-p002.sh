#!/bin/sh
# ws154-p002: the input method chosen on the Languages page, on the Venus guest (config-amd64-languages.mk).  zdesktop
# --glass at 1280x800 starts /usr/libexec/keiland-ime with --method=  from the setting ime.method; a change of the
# setting starts it again with the new method at once.  Judged by zdesktop's log (ZWL IME started ... --method=,
# ZWL IME method=, KEI-IME METHOD, ZWL IME language=) and the probe's log, never the guest's console:
#  1. The default: --method=ja, two languages; Alt+Space in the probe: Japanese, "kanji" composes (preedit かんじ).
#  2. ime.method 0 (keiland-settings set): zdesktop starts it again with --method=none, one language; Alt+Space does not
#     leave direct input.
#  3. ime.method 2: --method=skk; Alt+Space: skk; "Kanji" then Space in the probe: the preedit ▼漢字 (SKK's dictionary),
#     Enter puts 漢字 in.
#  4. ime.method 1: --method=ja again.
#  5. Settings' Languages page (settings languages): the three choices drawn (LANGUAGES ... in its log on a click on
#     the SKK switch through QMP), the setting is 2 and zdesktop starts SKK (languages.png).
# PASS: the last line "languages-p002: status=0".
#   plan/tools/files/files-guest.sh start BUILD/hdd-image.img
#   plan/ws154/tests/languages-p002.sh [OUTDIR]          (default build/ws154-shots/p002)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws154-shots/p002}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.7; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py --width 1280 --height 800 "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[i]me-probe|[k]eiland-ime|[s]ettings" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[i]me-probe|[k]eiland-ime" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
env='export XDG_RUNTIME_DIR=/tmp HOME=/root WAYLAND_DISPLAY=wayland-0'
start_desktop="$env; rm -f /tmp/wayland-0 /root/.config/kei/ime/ja-user.dict /root/.config/kei/ime/skk-jisyo; /bin/keiland-settings reset ime.method >/dev/null 2>&1; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass \$picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started"
status=0

# Counts the lines of a guest file that match a pattern (matched on the host, whose grep knows UTF-8).
count() {
	guest "cat $1" > "$out/.count" 2>/dev/null
	n=$(LC_ALL=C.UTF-8 grep -cE "$2" "$out/.count")
	case "$n" in ''|*[!0-9]*) n=0;; esac
	echo "$n"
}
expect_log() {
	want=${3:-1}
	tries=0
	found=0
	while [ $tries -lt 10 ]; do
		found=$(count "$1" "$2")
		[ "$found" -ge "$want" ] && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "$found" -ge "$want" ]; then echo "log: $2 ok ($found)"; else echo "log: $2 MISSING ($found of $want)"; status=1; fi
}
start_probe() {
	log=$1
	guest "$env; /bin/ime-probe --log=$log --app-id=probe > /dev/null 2>&1 </dev/null & echo \$! > $log.pid; sleep 3; echo started" >/dev/null
	expect_log "$log" 'PROBE ENTER'
}
stop_probe() {
	guest "kill \$(cat $1.pid); sleep 1" >/dev/null
}
set_method() {
	guest "$env; /bin/keiland-settings set ime.method $1 > /tmp/set.log 2>&1; echo set" >/dev/null
}

# 1. The default: Japanese.
guest "$stop_all" >/dev/null
guest "$start_desktop" >/dev/null
expect_log /tmp/zdesktop.log 'ZWL IME started pid=[0-9]+ client=[0-9]+ --method=ja'
expect_log /tmp/zdesktop.log 'KEI-IME METHOD 1'
expect_log /tmp/zdesktop.log 'KEI-IME READY languages=2'
start_probe /tmp/p1.log
keys '<alt-spc>'
expect_log /tmp/zdesktop.log 'ZWL IME language=ja'
keys 'kanji'
expect_log /tmp/p1.log 'preedit=かんじ '
keys '<esc>'
stop_probe /tmp/p1.log

# 2. None: started again with direct input alone.
set_method 0
expect_log /tmp/zdesktop.log 'ZWL IME method=0'
expect_log /tmp/zdesktop.log 'ZWL IME started pid=[0-9]+ client=[0-9]+ --method=none'
expect_log /tmp/zdesktop.log 'KEI-IME READY languages=1'
start_probe /tmp/p2.log
keys '<alt-spc>'
sleep 1
ja=$(count /tmp/zdesktop.log 'ZWL IME language=ja')
[ "$ja" -eq 1 ] && echo "none: Alt+Space stays in direct input ok" || { echo "none: Alt+Space chose Japanese ($ja) FAIL"; status=1; }
stop_probe /tmp/p2.log

# 3. SKK: started again with it; a word converted with its dictionary.
set_method 2
expect_log /tmp/zdesktop.log 'ZWL IME started pid=[0-9]+ client=[0-9]+ --method=skk'
expect_log /tmp/zdesktop.log 'KEI-IME METHOD 2'
start_probe /tmp/p3.log
keys '<alt-spc>'
expect_log /tmp/zdesktop.log 'ZWL IME language=skk'
keys 'Kanji'
expect_log /tmp/p3.log 'preedit=▽かんじ '
keys ' '
expect_log /tmp/p3.log 'preedit=▼漢字 '
keys '\n'
expect_log /tmp/p3.log 'commit=漢字'
check "$out/skk.png" >/dev/null
stop_probe /tmp/p3.log

# 4. Japanese again.
set_method 1
expect_log /tmp/zdesktop.log 'ZWL IME started pid=[0-9]+ client=[0-9]+ --method=ja' 2

# 5. The Languages page: its SKK switch (the second choice) chooses SKK.
guest "$env; /bin/settings --timeout-s=300 languages > /tmp/s.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
expect_log /tmp/s.log 'ZSETTINGS CONTROL index=3 '
line=$(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1")
set -- $(echo "$line" | sed -n 's/.* surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2 \3/p') 0 0 0
wx=$2; wy=$3
set -- $(guest "grep -a 'ZSETTINGS CONTROL index=3 ' /tmp/s.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p') 0 0 0 0
cx=$((wx + $1 + $3 / 2)); cy=$((wy + $2 + $4 / 2))
pointer move $((cx - 2)) "$cy" sleep 150 move "$cx" "$cy" sleep 300 down sleep 60 up sleep 1500
expect_log /tmp/s.log 'LANGUAGES ime method=2'
expect_log /tmp/zdesktop.log 'ZWL IME started pid=[0-9]+ client=[0-9]+ --method=skk' 2
pointer move 1270 790 sleep 500
check "$out/languages.png" >/dev/null
echo "shot: $out/languages.png"

expect_none=$(count /tmp/zdesktop.log 'ZWL ERROR')
[ "$expect_none" -eq 0 ] && echo "log: no ZWL ERROR ok" || { echo "log: ZWL ERROR ($expect_none)"; status=1; }
guest "grep -E 'ZWL IME|KEI-IME' /tmp/zdesktop.log" > "$out/zdesktop-ime.txt"
guest "$env; /bin/keiland-settings reset ime.method >/dev/null 2>&1; $stop_all" >/dev/null
echo "languages-p002: status=$status"
exit $status
