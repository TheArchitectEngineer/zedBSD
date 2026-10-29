#!/bin/sh
# Builds Keiland for Linux (userland/desktop/keiland-linux.mk) with the header before the split (from BASE)
# and with the tree's, and compares the dynamic symbols of every ELF file built; then checks what the
# FreeBSD build can be checked for on this host: the headers it installs, and each of them compiled alone
# for a FreeBSD target.  Run from the worktree's root.  Writes into build/ws314-p001-hosts/.
#
#   sh plan/ws314/tests/p001-hosts.sh [BASE]     # BASE: the commit before the split (default c910bba8c)
set -u

base=${1:-c910bba8c}
out=build/ws314-p001-hosts
header=userland/desktop/include/keiland/keiland.h
exports=userland/desktop/libkeiland/exports
mkdir -p "$out"

cp "$header" "$out/keiland.h.split"
cp "$exports.py" "$out/exports.py.split"
cp "$exports.map" "$out/exports.map.split"
restore() {
	cp "$out/keiland.h.split" "$header"
	cp "$out/exports.py.split" "$exports.py"
	cp "$out/exports.map.split" "$exports.map"
}
trap restore EXIT INT TERM

: > "$out/summary.txt"
for state in before after; do
	if [ "$state" = before ]; then
		git show "$base:$header" > "$header"
		git show "$base:$exports.py" > "$exports.py"
		git show "$base:$exports.map" > "$exports.map"
	else
		restore
	fi
	dir=build/ws314-p001-linux-$state
	timeout 3600 make -j32 -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD="$dir" all > "$out/linux-$state.log" 2>&1
	status=$?
	warnings=$(grep -cE ':[0-9]+:[0-9]+: warning:' "$out/linux-$state.log")
	: > "$out/linux-$state.symbols"
	count=0
	for f in $(find "$dir" -type f \( -name '*.so*' -o -perm -u+x \) ! -name '*.o' ! -name '*.d' | LC_ALL=C sort); do
		if ! head -c 4 "$f" | grep -q 'ELF'; then
			continue
		fi
		count=$((count + 1))
		echo "== ${f#$dir/}" >> "$out/linux-$state.symbols"
		nm -D "$f" 2>/dev/null | sed 's/^[0-9a-f]* //' | LC_ALL=C sort >> "$out/linux-$state.symbols"
	done
	echo "linux $state make=$status warnings=$warnings elf=$count" >> "$out/summary.txt"
done
if cmp -s "$out/linux-before.symbols" "$out/linux-after.symbols"; then
	echo "symbols linux same" >> "$out/summary.txt"
else
	echo "symbols linux DIFFER" >> "$out/summary.txt"
fi

# FreeBSD: the headers install-headers puts under include/keiland, and each compiled alone for FreeBSD.
stage=$out/freebsd-stage
make -f userland/desktop/keiland-freebsd.mk DESTDIR="$PWD/$stage" KEILAND_PREFIX=/usr/local install-headers > "$out/freebsd-install.log" 2>&1
echo "freebsd install-headers exit=$? keiland headers=$(ls "$stage/usr/local/include/keiland" 2>/dev/null | wc -l)" >> "$out/summary.txt"
failed=0
for h in userland/desktop/include/keiland/keiland*.h; do
	printf '#include <keiland/%s>\n' "$(basename "$h")" > "$out/one.c"
	if ! clang --target=x86_64-unknown-freebsd14 -ffreestanding -std=gnu17 -Wall -Wextra -Werror \
		-Iuserland/desktop/include -fsyntax-only "$out/one.c" >> "$out/freebsd-compile.log" 2>&1; then
		failed=$((failed + 1))
	fi
done
echo "freebsd headers compiled alone: failed=$failed" >> "$out/summary.txt"
cat "$out/summary.txt"
