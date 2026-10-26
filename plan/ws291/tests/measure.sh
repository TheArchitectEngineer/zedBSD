#!/bin/sh
# ws291: the measurements of the RX 5600 XT machine in one command each, run from the host at the top of the tree.
# Debian 13 is the machine's normal system (awe@10.0.10.3); zedBSD starts from the USB stick once with UEFI BootNext
# and gets its address by DHCP on the USB LAN adapter (found by its MAC).  Every step has a time limit; the long logs
# stay in OUT, the command prints only the summary.
#
#     measure.sh where                       which system runs: "zedbsd IP", "linux" or "none"
#     measure.sh zedbsd                      zedBSD running (BootNext from Debian when Debian runs); prints its IP
#     measure.sh linux                       Debian running (zedBSD rebooted: BootOrder brings Debian back)
#     measure.sh deploy [--kernel VMUNIX] [DEST=SRC]...
#                                            files copied into the running zedBSD (the kernel to /boot/boot0/vmunix,
#                                            the old one kept as vmunix.old), then zedBSD restarted (via Debian)
#     measure.sh desktop [--keep] OUT        frame-ops on a fresh session (zedBSD restarted first; --keep: the
#                                            running one, whose open windows change the steps): OUT/table.txt
#                                            (printed), session.log, marks, frame-ops.out, shot.png
#     measure.sh mview OUT [MVIEW-OPTION]... mview in the session (the kei user): its MVIEW lines and the
#                                            compositor's KWL SCANOUT lines of the run (printed), OUT/mview.log
#     measure.sh linux-mview OUT [MVIEW-OPTION]...
#                                            the Linux reference: mview (linux/build-mview.sh, copied to ~/linux-mview)
#                                            on weston's kiosk shell on the DRM backend (seatd, started when not
#                                            running), its MVIEW lines and weston's composition per repaint
#                                            ("plane-only" is the direct scanout) printed, OUT/mview.log
#     measure.sh bench zedbsd|linux OUT [LIST]
#                                            every run of LIST (default bench.list beside this script) on the one
#                                            system, mview copied first (USB_MVIEW, LINUX_MVIEW); OUT/bench.log and
#                                            the table of fps and the frame's stages (printed, OUT/table.txt)
#
# The environment: ZEDBSD_MAC, ZEDBSD_SUBNET (a /24, as 10.0.30), ZEDBSD_BOOT (the UEFI entry), DEBIAN (user@host),
# KEY (the zedBSD root key).  A candidate for Master's Tools (the same steps serve other GPUs' boards).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u

top=$(pwd)
ZEDBSD_MAC=${ZEDBSD_MAC:-6c:1f:f7:1a:08:b6}
ZEDBSD_SUBNET=${ZEDBSD_SUBNET:-10.0.30}
ZEDBSD_BOOT=${ZEDBSD_BOOT:-0005}
DEBIAN=${DEBIAN:-awe@10.0.10.3}
KEY=${KEY:-$top/plan/tmp/guest/id_ed25519}
SSHOPT="-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
SESSION=/run/user/1000/session.log

say() {
	echo "measure: $*" >&2
}

# zs IP COMMAND: a command on zedBSD as root, at most ZS_TIMEOUT seconds.
zs() {
	ip=$1
	shift
	timeout "${ZS_TIMEOUT:-60}" ssh -i "$KEY" $SSHOPT "root@$ip" "$@"
}

zcp() {
	timeout 120 scp -i "$KEY" $SSHOPT "$@"
}

deb() {
	timeout "${ZS_TIMEOUT:-60}" ssh $SSHOPT "$DEBIAN" "$@"
}

# The zedBSD's address from the neighbour table, with a sweep of the subnet when it is not there.
zedbsd_ip() {
	ip=$(ip neigh | awk -v mac="$ZEDBSD_MAC" '$0 ~ mac && $0 !~ /FAILED/ && $1 ~ /^[0-9.]+$/ {print $1; exit}')
	if [ -z "$ip" ]; then
		i=2
		while [ $i -lt 255 ]; do
			ping -c 1 -W 1 "$ZEDBSD_SUBNET.$i" > /dev/null 2>&1 &
			i=$((i + 1))
		done
		wait
		ip=$(ip neigh | awk -v mac="$ZEDBSD_MAC" '$0 ~ mac && $0 !~ /FAILED/ && $1 ~ /^[0-9.]+$/ {print $1; exit}')
	fi
	[ -n "$ip" ] || return 1
	name=$(ZS_TIMEOUT=15 zs "$ip" uname -s 2>/dev/null)
	[ "$name" = zedBSD ] || return 1
	echo "$ip"
}

