#!/bin/sh
# ws081-p006: the host test of the view's placed scroll (<browser.h> version 2: browser_view_scroll_to,
# browser_view_scroll_range, browser_view_set_overscroll), linked with the engine's objects of WS074's host
# build, and the browser shell's touch screen (userland/desktop/browser/shell/touch.c) on libkeiland.
#   sh plan/ws074/tests/host-build.sh plain      (first: the engine's objects)
#   plan/ws081/tests/run-browser-scroll.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
host=build/ws074-host/plain
out=build/ws081-p006-host
cc=${CC:-cc}
fonts=userland/desktop/fonts
mkdir -p "$out"
flags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -Iuserland/desktop/browser -Ibuild/ws074-host/include"
engine=$(ls $host/obj/*.o | grep -v -e '/main\.o$' -e '/browser_main\.o$')
"$cc" $flags -o "$out/host-browser-scroll" plan/ws081/tests/host-browser-scroll.c $engine -lvulkan -lm
"$out/host-browser-scroll" plan/ws081/tests/pages $fonts/Mahora-Regular.ttf $fonts/JetBrainsMono-Regular.ttf $fonts/DroidSansFallbackFull.ttf
