#!/bin/sh
# The graphical session zsessiond starts after a login, as the user, with HOME, USER, LOGNAME, PATH, SHELL and
# XDG_RUNTIME_DIR (the user's own directory, /run/user/UID) set.  Installed as /etc/zdesktop/session.  The
# session lasts as long as zdesktop does: App Home's Log Out ends it, and zsessiond shows the greeter again.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The usual folders, which Files shows in its sidebar and on its Home page (like xdg-user-dirs).
for folder in Desktop Documents Downloads Pictures Music Movies; do
	mkdir -p "$HOME/$folder"
done

# zdesktop, with the wallpaper when the image has one; its socket in the runtime directory.
picture=
[ -f /usr/share/zdesktop/wallpaper.ppm ] && picture=--wallpaper=/usr/share/zdesktop/wallpaper.ppm
exec /bin/zdesktop --session --glass --socket="$XDG_RUNTIME_DIR/wayland-0" $picture
