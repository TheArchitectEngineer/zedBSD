#!/bin/sh
#
# Builds the loaders' pure C helpers from a base commit and from the work
# tree, runs the same seeded driver over both, and compares their output.
#
# usage: diff-run.sh BASE_COMMIT OUT_DIR
#
# OUT_DIR must be a new or empty directory under the work tree's build/.

set -eu

base=$1
out=$2
top=$(cd "$(dirname "$0")/../../.." && pwd)
cc=${HOSTCC:-cc}
sources="bootloader/common/memory-map.c bootloader/common/logo-path.c bootloader/common/boot-override.c bootloader/bios/memory-map.c bootloader/bios/fat-directory.c bootloader/bios/logo.c bootloader/uefi/zedbsd-config.c bootloader/uefi/elf64.c bootloader/uefi/framebuffer.c bootloader/uefi/memory-map.c bootloader/uefi/memory-map-v6.c bootloader/uefi/logo.c bootloader/uefi/volume-discovery.c"

mkdir -p "$out/old" "$out/new"
git -C "$top" archive "$base" bootloader include | tar -x -C "$out/old"
cp -R "$top/bootloader" "$top/include" "$out/new/"

for tree in old new; do
	objects=""
	for source in $sources; do
		object="$out/$tree/$(echo "$source" | tr / _).o"
		$cc -std=c11 -O1 -Wall -Wextra -I"$out/$tree" -c "$out/$tree/$source" -o "$object"
		objects="$objects $object"
	done
	$cc -std=c11 -O1 -Wall -I"$out/$tree" "$top/plan/ws262/tests/diff-driver.c" $objects -o "$out/$tree/driver"
	"$out/$tree/driver" > "$out/$tree.txt"
done

wc -l "$out/old.txt" "$out/new.txt"
if cmp -s "$out/old.txt" "$out/new.txt"; then
	echo "PASS: identical output"
else
	echo "FAIL: outputs differ"
	diff "$out/old.txt" "$out/new.txt" | head -20
	exit 1
fi
