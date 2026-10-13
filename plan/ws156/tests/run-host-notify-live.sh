#!/bin/sh
# ws156-p006: the Linux backend's notification server on a real session bus of the host: dbus-run-session starts a
# private bus, host-notify-live serves org.freedesktop.Notifications on it, and gdbus (which introspects first) and
# dbus-send call it; gdbus monitor records the signals.  Each gdbus call is a connection of its own, so the close comes
# from sender 2 (this program takes it; the compositor takes back only a sender's own).  Needs dbus-daemon,
# dbus-run-session, gdbus and dbus-send.
#   sh plan/ws156/tests/run-host-notify-live.sh [OUTPUT]   (default build/ws156-notify-live)
# Each run gets a new directory behind OUTPUT (plan/tools/fresh-out.sh); nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws156-notify-live}
cc=${CC:-cc}
. plan/tools/fresh-out.sh
fresh_out "$out"
"$cc" -std=gnu17 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -I. userland/desktop/libkeiland-backend-linux/notify-linux.c \
	userland/desktop/libkeiland-backend-linux/dbus-linux.c plan/ws156/tests/host-notify-live.c -o "$out/host-notify-live"
dest="--dest org.freedesktop.Notifications --object-path /org/freedesktop/Notifications"
timeout 60 dbus-run-session -- sh -c "
	'$out/host-notify-live' 6 > '$out/server.txt' 2>&1 &
	server=\$!
	sleep 1
	gdbus monitor --session --dest org.freedesktop.Notifications > '$out/monitor.txt' 2>&1 &
	monitor=\$!
	sleep 1
	gdbus call --session $dest --method org.freedesktop.Notifications.GetServerInformation > '$out/information.txt' 2>&1
	gdbus call --session $dest --method org.freedesktop.Notifications.Notify gdbus-app 0 '' 'Live title' '<i>live</i> body' \"['default', 'Open']\" \"{'urgency': <byte 2>}\" 5000 > '$out/notify.txt' 2>&1
	gdbus call --session $dest --method org.freedesktop.Notifications.CloseNotification 1 > '$out/close.txt' 2>&1
	dbus-send --session --print-reply --dest=org.freedesktop.Notifications /org/freedesktop/Notifications org.freedesktop.Notifications.GetCapabilities > '$out/capabilities.txt' 2>&1
	sleep 1
	kill \$monitor
	wait \$server
" || true
status=0
grep -q "'Keiland', 'zedBSD'" "$out/information.txt" || { echo "FAIL GetServerInformation: $(cat "$out/information.txt")"; status=1; }
grep -q '(uint32 1,)' "$out/notify.txt" || { echo "FAIL Notify: $(cat "$out/notify.txt")"; status=1; }
grep -q 'flags=3 app="gdbus-app" title="Live title" body="live body"' "$out/server.txt" || { echo "FAIL the post: $(cat "$out/server.txt")"; status=1; }
grep -q 'withdrawn sender=2 id=1' "$out/server.txt" || { echo "FAIL CloseNotification"; status=1; }
grep -q 'NotificationClosed (uint32 1, uint32 3)' "$out/monitor.txt" || { echo "FAIL the NotificationClosed signal: $(cat "$out/monitor.txt")"; status=1; }
grep -q '"persistence"' "$out/capabilities.txt" || { echo "FAIL GetCapabilities: $(cat "$out/capabilities.txt")"; status=1; }
[ $status -eq 0 ] && echo "run-host-notify-live: PASS" || echo "run-host-notify-live: FAIL"
exit $status
