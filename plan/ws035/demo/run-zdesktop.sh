#!/bin/sh
# WS035 p069: the compositor on the machine's own display, started again if it ever ends (its deadline is
# a day).  Its log is kept in /var/log/zdesktop.log.
export XDG_RUNTIME_DIR=/tmp

# ws035-p090: the session's home (a service starts without one) and its usual folders, which Files shows in
# its sidebar and on its Home page (like xdg-user-dirs at a session's start).
[ -n "${HOME:-}" ] && [ "$HOME" != / ] || HOME=/root
export HOME
for folder in Desktop Documents Downloads Pictures Music Movies; do
	mkdir -p "$HOME/$folder"
done
cd "$HOME"
sleep 3
while :; do
	rm -f /tmp/wayland-0
	picture=
	[ -f /usr/share/zdesktop/wallpaper.ppm ] && picture=--wallpaper=/usr/share/zdesktop/wallpaper.ppm
	/bin/wayland --socket=/tmp/wayland-0 --timeout=86400 --glass $picture >> /var/log/zdesktop.log 2>&1
	sleep 2
done
