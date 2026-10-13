#!/bin/sh
# ws156-p006: builds the Linux backend's notification server (userland/desktop/libkeiland-backend-linux/notify-linux.c
# and dbus-linux.c) with host-notify-dbus.c for the host (plain, and ASan+UBSan) and runs it against a fake session bus
# (a Unix socket in the run's directory, named by DBUS_SESSION_BUS_ADDRESS).  Linux hosts only.
#   sh plan/ws156/tests/run-host-notify-dbus.sh [OUTPUT]   (default build/ws156-notify-dbus)
# Each run gets a new directory behind OUTPUT (plan/tools/fresh-out.sh); nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws156-notify-dbus}
cc=${CC:-cc}
. plan/tools/fresh-out.sh
fresh_out "$out"
status=0
for variant in plain sanitized; do
	flags="-std=gnu17 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -I."
	if [ "$variant" = sanitized ]; then
		flags="$flags -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
	fi
	# shellcheck disable=SC2086
	"$cc" $flags userland/desktop/libkeiland-backend-linux/notify-linux.c userland/desktop/libkeiland-backend-linux/dbus-linux.c \
		plan/ws156/tests/host-notify-dbus.c -o "$out/host-notify-dbus-$variant"
	if timeout 60 "$out/host-notify-dbus-$variant" "$out/bus-$variant.socket" > "$out/dbus-$variant.txt" 2>&1; then
		echo "host-notify-dbus $variant: $(grep -c '^ok' "$out/dbus-$variant.txt") checks ok"
	else
		grep -v '^ok' "$out/dbus-$variant.txt"
		status=1
	fi
done
[ $status -eq 0 ] && echo "run-host-notify-dbus: PASS" || echo "run-host-notify-dbus: FAIL"
exit $status
