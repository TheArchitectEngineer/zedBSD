#!/bin/sh
# ws291: builds mview (userland/tests/mview) for Debian with the system's libwayland-client, Vulkan loader and Mesa
# WSI (the Linux reference of the 3D scene, ws291-p002), and its model under OUT/share/mview/qs40:
#
#     plan/ws291/tests/linux/build-mview.sh OUT
#
# Run from the top of the tree on a Debian 13 host with gcc, wayland-scanner, wayland-protocols, libwayland-dev and
# libvulkan-dev.  Keiland's Linux build (keiland-linux.mk) is not used: it puts Keiland's libwayland and WSI under
# mview, and the reference must be Mesa's own.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
out=$1
here=plan/ws291/tests/linux
gen=$out/gen
protocols=/usr/share/wayland-protocols
mkdir -p "$gen/wayland" "$out/bin" "$out/share/mview/qs40/tex"

# The protocols' code; Keiland's content-type header names its includes under wayland/.
wayland-scanner client-header "$protocols/stable/xdg-shell/xdg-shell.xml" "$gen/xdg-shell-client-protocol.h"
wayland-scanner private-code "$protocols/stable/xdg-shell/xdg-shell.xml" "$gen/xdg-shell-protocol.c"
wayland-scanner private-code "$protocols/staging/content-type/content-type-v1.xml" "$gen/content-type-protocol.c"
echo '#include <wayland-client-core.h>' > "$gen/wayland/wayland-client-core.h"
echo '#include <wayland-client-protocol.h>' > "$gen/wayland/wayland-client-protocol.h"

gcc -O2 -g -std=gnu17 -Wall -Wextra -Werror -D_GNU_SOURCE -DKEILAND_DATADIR="\"/opt/mview/share\"" -I. -I"$gen" \
	userland/tests/mview/main.c userland/tests/mview/window.c userland/tests/mview/renderer.c \
	userland/tests/mview/model.c userland/tests/mview/camera.c userland/tests/mview/input.c \
	"$here/content-type-shim.c" "$gen/xdg-shell-protocol.c" "$gen/content-type-protocol.c" \
	-lwayland-client -lvulkan -lm -o "$out/bin/mview"
cp userland/tests/mview/models/qs40/model.txt "$out/share/mview/qs40/"
cp userland/tests/mview/models/qs40/tex/*.pam "$out/share/mview/qs40/tex/"
echo "build-mview: $out/bin/mview"
