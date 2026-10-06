#!/bin/sh
# ws089-p012 (C1), WS131 p011: builds Settings' network.c with host-slot.c (a pretend kl_system, the compositor's network) and
# runs it: requests asked for while another is out wait in one slot and go after its answer.  Last line: host-slot: PASS.
#   sh plan/ws089/tests/host-slot.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws089-host
mkdir -p "$out/include" "$out/obj"
mkdir -p "$out/include/truetype"
ln -sf "$(pwd)/userland/desktop/include/truetype/truetype.h" "$out/include/truetype/truetype.h"
mkdir -p "$out/include/keiland"
ln -sf "$(pwd)/userland/desktop/include/keiland/keiland.h" "$out/include/keiland/keiland.h"
cc=${CC:-cc}
flags="-O2 -g -std=gnu89 -Wall -Wextra -Werror -Wno-unused-parameter -D_GNU_SOURCE -I$out/include -Iuserland/desktop/settings -I."
"$cc" $flags -c userland/desktop/settings/network.c -o "$out/obj/slot-network.o"
"$cc" $flags -c plan/ws089/tests/host-slot.c -o "$out/obj/host-slot.o"
"$cc" -o "$out/host-slot" "$out/obj/host-slot.o" "$out/obj/slot-network.o"
exec "$out/host-slot"
