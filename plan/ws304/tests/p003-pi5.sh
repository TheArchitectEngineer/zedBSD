#!/bin/sh
# ws304-p003: put a kernel on the Raspberry Pi 5 over SSH, reboot it, and
# read back the PCIe/NVMe evidence (dmesg lines and a read-only dd test).
#
#   p003-pi5.sh HOST VMUNIX OUTDIR       deploy, reboot, collect
#   p003-pi5.sh HOST - OUTDIR            collect only (no deploy, no reboot)
#   p003-pi5.sh HOST --restore OUTDIR    put $BACKUP/vmunix (and config.txt) back
#
# The boot FAT has little room, so the kernel that was running is kept on the
# build host, not on the board: set BACKUP to a directory holding a copy of
# /boot/boot0/vmunix and config.txt taken before the first run.  Deploying
# checks that BACKUP/vmunix is there.
# config.txt gets "dtparam=pciex1" (the host copy in BACKUP is the original).
# Nothing is written to the NVMe drive.  No file is removed here.
#
# Login is by password (user's decision 2026-10-27: no authorized_keys):
# set SSHPASS in the environment; it is also given to sudo -S on the board.

set -u
host=$1
kernel=$2
out=$3
: "${SSHPASS:?set SSHPASS to kei's password}"
: "${BACKUP:?set BACKUP to the host copy of the board's /boot/boot0}"
export SSHPASS
ssh_opts="-o ConnectTimeout=10 -o ServerAliveInterval=5"
mkdir -p "$out"
sudo="echo '$SSHPASS' | sudo -S"

run() {
	timeout "${T:-60}" sshpass -e ssh $ssh_opts "kei@$host" "$@"
}

wait_down_up() {
	# Wait for SSH to go away, then for it to come back.
	i=0
	while run true >/dev/null 2>&1; do
		i=$((i + 1)); [ $i -gt 30 ] && break; sleep 2
	done
	i=0
	until T=15 run true >/dev/null 2>&1; do
		i=$((i + 1))
		if [ $i -gt 60 ]; then
			echo "p003: $host did not come back within ~10 minutes" >&2
			return 1
		fi
		sleep 10
	done
	return 0
}

case "$kernel" in
-)
	;;
--restore)
	timeout 120 sshpass -e scp $ssh_opts "$BACKUP/vmunix" "kei@$host:/tmp/vmunix.ws304-old" || exit 1
	timeout 60 sshpass -e scp $ssh_opts "$BACKUP/config.txt" "kei@$host:/tmp/config.txt.ws304-old" || exit 1
	run "cd /boot/boot0 && $sudo cp /tmp/vmunix.ws304-old vmunix && $sudo cp /tmp/config.txt.ws304-old config.txt && sync && cmp /tmp/vmunix.ws304-old vmunix && cmp /tmp/config.txt.ws304-old config.txt && echo restored" || exit 1
	T=10 run "$sudo reboot" >/dev/null 2>&1
	wait_down_up || exit 1
	;;
*)
	sha=$(sha256sum "$kernel" | cut -d' ' -f1)
	echo "p003: kernel $kernel sha256 $sha" | tee "$out/deploy.txt"
	run 'uname -a; ls -la /boot/boot0; cat /boot/boot0/config.txt; cat /boot/boot0/cmdline.txt' >"$out/before.txt" 2>&1 || exit 1
	[ -s "$BACKUP/vmunix" ] && [ -s "$BACKUP/config.txt" ] || { echo "p003: no host backup in $BACKUP" >&2; exit 1; }
	if ! grep -q '^dtparam=pciex1' "$out/before.txt"; then
		run 'cat /boot/boot0/config.txt' >"$out/config.txt.new" || exit 1
		printf '[all]\n# ws304: enable the external PCIe (pcie1, M.2 HAT).\ndtparam=pciex1\n' >>"$out/config.txt.new"
		timeout 60 sshpass -e scp $ssh_opts "$out/config.txt.new" "kei@$host:/tmp/config.txt.ws304" || exit 1
		run "cd /boot/boot0 && $sudo cp /tmp/config.txt.ws304 config.txt && sync && cat config.txt" >>"$out/deploy.txt" 2>&1 || exit 1
	fi
	timeout 120 sshpass -e scp $ssh_opts "$kernel" "kei@$host:/tmp/vmunix.ws304" || exit 1
	run "cd /boot/boot0 && $sudo cp /tmp/vmunix.ws304 vmunix && sync && cmp /tmp/vmunix.ws304 vmunix && cksum vmunix && df -k /boot/boot0" >>"$out/deploy.txt" 2>&1 || exit 1
	T=10 run "$sudo reboot" >/dev/null 2>&1
	wait_down_up || exit 1
	;;
esac

run 'uname -a; uptime' >"$out/after.txt" 2>&1
run 'dmesg' >"$out/dmesg.txt" 2>&1
grep -E 'rpi5-pcie|rpi5-mip|pcie: brcmstb|nvme|rp1|rpi5-serror|SError' "$out/dmesg.txt" >"$out/dmesg-pcie.txt"
run 'ls -la /dev/nvme* 2>&1' >"$out/dev.txt" 2>&1
if grep -q nvme0n1 "$out/dev.txt"; then
	T=300 run "date +%s; $sudo dd if=/dev/nvme0n1 of=/dev/null bs=1048576 count=256; echo dd-exit \$?; date +%s" >"$out/dd-256.txt" 2>&1
	T=120 run "$sudo dd if=/dev/nvme0n1 bs=1048576 count=64 2>/dev/null | cksum" >"$out/cksum-1.txt" 2>&1
	T=120 run "$sudo dd if=/dev/nvme0n1 bs=1048576 count=64 2>/dev/null | cksum" >"$out/cksum-2.txt" 2>&1
	T=60 run "$sudo blkid /dev/nvme0n1 2>&1; $sudo diskpart list /dev/nvme0n1 2>&1 || $sudo diskpart /dev/nvme0n1 2>&1" >"$out/parts.txt" 2>&1
fi
echo "p003: results in $out"
cat "$out/dmesg-pcie.txt"
