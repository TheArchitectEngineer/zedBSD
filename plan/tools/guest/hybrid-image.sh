#!/bin/sh
# Makes the full guest image of this tree (its kernel, libc, make and sh with the packages, clang and sshd among them)
# for the tests of kernel, libc, make and sh changes that need clang (expat) or the guest harness.  From ws064-p003.
#
# ws136-p003 (2026-10-04 user: test images are a config.mk build and files copied, not past builds): it once copied a
# full image of another tree (/home/awe/zedBSD-rpi4/build/ws053-full-hal-guest, gone) and put this tree's vmunix,
# BOOTX64.EFI, libc.so, make and sh into the copy.  It now builds the full image itself
# (plan/tools/guest/build-full-image.sh: config-amd64-full.mk through test-image.sh), which has all of them from this
# tree, and copies it to OUTPUT_IMAGE (the guest runs may change their copy).
#
#   sh plan/tools/guest/hybrid-image.sh BUILD OUTPUT_IMAGE
#
# BUILD is where the full image is built (the clang package with it: the test runners build it, see
# config-amd64-full.mk).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
build=${1:?usage: hybrid-image.sh BUILD OUTPUT_IMAGE}
out=${2:?usage: hybrid-image.sh BUILD OUTPUT_IMAGE}
if [ $# -gt 2 ]; then
	echo "hybrid-image: a base image is no longer taken (ws136-p003): the full image is built from this tree" >&2
	exit 2
fi
image=$(sh "$(dirname -- "$0")/build-full-image.sh" "$build")
cp --reflink=auto "$image" "$out"
echo "$out"
