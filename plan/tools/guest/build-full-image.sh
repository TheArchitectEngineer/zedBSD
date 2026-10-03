#!/bin/sh
# ws136-p003: builds the full guest image (plan/tools/guest/config-amd64-full.mk: the CI image with clang, make and sshd,
# booting to the text console) the one standard way, plan/tools/guest/test-image.sh, and prints the image's path.  The
# tools that once copied an image of another tree (hybrid-image.sh, plan/ws073/tests/kernel-image.sh, the sh and
# utility guest runs) take this image or its BUILD.
#
#   plan/tools/guest/build-full-image.sh BUILD      (prints BUILD/hdd-image.img)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:?usage: build-full-image.sh BUILD}
plan/tools/guest/test-image.sh plan/tools/guest/config-amd64-full.mk "$build" >&2
echo "$build/hdd-image.img"
