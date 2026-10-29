#!/bin/sh
# Builds and runs the host test of the handwriting's recognition pool (wayland/keyboard-hand.c with hand-cloud.c,
# lane.c and threads.c) under ASan+UBSan and TSan, with the templates of the package hand-hershey (TEMPLATES, default
# build/packages/hand-hershey/hershey.txt) put where the compositor looks for them under a test data directory.
# Prints "hand-pool: PASS" per sanitizer and checks the templates were read once.
#   sh plan/ws309/tests/hand-pool/run.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
out=${1:-build/ws309/host-hand-pool}
templates=${TEMPLATES:-build/packages/hand-hershey/hershey.txt}
mkdir -p "$out/share/keiland/hand"
cp "$templates" "$out/share/keiland/hand/hershey.txt"
status=0
for sanitizer in address,undefined thread; do
	name=host-hand-pool-${sanitizer%%,*}
	${CC:-cc} -std=gnu89 -Wall -Wextra -Werror -D_GNU_SOURCE -I. -DKEILAND_DATADIR="\"$(pwd)/$out/share\"" \
	    -fsanitize=$sanitizer -g -pthread \
	    userland/desktop/wayland/keyboard-hand.c userland/desktop/wayland/hand-cloud.c \
	    userland/desktop/wayland/lane.c userland/desktop/wayland/threads.c \
	    plan/ws309/tests/hand-pool/host-hand-pool.c -lm -o "$out/$name"
	echo "== sanitizer $sanitizer"
	timeout 120 "$out/$name" > "$out/$name.out" 2>&1 || status=1
	grep -E 'hand-pool|KWL OSK hand pool|KWL THREADS' "$out/$name.out"
	reads=$(grep -c 'KWL OSK hand templates path=' "$out/$name.out" || true)
	echo "templates read $reads time(s)"
	[ "$reads" = 1 ] || { echo "hand-pool: FAIL the templates were read $reads times"; status=1; }
	grep -E 'ERROR|WARNING: ThreadSanitizer|runtime error' "$out/$name.out" && status=1
done
[ $status = 0 ] && echo "hand-pool run: PASS" || echo "hand-pool run: FAIL"
exit $status
