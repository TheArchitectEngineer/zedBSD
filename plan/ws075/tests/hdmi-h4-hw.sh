#!/bin/sh
# ws075-p013 (H4): the demonstration image (plan/ws075/demo/build-demo-image.sh BUILD passthrough) on the 5330's i915
# passthrough, driven step by step from this host: the loader's splash on the firmware's display (the standard VGA
# of the passthrough guest), the greeter and the session on the resident display, a long run with a periodic load,
# and the shutdown from the session.
#
#   plan/ws075/tests/hdmi-h4-hw.sh start IMAGE OUTDIR     takes the machine (flock /tmp/i915-hw.lock, held by a
#                                                         background process until stop), copies the image and the
#                                                         helpers to ~/bigbang/h4/ and starts QEMU (hdmi/h4-qemu.sh)
#   plan/ws075/tests/hdmi-h4-hw.sh ctl ARGS...            one command of hdmi/h4-ctl.py on the 5330 (shot, splash,
#                                                         pointer, keys, hmp, load, quit)
#   plan/ws075/tests/hdmi-h4-hw.sh fetch OUTDIR           copies the logs and the shots, and makes PNG of the shots
#   plan/ws075/tests/hdmi-h4-hw.sh stop OUTDIR            ends QEMU if it still runs, fetches, reads the guest's logs
#                                                         from its disk and gives the machine back
# I915_HOST names the 5330 (default solaris10-man); H4_MINUTES bounds the QEMU run (default 60).
# ws075-p018: start waits for the lock as long as another run holds it (do not bound it with timeout: a start killed
# while waiting leaves the lock to be taken later with no QEMU of its own), and records its OUTDIR in
# /tmp/i915-h4-owner once it has the lock; ctl and fetch refuse unless that run of this tree holds the lock, so they
# never drive or read another agent's QEMU on the shared ~/bigbang/h4 (2026-09-29: pointer steps of a start still
# waiting for the lock went to another agent's run).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
host=${I915_HOST:-solaris10-man}
remote=bigbang/h4
command=${1:-}
[ $# -gt 0 ] && shift
owner=/tmp/i915-h4-owner

# The run of this tree holds the machine: its OUTDIR is the recorded owner and still has the lock.
owns_machine() {
	[ -f "$owner" ] || return 1
	current=$(cat "$owner")
	case "$current" in
	"$(pwd)"/*) ;;
	*) return 1 ;;
	esac
	[ -f "$current/.locked" ]
}

case "$command" in
start)
	image=$1
	out=$2
	mkdir -p "$out"
	[ -f "$out/.running" ] && { echo "hdmi-h4-hw: $out is running"; exit 1; }
	touch "$out/.running"
	# The machine's lock, held by a process of its own until stop removes the marker.
	rm -f "$out/.locked"
	setsid nohup plan/ws075/tests/hdmi/h4-lock.sh "$out" < /dev/null > /dev/null 2>&1 &
	until [ -f "$out/.locked" ]; do sleep 1; done
	(cd "$out" && pwd) > "$owner"
	ssh "$host" bigbang/igpu-mode.sh vfio > /dev/null || { echo "iGPU is not on vfio-pci"; rm -f "$out/.running"; exit 1; }
	ssh "$host" "mkdir -p $remote && sudo -n rm -rf $remote/shots $remote/load.log $remote/splash.log $remote/watch.log"
	scp -q "$image" "$host:$remote/guest.img" || { rm -f "$out/.running"; exit 1; }
	scp -q plan/ws075/tests/hdmi/h4-qemu.sh plan/ws075/tests/hdmi/h4-ctl.py "$host:$remote/" || { rm -f "$out/.running"; exit 1; }
	ssh -n -f "$host" "cd $remote && H4_MINUTES=${H4_MINUTES:-60} setsid nohup bash h4-qemu.sh /home/awe/$remote/guest.img < /dev/null > /dev/null 2>&1 &"
	date +%s > "$out/start.epoch"
	echo "hdmi-h4-hw: QEMU started on $host at $(date '+%H:%M:%S')"
	;;
ctl)
	owns_machine || { echo "hdmi-h4-hw: this tree's run does not hold the machine; ctl refused"; exit 1; }
	ssh "$host" "sudo -n python3 $remote/h4-ctl.py $*"
	;;
fetch)
	out=$1
	owns_machine || { echo "hdmi-h4-hw: this tree's run does not hold the machine; fetch refused"; exit 1; }
	mkdir -p "$out/shots"
	ssh "$host" "sudo -n chown -R awe: $remote/shots $remote/load.log $remote/splash.log $remote/watch.log $remote/c6.log 2>/dev/null; true"
	scp -q "$host:$remote/run.log" "$out/kernel.log"
	scp -q "$host:$remote/serial.log" "$out/serial.log"
	scp -q "$host:$remote/qemu.log" "$out/qemu.log"
	scp -q "$host:$remote/load.log" "$out/load.log" 2>/dev/null
	scp -q "$host:$remote/splash.log" "$out/splash.log" 2>/dev/null
	scp -q "$host:$remote/watch.log" "$out/watch.log" 2>/dev/null
	scp -q "$host:$remote/c6.log" "$out/c6.log" 2>/dev/null
	rsync -a "$host:$remote/shots/" "$out/shots/" 2>/dev/null || scp -qr "$host:$remote/shots/." "$out/shots/"
	python3 plan/ws075/tests/hdmi/h4-png.py "$out/shots"
	;;
stop)
	out=$1
	# A run that never got the machine only gives its place in the queue back; the QEMU there is another run's.
	if ! owns_machine; then
		rm -f "$out/.running"
		echo "hdmi-h4-hw: this tree's run does not hold the machine; only the wait for the lock is ended"
		exit 0
	fi
	ssh "$host" "pgrep -f '^qemu-system-x86_64.*$remote' > /dev/null && sudo -n python3 $remote/h4-ctl.py quit; sleep 2; true"
	"$0" fetch "$out"
	scp -q plan/ws031/tests/ufs-cat.py tools/build/check-ufs-image.py "$host:bigbang/"
	ssh "$host" "python3 bigbang/ufs-cat.py $remote/guest.img /var/log/sessiond.log /var/log/greeter.log /var/log/messages /run/user/0/session.log /run/user/1000/session.log" \
		> "$out/guest-logs.txt" 2>&1
	rm -f "$out/.running"
	while [ -f "$out/.locked" ]; do sleep 1; done
	[ "$(cat "$owner" 2>/dev/null)" = "$(cd "$out" && pwd)" ] && rm -f "$owner"
	echo "hdmi-h4-hw: machine given back"
	;;
*)
	echo "usage: $0 start IMAGE OUTDIR | ctl ARGS... | fetch OUTDIR | stop OUTDIR"
	exit 2
	;;
esac
