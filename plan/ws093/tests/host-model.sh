#!/bin/sh
# ws093-p002: Files' host model test (plan/tools/files/host-model.c) with the openers of this WS.
# plan/tools/files/host-build.sh no longer links (files/touch.c uses libkeiland's gesture and scroller since
# ws081-p010), so this runs a copy of it into build/ws093-host that also builds libkeiland's gesture.c, motion.c and
# scroll.c, then runs files-model.
#   plan/ws093/tests/host-model.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
root=$(pwd)
mkdir -p build/ws093-host
sed -e "s|^cd .*|cd '$root'|" \
    -e 's|^out=build/ws071-host|out=build/ws093-host|' \
    -e 's|^# files without the window|for file in userland/desktop/libkeiland/gesture.c userland/desktop/libkeiland/scroll.c userland/desktop/libkeiland/motion.c; do "$cc" $flags -c "$file" -o "$out/obj/keiland-$(basename "$file" .c).o"; objects="$objects $out/obj/keiland-$(basename "$file" .c).o"; done\n&|' \
    plan/tools/files/host-build.sh > build/ws093-host/host-build.sh
sh build/ws093-host/host-build.sh
temporary=$(mktemp -d)
timeout 300 build/ws093-host/files-model "$temporary"
status=$?
rm -rf "$temporary"
exit $status
