#!/bin/sh
# WS035 p069: nothing in the viewer's place; App Home starts the applications.  The logs and the kernel's
# messages (and the X server's log, when App Home started it, WS035 p070, WS069 p008) are written out every two seconds for 200
# seconds, then the guest goes on to power off.  The Vulkan executor's messages are kept apart (vk.log, with
# repeats), as the kernel's buffer keeps only the latest (WS068 p006).
i=0
while [ $i -lt 100 ]; do dmesg > /var/log/dmesg.log 2>&1; cp /tmp/x11server.log /var/log/x11server.log 2>/dev/null; grep -E 'i915: vk|gpu: ioctl' /var/log/dmesg.log >> /var/log/vk.log; sync; sleep 2; i=$((i+1)); done
