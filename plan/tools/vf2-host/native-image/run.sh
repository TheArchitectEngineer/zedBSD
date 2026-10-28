#!/bin/sh
# Host test of the riscv64 native disk image (ws256-p015): builds the image from small stand-in inputs with and
# without the VisionFive 2's firmware partitions and checks the partition table, the EFI system partition, the UFS
# root partition and the firmware's bytes.  Usage: run.sh OUTDIR (a fresh directory is made under it).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
here=$(cd "$(dirname -- "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
. "$root/plan/tools/fresh-out.sh"
fresh_out "${1:?usage: run.sh OUTDIR}/native-image"
exec python3 -I "$here/check-native-image.py" "$root" "$fresh_dir"
