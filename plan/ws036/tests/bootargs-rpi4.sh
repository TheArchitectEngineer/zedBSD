#!/bin/sh
# ws036-p027: boots a Raspberry Pi 4 image in QEMU raspi4b with command lines
# of the kind the firmware passes (/chosen/bootargs, from cmdline.txt) and
# checks, in the guest, which parameters the kernel took and which root it
# mounted.
#   sh plan/ws036/tests/bootargs-rpi4.sh build/rpi4/hdd-image.img
set -e
IMAGE=${1:?image}
T=plan/ws044/tests/rpi4-serial.sh
TAB=$(printf '\t')
NL=$(printf '\nx')
NL=${NL%x}

echo "== the firmware's Linux line: counted, legacy autoroot"
APPEND="coherent_pool=1M 8250.nr_uarts=0 console=ttyS0,115200 console=tty1 root=/dev/mmcblk0p2 rootfstype=ext4 fsck.repair=yes rootwait quiet" \
	sh $T "$IMAGE" 'dmesg | grep -e "boot: ignored" -e "legacy autoroot"' 'mount | head -n 1'

echo "== tabs and a line end separate parameters"
APPEND="rootwait${TAB}quiet${NL}init=/sbin/init${NL}" \
	sh $T "$IMAGE" 'dmesg | grep -e "boot: ignored" -e "legacy autoroot"'

echo "== rootpart= selects the native root"
APPEND="rootwait quiet rootpart=/dev/mmcblk0p2" \
	sh $T "$IMAGE" 'dmesg | grep -e "boot: ignored" -e "rootpart selector"' 'mount | head -n 1'

echo "== no -append: the DTB's own bootargs (firmware defaults), legacy autoroot"
sh $T "$IMAGE" 'dmesg | grep -e "boot: parameters" -e "legacy autoroot"'
