#!/bin/sh
# ws211-p003 test: userland/desktop/sessiond/session.sh with --log-frames, and KWL_BATCH=0 while
# /etc/keiland/batch-off exists (the test makes or removes it before the session starts).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
for folder in Desktop Documents Downloads Pictures Music Movies; do
	mkdir -p "$HOME/$folder"
done
picture=
[ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
[ -f /etc/keiland/batch-off ] && export KWL_BATCH=0
exec /bin/wayland --session --glass --log-frames --socket="$XDG_RUNTIME_DIR/wayland-0" $picture "$@"
