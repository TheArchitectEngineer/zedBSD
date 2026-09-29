#!/bin/sh
# ws099-p004, C1: the boot's and the Shut Down's hand-overs show no black picture and no text console, on the Venus
# guest of the criteria image (kei logged in by itself at boot).  plan/ws099/tests/c1-watch.py photographs both
# displays (the standard VGA: the boot logo or the text console; Venus: the compositor) several times a second, and
# takes the one a person would see (Venus once it is not black, else the VGA).
#  1. Boot: the guest started afresh and watched from the emulator's start until kei's desktop (SESSIOND HANDOFF go)
#     plus C1_SETTLE_S.  From the first picture (the boot logo) on, no seen moment may be black or the text console
#     (C1_MAX_BLACK, C1_MAX_TEXT); the firmware's moments before it are counted apart.
#  2. Shut Down: App Home's Log Out to the greeter, then the greeter's Shut Down, watched for C1_SHUTDOWN_S seconds (the
#     guest's poweroff stops the machine but QEMU stays, keeping the last picture).  The machine must go down (its SSH
#     stops answering); no seen moment may be black or the text console (the last black ones are reported apart as the
#     end's: a machine that goes dark when it is off).
# Prints "C1 RESULT boot_black=N boot_text=M shutdown_black=N shutdown_text=M" and "C1-boot: PASS|FAIL".
#
#   plan/ws099/tests/c1-boot-shutdown.sh IMAGE [OUTDIR]     (starts and stops the guest itself)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws035-sq-run}"
export GUEST_RUNTIME
image=${1:?usage: c1-boot-shutdown.sh IMAGE [OUTDIR]}
out=${2:-build/ws099-shots/c1-boot}
C1_MAX_BLACK=${C1_MAX_BLACK:-0}
C1_MAX_TEXT=${C1_MAX_TEXT:-0}
C1_SETTLE_S=${C1_SETTLE_S:-3}
C1_SHUTDOWN_S=${C1_SHUTDOWN_S:-40}
mkdir -p "$out"
guest() { timeout 60 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py --width 1280 --height 800 "$GUEST_RUNTIME/qmp.sock" "$@"; }
status=0

# Counts a watch's seen moments: black and text, from the first picture on, and the ones before it.
count_seen() {
	awk '
		/seen=/ { split($0, a, "seen="); k = a[2]
			if (!started && k == "picture") started = 1
			if (!started) { before[k]++; next }
			after[k]++; last = k }
		END { printf "%d %d %d %d\n", after["black"] + 0, after["text"] + 0, before["black"] + 0, before["text"] + 0 }' "$1"
}

# 1. Boot, watched from the emulator's start.
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
rm -f "$out/boot.stop"
env -u VENUS_SIZE timeout 180 sh plan/ws035/tests/zdesktop-guest.sh start "$image" >/dev/null 2>&1
python3 plan/ws099/tests/c1-watch.py "$out/boot" --runtime "$GUEST_RUNTIME" --seconds 150 --stop "$out/boot.stop" > "$out/boot.txt" 2>&1 &
watcher=$!
tries=0
while [ $tries -lt 60 ]; do
	found=$(guest "grep -c 'SESSIOND HANDOFF go written=3' /var/log/sessiond.log" | tail -1)
	[ "${found:-0}" -ge 1 ] 2>/dev/null && break
	tries=$((tries + 1))
	sleep 2
done
[ "${found:-0}" -ge 1 ] 2>/dev/null || { echo "boot: kei's desktop not reached"; status=1; }
sleep "$C1_SETTLE_S"
touch "$out/boot.stop"
wait $watcher
set -- $(count_seen "$out/boot.txt")
boot_black=$1 boot_text=$2
echo "boot: $(grep -c 'seen=' "$out/boot.txt") moments; after the first picture: black $1, text $2; before it (firmware): black $3, text $4"
[ "$boot_black" -le "$C1_MAX_BLACK" ] && [ "$boot_text" -le "$C1_MAX_TEXT" ] && echo "C1-boot: PASS" || { echo "C1-boot: FAIL"; status=1; }

# 2. Log Out to the greeter (App Home's Log Out, as ws035-p126), then Shut Down at the greeter.
sleep 3
pointer move 23 17 sleep 300 down sleep 60 up sleep 1500
set -- $(guest "grep 'ZWL HOME icon name=\"Log Out\"' /run/user/1000/session.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p')
if [ -z "${1:-}" ]; then
	echo "shutdown: no Log Out icon"
	echo "C1 RESULT boot_black=$boot_black boot_text=$boot_text shutdown_black=? shutdown_text=?"
	echo "C1: FAIL"
	sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
	exit 1
fi
pointer move "$1" "$2" sleep 400 down sleep 60 up
tries=0
while [ $tries -lt 20 ]; do
	found=$(guest "grep -c 'SESSIOND GREETER adopt pid=' /var/log/sessiond.log" | tail -1)
	[ "${found:-0}" -ge 1 ] 2>/dev/null && break
	tries=$((tries + 1))
	sleep 1
done
sleep 3
# The greeter's Shut Down button: bottom right (GREETER_MARGIN 24, 112x36).
pointer move 1190 750 sleep 300 move 1200 758 sleep 400 down sleep 100 up
python3 plan/ws099/tests/c1-watch.py "$out/shutdown" --runtime "$GUEST_RUNTIME" --seconds "$C1_SHUTDOWN_S" > "$out/shutdown.txt" 2>&1 &
watcher=$!
sleep 20
down=0
answer=$(guest 'echo alive' | tail -1)
[ "$answer" = alive ] || down=1
wait $watcher
echo "shutdown: the machine went down (SSH no longer answers): $down"
[ "$down" -eq 1 ] || status=1
set -- $(awk '/seen=/ { split($0, a, "seen="); k[++n] = a[2] }
	END { tail = 0; while (n - tail > 0 && k[n - tail] == "black") tail++
	      for (i = 1; i <= n - tail; i++) { if (k[i] == "black") b++; if (k[i] == "text") t++ }
	      printf "%d %d %d %d\n", b + 0, t + 0, tail, n }' "$out/shutdown.txt")
shutdown_black=$1 shutdown_text=$2
echo "shutdown: $4 moments in ${C1_SHUTDOWN_S} s; black $1, text $2 before the end, black at the end $3"
[ "$shutdown_black" -le "$C1_MAX_BLACK" ] && [ "$shutdown_text" -le "$C1_MAX_TEXT" ] && echo "C1-shutdown: PASS" || { echo "C1-shutdown: FAIL"; status=1; }
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1

echo "C1 RESULT boot_black=$boot_black boot_text=$boot_text shutdown_black=$shutdown_black shutdown_text=$shutdown_text shutdown_down=$down"
[ $status -eq 0 ] && echo "C1: PASS" || echo "C1: FAIL"
exit $status
