#!/bin/sh
# ws074: builds zdesktop-browser's engine and its host tests with the host's C compiler.
#
#   sh plan/ws074/tests/host-build.sh [plain|asan]     (default plain)
#
# Every engine source listed in userland/base/zdesktop-browser/Makefile is built except the
# shell/ directory (the zdesktop window); plan/ws074/tests/host-shell.c stands in for it.
# The outputs, in build/ws074-host/<variant>/:
#   zdesktop-browser   the program with its headless modes (the same main.c as on zedBSD)
#   host-NAME          each plan/ws074/tests/host-NAME.c unit test, linked with the engine
# The asan variant adds -fsanitize=address,undefined; the runners use it to find crashes.  Run it with
# ASAN_OPTIONS=detect_stack_use_after_return=0: the collector scans the real stack, and the sanitizer's
# separate stacks for address-taken locals would hide cells from it.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
variant=${1:-plain}
out=build/ws074-host/$variant
src=userland/base/zdesktop-browser
cc=${CC:-cc}
flags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -I$src -Iplan/ws074/tests -Ibuild/ws074-host/include"
case $variant in
plain) ;;
asan) flags="$flags -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined" ;;
*) echo "host-build: unknown variant $variant" >&2; exit 2 ;;
esac
mkdir -p "$out/obj"

# The engine's sources, from the package's list (the window's directory stays out).
sources=$(sh plan/ws074/tests/list-sources.sh | grep -v '/shell/')
objects=""
engine=""

# The base libraries the engine links on zedBSD, built from their sources (their public headers
# are linked into build/ws074-host/include, since the host's C library does not have them).
mkdir -p build/ws074-host/include
ln -sf "$(pwd)/include/libc/truetype.h" build/ws074-host/include/truetype.h
for file in userland/base/libtruetype/face.c userland/base/libtruetype/cmap.c userland/base/libtruetype/outline.c \
    userland/base/libtruetype/render.c userland/base/libtruetype/glyph.c; do
	object=$out/obj/truetype-$(basename "$file" .c).o
	if [ ! -f "$object" ] || [ "$file" -nt "$object" ]; then
		"$cc" $flags -Wno-error -Iuserland/base/libtruetype -c "$file" -o "$object"
	fi
	engine="$engine $object"
	objects="$objects $object"
done

# The tables generated from downloaded lists, made the way the package's Makefile makes them.
mkdir -p "$out/gen"
sh plan/ws074/tests/fetch-distfiles.sh
if [ ! -f "$out/gen/html-entities.c" ] || [ "$src/tools/gen-entities.py" -nt "$out/gen/html-entities.c" ]; then
	python3 "$src/tools/gen-entities.py" "$src/distfiles/entities.json" "$out/gen/html-entities.c"
fi
sources="$sources $out/gen/html-entities.c"
for file in $sources; do
	object=$out/obj/$(printf '%s' "${file#$src/}" | sed "s|^$out/||" | tr '/' '_' | sed 's/\.c$/.o/')
	if [ ! -f "$object" ] || [ "$file" -nt "$object" ] || [ -n "$(find "$src" -name '*.h' -newer "$object" | head -1)" ]; then
		"$cc" $flags -c "$file" -o "$object"
	fi
	objects="$objects $object"
	case $file in
	*/main.c) ;;
	*) engine="$engine $object" ;;
	esac
done

# The stand-in for the window.
"$cc" $flags -c plan/ws074/tests/host-shell.c -o "$out/obj/host-shell.o"
"$cc" $flags -o "$out/zdesktop-browser" $objects "$out/obj/host-shell.o" -lm
echo "built $out/zdesktop-browser"

# The unit tests.
for test in plan/ws074/tests/host-*.c; do
	name=$(basename "$test" .c)
	[ "$name" = host-shell ] && continue
	"$cc" $flags -o "$out/$name" "$test" $engine "$out/obj/host-shell.o" -lm
	echo "built $out/$name"
done
