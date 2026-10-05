#!/bin/sh
# ws101-p010: the compositor (as plan/ws031/tests/zdesktop/run-zdesktop.sh, without a wallpaper), its output kept on
# the disk.
exec /bin/wayland --testing --socket=/tmp/wayland-0 --width=1920 --height=1080 --timeout=300 --glass --log-frames > /var/log/zdesktop.log 2>&1
