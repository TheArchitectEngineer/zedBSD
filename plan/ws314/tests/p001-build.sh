#!/bin/sh
# Builds libkeiland and every zedBSD program that includes <keiland/keiland.h>, for amd64, arm64 and
# riscv64, once with the header before the split (from BASE) and once with the tree's, and compares the
# dynamic symbols of what was built.  Run from the worktree's root.  Writes into build/ws314-p001/.
#
#   sh plan/ws314/tests/p001-build.sh [BASE]     # BASE: the commit before the split (default c910bba8c)
set -u

base=${1:-c910bba8c}
out=build/ws314-p001
header=userland/desktop/include/keiland/keiland.h
exports=userland/desktop/libkeiland/exports
nm=build/llvm/bin/llvm-nm
mkdir -p "$out"

# The split header and the exports' script and map that read it are kept aside while the old ones
# build, and put back however the script ends.
cp "$header" "$out/keiland.h.split"
cp "$exports.py" "$out/exports.py.split"
cp "$exports.map" "$out/exports.map.split"
restore() {
	cp "$out/keiland.h.split" "$header"
	cp "$out/exports.py.split" "$exports.py"
	cp "$out/exports.map.split" "$exports.map"
}
trap restore EXIT INT TERM

desktop="libkeiland wayland xserver files imageview keiland-ime browser calendar mailer mediastorage monitor
music notes pdfviewer phone photos settings terminal textedit videoplayer"
tests_amd64="kuidemo keiland-notify keiland-settings keiland-system menu-probe popup-probe titlebar-probe
printtest wlshm"

target_of() {
	case $1 in
	libkeiland) echo "dynamic/libkeiland.so" ;;
	*) echo "bin/$1" ;;
	esac
}

build_one() {
	state=$1
	arch=$2
	config=$3
	programs=$4
	dir=build/ws314-p001-$state-$arch
	targets=
	for p in $programs; do
		targets="$targets $dir/$(target_of "$p")"
	done
	timeout 1800 make -j32 ZEDBSD_CONFIG="$config" BUILD="$dir" "sysroot-$arch" > "$out/$state-$arch.log" 2>&1
	timeout 3600 make -j32 ZEDBSD_CONFIG="$config" BUILD="$dir" $targets >> "$out/$state-$arch.log" 2>&1
	status=$?

	# The installer is built only for the installer's image.
	if [ "$arch" = amd64 ]; then
		timeout 1800 make -j32 ZEDBSD_CONFIG="$config" ZEDBSD_IMAGE_PROFILE=installer BUILD="$dir-installer" \
			"$dir-installer/bin/installer" >> "$out/$state-$arch.log" 2>&1
		status=$((status + $?))
		programs="$programs installer"
	fi
	warnings=$(grep -E ':[0-9]+:[0-9]+: warning:' "$out/$state-$arch.log" | grep -vE '/packages/|^\.\./src/|userland/base/noct/noct/' | wc -l)
	: > "$out/$state-$arch.symbols"
	for p in $programs; do
		f=$dir/$(target_of "$p")
		if [ "$p" = installer ]; then
			f=$dir-installer/bin/installer
		fi
		if [ -f "$f" ]; then
			echo "== $p" >> "$out/$state-$arch.symbols"
			"$nm" -D "$f" | sed 's/^[0-9a-f]* //' | LC_ALL=C sort >> "$out/$state-$arch.symbols"
		else
			echo "== $p MISSING" >> "$out/$state-$arch.symbols"
		fi
	done
	echo "$state $arch make=$status warnings=$warnings built=$(grep -c '^== [^ ]*$' "$out/$state-$arch.symbols")/$(echo $programs | wc -w)" >> "$out/summary.txt"
}

: > "$out/summary.txt"
for state in before after; do
	if [ "$state" = before ]; then
		git show "$base:$header" > "$header"
		git show "$base:$exports.py" > "$exports.py"
		git show "$base:$exports.map" > "$exports.map"
	else
		restore
	fi
	build_one "$state" amd64 config/ci/config-amd64.mk "$desktop $tests_amd64"
	build_one "$state" arm64 config/ci/config-odroid-c5-keiland.mk "$desktop"
	build_one "$state" riscv64 config/ci/config-visionfive2-keiland.mk "$desktop"
done

for arch in amd64 arm64 riscv64; do
	if cmp -s "$out/before-$arch.symbols" "$out/after-$arch.symbols"; then
		echo "symbols $arch same" >> "$out/summary.txt"
	else
		echo "symbols $arch DIFFER" >> "$out/summary.txt"
	fi
done
cat "$out/summary.txt"
