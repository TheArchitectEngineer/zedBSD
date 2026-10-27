#!/bin/sh
# ws305: the GPU passthrough VM on the desktop (10.0.10.3), driven from the build host at the top of the tree.  The
# host boots Debian (amdgpu, the Linux reference), "Debian VFIO" (the GPU for the VM) or zedBSD from the USB stick
# (BootNext, plan/ws291/tests/measure.sh); plan/ws305/phase001/phase.md has the design and the reset steps R1-R4.
#
#     vfio.sh mode                 which system runs: "vfio", "debian" or "down"
#     vfio.sh setup                once: qemu and OVMF installed, the "Debian VFIO" grub entry (41_zedbsd_vfio) and
#                                  GRUB_RECORDFAIL_TIMEOUT=5 (the files before kept as *.ws305-orig), update-grub
#     vfio.sh enter                Debian rebooted once into "Debian VFIO" (grub-reboot); waits for it
#     vfio.sh leave                Debian rebooted into its default entry; waits for it
#     vfio.sh push IMAGE           the guest image to the host (~/ws305/hdd-image.img)
#     vfio.sh status|bind|stop     the host half (vfio-host.py): the GPU's functions, drivers, group; the VM
#     vfio.sh start [--rom FILE]   the VM started from a fresh copy of the image; waits for the guest's SSH
#     vfio.sh restart              stop, then start (R1: the guest resets the GPU itself, amdrdna.reset=1)
#     vfio.sh reset                QMP system_reset (the guest rebooted inside the same QEMU); waits for SSH
#     vfio.sh kill                 QEMU killed without a word to the guest (the hung guest's case)
#     vfio.sh qmp COMMAND [JSON]   one QMP command
#     vfio.sh reset-method [M]     the GPU's reset_method on the host, shown or set ("" none, "default")
#     vfio.sh rom FILE             the GPU's video BIOS read on the host into ~/ws305/FILE
#     vfio.sh wait                 waits for the guest's SSH (at most 240 s)
#     vfio.sh ssh COMMAND...       a command on the guest as root (one SSH connection used again)
#     vfio.sh gpu                  the guest's amdrdna lines (dmesg) and whether the session runs
#     vfio.sh shot OUT             the guest's screen (keiland-shot inside the guest) into OUT/shot.png
#
# The machine is shared with the bare zedBSD work: every command takes /tmp/zedbsd-desktop.lock on the build host
# (WS305_LOCKED=1 when the caller holds it already).  Environment: DEBIAN (user@host), KEY (the guest's root key).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u

if [ "${WS305_LOCKED:-}" != 1 ]; then
	WS305_LOCKED=1 exec flock -w "${WS305_LOCK_WAIT:-60}" /tmp/zedbsd-desktop.lock "$0" "$@"
fi

top=$(pwd)
here=$(dirname -- "$0")
DEBIAN=${DEBIAN:-awe@10.0.10.3}
KEY=${KEY:-$top/plan/tmp/guest/id_ed25519}
SSH_PORT=${WS305_SSH_PORT:-10322}
SSHOPT="-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
mkdir -p "$top/build/tmp"
GSSHOPT="$SSHOPT -o ProxyJump=$DEBIAN -o ControlMaster=auto -o ControlPath=$top/build/tmp/ws305-%p -o ControlPersist=600"

say() {
	echo "vfio: $*" >&2
}

deb() {
	timeout "${DEB_TIMEOUT:-60}" ssh $SSHOPT "$DEBIAN" "$@"
}

# The host half, copied each time (it is small) and run as root.
hostpy() {
	timeout 30 scp -q $SSHOPT "$here/vfio-host.py" "$DEBIAN:ws305/vfio-host.py" || return 1
	deb "sudo python3 -I ws305/vfio-host.py $*"
}

guest() {
	timeout "${GUEST_TIMEOUT:-60}" ssh -i "$KEY" $GSSHOPT -p "$SSH_PORT" root@127.0.0.1 "$@"
}

mode() {
	line=$(DEB_TIMEOUT=15 deb cat /proc/cmdline 2>/dev/null) || { echo down; return 0; }
	case $line in
	*zedbsd.vfio=1*) echo vfio ;;
	*) echo debian ;;
	esac
}