linux_up() {
	name=$(ZS_TIMEOUT=15 deb uname -s 2>/dev/null)
	[ "$name" = Linux ]
}

where() {
	ip=$(zedbsd_ip) && { echo "zedbsd $ip"; return 0; }
	linux_up && { echo linux; return 0; }
	echo none
}

# wait_for WHAT SECONDS: until the system answers.
wait_for() {
	end=$(($(date +%s) + $2))
	while [ "$(date +%s)" -lt $end ]; do
		if [ "$1" = zedbsd ]; then
			ip=$(zedbsd_ip) && { echo "$ip"; return 0; }
		elif linux_up; then
			return 0
		fi
		sleep 5
	done
	say "$1 did not come up in $2 s"
	return 1
}

to_zedbsd() {
	ip=$(zedbsd_ip) && { echo "$ip"; return 0; }
	linux_up || { say "neither system answers"; return 1; }
	say "Debian: BootNext $ZEDBSD_BOOT, reboot"
	deb "sudo efibootmgr -q -n $ZEDBSD_BOOT && (sleep 1; sudo systemctl reboot) > /dev/null 2>&1 &" || return 1
	sleep 30
	wait_for zedbsd 300 > /dev/null || return 1
	ip=$(zedbsd_ip)
	# The session's compositor starts after the network; wait for its log.
	end=$(($(date +%s) + 120))
	while [ "$(date +%s)" -lt $end ]; do
		zs "$ip" "test -S /run/user/1000/wayland-0 && test -f $SESSION" && break
		sleep 3
	done
	echo "$ip"
}

to_linux() {
	linux_up && return 0
	ip=$(zedbsd_ip) || { say "neither system answers"; return 1; }
	say "zedBSD $ip: reboot"
	ZS_TIMEOUT=10 zs "$ip" "(sleep 1; reboot) > /dev/null 2>&1 &"
	sleep 30
	wait_for linux 300
}

