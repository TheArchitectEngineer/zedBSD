#!/bin/sh
# ws074-p050: the asynchronous loader on the Venus guest (build-browser-image.sh).
# plan/ws074/tests/http-server.py runs on the host (0.0.0.0:PORT, with TLS on 18443 and 18444); the guest reaches it
# as 10.0.2.2.  zdesktop runs at 1280x800 with --glass and the wallpaper; the browser opens images.html over http at
# 900x640.  Checks, from the browser's ZBROWSER lines and zdesktop's log:
#  1. images.png: the page's JPEG, PNG and GIF images arrive through the loader after the page is shown.
#  2. A slow page (backgrounds.html?delay=4000) typed in the location starts LOADING, the page shown stays, and Esc
#     stops it (STOPPED) with images.html still shown.
#  3. backgrounds.png: the same page without the delay is shown (NAVIGATE) with its background images.
#  4. Neither log has an ERROR line.
#  5. run-http-tests.py --guest --async: the HTTP and HTTPS cases and the image cases with the loader in the guest.
#
#   plan/ws074/tests/browser-guest.sh start      (the Venus guest must be up)
#   plan/ws074/tests/browser-p050.sh [OUTDIR [PORT]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws074-run}"
export GUEST_RUNTIME
out=${1:-build/ws074-p050}
port=${2:-8074}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
site=http://10.0.2.2:$port
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[b]rowser" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[b]rowser" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# Fails the run unless a log has a line matching a pattern.
expect_log() {
	found=$(guest "grep -cE '$2' $1" | tail -1)
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
		echo "log: $2 ok"
	else
		echo "log: $2 MISSING"
		status=1
	fi
}

# Fails the run unless the browser's last line of a kind matches.
expect_last() {
	last=$(guest "grep 'ZBROWSER $1 ' /tmp/b.log | tail -1")
	if printf '%s\n' "$last" | grep -qE "$2"; then
		echo "last $1: $2 ok"
	else
		echo "last $1: $2 MISSING (got: $last)"
		status=1
	fi
}

# Takes a picture with the pointer out of the way.
shot() {
	pointer move 1270 790 sleep 400
	check "$out/$1" >/dev/null
}

# Types a location into the titlebar's field and opens it.
open_location() {
	pointer move $((wx + 450)) $((wy + 300)) sleep 300
	keys '<ctrl-l>'
	sleep 1
	keys '<ctrl-a>' "$1"
	keys '<ret>'
}

# The pictures, the test CA and the test server on the host.
python3 plan/ws074/tests/make-test-images.py >/dev/null
sh plan/ws074/tests/make-test-ca.sh build/ws074-tls >/dev/null
python3 plan/ws074/tests/http-server.py --port "$port" --tls-dir build/ws074-tls --tls-port 18443 --tls-wrong-port 18444 \
    > "$out/server.log" 2>&1 &
server=$!
sleep 1

# 1. The images page over http; its images arrive after it.
guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp; /bin/browser --width=900 --height=640 $site/images/images.html > /tmp/b.log 2>&1 </dev/null & sleep 2; echo started" >/dev/null
tries=0
while [ $tries -lt 30 ]; do
	ready=$(guest "grep -c 'ZBROWSER READY' /tmp/b.log" | tail -1)
	[ "${ready:-0}" -gt 0 ] 2>/dev/null && break
	sleep 1
	tries=$((tries + 1))
done
sleep 3
set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
wx=${1:-0}; wy=${2:-0}
echo "browser: window at $wx,$wy"
expect_log /tmp/b.log 'ZBROWSER READY width=900 height=640'
expect_last NAVIGATE "path=$site/images/images.html title=browser: images"
expect_log /tmp/b.log 'ZBROWSER FRAME'
shot images.png

# 2. A slow page starts loading; Esc stops it and the page shown stays.
open_location "$site/images/backgrounds.html?delay=4000"
sleep 1
expect_log /tmp/b.log "ZBROWSER LOADING url=$site/images/backgrounds.html\\?delay=4000"
keys '<esc>'
sleep 5
expect_log /tmp/b.log "ZBROWSER STOPPED url=$site/images/backgrounds.html\\?delay=4000"
expect_last NAVIGATE "path=$site/images/images.html "

# 3. The same page without the delay.
open_location "$site/images/backgrounds.html"
sleep 4
expect_last NAVIGATE "path=$site/images/backgrounds.html title=browser: backgrounds"
shot backgrounds.png

# 4. Neither log has an error line; then everything ends.
errors=$(guest "grep -c 'ERROR' /tmp/zdesktop.log" | tail -1)
if [ "${errors:-1}" = 0 ]; then echo "zdesktop: no ERROR"; else echo "zdesktop: ERROR lines"; status=1; fi
browser_errors=$(guest "grep -c 'ZBROWSER ERROR' /tmp/b.log" | tail -1)
if [ "${browser_errors:-1}" = 0 ]; then echo "browser: no ERROR"; else echo "browser: ERROR lines"; status=1; fi
guest "$stop_all" >/dev/null

# 5. The HTTP cases with the loader in the guest.
if ! python3 plan/ws074/tests/run-http-tests.py --guest --async --host 10.0.2.2 --port "$port"; then
	status=1
fi
kill $server
echo "browser-p050: status $status (pictures in $out)"
exit $status
