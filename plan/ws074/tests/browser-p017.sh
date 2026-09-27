#!/bin/sh
# ws074-p017: browser opens pages over HTTPS in its window on the Venus guest (build-browser-image.sh).
# plan/ws074/tests/http-server.py runs on the host with the test CA (make-test-ca.sh); the guest reaches it as 10.0.2.2
# and trusts the CA with --ca-file.  zdesktop runs at 1280x800 with --glass and the wallpaper; the browser opens
# first.html over https at 900x640.  Checks, from the browser's ZBROWSER lines and zdesktop's log:
#  1. https-first.png: https://10.0.2.2:TLS/pages/first.html is shown (NAVIGATE with its URL and title).
#  2. https-link.png: a click on "link to the second page" opens second.html over https.
#  3. The location https://10.0.2.2:WRONG/ (a certificate for another name) is refused: an ERROR line with the TLS
#     reason, and the page shown stays second.html.
#  4. https-site.png: the location https://example.com/ (a real site, the image's ca-certificates), when the guest
#     can reach the Internet; otherwise the line says so and the step is skipped.
#  5. zdesktop's log has no ERROR line, and the browser has no ERROR line but step 3's.
# The link's place comes from the host build's --dump=layout of first.html at the same width.
#
#   plan/ws074/tests/browser-guest.sh start      (the Venus guest must be up)
#   plan/ws074/tests/browser-p017.sh [OUTDIR [PORT TLSPORT WRONGPORT]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws074-run}"
export GUEST_RUNTIME
out=${1:-build/ws074-p017}
port=${2:-8075}
tls=${3:-18445}
wrong=${4:-18446}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
site=https://10.0.2.2:$tls
stop_all='for p in $(ps -A -o pid,args | grep -E "[z]desktop( |$)|[z]desktop-browser" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[z]desktop( |$)|[z]desktop-browser" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
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

# Opens a location from the titlebar's field: Ctrl+L, the text, Enter.
open_location() {
	pointer move $((wx + 450)) $((wy + 300)) sleep 300
	keys '<ctrl-l>'
	sleep 1
	keys '<ctrl-a>' "$1"
	keys '<ret>'
	sleep "$2"
}

# The test server on the host, with the test CA; the CA goes into the guest.
sh plan/ws074/tests/make-test-ca.sh build/ws074-tls >/dev/null
python3 plan/ws074/tests/http-server.py --port "$port" --tls-dir build/ws074-tls --tls-port "$tls" \
    --tls-wrong-port "$wrong" > "$out/server.log" 2>&1 &
server=$!
sleep 1
python3 plan/tools/guest/guest.py put build/ws074-tls/ca.pem /tmp/ws074-ca.pem >/dev/null

# The link's middle in the page at 900 wide, from the host's layout: "x y".
f=build/ws035-fonts
link=$(build/ws074-host/plain/browser --dump=layout --width=900 --height=640 --font=$f/Inter.ttf \
    --mono-font=$f/JetBrainsMono-Regular.ttf --fallback-font=$f/DroidSansFallbackFull.ttf plan/ws074/tests/pages/first.html |
    awk '$1 == "line" { y = $3; h = $5 } $1 == "text" && $NF == "\"link\"" { printf "%d %d\n", $2 + $4 / 2, y + h / 2; exit }')
echo "link: at $link in the page"

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp; /bin/browser --width=900 --height=640 --ca-file=/tmp/ws074-ca.pem $site/pages/first.html > /tmp/b.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
wx=${1:-0}; wy=${2:-0}
echo "browser: window at $wx,$wy"

# 1. The first page over https.
expect_log /tmp/b.log 'ZBROWSER READY width=900 height=640'
expect_last NAVIGATE "path=$site/pages/first.html title=browser: the first page"
shot https-first.png

# 2. The link, relative to the https URL.
set -- $link
pointer move $((wx + $1 - 2)) $((wy + $2)) sleep 150 move $((wx + $1)) $((wy + $2)) sleep 300 down sleep 60 up sleep 2000
expect_log /tmp/b.log 'ZBROWSER LINK href=second.html'
expect_last NAVIGATE "path=$site/pages/second.html title=browser: the second page"
shot https-link.png

# 3. A certificate for another name is refused, and the page stays.
open_location "https://10.0.2.2:$wrong/pages/first.html" 2
expect_log /tmp/b.log "ZBROWSER ERROR load path=https://10.0.2.2:$wrong/pages/first.html .*mismatch"
expect_last NAVIGATE "path=$site/pages/second.html "

# 4. A real site, when the guest reaches the Internet.
reach=$(guest "/bin/browser --dump=dom https://example.com/ 2>&1 | grep -c 'Example Domain'" | tail -1)
if [ "${reach:-0}" -gt 0 ] 2>/dev/null; then
	keys '<esc>'
	open_location "https://example.com/" 4
	expect_last NAVIGATE "path=https://example.com/ title=Example Domain"
	shot https-site.png
else
	echo "site: the guest cannot reach https://example.com/ (skipped)"
fi

# 5. No error in zdesktop's log, and none in the browser's but step 3's, then everything ends.
errors=$(guest "grep -c 'ERROR' /tmp/zdesktop.log" | tail -1)
if [ "${errors:-1}" = 0 ]; then echo "zdesktop: no ERROR"; else echo "zdesktop: ERROR lines"; status=1; fi
browser_errors=$(guest "grep 'ZBROWSER ERROR' /tmp/b.log | grep -vc ':$wrong/'" | tail -1)
if [ "${browser_errors:-1}" = 0 ]; then echo "browser: no other ERROR"; else echo "browser: other ERROR lines"; status=1; fi
guest "$stop_all" >/dev/null
kill $server
echo "browser-p017: status $status (pictures in $out)"
exit $status