deploy() {
	ip=$(to_zedbsd) || return 1
	if [ "${1:-}" = --kernel ]; then
		zcp "$2" "root@$ip:/tmp/vmunix.new" || return 1
		zs "$ip" "cp /boot/boot0/vmunix /boot/boot0/vmunix.old && cp /tmp/vmunix.new /boot/boot0/vmunix && sync" || return 1
		say "kernel $(sha256sum "$2" | cut -c1-16) put in"
		shift 2
	fi
	for pair in "$@"; do
		dest=${pair%%=*}
		src=${pair#*=}
		zcp "$src" "root@$ip:/tmp/deploy.file" || return 1
		zs "$ip" "cp /tmp/deploy.file '$dest' && sync" || return 1
		say "$dest put in"
	done
	to_linux || return 1
	to_zedbsd
}

desktop() {
	keep=0
	if [ "$1" = --keep ]; then
		keep=1
		shift
	fi
	out=$1
	mkdir -p "$out"
	if [ $keep = 0 ]; then
		to_linux || return 1
	fi
	ip=$(to_zedbsd) || return 1
	zcp plan/tools/frame-ops/frame-ops.sh "root@$ip:/tmp/frame-ops.sh" || return 1
	# The input daemon frame-ops starts keeps no descriptor of the SSH session.
	ZS_TIMEOUT=400 zs "$ip" "sh /tmp/frame-ops.sh $SESSION /tmp/marks 1920 1080 > /tmp/frame-ops.out 2>&1 < /dev/null; keiland-shot /tmp/shot.png > /dev/null 2>&1 < /dev/null"
	zcp "root@$ip:$SESSION" "root@$ip:/tmp/marks" "root@$ip:/tmp/frame-ops.out" "root@$ip:/tmp/shot.png" "$out/" || return 1
	python3 -I plan/tools/frame-ops/frame-times.py "$out/session.log" "$out/marks" > "$out/table.txt" 2>&1
	uname=$(zs "$ip" uname -v)
	echo "zedBSD $ip $uname"
	cat "$out/table.txt"
}

mview() {
	out=$1
	shift
	mkdir -p "$out"
	ip=$(to_zedbsd) || return 1
	# The run's own lines of the compositor's log follow the line count taken before it.
	ZS_TIMEOUT=600 zs "$ip" "n=\$(wc -l < $SESSION); su kei -c 'env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 /bin/mview $*' > /tmp/mview.log 2>&1 < /dev/null; tail -n +\$n $SESSION | grep -E 'KWL SCANOUT' > /tmp/mview-scanout.log"
	zcp "root@$ip:/tmp/mview.log" "root@$ip:/tmp/mview-scanout.log" "$out/" || return 1
	grep -vE 'MVIEW (FRAME|INPUT)' "$out/mview.log"
	head -5 "$out/mview-scanout.log"
}

linux_mview() {
	out=$1
	shift
	mkdir -p "$out"
	to_linux || return 1
	# weston on the display, its DRM backend's log telling where each repaint put the window.
	deb "test -S /run/user/1000/wayland-1 && pgrep -x weston > /dev/null || (export XDG_RUNTIME_DIR=/run/user/1000 LIBSEAT_BACKEND=seatd; nohup weston --backend=drm --shell=kiosk --continue-without-input --socket=wayland-1 --log=/tmp/weston.log --logger-scopes=log,drm-backend > /tmp/weston.out 2>&1 < /dev/null & sleep 5)" || return 1
	ZS_TIMEOUT=600 deb "export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1; n=\$(wc -l < /tmp/weston.log); ~/linux-mview/bin/mview --model=\$HOME/linux-mview/share/mview/qs40 $* > /tmp/mview.log 2>&1 < /dev/null; tail -n +\$n /tmp/weston.log | grep -E 'Using (plane-only|mixed|renderer)' | sed -E 's/^\[[0-9:.]+\][[:space:]]*//' | sort | uniq -c > /tmp/mview-planes.log"
	timeout 60 scp $SSHOPT "$DEBIAN:/tmp/mview.log" "$DEBIAN:/tmp/mview-planes.log" "$out/" || return 1
	grep -vE 'MVIEW (FRAME|INPUT)' "$out/mview.log"
	cat "$out/mview-planes.log"
}

bench() {
	system=$1
	bout=$2
	list=${3:-$(dirname "$0")/bench.list}
	mkdir -p "$bout"
	: > "$bout/bench.log"
	if [ "$system" = zedbsd ]; then
		ip=$(to_zedbsd) || return 1
		zcp "${USB_MVIEW:-build/ws291/usb/bin/mview}" "root@$ip:/tmp/mview" || return 1
		zs "$ip" "cp /tmp/mview /bin/mview" || return 1
	else
		to_linux || return 1
		timeout 120 scp -q -r $SSHOPT "${LINUX_MVIEW:-build/ws291/linux-mview}/." "$DEBIAN:linux-mview/" || return 1
	fi
	grep -v '^#' "$list" | while IFS='|' read -r blabel boptions; do
		[ -n "$blabel" ] || continue
		echo "RUN $blabel $boptions" >> "$bout/bench.log"
		if [ "$system" = zedbsd ]; then
			mview "$bout/$blabel" --token="$blabel" --spin=8 --content=game --present=mailbox $boptions > "$bout/$blabel.txt" 2>&1 < /dev/null
		else
			linux_mview "$bout/$blabel" --token="$blabel" --spin=8 --content=game --present=mailbox $boptions > "$bout/$blabel.txt" 2>&1 < /dev/null
		fi
		cat "$bout/$blabel.txt" >> "$bout/bench.log"
	done
	python3 -I "$(dirname "$0")/mview-table.py" "$bout/bench.log" > "$bout/table.txt"
	cat "$bout/table.txt"
}

command=${1:-}
[ $# -gt 0 ] && shift
case $command in
where) where ;;
zedbsd) to_zedbsd ;;
linux) to_linux && echo linux ;;
deploy) deploy "$@" ;;
desktop) desktop "$@" ;;
mview) mview "$@" ;;
linux-mview) linux_mview "$@" ;;
bench) bench "$@" ;;
*) sed -n '2,33p' "$0"; exit 2 ;;
esac
