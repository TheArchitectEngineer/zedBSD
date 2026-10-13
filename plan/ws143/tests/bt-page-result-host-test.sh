#!/bin/sh
# ws143-p007 (T1-593): the host test of the words the Settings Bluetooth page shows for a request's result.  Builds Linux
# Settings with userland/desktop/keiland-linux.mk into BUILD (its objects), links plan/ws143/tests/bt-page-result-host-test.c
# with them in place of main.o, and runs it.  Linux hosts only.
#   sh plan/ws143/tests/bt-page-result-host-test.sh [BUILD]   (default build/ws143-bt-page)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/ws143-bt-page}
cc=${CC:-cc}
timeout 900 make -s -f userland/desktop/keiland-linux.mk -j8 KEILAND_LINUX_BUILD="$build" "$build/bin/settings"
objects=$(find "$build/obj/userland/desktop/settings" -name '*.o' ! -name main.o | sort)
# shellcheck disable=SC2086
"$cc" -std=gnu17 -D_GNU_SOURCE -Wall -Wextra -Werror -I. -Iuserland/desktop/include -I"$build/include" -o "$build/bt-page-result-host-test" \
	plan/ws143/tests/bt-page-result-host-test.c $objects \
	"$build/obj/userland/desktop/artwork/mark.o" "$build/obj/userland/desktop/preview/client.o" \
	"$build/obj/userland/desktop/preview/linux/spawn.o" \
	-L"$build/lib" -Wl,-rpath-link,"$build/lib" \
	-l:libkeiland.so -l:libwayland-client.so -l:libtruetype.so -l:libvulkan.so.1 -lm -pthread
LD_LIBRARY_PATH="$build/lib" timeout 30 "$build/bt-page-result-host-test"
