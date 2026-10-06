#!/bin/sh
# ws089: builds Settings' host test with the host's C compiler into build/ws089-host/.
#
# The model and the drawing of Settings (settings.h: ui, widgets, glyphs, pages, about) and the
# file manager's canvas, text and icons it shares are built without Wayland and Vulkan (main.c,
# window.c, present.c, menu.c, titlebar.c and glass.c stay out); libtruetype from its sources.
#   settings-render   draws the interface's frames into PPM pictures (host-render.c), with a network of
#                     made-up states in place of the daemon (host-network.c)
#
#   plan/ws089/tests/host-build.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws089-host
src=userland/desktop/settings
mkdir -p "$out/include" "$out/obj"
# Objects of an earlier layout (as shared-audio.o before ws131-p004) would be linked again by host-wallpaper.sh.
rm -f "$out"/obj/*.o
ln -sf "$(pwd)/userland/desktop/keiland/truetype.h" "$out/include/truetype.h"
ln -sf "$(pwd)/userland/desktop/keiland/keiland.h" "$out/include/keiland.h"
ln -sf "$(pwd)/userland/desktop/keiland/keiland-ui.h" "$out/include/keiland-ui.h"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
cc=${CC:-cc}
flags="-O2 -g -std=gnu89 -Wall -Wextra -Werror -Wno-unused-parameter -D_GNU_SOURCE -I$out/include -I$src -I."

objects=""
for file in userland/desktop/libtruetype/face.c userland/desktop/libtruetype/cmap.c \
    userland/desktop/libtruetype/outline.c userland/desktop/libtruetype/render.c \
    userland/desktop/libtruetype/glyph.c; do
	object="$out/obj/truetype-$(basename "$file" .c).o"
	"$cc" -O2 -g -w -I$out/include -Iuserland/desktop/libtruetype -c "$file" -o "$object"
	objects="$objects $object"
done
for file in userland/desktop/files/canvas.c userland/desktop/files/text.c userland/desktop/files/icons.c userland/desktop/artwork/mark.c; do
	object="$out/obj/shared-$(basename "$file" .c).o"
	"$cc" -O2 -g -Wall -Werror -D_GNU_SOURCE -I$out/include -c "$file" -o "$object"
	objects="$objects $object"
done
# The desktop's settings (WS135): libkeiland's cache and the settings' table, under a stand-in for kl_settings_*
# without Wayland (plan/tools/settings/host-kl-settings.c: the compositor's keys in memory, a set in effect at once).
for file in userland/desktop/libkeiland/settings-cache.c userland/desktop/libkeiland/settings-app.c userland/desktop/libkeiland/translate-follow.c userland/desktop/libkeiland/translate.c userland/desktop/settings-keys/settings-keys.c plan/tools/settings/host-kl-settings.c; do
	object="$out/obj/shared-$(basename "$file" .c).o"
	"$cc" -O2 -g -Wall -Werror -D_GNU_SOURCE -I$out/include -I. -Iuserland/desktop/libkeiland -c "$file" -o "$object"
	objects="$objects $object"
done
# The desktop's system (WS131 p011): a stand-in for kl_system_* without Wayland (no compositor: the sound page
# says it is not available; host-network.c fills the network pages by hand).
object="$out/obj/shared-host-kl-system.o"
"$cc" -O2 -g -Wall -Werror -D_GNU_SOURCE -I$out/include -c plan/ws089/tests/host-kl-system.c -o "$object"
objects="$objects $object"
# The wallpaper decoding look.c uses (ws138-p001): the shared decoder and libz-, libpng- and libjpeg-compat.
for file in userland/base/libz-compat/inflate.c userland/base/libz-compat/checksum.c userland/base/libpng-compat/read.c \
    userland/base/libjpeg-compat/decompress.c userland/base/libjpeg-compat/error.c userland/base/libjpeg-compat/huffman.c \
    userland/base/libjpeg-compat/idct.c userland/base/libjpeg-compat/marker.c userland/base/libjpeg-compat/memory.c \
    userland/base/libjpeg-compat/source.c userland/desktop/picture/wallpaper.c; do
	object="$out/obj/shared-$(basename "$file" .c).o"
	"$cc" -O2 -g -Wall -Werror -D_GNU_SOURCE -I$out/include -I. -c "$file" -o "$object"
	objects="$objects $object"
done
for file in $src/*.c; do
	case $(basename "$file") in
	main.c|window.c|present.c|menu.c|titlebar.c|glass.c|network.c) continue ;;
	esac
	object="$out/obj/settings-$(basename "$file" .c).o"
	"$cc" $flags -c "$file" -o "$object"
	objects="$objects $object"
done
"$cc" $flags -c plan/ws089/tests/host-render.c -o "$out/obj/host-render.o"
"$cc" $flags -c plan/ws089/tests/host-network.c -o "$out/obj/host-network.o"
"$cc" -o "$out/settings-render" "$out/obj/host-render.o" "$out/obj/host-network.o" $objects -lm -pthread
echo "built $out/settings-render"
