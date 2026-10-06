#!/bin/sh
# ws154-p004: SKK's modes as languages of their own (kana skk, katakana skk-katakana, Latin skk-latin, wide skk-wide),
# remembered for each application by zdesktop (ws095-p016), on the Venus guest (config-amd64-languages.mk).  Judged by
# zdesktop's log (KWL IME language=, KWL IME app ...) and the probes' logs:
#  1. ime.method 2: SKK.  Probe A (app probe-a): Alt+Space chooses skk; q goes to katakana (language=skk-katakana);
#     "ai" puts ア and イ in (one commit each); the system bar shows ア (skk-katakana.png).
#  2. Probe B (app probe-b) starts with the desktop's language (direct), not A's.
#  3. B goes: A's katakana comes back (language=skk-katakana from=remembered); "ka" puts カ in.
#  4. In A: l goes to Latin (skk-latin; the key x goes to the probe, PROBE KEY key=45), C-j back to kana (skk); "a" puts あ in.
# PASS: the last line "languages-p004: status=0".
#   plan/tools/files/files-guest.sh start BUILD/hdd-image.img
#   plan/ws154/tests/languages-p004.sh [OUTDIR]          (default build/ws154-shots/p004)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws154-shots/p004}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.7; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[i]me-probe|[k]eiland-ime" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[i]me-probe|[k]eiland-ime" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
env='export XDG_RUNTIME_DIR=/tmp HOME=/root WAYLAND_DISPLAY=wayland-0'
start_desktop="$env; rm -f /tmp/wayland-0 /root/.config/kei/ime/skk-jisyo; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass \$picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started"
status=0

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
	log=$1; app=$2
	guest "$env; /bin/ime-probe --log=$log --app-id=$app > /dev/null 2>&1 </dev/null & echo \$! > $log.pid; sleep 3; echo started" >/dev/null
	expect_log "$log" 'PROBE ENTER'
}

# 1. SKK, probe A in katakana.
guest "$stop_all" >/dev/null
guest "$start_desktop" >/dev/null
# keiland-settings is a Wayland client of zdesktop: the method is set once the desktop runs, and it starts SKK at once.
guest "$env; /bin/keiland-settings set ime.method 2 > /tmp/set.log 2>&1; echo set" >/dev/null
expect_log /tmp/zdesktop.log 'KWL IME started pid=[0-9]+ client=[0-9]+ --method=skk'
start_probe /tmp/a.log probe-a
keys '<alt-spc>'
expect_log /tmp/zdesktop.log 'KWL IME language=skk$'
keys 'q'
expect_log /tmp/zdesktop.log 'KWL IME language=skk-katakana'
keys 'ai'
# In SKK's katakana (as in its kana) a letter's kana goes in at once: ア and イ are two commits.
expect_log /tmp/a.log 'commit=ア$'
expect_log /tmp/a.log 'commit=イ$'
check "$out/skk-katakana.png" >/dev/null
echo "shot: $out/skk-katakana.png"

# 2. Probe B with the desktop's language.
start_probe /tmp/b.log probe-b
expect_log /tmp/zdesktop.log 'KWL IME app key=app:probe-b language=direct from=inherited'

# 3. B goes: A's katakana comes back.
guest 'kill $(cat /tmp/b.log.pid); sleep 2' >/dev/null
expect_log /tmp/zdesktop.log 'KWL IME app key=app:probe-a language=skk-katakana from=remembered'
keys 'ka'
expect_log /tmp/a.log 'commit=カ'

# 4. Latin and back.
keys 'q'
expect_log /tmp/zdesktop.log 'KWL IME language=skk$' 2
keys 'l'
expect_log /tmp/zdesktop.log 'KWL IME language=skk-latin'
keys 'x'
expect_log /tmp/a.log 'PROBE KEY key=45 state=1'
keys '<ctrl-j>'
expect_log /tmp/zdesktop.log 'KWL IME language=skk$' 3
keys 'a'
expect_log /tmp/a.log 'commit=あ'

errors=$(count /tmp/zdesktop.log 'KWL ERROR')
[ "$errors" -eq 0 ] && echo "log: no KWL ERROR ok" || { echo "log: KWL ERROR ($errors)"; status=1; }
guest "grep -E 'KWL IME|KEI-IME' /tmp/zdesktop.log" > "$out/zdesktop-ime.txt"
guest "$env; /bin/keiland-settings reset ime.method >/dev/null 2>&1; $stop_all" >/dev/null
echo "languages-p004: status=$status"
exit $status
