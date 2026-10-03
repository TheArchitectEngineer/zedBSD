#!/bin/sh
# ws075-p005: egltest's scenes in a zdesktop window, one after another, in the viewer's place
# (KEILAND_APP=egltest): the OpenGL ES 2 and 3 scenes whose textures, framebuffer objects and formats the
# i915 executor draws natively (not the MRT, multisample, query and transform feedback ones, ws075-p006).  Each
# scene's lines (EGLTEST CHECK run=<scene> failures=N, EGLTEST DONE or FAILED) go to the viewer's log, which
# the run collects; the logs and the kernel's messages are written out every two seconds meanwhile, as
# run-home.sh does, and the Vulkan executor's messages are kept apart (vk.log).
(
	for scene in glsl glsl3 fbo cube es3 formats volumes; do
		/bin/egltest --display=/tmp/wayland-0 --size=640x400 --scene=$scene --frames=150 --delay-ms=30 \
			--token=$scene >> /var/log/mview.log 2>&1 < /dev/null
		sync
	done
	echo "EGLTEST SCENES DONE" >> /var/log/mview.log
	sync
) &
i=0
while [ $i -lt 100 ]; do dmesg > /var/log/dmesg.log 2>&1; grep -E 'i915: vk|gpu: ioctl' /var/log/dmesg.log >> /var/log/vk.log; sync; sleep 2; i=$((i+1)); done
