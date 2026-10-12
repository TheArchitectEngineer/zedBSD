#!/bin/sh
# T1-549: userland/desktop/sessiond/session.sh with --log-frames.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
for folder in Desktop Documents Downloads Pictures Music Movies; do
	mkdir -p "$HOME/$folder"
done
picture=
[ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
exec /bin/wayland --session --glass --log-frames --socket="$XDG_RUNTIME_DIR/wayland-0" $picture "$@"
