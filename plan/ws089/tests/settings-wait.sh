# ws089: waiting for the Venus guest before a Settings test (sourced by settings-p004.sh, settings-p005.sh).
#
# plan/ws089/tests/settings-guest.sh start returns before the guest's SSH answers; a test that starts at once
# loses its first commands (ws089-p004's first run).  wait_guest waits until a command runs in the guest (at most
# two minutes); wait_desktop waits until zdesktop, started by the test, says ZWL READY in /tmp/zdesktop.log.
# Both need guest() and set status=1 on a timeout.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

wait_guest() {
	tries=0
	while [ $tries -lt 60 ]; do
		answer=$(guest 'echo guest-up' | tail -1)
		[ "$answer" = guest-up ] && { echo "guest: up"; return 0; }
		tries=$((tries + 1))
		sleep 2
	done
	echo "guest: no answer"
	status=1
	return 1
}

wait_desktop() {
	tries=0
	while [ $tries -lt 30 ]; do
		ready=$(guest 'grep -c "ZWL READY" /tmp/zdesktop.log 2>/dev/null' | tail -1)
		[ "${ready:-0}" -gt 0 ] 2>/dev/null && { echo "zdesktop: ready"; return 0; }
		tries=$((tries + 1))
		sleep 1
	done
	echo "zdesktop: not ready"
	status=1
	return 1
}
