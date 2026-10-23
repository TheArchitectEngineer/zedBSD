#!/bin/sh
# Reads PS registers of a running ZYBO over JTAG (the DAP's AHB-AP, no core
# is halted), for checking what the boot left in the SLCR and the AFI of the
# HP ports (WS288 p003).
#   plan/tools/zybo/ps-regs.sh ADDR...      (e.g. 0xF8008000 0xF8000900)
# Prints "zybo: ADDR = VALUE" per register.  Takes the host's board lock.
# Not for DDR before SPL has set it up (that read hangs the AHB-AP).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
. "$(dirname -- "$0")/zybo-common.sh"
[ $# -ge 1 ] || { echo "usage: ps-regs.sh ADDR..." >&2; exit 2; }
zybo_lock
zybo_openocd "init; foreach a {$*} { echo [format {zybo: %s = 0x%08x} \$a [zybo_rd \$a]] }; shutdown" |
	grep -E '^zybo:|Error'
