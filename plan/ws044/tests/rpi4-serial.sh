#!/bin/sh
# ws044: boots a Raspberry Pi 4 image in QEMU raspi4b, logs in on the serial
# console and runs each argument as one command, printing its output.  The
# image is the SD card; the kernel beside it (or KERNEL) and the firmware's
# DTB are given to QEMU, which does not run the Pi's GPU firmware.
#   sh plan/ws044/tests/rpi4-serial.sh build/rpi4/hdd-image.img 'uname -a' 'df'
# RUN is the work directory (default build/rpi4-serial-run).  The image is
# opened with snapshot=on unless KEEP=1, so the card is left as it was.
# APPEND, when set, is the kernel command line QEMU puts in the DTB's
# /chosen/bootargs, the way the firmware passes cmdline.txt.
set -e
IMAGE=${1:?image}
shift
KERNEL=${KERNEL:-$(dirname "$IMAGE")/vmunix}
DTB=${DTB:-vendor/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb}
D=${RUN:-build/rpi4-serial-run}
SNAPSHOT=,snapshot=on
test "${KEEP:-0}" = 1 && SNAPSHOT=
mkdir -p "$D"
rm -f "$D/serial.sock"
qemu_start() {
	qemu-system-aarch64 -M raspi4b -m 2G -kernel "$KERNEL" -dtb "$DTB" \
	  -drive "file=$IMAGE,format=raw,if=sd$SNAPSHOT" \
	  -display none -serial "unix:$D/serial.sock,server=on,wait=off" \
	  -serial null -monitor none "$@" > "$D/qemu.log" 2>&1 &
}
if [ -n "${APPEND:-}" ]; then
	qemu_start -append "$APPEND"
else
	qemu_start
fi
echo $! > "$D/qemu.pid"
trap 'kill "$(cat "$D/qemu.pid")" 2>/dev/null || true' EXIT
sleep 2
S="python3 plan/tools/guest/serial.py --socket $D/serial.sock --timeout ${TIMEOUT:-180}"
$S expect 'login: ' > /dev/null
echo "rpi4: login prompt"
$S login
status=0
for command in "$@"; do
	echo "rpi4\$ $command"
	$S run "$command" || status=$?
done
exit $status
