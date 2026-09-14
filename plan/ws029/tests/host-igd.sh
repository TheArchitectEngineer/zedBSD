#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# Moves the Latitude 5330 IGD (0000:00:02.0) between the host i915 driver and
# vfio-pci for one zedBSD passthrough attempt, and reports the host state.
#
#   status   print the current state as JSON (no changes)
#   attach   stop GDM, unbind i915, bind vfio-pci
#   restore  unbind vfio-pci, rebind i915, start GDM
#
# Runs as root through "sudo -n sh host-igd.sh <action>". Permitted by the
# user for WS029: GDM stop/start, i915 unbind, vfio-pci bind. Not permitted
# and never done here: reboot, package installation, kernel command line,
# modprobe.d, udev or limits changes, killing other users' processes.
set -eu
device=${IGD_DEVICE:-0000:00:02.0}
sysfs=/sys/bus/pci/devices/$device
action=${1:-status}

status() {
	python3 - "$device" <<'PY'
import json, os, subprocess, sys
device = sys.argv[1]
sysfs = "/sys/bus/pci/devices/" + device
def run(argv):
    try:
        return subprocess.run(argv, capture_output=True, text=True, timeout=20).stdout.strip()
    except Exception as error:
        return "ERROR %s" % error
driver = None
if os.path.islink(sysfs + "/driver"):
    driver = os.path.basename(os.readlink(sysfs + "/driver"))
users = run(["sh", "-c", "fuser -v /dev/dri/* 2>&1 | head -20"])
print(json.dumps({
    "device": device,
    "driver": driver,
    "driver_override": open(sysfs + "/driver_override").read().strip() if os.path.exists(sysfs + "/driver_override") else None,
    "gdm": run(["systemctl", "is-active", "gdm"]),
    "graphical_sessions": [l for l in run(["loginctl", "list-sessions", "--no-legend"]).splitlines() if "seat0" in l],
    "dev_vfio": sorted(os.listdir("/dev/vfio")) if os.path.isdir("/dev/vfio") else [],
    "drm_nodes": sorted(os.listdir("/sys/class/drm")),
    "dri_users": users,
    "qemu_processes": run(["sh", "-c", "pgrep -a qemu-system | head -5"]),
}, indent=2))
PY
}

dri_users() {
	# Lists DRM users other than systemd and logind, which hold card0 permanently.
	fuser -v /dev/dri/* 2>&1 | awk 'NR > 1 && $NF != "systemd" && $NF != "systemd-logind" && $NF != "COMMAND"'
}

wait_for() {
	# wait_for <seconds> <shell test>: polls once per second.
	seconds=$1
	shift
	while [ "$seconds" -gt 0 ]; do
		if sh -c "$*"; then
			return 0
		fi
		seconds=$((seconds - 1))
		sleep 1
	done
	return 1
}

attach() {
	# The DRM-user check is only meaningful when fuser exists; refuse to guess.
	if ! command -v fuser >/dev/null 2>&1 || ! command -v timeout >/dev/null 2>&1; then
		echo "host-igd: fuser and timeout are required" >&2
		exit 10
	fi

	# The display manager and every DRM user must be gone before i915 lets go.
	systemctl stop gdm
	if ! wait_for 15 'test -z "$(fuser -v /dev/dri/* 2>&1 | awk '"'"'NR > 1 && $NF != "systemd" && $NF != "systemd-logind" && $NF != "COMMAND"'"'"')"'; then
		echo "host-igd: /dev/dri still in use after stopping gdm:" >&2
		dri_users >&2 || true
		exit 2
	fi

	modprobe vfio-pci

	# Unbinding can block while the driver tears down; bound the wait.
	if [ "$(basename "$(readlink "$sysfs/driver" 2>/dev/null || echo none)")" = "i915" ]; then
		timeout 120 sh -c "echo $device > /sys/bus/pci/drivers/i915/unbind"
	fi
	if ! wait_for 30 "! test -e /sys/class/drm/card0"; then
		echo "host-igd: card0 did not disappear after unbind" >&2
		exit 3
	fi

	# The override keeps the xe or i915 modules from claiming the device again.
	echo vfio-pci > "$sysfs/driver_override"
	echo "$device" > /sys/bus/pci/drivers_probe
	if ! wait_for 10 "test \"\$(basename \"\$(readlink $sysfs/driver)\")\" = vfio-pci"; then
		echo "host-igd: vfio-pci did not bind" >&2
		exit 4
	fi
	if ! wait_for 10 'test -e /dev/vfio/0'; then
		echo "host-igd: /dev/vfio/0 did not appear" >&2
		exit 5
	fi
	status
}

restore() {
	# No QEMU may still own the device.
	if pgrep -x qemu-system-x86 >/dev/null 2>&1 || pgrep -f 'qemu-system-x86_64.*vfio-pci' >/dev/null 2>&1; then
		echo "host-igd: a QEMU with vfio-pci is still running" >&2
		pgrep -a qemu-system >&2 || true
		exit 6
	fi

	if [ "$(basename "$(readlink "$sysfs/driver" 2>/dev/null || echo none)")" = "vfio-pci" ]; then
		timeout 120 sh -c "echo $device > /sys/bus/pci/drivers/vfio-pci/unbind"
	fi
	echo > "$sysfs/driver_override"
	echo "$device" > /sys/bus/pci/drivers_probe
	if ! wait_for 30 "test \"\$(basename \"\$(readlink $sysfs/driver 2>/dev/null || echo none)\")\" = i915"; then
		echo "host-igd: i915 did not rebind" >&2
		status
		exit 7
	fi
	if ! wait_for 30 'test -e /sys/class/drm/card0'; then
		echo "host-igd: card0 did not reappear" >&2
		status
		exit 8
	fi

	systemctl start gdm
	sleep 15
	if [ "$(systemctl is-active gdm)" != "active" ]; then
		echo "host-igd: gdm is not active after restart" >&2
		status
		exit 9
	fi
	status
}

case $action in
	status) status ;;
	attach) attach ;;
	restore) restore ;;
	*) echo "usage: host-igd.sh status|attach|restore" >&2; exit 1 ;;
esac
