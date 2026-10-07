#!/bin/sh
# ws051-p004a: builds and runs the host test of the external DP sink probe (dp-ext.c over a fake sink, built with
# ASan/UBSan), with the 5330's USB-C monitor's EDID from m3-5330-20261007.
#   sh plan/ws051/tests/host-dpext.sh [OUT_DIR]   (default build/ws051-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
dir=${1:-build/ws051-host}
mkdir -p "$dir"
display=src/drivers/gpu/i915/display
flags="-std=c11 -O1 -g -Wall -Wextra -Werror -Wdeclaration-after-statement -Wstrict-prototypes -Wmissing-prototypes -Wshadow"
sanitize="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
cc $flags $sanitize -I"$display" "$display/dp-ext.c" plan/ws051/tests/host-dpext.c -o "$dir/host-dpext"
"$dir/host-dpext" plan/ws051/tests/m3-5330-20261007/debugfs/DP-2.edid
