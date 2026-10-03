#!/bin/sh
# ws073-p049 (BUG-033), run in the guest from the unpacked kit (bug033-kit.sh): times the guest clang's start
# (clang --version) and the -O2 compile of expat's xmlparse.c, xmltok.c and xmlrole.c, three times each, then
# the three at once.  Prints one "BUG033 what=... real=..." line per measurement (POSIX time -p).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
cd "$(dirname -- "$0")" || exit 1
measure() {
	what=$1
	shift
	real=$(time -p sh -c '"$@" >/dev/null 2>&1' sh "$@" 2>&1 | awk '/^real/ {print $2}')
	echo "BUG033 what=$what real=$real"
}
for round in 1 2 3; do
	measure "version-$round" clang --version
	for f in xmlparse xmltok xmlrole; do
		measure "$f-$round" clang -DHAVE_EXPAT_CONFIG_H -I. -O2 -w -c "$f.c" -o "/tmp/bug033-$f.o"
	done
done
start=$(date +%s)
clang -DHAVE_EXPAT_CONFIG_H -I. -O2 -w -c xmlparse.c -o /tmp/bug033-p1.o &
clang -DHAVE_EXPAT_CONFIG_H -I. -O2 -w -c xmltok.c -o /tmp/bug033-p2.o &
clang -DHAVE_EXPAT_CONFIG_H -I. -O2 -w -c xmlrole.c -o /tmp/bug033-p3.o &
wait
end=$(date +%s)
echo "BUG033 what=parallel3 real=$((end - start))"
rm -f /tmp/bug033-*.o
