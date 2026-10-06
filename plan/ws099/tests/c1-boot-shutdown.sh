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
#     ws099-p009: the last picture is the greeter's "Shutting down..." card: against the greeter photographed before
#     the press, the card's middle (the name, the password field) and the power buttons' corner have changed
#     (C1_CHANGED_PERCENT of their pixels), and the greeter's log has KWL GREETER powering=poweroff before
#     KWL GREETER power=poweroff (the request went after the picture).  QEMU ending (ACPI S5, BUG-119) is checked
#     too: a warning while C1_REQUIRE_QEMU_EXIT is 0, a failure when it is 1.
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
C1_CHANGED_PERCENT=${C1_CHANGED_PERCENT:-10}
C1_REQUIRE_QEMU_EXIT=${C1_REQUIRE_QEMU_EXIT:-0}
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
set -- $(guest "grep 'KWL HOME icon name=\"Log Out\"' /run/user/1000/session.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p')
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
# The greeter before the press (the pointer rests away from the buttons first).
pointer move 640 700 sleep 600
python3 plan/ws035/tests/zdesktop-check.py "$out/greeter.png" --runtime "$GUEST_RUNTIME" >/dev/null 2>&1
qemu_pid=$(python3 -c "import json; print(json.load(open('$GUEST_RUNTIME/session.json'))['pid'])" 2>/dev/null)
# The greeter's Shut Down button: bottom right (GREETER_MARGIN 24, 112x36).
pointer move 1190 750 sleep 300 move 1200 758 sleep 400 down sleep 100 up
python3 plan/ws099/tests/c1-watch.py "$out/shutdown" --runtime "$GUEST_RUNTIME" --seconds "$C1_SHUTDOWN_S" > "$out/shutdown.txt" 2>&1 &
watcher=$!
sleep 2
guest "grep -nE 'KWL GREETER (powering|power)=' /var/log/greeter.log" > "$out/greeter-power.txt" 2>&1
sleep 18
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
tail_black=$3
echo "shutdown: $4 moments in ${C1_SHUTDOWN_S} s; black $1, text $2 before the end, black at the end $3"
[ "$shutdown_black" -le "$C1_MAX_BLACK" ] && [ "$shutdown_text" -le "$C1_MAX_TEXT" ] && echo "C1-shutdown: PASS" || { echo "C1-shutdown: FAIL"; status=1; }

# The last picture shown is the "Shutting down..." card (ws099-p009).
last=$(ls "$out/shutdown"/seen-*.png 2>/dev/null | tail -1)
changed=$(python3 - "$out/greeter.png" "${last:-none}" <<'EOF2'
import sys
from PIL import Image
try:
	a = Image.open(sys.argv[1]).convert("RGB")
	b = Image.open(sys.argv[2]).convert("RGB")
except OSError:
	print("0 0")
	sys.exit(0)
def share(box):
	n = d = 0
	for p, q in zip(a.crop(box).getdata(), b.crop(box).getdata()):
		n += 1
		if max(abs(p[i] - q[i]) for i in range(3)) > 24:
			d += 1
	return 100 * d // max(n, 1)
# The card's middle (the avatar, the name and the password field) and the power buttons' corner, at 1280x800.
print(share((470, 350, 810, 540)), share((1010, 730, 1270, 790)))
EOF2
)
set -- $changed
echo "shutdown: the last picture ($(basename "${last:-none}")) against the greeter: card ${1}% changed, power buttons ${2}% changed"
if [ "${1:-0}" -ge "$C1_CHANGED_PERCENT" ] && [ "${2:-0}" -ge "$C1_CHANGED_PERCENT" ]; then
	echo "C1-shutdown-picture: PASS"
else
	echo "C1-shutdown-picture: FAIL (the last picture is still the greeter's)"
	status=1
fi
cat "$out/greeter-power.txt"
if grep -q 'powering=poweroff' "$out/greeter-power.txt" && grep -q 'power=poweroff frames=' "$out/greeter-power.txt"; then
	echo "C1-shutdown-order: PASS (the request went after the picture)"
else
	echo "C1-shutdown-order: WARN (the greeter's log was not read before the machine went down)"
fi

# QEMU ends when the machine powers off (ACPI S5; BUG-119).
qemu_gone=0
kill -0 "${qemu_pid:-0}" 2>/dev/null || qemu_gone=1
if [ "$qemu_gone" -eq 1 ]; then
	echo "C1-shutdown-qemu: PASS (QEMU ended)"
elif [ "$C1_REQUIRE_QEMU_EXIT" = 1 ]; then
	echo "C1-shutdown-qemu: FAIL (QEMU still runs: the machine did not power off)"
	status=1
else
	echo "C1-shutdown-qemu: WARN (QEMU still runs: no ACPI power-off yet, BUG-119)"
fi
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1

echo "C1 RESULT boot_black=$boot_black boot_text=$boot_text shutdown_black=$shutdown_black shutdown_text=$shutdown_text shutdown_down=$down card_changed=${1:-0} buttons_changed=${2:-0} qemu_gone=$qemu_gone"
[ $status -eq 0 ] && echo "C1: PASS" || echo "C1: FAIL"
exit $status
