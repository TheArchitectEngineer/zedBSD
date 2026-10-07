#!/bin/sh
# ws090-p025: builds and runs the host test of an input method's text in a page's form controls
# (host-browser-ime.c, through <browser/browser.h> only) against the host build of libbrowser.
#   sh plan/ws090/tests/host-browser-ime.sh [-v]
# The engine is built by plan/ws074/tests/host-build.sh into $BROWSER_HOST_BUILD/plain (default
# build/ws090-browser); the test goes beside it.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
base=${BROWSER_HOST_BUILD:-build/ws090-browser}
BROWSER_HOST_BUILD=$base sh plan/ws074/tests/host-build.sh plain > "$base-build.log" 2>&1 || { tail -20 "$base-build.log"; exit 1; }
out=$base/plain
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -I"$base/include" -o "$out/host-browser-ime" plan/ws090/tests/host-browser-ime.c \
	-L"$out" -Wl,-rpath,'$ORIGIN' -l:libbrowser.so -lm
F=userland/desktop/fonts
"$out/host-browser-ime" plan/ws090/tests/pages $F/Mahora-Regular.ttf $F/JetBrainsMono-Regular.ttf $F/DroidSansFallbackFull.ttf "$@"
