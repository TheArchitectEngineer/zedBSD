#!/bin/sh
# ws063-p002: builds the SSH guest image (config-amd64-ssh.mk) into BUILD.
#   sh plan/ws063/tests/build-image.sh [BUILD]    (default build/amd64, which the packages link against)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root" || exit 1
build=${1:-build/amd64}
extra=$(python3 plan/tools/guest/guest.py extra-files)
eval "make -j48 ZEDBSD_CONFIG=plan/ws063/tests/config-amd64-ssh.mk BUILD=$build $extra disk-image"
