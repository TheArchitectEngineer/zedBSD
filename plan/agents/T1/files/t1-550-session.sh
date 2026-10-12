#!/bin/sh
# T1-550: session.sh with --log-frames; KL_PRESENT_CHECK=1 or KL_PRESENT_WHOLE=1 from /etc/keiland/present-mode.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
for folder in Desktop Documents Downloads Pictures Music Movies; do
	mkdir -p "$HOME/$folder"
done
picture=
[ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
mode=$(cat /etc/keiland/present-mode 2>/dev/null)
[ "$mode" = whole ] && export KL_PRESENT_WHOLE=1
[ "$mode" = check ] && export KL_PRESENT_CHECK=1
echo "T1-550 present-mode=$mode"
exec /bin/wayland --session --glass --log-frames --socket="$XDG_RUNTIME_DIR/wayland-0" $picture "$@"
