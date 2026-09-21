#!/bin/sh
# WS033: the networking service, on the target.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# The service is not started here.  It ran before the login prompt did,
# because init read its definition, and what is checked is what it left
# behind: the loopback configured, the daemon managing the wired
# interfaces, and the commands that say whether it should.
#
# usage: networking-target.sh [image]
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
image=${1:-$root/build/amd64/hdd-image.img}

exec python3 "$root/plan/ws032/tests/run-target-console.py" \
	--image "$image" --timeout 300 \
	'cat /etc/service.d/networking' \
	'service status networking' \
	'net show lo0' \
	'ping -c 1 127.0.0.1 >/dev/null 2>&1 && echo LOOPBACK-OK' \
	'net lan disable && echo LAN-DISABLE-OK' \
	'net lan enable && echo LAN-ENABLE-OK' \
	'net startup && echo STARTUP-OK' \
	'net lan enable --wait 2>&1 | head -n 1' \
	'net lan bogus 2>&1 | head -n 1' \
	'echo NETWORKING-VERIFIED'
