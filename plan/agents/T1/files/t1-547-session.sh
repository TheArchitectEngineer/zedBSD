#!/bin/sh
# T1-547: userland/desktop/sessiond/session.sh with --log-frames; /bin/wayland-before (the compositor of main
# 255efde24, before the draw-list) while /etc/keiland/before exists.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
for folder in Desktop Documents Downloads Pictures Music Movies; do
	mkdir -p "$HOME/$folder"
done
picture=
[ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
compositor=/bin/wayland
[ -f /etc/keiland/before ] && compositor=/bin/wayland-before
exec $compositor --session --glass --log-frames --socket="$XDG_RUNTIME_DIR/wayland-0" $picture "$@"
