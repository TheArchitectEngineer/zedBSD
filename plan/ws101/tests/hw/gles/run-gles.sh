#!/bin/sh
# ws101-p010: glescompute (Noct's order of platforms, then the default display; every step, the indirect one since the
# executor's vkCmdDispatchIndirect of ws101-p007), then egltest's transform feedback and query scenes in
# the compositor's window (the regression of ws068 egl-p030 and p027).  Each program's lines go to its log on the
# disk, with the compositor's processes after each run; the kernel's messages are kept at the end.
run() {
	log=$1
	shift
	/bin/timeout -s KILL "$@" >> "$log" 2>&1 < /dev/null
	echo "RUN exit=$? $*" >> "$log"
	ps -A -o pid,args | grep '/bin/[w]ayland' >> /var/log/gles-ps.log
	echo "== after $log" >> /var/log/gles-ps.log
	sync
}
ps -A -o pid,args | grep '/bin/[w]ayland' > /var/log/gles-ps.log
echo "== before" >> /var/log/gles-ps.log
run /var/log/gles-auto.log 100 /bin/glescompute --repeat=100
run /var/log/gles-default.log 60 /bin/glescompute --platform=default --repeat=20
for scene in feedback queries; do
	run /var/log/egl-$scene.log 40 /bin/egltest --display=/tmp/wayland-0 --size=640x400 --scene=$scene --frames=60 \
		--delay-ms=30 --token=$scene
done
echo "GLES HW DONE" >> /var/log/gles-ps.log
dmesg > /var/log/dmesg.log 2>&1
sync
