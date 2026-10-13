#!/bin/sh
# ws081-p017 (L3): the graphical session of userland/desktop/sessiond/session.sh with the compositor's --log-frames,
# so that session.log has each finger's report ("KWL TOUCH report ... host_ms=") and each composed frame ("KWL COMPOSE
# frame=... at_ms=") for plan/ws081/tests/touch-latency.py.  Put in place of /etc/keiland/session by
# plan/ws081/tests/build-demo-win.sh with WIN_LOG_FRAMES=1; everything but --log-frames is session.sh's.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The usual folders, which Files shows in its sidebar and on its Home page (like xdg-user-dirs).
for folder in Desktop Documents Downloads Pictures Music Movies; do
	mkdir -p "$HOME/$folder"
done

# The compositor, with the wallpaper when the image has one; its socket in the runtime directory; every frame logged.
picture=
[ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
exec /bin/wayland --session --glass --log-frames --socket="$XDG_RUNTIME_DIR/wayland-0" $picture "$@"
