#!/bin/sh
# ws075-p012: the compositor as plan/ws031/tests/zdesktop/run-zdesktop.sh starts it, without --width and --height, so its
# output takes the display's preferred size (the HDMI mode with display=hdmi, the panel's without): Keiland full screen.
exec /bin/wayland --socket=/tmp/wayland-0 --timeout=300 --glass --wallpaper=/usr/share/keiland/wallpaper.ppm --log-frames > /var/log/zdesktop.log 2>&1
