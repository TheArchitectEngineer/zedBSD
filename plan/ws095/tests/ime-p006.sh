#!/bin/sh
# ws095-p006: Terminal and the input method on the Venus guest of the Settings image with the input method
# (plan/ws089/tests/config-amd64-settings-ime.mk: the terminal, keiland-ime and its dictionary, the fallback font).
# zdesktop --glass at 1280x800 starts /usr/libexec/keiland-ime; the terminal runs the shell.  Checks:
#  1. The input method follows the terminal (ZWL IME activate client=); the CJK fallback font is open (no ZTERM FONT
#     fallback error).
#  2. Japanese (Alt+Space): "nihongo", Space, Enter commits 日本語 after "echo " (ZTERM IME commit text=日本語); Alt+Space
#     back and Enter: the shell prints 日本語 (shown.png, the line drawn with the fallback font) and, sent to a file,
#     the file reads 日本語.
#  3. A password prompt: `stty -echo; read secret` (the echo off on a whole line, as sudo, su, ssh and passwd have it)
#     turns the text input off (ZTERM IME secret=1, ZWL IME deactivate); Alt+Space to Japanese and "abc" typed reach the
#     shell as keys (no preedit, the file reads abc); `stty echo` turns it on again (ZTERM IME secret=0, ZWL IME activate
#     a second time), and "kana" is composed again (ZTERM IME preedit=かな).
#  No ZWL ERROR in zdesktop's log.
#
#   SETTINGS_CONFIG=plan/ws089/tests/config-amd64-settings-ime.mk plan/ws089/tests/build-settings-image.sh BUILD
#   plan/ws095/tests/ime-p006.sh BUILD/hdd-image.img [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=${1:?usage: ime-p006.sh IMAGE [OUTDIR]}
out=${2:-build/ws095-shots/p006}
mkdir -p "$out"
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws095/p006-run}"
export GUEST_RUNTIME
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.7; }
shot() { python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null 2>&1; echo "shot: $out/$1"; }
status=0

# Counts the lines of a guest file that match a pattern (matched on the host, whose grep knows UTF-8).
count() {
	guest "cat $1" > "$out/.count" 2>/dev/null
	n=$(LC_ALL=C.UTF-8 grep -cE "$2" "$out/.count")
	case "$n" in ''|*[!0-9]*) n=0;; esac
	echo "$n"
}

# Fails the run unless a log has at least a number of lines matching a pattern (within a few seconds).
expect_count() {
	tries=0
	found=0
	while [ $tries -lt 10 ]; do
		found=$(count "$1" "$2")
		[ "$found" -ge "$3" ] && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "$found" -ge "$3" ]; then
		echo "log: $2 (x$3) ok"
	else
		echo "log: $2 (x$3, found $found) MISSING"
		status=1
	fi
}
expect_log() { expect_count "$1" "$2" 1; }

# Fails the run unless a guest file's last line is a text.
expect_file() {
	guest "cat $1" | tail -1 > "$out/.file"
	text=$(cat "$out/.file")
	if [ "$text" = "$2" ]; then
		echo "file: $1 = $2 ok"
	else
		echo "file: $1 = '$text' (expected $2) FAIL"
		status=1
	fi
}

# 0. The guest, zdesktop with the input method, and a terminal.
mkdir -p "$(dirname "$GUEST_RUNTIME")"
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
timeout 180 sh plan/ws035/tests/zdesktop-guest.sh start "$image" >/dev/null 2>&1
sleep 25
guest 'service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[k]eiland-ime|[t]erminal" | awk "{print \$1}"); do kill $p; done; sleep 1; echo stopped' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp HOME=/root; rm -f /tmp/wayland-0 /root/.config/kei/ime/ja-user.dict; : > /root/ja.txt; : > /root/secret.txt; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
expect_log /tmp/zdesktop.log 'KEI-IME READY'
guest 'export XDG_RUNTIME_DIR=/tmp HOME=/root; cd /root; /bin/terminal --token=p006 --timeout-s=600 > /tmp/t.log 2>&1 </dev/null & sleep 6; echo started' >/dev/null
expect_log /tmp/t.log 'ZTERM START run=p006'

# 1. The input method follows the terminal; the fallback font opened.
expect_log /tmp/zdesktop.log 'ZWL IME activate client='
n=$(count /tmp/t.log 'ZTERM FONT fallback')
[ "$n" = 0 ] && echo "font: fallback opened ok" || { echo "font: fallback error FAIL"; status=1; }

# 2. 日本語 composed, committed after "echo ", printed by the shell, and written to a file.
keys 'echo '
keys '<alt-spc>'
expect_log /tmp/zdesktop.log 'ZWL IME language=ja'
keys 'nihongo'
expect_log /tmp/t.log 'ZTERM IME preedit=にほんご'
keys ' '
expect_log /tmp/t.log 'ZTERM IME preedit=日本語'
keys '\n'
expect_log /tmp/t.log 'ZTERM IME commit bytes=9 text=日本語'
keys '<alt-spc>'
expect_log /tmp/zdesktop.log 'ZWL IME language=direct'
keys '\n'
sleep 1
shot shown.png
keys '<up>' ' > /root/ja.txt' '\n'
sleep 2
expect_file /root/ja.txt '日本語'

# 3. A password prompt: the text input off while the echo is off on a whole line.
keys 'stty -echo; read secret; stty echo; echo "$secret" > /root/secret.txt' '\n'
expect_log /tmp/t.log 'ZTERM IME secret=1'
expect_log /tmp/zdesktop.log 'ZWL IME deactivate'
keys '<alt-spc>'
keys 'abc'
sleep 1
shot secret.png
n=$(count /tmp/t.log 'ZTERM IME preedit=あ')
[ "$n" = 0 ] && echo "secret: no preedit ok" || { echo "secret: preedit while the echo is off FAIL"; status=1; }
keys '\n'
sleep 2
expect_file /root/secret.txt 'abc'
expect_log /tmp/t.log 'ZTERM IME secret=0'
expect_count /tmp/zdesktop.log 'ZWL IME activate client=' 2
keys 'kana'
expect_log /tmp/t.log 'ZTERM IME preedit=かな'
sleep 1
shot after-secret.png
keys '<esc>'

guest 'grep -E "ZTERM (IME|START|FONT)" /tmp/t.log' > "$out/t-ime.log"
guest 'grep -E "ZWL (IME|ERROR)|KEI-IME" /tmp/zdesktop.log' > "$out/zdesktop-ime.log"
errors=$(count /tmp/zdesktop.log 'ZWL ERROR')
[ "$errors" = 0 ] && echo "no ZWL ERROR ok" || { echo "ZWL ERROR FAIL"; status=1; }
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
[ $status -eq 0 ] && echo "ime-p006: PASS" || echo "ime-p006: FAIL"
exit $status
