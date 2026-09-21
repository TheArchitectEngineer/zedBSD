#!/bin/sh
# WS032: OpenSSH on the target, over the loopback interface.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# The client and the server are both the cross-built ones, so one session
# exercises the whole of what was built: the crypto, the pseudo terminal,
# the separation of privilege and the file transfer.
#
# The data image is remade first, because the target keeps what a previous
# run left there and ssh-keygen would stop to ask before overwriting a key.
#
# usage: openssh-target.sh [image]
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
image=${1:-$root/build/amd64/hdd-image.img}

rm -f "$root/build/data.img"
make -C "$root" disk-image >/dev/null

exec python3 "$root/plan/ws032/tests/run-target-console.py" \
	--image "$image" --timeout 340 \
	'ssh -V' \
	'ssh-keygen -A' \
	'mkdir -p /root/.ssh' \
	'chmod 700 /root/.ssh' \
	'ssh-keygen -q -t ed25519 -f /root/.ssh/id_ed25519 -N ""' \
	'cp /root/.ssh/id_ed25519.pub /root/.ssh/authorized_keys' \
	'chmod 600 /root/.ssh/authorized_keys' \
	'/usr/sbin/sshd -E /tmp/sshd.log' \
	'ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/root/.ssh/kh root@127.0.0.1 id' \
	'ssh -t -o StrictHostKeyChecking=no -o UserKnownHostsFile=/root/.ssh/kh root@127.0.0.1 tty' \
	'echo payload-for-scp > /tmp/src.txt' \
	'scp -q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/root/.ssh/kh /tmp/src.txt root@127.0.0.1:/tmp/dst.txt' \
	'cat /tmp/dst.txt' \
	'echo VERIFIED'
