#!/bin/sh
# ws100: builds the audiod guest image (plan/ws100/tests/config-amd64-audiod.mk, with audiod-feedback compiled from
# plan/ws100/tests/audiod-feedback.c into BUILD/tests/ first) with the guest harness's files.
# ws136-p001 (2026-10-04): through plan/tools/guest/test-image.sh.
#
#   plan/ws100/tests/build-audiod-image.sh [BUILD]     (default build/amd64)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/amd64}
AUDIOD_CONFIG=plan/ws100/tests/config-amd64-audiod.mk sh plan/ws100/tests/build-audiod-feedback.sh "$build"
exec plan/tools/guest/test-image.sh plan/ws100/tests/config-amd64-audiod.mk "$build"
