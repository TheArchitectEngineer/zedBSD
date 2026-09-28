#!/bin/sh
# ws075-p013: the demonstration image's graphical login.  It waits (at most two minutes) for the GPU node /dev/gpu0
# and then becomes sessiond, the service "greeter" of the other images.  The i915 publishes its node seconds after
# init has started the services (the GT and firmware bring-up), and sessiond, started at once, found no /dev/gpu0
# (SESSIOND CONSOLE reason=no-display errno=6, ENOENT) and gave the console back.  The kernel's splash keeps turning
# meanwhile.  The seconds waited are in /var/log/greeter-gpu.log.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
waited=0
while [ ! -e /dev/gpu0 ] && [ "$waited" -lt 120 ]; do
	sleep 1
	waited=$((waited + 1))
done
if [ -e /dev/gpu0 ]; then
	echo "GREETER-GPU /dev/gpu0 after ${waited} s" >> /var/log/greeter-gpu.log
else
	echo "GREETER-GPU no /dev/gpu0 after ${waited} s" >> /var/log/greeter-gpu.log
fi
exec /sbin/sessiond
