#!/bin/sh
# backend-test.sh [OUT] -- the native FreeBSD build and tests of libkeiland-backend, libkeiland and the compositor
# (WS137; the FreeBSD parts WS131 wrote without building).
#
# In the WS137 FreeBSD guest (plan/tools/keiland-freebsd/guest.sh; started here when it is not running and stopped
# again afterwards) it copies this working tree's files that the native build reads, then:
#   build      gmake -f userland/desktop/keiland-freebsd.mk all with the base clang; exit 0 and no "warning:"
#   install    DESTDIR install and the system-inclusive header dependencies
#   audit      native-build-audit.py (selected objects, native header families, SONAME/NEEDED, install boundary)
#   host-seat-freebsd, host-session, host-power   plan/ws131/tests on native FreeBSD (ASan and UBSan)
#   sync-rejected, dmabuf-export-rejected         the native sync adapters against real pipes and closed files
# Each step's output is OUT/<step>.log (default build/keiland-freebsd/backend-test) and OUT/summary.txt has one
# PASS/FAIL/SKIP line per step.  The exit status is 0 only when every step passed.  No GPU is used: the GPU, seat
# and window probes of README.md need the owned i915 guest or a FreeBSD machine.
# Environment: GUEST_* and SSH_PORT as guest.sh, JOBS (8), BACKEND_TEST_EXTRA_PATHS (more paths to copy).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

TOOLS=$(cd "$(dirname -- "$0")" && pwd)
ROOT=$(cd "$TOOLS/../../.." && pwd)
OUT=${1:-$ROOT/build/keiland-freebsd/backend-test}
JOBS=${JOBS:-8}
SRC=/root/keiland-src
STAGE=/root/keiland-stage
BUILD=build/native
MK="gmake -f userland/desktop/keiland-freebsd.mk KEILAND_FREEBSD_BUILD=$BUILD CC=cc"
PATHS="GNUmakefile Makefile include src/libc userland/desktop userland/base userland/packages userland/tests
	plan/tools/keiland-freebsd plan/ws131/tests ${BACKEND_TEST_EXTRA_PATHS:-}"

guest() {
	python3 "$TOOLS/guest.py" "$@"
}

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
rm -f "$OUT"/*.log "$OUT/summary.txt"
started=0
if ! guest status | grep -q '^ssh: ready'; then
	guest start
	started=1
	trap 'guest stop >/dev/null 2>&1 || true' EXIT INT TERM
fi

{
	echo "tree: $(git -C "$ROOT" rev-parse HEAD)$(git -C "$ROOT" diff --quiet HEAD -- $PATHS || echo ' (with uncommitted changes)')"
	echo "guest: $(guest ssh 'freebsd-version -ku | tr "\n" " "; uname -m')"
} > "$OUT/summary.txt"
# shellcheck disable=SC2086
guest copy "$SRC" $PATHS
# The verified downloads of the build (seatd, the emoji font, the dictionary) persist across runs.
guest ssh "mkdir -p /root/keiland-distfiles $SRC/build && ln -s /root/keiland-distfiles $SRC/build/distfiles"

failed=0
# step NAME TIMEOUT COMMAND: runs COMMAND in the guest's tree, records PASS or FAIL.
step() {
	name=$1
	limit=$2
	shift 2
	if GUEST_COMMAND_TIMEOUT=$limit guest ssh "cd $SRC && $*" > "$OUT/$name.log" 2>&1; then
		result=PASS
	else
		result=FAIL
		failed=1
	fi
	echo "$result $name" | tee -a "$OUT/summary.txt"
}
skip() {
	echo "SKIP $1 ($2)" | tee -a "$OUT/summary.txt"
	failed=1
}

step build 3600 "$MK -j$JOBS all"
built=$(tail -1 "$OUT/summary.txt")
warnings=$(grep -c 'warning:' "$OUT/build.log" || true)
if [ "$warnings" != 0 ]; then
	echo "FAIL build-warnings ($warnings lines with warning:)" | tee -a "$OUT/summary.txt"
	failed=1
else
	echo "PASS build-warnings (0)" | tee -a "$OUT/summary.txt"
fi
if [ "$built" = "PASS build" ]; then
	step install 600 "rm -rf $STAGE && $MK install DESTDIR=$STAGE && $MK -j$JOBS header-dependencies"
	if [ "$(tail -1 "$OUT/summary.txt")" = "PASS install" ]; then
		step audit 300 "python3 plan/tools/keiland-freebsd/native-build-audit.py $BUILD $STAGE/opt/keiland"
	else
		skip audit 'install failed'
	fi
else
	skip install 'build failed'
	skip audit 'build failed'
fi

step host-seat-freebsd 300 "env CC=cc sh plan/ws131/tests/host-seat-freebsd.sh"
step host-session 300 "env CC=cc sh plan/ws131/tests/host-session.sh"
step host-power 300 "env CC=cc sh plan/ws131/tests/host-power.sh"
probe="cc -std=gnu17 -Wall -Wextra -Werror -I. -I/usr/local/include"
step sync-rejected 120 "mkdir -p build/probes && $probe plan/tools/keiland-freebsd/sync-rejected.c \
	userland/desktop/libvulkan-compat/freebsd/sync-freebsd.c -o build/probes/sync-rejected && build/probes/sync-rejected"
step dmabuf-export-rejected 120 "mkdir -p build/probes && $probe plan/tools/keiland-freebsd/dmabuf-export-rejected.c \
	userland/desktop/libkeiland-backend-freebsd/sync-freebsd.c -o build/probes/dmabuf-export-rejected && \
	build/probes/dmabuf-export-rejected"

if [ "$started" = 1 ]; then
	guest stop
	trap - EXIT INT TERM
fi
if [ "$failed" = 0 ]; then
	echo "backend-test: PASS ($OUT/summary.txt)"
else
	echo "backend-test: FAIL ($OUT/summary.txt)"
fi
exit "$failed"