# wait_mode MODE SECONDS: the host down first (when it was up), then up in MODE.
wait_mode() {
	want=$1
	limit=$2
	start=$(date +%s)
	while [ $(($(date +%s) - start)) -lt "$limit" ]; do
		now=$(mode)
		[ "$now" = "$want" ] && { say "host: $now after $(($(date +%s) - start)) s"; return 0; }
		sleep 5
	done
	say "host: not $want within $limit s (now $(mode))"
	return 1
}

wait_guest() {
	start=$(date +%s)
	while [ $(($(date +%s) - start)) -lt 240 ]; do
		if GUEST_TIMEOUT=15 guest true 2> /dev/null; then
			say "guest: SSH after $(($(date +%s) - start)) s"
			return 0
		fi
		sleep 5
	done
	say "guest: no SSH within 240 s"
	return 1
}

setup() {
	deb "mkdir -p ws305" || return 1
	timeout 30 scp -q $SSHOPT "$here/41_zedbsd_vfio" "$DEBIAN:ws305/41_zedbsd_vfio" || return 1
	DEB_TIMEOUT=900 deb 'set -e
command -v qemu-system-x86_64 > /dev/null && [ -f /usr/share/OVMF/OVMF_CODE_4M.fd ] ||
	sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y qemu-system-x86 ovmf
[ -f /etc/default/grub.ws305-orig ] || sudo cp -p /etc/default/grub /etc/default/grub.ws305-orig
grep -q "^GRUB_RECORDFAIL_TIMEOUT=" /etc/default/grub ||
	printf "\n# ws305: a boot cut short (the remote power plug) does not leave the menu waiting\nGRUB_RECORDFAIL_TIMEOUT=5\n" |
	sudo tee -a /etc/default/grub > /dev/null
sudo install -m 755 ws305/41_zedbsd_vfio /etc/grub.d/41_zedbsd_vfio
sudo update-grub
grep -n "^menuentry\|zedbsd-vfio\|recordfail" /boot/grub/grub.cfg | head -20
diff /etc/default/grub.ws305-orig /etc/default/grub || true'
}

action=${1:-}
[ $# -gt 0 ] && shift
case $action in
mode) mode ;;
setup) setup ;;
enter)
	deb "sudo grub-reboot zedbsd-vfio && sudo systemctl reboot" || true
	sleep 20
	wait_mode vfio 300
	;;
leave)
	deb "sudo systemctl reboot" || true
	sleep 20
	wait_mode debian 300
	;;
push)
	deb "mkdir -p ws305" || exit 1
	timeout 900 scp -q $SSHOPT "$1" "$DEBIAN:ws305/hdd-image.img"
	;;
status|bind|stop|qmp|reset-method)
	hostpy "$action" "$@"
	;;
rom)
	hostpy rom "/home/${DEBIAN%@*}/ws305/$1"
	;;
start)
	hostpy start "$@" && wait_guest
	;;
restart)
	hostpy stop && hostpy start && wait_guest
	;;
reset)
	hostpy qmp system_reset && sleep 10 && wait_guest
	;;
kill)
	deb 'pid=$(cat ws305/run/qemu.pid) && sudo kill -9 "$pid" && echo killed "$pid"'
	;;
wait) wait_guest ;;
ssh) guest "$@" ;;
gpu)
	guest 'dmesg | grep -E "amdrdna|A[0-9][a-z]* (ok|fail)|reset:" | tail -n 40; ls -l /run/user/1000/wayland-0 2>&1'
	;;
shot)
	out=$1
	mkdir -p "$out"
	guest '[ -S /run/aat-input.sock ] || { aat-input start --width 1920 --height 1080 > /dev/null 2>&1; sleep 3; }
aat-input move-to 960 600 > /dev/null 2>&1
aat-input move-to 962 602 > /dev/null 2>&1
sleep 1
env XDG_RUNTIME_DIR=/run/user/1000 keiland-shot /tmp/shot.png > /dev/null 2>&1 < /dev/null' || exit 1
	guest 'cat /tmp/shot.png' > "$out/shot.png" || exit 1
	ls -l "$out/shot.png"
	;;
*) sed -n '2,31p' "$0"; exit 2 ;;
esac
