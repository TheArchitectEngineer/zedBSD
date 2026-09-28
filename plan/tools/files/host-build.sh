#!/bin/sh
# ws071: builds files' host tests with the host's C compiler into build/ws071-host/.
#
# The drawing (canvas, text, icons), the interface and the model of files are built
# without Wayland and Vulkan (window.c, present.c, menu.c and titlebar.c stay out); libtruetype is built
# from its sources.  The test programs:
#   files-render   draws scenes of the interface into PPM pictures (host-render.c)
#   files-model    checks the model in temporary directories (host-model.c)
#
#   plan/tools/files/host-build.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws071-host
src=userland/desktop/files
mkdir -p "$out/include" "$out/obj"
ln -sf "$(pwd)/include/libc/truetype.h" "$out/include/truetype.h"
ln -sf "$(pwd)/include/libc/keiland.h" "$out/include/keiland.h"
ln -sf "$(pwd)/include/libc/sha2.h" "$out/include/sha2.h"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
cc=${CC:-cc}
flags="-O2 -g -Wall -Wextra -Werror -Wno-unused-parameter -D_GNU_SOURCE -I$out/include -I$src"

# The libraries the program uses, from their sources.
objects=""
for file in userland/desktop/libtruetype/face.c userland/desktop/libtruetype/cmap.c \
    userland/desktop/libtruetype/outline.c userland/desktop/libtruetype/render.c \
    userland/desktop/libtruetype/glyph.c; do
	object="$out/obj/truetype-$(basename "$file" .c).o"
	"$cc" $flags -Wno-error -Iuserland/desktop/libtruetype -c "$file" -o "$object"
	objects="$objects $object"
done
# The C library's SHA-2 (the information's checksum), which the host's C library does not have.
"$cc" $flags -c src/libc/openbsd-sha2.c -o "$out/obj/libc-sha2.o"
objects="$objects $out/obj/libc-sha2.o"
# libz-compat and libpng-compat (the PNG thumbnails).
for file in userland/base/libz-compat/inflate.c userland/base/libz-compat/checksum.c userland/base/libpng-compat/read.c; do
	object="$out/obj/compat-$(basename "$file" .c).o"
	"$cc" $flags -c "$file" -o "$object"
	objects="$objects $object"
done
if [ -f userland/desktop/libkeiland/recent.c ]; then
	"$cc" $flags -c userland/desktop/libkeiland/recent.c -o "$out/obj/zdesktop-recent.o"
	objects="$objects $out/obj/zdesktop-recent.o"
fi

# files without the window, the presenter, the menus, the titlebar and the glass.
for file in $src/*.c; do
	case $(basename "$file") in
	main.c|window.c|present.c|menu.c|titlebar.c|glass.c|dnd.c) continue ;;
	esac
	object="$out/obj/files-$(basename "$file" .c).o"
	"$cc" $flags -c "$file" -o "$object"
	objects="$objects $object"
done

# The test programs.
for test in render model; do
	if [ -f "plan/tools/files/host-$test.c" ]; then
		"$cc" $flags -c "plan/tools/files/host-$test.c" -o "$out/obj/host-$test.o"
		extra=
		if [ "$test" = render ]; then
			"$cc" $flags -c plan/tools/files/host-glass.c -o "$out/obj/host-glass.o"
			extra="$out/obj/host-glass.o"
		fi
		"$cc" -o "$out/files-$test" "$out/obj/host-$test.o" $extra $objects -lm
		echo "built $out/files-$test"
	fi
done
