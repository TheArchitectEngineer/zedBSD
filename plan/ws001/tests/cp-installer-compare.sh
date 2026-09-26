#!/bin/sh
# ws001-p025: runs the cp invocations the installer (userland/base/zedinst)
# makes with two cp binaries and compares the trees and the reports they
# leave, so that the rewritten cp keeps the installer's contract.
#   sh plan/ws001/tests/cp-installer-compare.sh OLD_CP NEW_CP
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
old=$1
new=$2
work=$(mktemp -d)
trap 'chmod -R u+w "$work"; rm -rf "$work"' EXIT

# The source tree: files, a directory, links, a FIFO, hard links, modes.
mkdir -p "$work/src/etc/sub" "$work/src/bin"
printf 'conf\n' > "$work/src/etc/rc.conf"
printf 'x\n' > "$work/src/etc/sub/deep"
printf '#!/bin/sh\n' > "$work/src/bin/tool"
chmod 755 "$work/src/bin/tool"
chmod 4755 "$work/src/bin/tool"
ln "$work/src/bin/tool" "$work/src/bin/tool2"
ln -s ../etc/rc.conf "$work/src/bin/link"
mkfifo "$work/src/etc/fifo"
chmod 700 "$work/src/etc/sub"
touch -t 200001020304.05 "$work/src/etc/rc.conf" "$work/src/etc/sub"

status=0
for side in old new; do
	eval cp=\$$side
	out=$work/$side
	mkdir "$out"

	# The tree copy (treecopy.noct): cp -a -T --report-file=REPORT -- SRC DST.
	"$cp" -a -T "--report-file=$out/report" -- "$work/src" "$out/tree"
	echo "tree=$?" > "$out/status"

	# The seed and stage copies (install.noct, files.noct, pc98.noct).
	printf 'seed\n' > "$out/seed-source"
	chmod 750 "$out/seed-source"
	"$cp" -T --attributes-only --preserve=mode --update=none-fail -- "$out/seed-source" "$out/seed"
	echo "seed=$?" >> "$out/status"
	"$cp" -T --attributes-only --preserve=mode --update=none-fail -- "$out/seed-source" "$out/seed" 2>/dev/null
	echo "seed-again=$?" >> "$out/status"

	# What the copies left, with names relative to their side.
	(cd "$out/tree" && find . | sort) > "$out/names"
	(cd "$out/tree" && ls -ln etc etc/sub bin | awk '{ print $1, $2, $5, $NF }') > "$out/modes"
	ls -ln "$out/seed" | awk '{ print $1, $5 }' >> "$out/modes"
	sed "s|$(printf '%s' "$work/src" | od -An -tx1 | tr -d ' \n')|SRC|" "$out/report" > "$out/report.relative"
done

# The two sides must agree.
for file in status names modes report.relative; do
	if ! cmp -s "$work/old/$file" "$work/new/$file"; then
		echo "DIFF $file"
		diff "$work/old/$file" "$work/new/$file"
		status=1
	fi
done
if [ $status -eq 0 ]; then
	echo "SAME: tree, modes, seeds, and report"
fi
exit $status
