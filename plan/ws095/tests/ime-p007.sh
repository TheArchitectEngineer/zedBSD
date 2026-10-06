#!/bin/sh
# ws095-p007: Japanese typed into the Text Editor through the input method, saved and read back, on the Venus guest
# (the lean image with the editor, plan/ws095/tests/build-ime-textedit-image.sh).  zdesktop --glass at 1280x800 starts
# /usr/libexec/keiland-ime; textedit opens /tmp/p007.txt ("Hello" and a newline).  Judged by the editor's and zdesktop's
# logs, the saved file read over SSH and the screens (never the guest's console):
#  1. Ctrl+End, Alt+Space to Japanese, "kanji", Space, Enter: 漢字 committed on the second line.
#  2. "kana" and Esc: the preedit is cancelled (an empty preedit), nothing committed (cancelled.png).
#  3. Alt+Space to direct input, Enter (a new line), Alt+Space, "nihongo", Space, Enter: 日本語 on the third line
#     (two-lines.png).
#  4. Alt+Space to direct input, Ctrl+S: TEXTEDIT SAVE; the file reads "Hello", "漢字", "日本語".
#  5. The editor started again on the file: its lines are drawn as saved (reopened.png), and the file is unchanged.
#
#   plan/ws095/tests/ime-guest.sh start IMAGE     (the guest must be up; GUEST_RUNTIME names its runtime)
#   plan/ws095/tests/ime-p007.sh [OUTDIR]          (default build/ws095-shots/p007)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws095-run}"
export GUEST_RUNTIME
out=${1:-build/ws095-shots/p007}
mkdir -p "$out"
status=0
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 1.2; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
shot() { pointer move 1270 790 sleep 600; check "$out/$1" >/dev/null; echo "shot: $out/$1"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[t]extedit|[k]eiland-ime" | awk "{print \$1}"); do kill -CONT $p 2>/dev/null; kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[t]extedit|[k]eiland-ime" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
stop_editor='for p in $(ps -A -o pid,args | grep "[t]extedit" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -q "[t]extedit" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
start_editor='export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/textedit --timeout-s=800 --width=900 --height=520 /tmp/p007.txt > /tmp/te.log 2>&1 </dev/null & echo started'

# Waits up to 10 s for a log line matching a pattern; fails the run without one.
expect_log() {
	i=0
	while [ $i -lt 20 ]; do
		found=$(guest "grep -cE '$2' $1" | tail -1)
		if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
			echo "log: $2 ok"
			return 0
		fi
		sleep 0.5
		i=$((i+1))
	done
	echo "log: $2 MISSING"
	status=1
}

# Fails the run unless the saved file (read on the host, which knows UTF-8) has the three lines.
expect_file() {
	timeout 60 python3 plan/tools/guest/guest.py get /tmp/p007.txt "$out/p007-$1.txt" >/dev/null 2>&1
	printf 'Hello\n漢字\n日本語\n' > "$out/.expected"
	if cmp -s "$out/.expected" "$out/p007-$1.txt"; then
		echo "file ($1): Hello / 漢字 / 日本語 ok"
		return 0
	fi
	# The same lines without the last newline.
	printf 'Hello\n漢字\n日本語' > "$out/.expected"
	if cmp -s "$out/.expected" "$out/p007-$1.txt"; then
		echo "file ($1): Hello / 漢字 / 日本語 (no last newline) ok"
		return 0
	fi
	echo "file ($1): $(od -c "$out/p007-$1.txt" | head -3) FAIL"
	status=1
}

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp HOME=/root; rm -f /tmp/wayland-0 /root/.config/kei/ime/ja-user.dict
printf "Hello\n" > /tmp/p007.txt
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & echo started' >/dev/null
expect_log /tmp/zdesktop.log 'KEI-IME READY'
guest "$start_editor" >/dev/null
expect_log /tmp/te.log 'TEXTEDIT READY'

# 1. 漢字 on the second line.
keys '<ctrl-end>'
keys '<alt-spc>'
expect_log /tmp/zdesktop.log 'ZWL IME language=ja'
keys 'kanji'
keys ' '
keys '\n'

# 2. A preedit cancelled by Esc.
keys 'kana'
keys '<esc>'
sleep 1
shot cancelled.png

# 3. A new line by direct input, then 日本語.
keys '<alt-spc>'
keys '\n'
keys '<alt-spc>'
keys 'nihongo'
keys ' '
keys '\n'
sleep 1
shot two-lines.png

# 4. Saved by direct input's Ctrl+S.
keys '<alt-spc>'
expect_log /tmp/zdesktop.log 'ZWL IME language=direct'
keys '<ctrl-s>'
expect_log /tmp/te.log 'SAVE path=/tmp/p007.txt'
expect_file saved

# The editor's log, read on the host (the guest's grep takes no UTF-8 pattern): each step's line.
timeout 60 python3 plan/tools/guest/guest.py get /tmp/te.log "$out/te.log" >/dev/null 2>&1
for line in 'TEXT input preedit=かんじ' 'TEXT input commit=漢字' 'TEXT input preedit=かな' 'TEXT input preedit= begin=' \
    'TEXT input preedit=にほんご' 'TEXT input commit=日本語'; do
	if grep -qF "$line" "$out/te.log"; then
		echo "te: $line ok"
	else
		echo "te: $line MISSING"
		status=1
	fi
done
if grep -qF 'TEXT input commit=かな' "$out/te.log"; then
	echo "te: かな committed after Esc FAIL"
	status=1
else
	echo "te: nothing committed by Esc ok"
fi

# 5. The editor again on the saved file.
guest "$stop_editor" >/dev/null
guest "$start_editor" >/dev/null
expect_log /tmp/te.log 'TEXTEDIT READY'
sleep 1
shot reopened.png
expect_file reopened

errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest "$stop_all" >/dev/null
[ $status = 0 ] && echo "ime-p007: PASS" || echo "ime-p007: FAIL"
exit $status
