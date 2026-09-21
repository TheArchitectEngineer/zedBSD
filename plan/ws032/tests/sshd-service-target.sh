#!/bin/sh
# WS032: the OpenSSH server started by init, from its service definition.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# The other test starts the server by hand.  This one does not start it at
# all: it is already running when the login prompt appears, because the
# package installed a definition in /etc/service.d and init read it.  What
# is tested is therefore the whole path a machine takes on its own, the
# start-up script that makes the host key included.
#
# The data image is remade first, because the target keeps what a previous
# run left there and ssh-keygen would stop to ask before overwriting a key.
#
# usage: sshd-service-target.sh [image]
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
image=${1:-$root/build/amd64/hdd-image.img}

rm -f "$root/build/data.img"
make -C "$root" disk-image >/dev/null

exec python3 "$root/plan/ws032/tests/run-target-console.py" \
	--image "$image" --timeout 340 \
	'cat /etc/service.d/sshd' \
	'ls /etc/ssh' \
	'mkdir -p /root/.ssh' \
	'chmod 700 /root/.ssh' \
	'ssh-keygen -q -t ed25519 -f /root/.ssh/id_ed25519 -N ""' \
	'cp /root/.ssh/id_ed25519.pub /root/.ssh/authorized_keys' \
	'chmod 600 /root/.ssh/authorized_keys' \
	'ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/root/.ssh/kh root@127.0.0.1 echo SERVICE-LOGIN-OK' \
	'echo SERVICE-VERIFIED'
