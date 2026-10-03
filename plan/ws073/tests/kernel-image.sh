#!/bin/sh
# Makes a guest image that boots this tree's kernel with the full userland (clang, sshd) for the kernel bug fixes of
# WS073.
#
# ws136-p003 (2026-10-04 user: test images are a config.mk build and files copied, not past builds): it once copied a
# full image of another tree (/home/awe/zedBSD-rpi4/build/ws053-full-hal-guest, gone) and replaced /vmunix on its ESP.
# It now builds the full image of this tree (plan/tools/guest/build-full-image.sh), which has this tree's kernel, in
# BUILD, and copies it to OUTPUT_IMAGE (the tests change their copy).
#
#   sh plan/ws073/tests/kernel-image.sh BUILD OUTPUT_IMAGE
#
# BUILD is where the full image is built (the clang package with it: the test runners build it).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
build=${1:?usage: kernel-image.sh BUILD OUTPUT_IMAGE}
out=${2:?usage: kernel-image.sh BUILD OUTPUT_IMAGE}
if [ $# -gt 2 ]; then
	echo "kernel-image: a base image is no longer taken (ws136-p003): the full image is built from this tree" >&2
	exit 2
fi
here=$(cd "$(dirname -- "$0")" && pwd)
image=$(sh "$here/../../tools/guest/build-full-image.sh" "$build")
cp --reflink=auto "$image" "$out"
echo "$out"
