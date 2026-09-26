#!/bin/sh
# ws063-p002: runs plan/tools/ufs/crash-test.sh with this phase's runtime and
# volume paths, output to build/ws063/crash-NAME.log.
#   sh plan/ws063/tests/run-crash.sh NAME IMAGE SECONDS...   (PROFILE= passes through)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root" || exit 1
name=$1
shift
mkdir -p build/ws063
GUEST_RUNTIME=build/ws063-crash-run VOLUME=build/ws063/crash-$name.img \
	bash plan/tools/ufs/crash-test.sh "$@" > "build/ws063/crash-$name.log" 2>&1
echo "exit $?" >> "build/ws063/crash-$name.log"
