#!/bin/sh
# ws074-p058: kept connections and the memory cache on the Venus guest (build-browser-image.sh).
# plan/ws074/tests/http-server.py runs on the host (0.0.0.0:PORT, with TLS on 18443 and 18444, idle connections
# closed after 1 second); the guest reaches it as 10.0.2.2.  zdesktop runs at 1280x800 with --glass and the
# wallpaper; the browser opens images.html over http at 900x640.  The server's /stats (read on the host) counts the
# connections and requests.  Checks:
#  1. images.png: images.html's eight pictures come over fewer connections than requests, at most six (the first
#     page itself is read on the spot before the window opens, over a connection of its own).
#  2. A page with max-age=60 opened twice from the location: shown twice, requested once (the cache answers).
#  3. A page with an ETag opened twice: the second is revalidated (one 304) and shown.
#  4. After 3 seconds (the server has closed the kept connection) another page is shown: the loader opens a new one.
#  5. Neither log has an ERROR line.
#  6. run-http-tests.py --guest --async: the HTTP and HTTPS cases and the image cases with the loader in the guest.
#
#   plan/ws074/tests/browser-guest.sh start      (the Venus guest must be up)
#   plan/ws074/tests/browser-p058.sh [OUTDIR [PORT]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws074-run}"
export GUEST_RUNTIME
out=${1:-build/ws074-p058}
port=${2:-8074}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
site=http://10.0.2.2:$port
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[b]rowser" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[b]rowser" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# The server's counts (read on the host), and starting them again.
stats() { python3 -c "import urllib.request; print(urllib.request.urlopen('http://127.0.0.1:$port/stats', timeout=10).read().decode())"; }
reset() { python3 -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:$port/stats/reset', timeout=10).read()"; }

# Fails the run unless the counts satisfy a Python expression of s (the /stats object).
expect_stats() {
	counts=$(stats)
	if python3 -c "import json, sys; s = json.loads(sys.argv[1]); sys.exit(0 if ($2) else 1)" "$counts"; then
		echo "stats: $1 ok ($counts)"
	else
		echo "stats: $1 MISSING ($counts)"
		status=1
	fi
}

# Fails the run unless a log has a line matching a pattern (at least N times, default 1).
expect_log() {
	found=$(guest "grep -cE '$2' $1" | tail -1)
	if [ "${found:-0}" -ge "${3:-1}" ] 2>/dev/null; then
		echo "log: $2 ok"
	else
		echo "log: $2 MISSING (${found:-0})"
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
    --idle 1 > "$out/server.log" 2>&1 &
server=$!
sleep 1

# 1. The images page over http, its pictures over kept connections.
guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
reset
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
shot images.png
expect_stats "images over kept connections" \
    's["connections"] - 1 <= 6 and s["connections"] - 1 < sum(s["requests"].values()) - 1 and s["requests"].get("/images/images.html") == 1'

# 2. A page with max-age=60 twice: the second comes from the cache.
reset
open_location "$site/cached/fresh?max-age=60"
sleep 3
expect_last NAVIGATE "path=$site/cached/fresh\\?max-age=60 title=cached"
open_location "$site/images/images.html"
sleep 3
open_location "$site/cached/fresh?max-age=60"
sleep 3
expect_last NAVIGATE "path=$site/cached/fresh\\?max-age=60 title=cached"
shot cached.png
expect_log /tmp/b.log "ZBROWSER NAVIGATE path=$site/cached/fresh\\?max-age=60 " 2
expect_stats "fresh page requested once" 's["requests"].get("/cached/fresh") == 1'

# 3. A page with an ETag twice: the second is revalidated.
reset
open_location "$site/cached/tagged?etag=1"
sleep 3
open_location "$site/cached/tagged?etag=1"
sleep 3
expect_last NAVIGATE "path=$site/cached/tagged\\?etag=1 title=cached"
expect_stats "tagged page revalidated" 's["requests"].get("/cached/tagged") == 2 and s["not_modified"] == 1'

# 4. After the server closed the idle connection, another page.
sleep 3
reset
open_location "$site/pages/first.html"
sleep 3
expect_last NAVIGATE "path=$site/pages/first.html title=browser: the first page"
expect_stats "page after the idle close" 's["requests"].get("/pages/first.html") == 1'

# 5. Neither log has an error line; then everything ends.
errors=$(guest "grep -c 'ERROR' /tmp/zdesktop.log" | tail -1)
if [ "${errors:-1}" = 0 ]; then echo "zdesktop: no ERROR"; else echo "zdesktop: ERROR lines"; status=1; fi
browser_errors=$(guest "grep -c 'ZBROWSER ERROR' /tmp/b.log" | tail -1)
if [ "${browser_errors:-1}" = 0 ]; then echo "browser: no ERROR"; else echo "browser: ERROR lines"; status=1; fi
guest "$stop_all" >/dev/null

# 6. The HTTP cases with the loader in the guest.
if ! python3 plan/ws074/tests/run-http-tests.py --guest --async --host 10.0.2.2 --port "$port"; then
	status=1
fi
kill $server
echo "browser-p058: status $status (pictures in $out)"
exit $status
