#!/bin/sh
# ws143-p007: the host test of the Linux Bluetooth backend (userland/desktop/libkeiland-backend-linux/bluetooth-linux.c and
# dbus-linux.c).  For each variant (plain, and ASan+UBSan) it builds plan/ws143/tests/bt-linux-host-test.c with the backend,
# starts a private dbus-daemon on a socket in the run's directory and plan/ws143/tests/bt-linux-fake-bluez.py on it (a fake
# org.bluez: dbus-python and GLib), and runs the test against them.  Linux hosts with dbus-daemon, python3-dbus and
# python3-gi only.  Nothing is removed: each run gets a new directory behind OUTPUT (plan/tools/fresh-out.sh).
#   sh plan/ws143/tests/bt-linux-host-test.sh [OUTPUT]   (default build/ws143-bt-linux)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws143-bt-linux}
cc=${CC:-cc}
. plan/tools/fresh-out.sh
fresh_out "$out"
run=$(cd "$fresh_dir" && pwd)
status=0
for variant in plain sanitized; do
	socket="$run/bus-$variant.socket"
	flags="-std=gnu17 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -I. -DBT_DBUS_SOCKET=\"$socket\""
	if [ "$variant" = sanitized ]; then
		flags="$flags -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
	fi
	# shellcheck disable=SC2086
	"$cc" $flags userland/desktop/libkeiland-backend-linux/bluetooth-linux.c userland/desktop/libkeiland-backend-linux/dbus-linux.c \
		plan/ws143/tests/bt-linux-host-test.c -o "$run/bt-linux-host-test-$variant"

	# The private bus, then the fake on it once the socket is there.
	dbus-daemon --session --nofork --address="unix:path=$socket" > "$run/dbus-$variant.txt" 2>&1 &
	daemon=$!
	waited=0
	while [ ! -S "$socket" ] && [ $waited -lt 50 ]; do
		sleep 0.1
		waited=$((waited + 1))
	done
	python3 -I plan/ws143/tests/bt-linux-fake-bluez.py "unix:path=$socket" > "$run/fake-$variant.txt" 2>&1 &
	fake=$!
	waited=0
	while ! grep -q ready "$run/fake-$variant.txt" 2>/dev/null && [ $waited -lt 100 ]; do
		sleep 0.1
		waited=$((waited + 1))
	done

	# The test, bounded.
	if timeout 180 "$run/bt-linux-host-test-$variant" > "$run/test-$variant.txt" 2> "$run/backend-$variant.txt"; then
		echo "bt-linux-host-test $variant: $(grep -c '^ok' "$run/test-$variant.txt") checks ok"
	else
		grep -v '^ok' "$run/test-$variant.txt" || true
		status=1
	fi
	kill "$fake" "$daemon" 2>/dev/null || true
	wait "$fake" "$daemon" 2>/dev/null || true
done
echo "output: $run"
[ $status -eq 0 ] && echo "bt-linux-host-test: PASS" || echo "bt-linux-host-test: FAIL"
exit $status
