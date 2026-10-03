#!/bin/sh
# Builds the SSH guest image without clang (plan/tools/guest/config-amd64-ssh.mk) with the guest harness's files.
# ws136-p001 (2026-10-04): through plan/tools/guest/test-image.sh, the one way test images are built.
#   sh plan/tools/guest/build-ssh-image.sh [BUILD]    (default build/amd64, which the packages link against)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/amd64}
exec plan/tools/guest/test-image.sh plan/tools/guest/config-amd64-ssh.mk "$build"
