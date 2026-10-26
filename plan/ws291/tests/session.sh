#!/bin/sh
# ws291: /etc/keiland/session of the test image, as userland/desktop/sessiond/session.sh with the compositor's
# frame log (--log-frames) that plan/tools/frame-ops/frame-times.py reads.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
for folder in Desktop Documents Downloads Pictures Music Movies; do
	mkdir -p "$HOME/$folder"
done
picture=
[ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
exec /bin/wayland --session --glass --log-frames --socket="$XDG_RUNTIME_DIR/wayland-0" $picture "$@"
