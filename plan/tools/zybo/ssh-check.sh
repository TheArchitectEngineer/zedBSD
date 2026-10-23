#!/bin/sh
# Judges a ZYBO's network login from the host (WS289 p005, the U4
# exception): ssh in as root and check that the machine is armv7, then send
# an 8 MiB file of random bytes there and back with scp and check that the
# guest's cksum and the returned file both match it.
#   plan/tools/zybo/ssh-check.sh HOST PORT OUTDIR
# HOST and PORT are the guest's SSH (127.0.0.1 and the forwarded port of
# qemu-zynq.sh --net, or the board's DHCP address and 22).  OUTDIR is a
# directory of the caller's for the two copies of the file (build/tmp/...);
# nothing is removed here.  The root's password is the image's (root).
# Exits 0 when every check passed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
if [ $# -ne 3 ]; then
	echo "usage: ssh-check.sh HOST PORT OUTDIR" >&2
	exit 2
fi
host=$1 port=$2 out=$3
command -v sshpass >/dev/null || { echo "ssh-check: sshpass is not installed" >&2; exit 2; }
mkdir -p "$out" || exit 2

options="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=10"
ssh_run() {
	# shellcheck disable=SC2086
	timeout 120 sshpass -p root ssh $options -p "$port" "root@$host" "$@"
}

# The machine answers SSH and is the ARMv7 port.  The server starts only
# once the first boot has made its host keys, which takes minutes in the
# emulator, so the first connection is tried for up to five minutes.
tries=0
until machine=$(ssh_run uname -m); do
	tries=$((tries + 1))
	[ "$tries" -lt 30 ] || { echo "ssh-check: FAIL ssh"; exit 1; }
	sleep 10
done
echo "ssh-check: uname -m = $machine"
[ "$machine" = armv7 ] || { echo "ssh-check: FAIL not armv7"; exit 1; }

# An 8 MiB file goes there, is summed there, and comes back unchanged.
head -c 8388608 /dev/urandom > "$out/x8m" || exit 2
local_sum=$(cksum < "$out/x8m")
echo "ssh-check: local cksum $local_sum"
start=$(date +%s)
# shellcheck disable=SC2086
timeout 600 sshpass -p root scp $options -P "$port" "$out/x8m" "root@$host:/tmp/x8m" ||
	{ echo "ssh-check: FAIL scp to the guest"; exit 1; }
middle=$(date +%s)
remote_sum=$(ssh_run 'cksum < /tmp/x8m') || { echo "ssh-check: FAIL cksum on the guest"; exit 1; }
echo "ssh-check: guest cksum $remote_sum"
[ "$remote_sum" = "$local_sum" ] || { echo "ssh-check: FAIL the guest's copy differs"; exit 1; }
# shellcheck disable=SC2086
timeout 600 sshpass -p root scp $options -P "$port" "root@$host:/tmp/x8m" "$out/x8m.back" ||
	{ echo "ssh-check: FAIL scp from the guest"; exit 1; }
end=$(date +%s)
cmp "$out/x8m" "$out/x8m.back" || { echo "ssh-check: FAIL the returned copy differs"; exit 1; }
echo "ssh-check: scp up $((middle - start)) s, down $((end - middle)) s"
echo "ssh-check: PASS"
