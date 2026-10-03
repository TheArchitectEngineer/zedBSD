#!/bin/sh
# Makes a guest image that boots this tree's kernel with the userland of an
# existing full guest image (clang, sshd), for kernel-only bug fixes (WS073).
# The base image is copied, never changed; only /vmunix on the copy's ESP is
# replaced (mtools).
#
#   sh plan/ws073/tests/kernel-image.sh VMUNIX OUTPUT_IMAGE [BASE_IMAGE]
#
# BASE_IMAGE defaults to the measurement guest image of the main tree,
# /home/awe/zedBSD-rpi4/build/ws053-full-hal-guest/hdd-image.img.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
vmunix=$1
out=$2
base=${3:-/home/awe/zedBSD-rpi4/build/ws053-full-hal-guest/hdd-image.img}

# The ESP starts at sector 2048 in these images.
cp --reflink=auto "$base" "$out.tmp"
mcopy -o -i "$out.tmp@@1048576" "$vmunix" ::/vmunix
mv "$out.tmp" "$out"
echo "$out"
