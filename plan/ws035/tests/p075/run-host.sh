#!/bin/sh
# ws035-p075: libwayland's generic event dispatch and server-created objects, on the host.
# The test protocol (generic-test.xml) goes through the host's wayland-scanner; the server is built on the
# host's libwayland-server, the client on zedBSD's libwayland-client (userland/desktop/libwayland, compiled
# for the host with only its Wayland headers on the include path).  Both run in a private
# XDG_RUNTIME_DIR under /tmp; the client must print CLIENT DONE failures=0 and the server SERVER DONE children_destroyed=4.
#
#   plan/ws035/tests/p075/run-host.sh [OUTDIR]     (default build/ws035-p075-host)
#   LIBWAYLAND_ROOT=DIR ...   builds the client on DIR/userland/desktop/libwayland (another revision's copy)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
root=$(pwd)
here=plan/ws035/tests/p075
out=${1:-build/ws035-p075-host}
rm -rf "$out"
mkdir -p "$out/include" "$out/objects"

# The runtime directory is short: a socket path has at most 107 bytes.
runtime=$(mktemp -d /tmp/p075.XXXXXX)
trap 'rm -rf "$runtime"' EXIT

# Only the Wayland headers of zedBSD, never its libc's, on the client's include path.
ln -s "$root/include/libc/wayland" "$out/include/wayland"
for header in wayland-client.h wayland-client-core.h wayland-client-protocol.h wayland-util.h; do
	ln -s "$root/include/libc/$header" "$out/include/$header"
done

# The protocol's code for both ends.
wayland-scanner server-header $here/generic-test.xml "$out/generic-test-server-protocol.h"
wayland-scanner client-header $here/generic-test.xml "$out/generic-test-client-protocol.h"
wayland-scanner private-code $here/generic-test.xml "$out/generic-test-protocol.c"

# The server on the host's library.
cc -std=gnu99 -O1 -Wall -Wextra -I"$out" $here/server.c "$out/generic-test-protocol.c" -lwayland-server -o "$out/server"

# The client on zedBSD's library (or another revision's copy of it).
library=${LIBWAYLAND_ROOT:-.}
for source in "$library"/userland/desktop/libwayland/*.c; do
	cc -std=gnu99 -O1 -g -Wall -Wextra -Werror -I"$out/include" -I"$library" -I. -c "$source" -o "$out/objects/$(basename "$source" .c).o"
done
cc -std=gnu99 -O1 -g -Wall -Wextra -I"$out/include" -I"$out" -c "$out/generic-test-protocol.c" -o "$out/objects/protocol-code.o"
cc -std=gnu99 -O1 -g -Wall -Wextra -I"$out/include" -I"$out" $here/client.c "$out"/objects/*.o -lpthread -o "$out/client"

# The run.
XDG_RUNTIME_DIR="$runtime"
export XDG_RUNTIME_DIR
"$out/server" wayland-p075 > "$out/server.log" 2>&1 &
server=$!
i=0
while [ ! -S "$runtime/wayland-p075" ] && [ $i -lt 50 ]; do
	sleep 0.1
	i=$((i + 1))
done
status=0
timeout 30 "$out/client" wayland-p075 > "$out/client.log" 2>&1 || status=1
wait $server || status=1
cat "$out/client.log" "$out/server.log"
grep -q 'CLIENT DONE failures=0' "$out/client.log" || status=1
grep -q 'SERVER DONE children_destroyed=4 client_gone=1' "$out/server.log" || status=1
[ $status -eq 0 ] && echo "p075-host: PASS" || echo "p075-host: FAIL"
exit $status
