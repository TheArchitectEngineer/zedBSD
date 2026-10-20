#!/bin/sh
# Puts the files of NEW_ROOTFS that are missing from BASE_ROOTFS or differ
# from it onto the running VisionFive 2 board's root, without rewriting the
# SD card: the files go to the board's /tmp with scp, then root copies them
# over / and gives the setuid programs their modes back (the root image makes
# every file root's, with the host file's mode, so this matches a new image).
#   VF2_HOST=user@host VF2_PASSWORD=... plan/tools/vf2/overlay.sh BASE_ROOTFS NEW_ROOTFS OUTDIR
# BASE_ROOTFS is the rootfs the board's SD was made from (for example
# build/uat/vf2-gpu/rootfs), NEW_ROOTFS the one to bring it to.  OUTDIR gets
# the list (overlay.txt) and the staged tree (stage/).  Symbolic links are
# copied as the files they point at.  Files only in BASE_ROOTFS stay.
# The password comes from the environment, never from this file.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
base=$1
new=$2
out=$3
: "${VF2_HOST:?VF2_HOST=user@host}"
: "${VF2_PASSWORD:?VF2_PASSWORD}"
export SSHPASS="$VF2_PASSWORD"
ssh_run() { sshpass -e ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5 "$VF2_HOST" "$@"; }
mkdir -p "$out/stage"
: > "$out/overlay.txt"
(cd "$new" && find . \( -type f -o -type l \) ! -path './var/run' ! -path './etc/ssh/ssh_host_*') | sort |
while read -r f; do
	if [ -d "$new/$f" ]; then
		continue
	fi
	if [ -e "$base/$f" ] && cmp -s "$base/$f" "$new/$f"; then
		continue
	fi
	echo "$f" >> "$out/overlay.txt"
	mkdir -p "$out/stage/$(dirname "$f")"
	cp -pL "$new/$f" "$out/stage/$f"
done
echo "files: $(wc -l < "$out/overlay.txt")"
# scp writes a file only when its owner may write it, and the copy over /
# loses setuid bits: stage every file owner-writable and put back each mode
# that is not plain 644 or 755 with a script run on the board after the copy.
remote=/tmp/overlay.$(date +%s)
printf '#!/bin/sh\n' > "$out/modes.sh"
(cd "$out/stage" && find . -type f ! -name modes.sh) | sed 's#^\.##' | while read -r f; do
	mode=$(stat -c %a "$out/stage$f")
	case "$mode" in 644|755) ;; *) printf 'chmod %s %s\n' "$mode" "$f" >> "$out/modes.sh";; esac
	chmod u+rw "$out/stage$f"
done
echo "modes kept: $(($(wc -l < "$out/modes.sh") - 1))"
sshpass -e scp -rp -o StrictHostKeyChecking=no "$out/stage" "$VF2_HOST:$remote" >/dev/null || exit 1
sshpass -e scp -p -o StrictHostKeyChecking=no "$out/modes.sh" "$VF2_HOST:$remote.modes.sh" >/dev/null || exit 1
printf '%s\n' "$VF2_PASSWORD" | ssh_run "sudo -S sh -c 'cp -R $remote/. / && sh $remote.modes.sh && sync'" 2>/dev/null || exit 1
echo "installed from $remote"
