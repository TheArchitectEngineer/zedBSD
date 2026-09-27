#!/bin/sh
# ws075-p006: egltest's OpenGL ES 3 MRT, blit, query and transform feedback scenes in a zdesktop window, one
# after another, in the viewer's place (ZDESKTOP_APP=egltest6; run-egltest.sh has p005's scenes).  Each
# scene's lines (EGLTEST CHECK run=<scene> failures=N, EGLTEST DONE or FAILED) go to the viewer's log, which
# the run collects; the logs and the kernel's messages are written out every two seconds meanwhile, as
# run-home.sh does, and the Vulkan executor's messages are kept apart (vk.log).
(
	for scene in targets blits queries feedback; do
		/bin/egltest --display=/tmp/wayland-0 --size=640x400 --scene=$scene --frames=150 --delay-ms=30 \
			--token=$scene >> /var/log/mview.log 2>&1 < /dev/null
		sync
	done
	echo "EGLTEST SCENES DONE" >> /var/log/mview.log
	sync
) &
i=0
while [ $i -lt 100 ]; do dmesg > /var/log/dmesg.log 2>&1; grep -E 'i915: vk|gpu: ioctl' /var/log/dmesg.log >> /var/log/vk.log; sync; sleep 2; i=$((i+1)); done
