#!/bin/sh
# ws063-p002: makes the same UFS images with an older zedimage-host (OLD) and
# the current one (build/zedimage-host) and compares them byte for byte, then
# checks the current ones with plan/tools/ufs/check-volume.py.  A style-only
# change of the producer must leave its output unchanged.
#   sh plan/ws063/tests/zedimage-compare.sh OLD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
old=$1
work=build/ws063/zicmp
rm -rf "$work"
mkdir -p "$work/tree/etc" "$work/tree/usr/bin"
echo hello > "$work/tree/etc/motd"
head -c 300000 /dev/urandom > "$work/tree/usr/bin/blob"
ln -s ../etc/motd "$work/tree/motd"
status=0
for args in "268435456" "268435456 --journal-size=0" "268435456 --journal-size=32" \
	"268435456 --profile=journal-snapshot" "268435456 --inodes=4096" \
	"4096M" "64M --journal-size=1024" ; do
	size=${args%% *}
	rest=${args#"$size"}
	name=$(echo "$args" | tr ' =-' '___')
	"$old" ufs "$size" "$work/tree" "$work/old-$name.img" $rest || { echo "old failed: $args"; status=1; continue; }
	build/zedimage-host ufs "$size" "$work/tree" "$work/new-$name.img" $rest || { echo "new failed: $args"; status=1; continue; }
	if cmp -s "$work/old-$name.img" "$work/new-$name.img"; then
		echo "same: $args"
	else
		echo "DIFFERENT: $args"
		status=1
	fi
	python3 plan/tools/ufs/check-volume.py "$work/new-$name.img" | tail -1
	rm -f "$work/old-$name.img" "$work/new-$name.img"
done
# Refused arguments stay refused.
for bad in "--journal-size=1025" "--journal-size=x" "--bogus"; do
	if build/zedimage-host ufs 268435456 "$work/tree" "$work/bad.img" "$bad" 2> /dev/null; then
		echo "ACCEPTED: $bad"
		status=1
	else
		echo "refused: $bad"
	fi
done
rm -rf "$work"
exit $status
