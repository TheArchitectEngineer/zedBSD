#!/bin/sh
# Switches the iGPU (0000:00:02.0) between the host i915 driver (Venus tests: virtio-gpu + venus
# use the host GPU) and vfio-pci (the i915 passthrough tests).  Runtime only: the boot default
# stays vfio-pci (/etc/modprobe.d/vfio-igd.conf).  usage: igpu-mode.sh host|vfio|show
set -eu
DEV=0000:00:02.0
SYS=/sys/bus/pci/devices/$DEV
current() { basename "$(readlink $SYS/driver 2>/dev/null)" 2>/dev/null || echo none; }
case "${1:-show}" in
host)
	[ "$(current)" = i915 ] && { echo i915; exit 0; }
	pgrep -f ^qemu-system-x86_64 >/dev/null && { echo "a QEMU is running; not switching" >&2; exit 1; }
	[ "$(current)" = none ] || echo $DEV | sudo tee $SYS/driver/unbind >/dev/null
	echo i915 | sudo tee $SYS/driver_override >/dev/null
	sudo modprobe i915
	echo $DEV | sudo tee /sys/bus/pci/drivers_probe >/dev/null
	for i in 1 2 3 4 5 6 7 8 9 10; do [ -e /dev/dri/renderD128 ] && break; sleep 1; done
	# the Venus QEMU runs as this user, who is in neither the render nor the kvm group: runtime ACLs only
	sudo setfacl -m u:$(id -un):rw /dev/dri/renderD128 /dev/kvm ;;
vfio)
	[ "$(current)" = vfio-pci ] && { echo vfio-pci; exit 0; }
	pgrep -f ^qemu-system-x86_64 >/dev/null && { echo "a QEMU is running; not switching" >&2; exit 1; }
	[ "$(current)" = none ] || echo $DEV | sudo tee $SYS/driver/unbind >/dev/null
	echo vfio-pci | sudo tee $SYS/driver_override >/dev/null
	echo $DEV | sudo tee /sys/bus/pci/drivers_probe >/dev/null ;;
show) ;;
*) echo "usage: $0 host|vfio|show" >&2; exit 2 ;;
esac
echo $(current)
