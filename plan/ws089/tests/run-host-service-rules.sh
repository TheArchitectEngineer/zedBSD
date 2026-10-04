#!/bin/sh
# ws089-p025: builds and runs the host test of sessiond's SERVICE rules (host-service-rules.c with
# userland/desktop/sessiond/service-rules.c).  Last line: host-service-rules: PASS.
#   sh plan/ws089/tests/run-host-service-rules.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=gnu89 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I. \
    plan/ws089/tests/host-service-rules.c userland/desktop/sessiond/service-rules.c -o "$work/host-service-rules"
UBSAN_OPTIONS=halt_on_error=1 "$work/host-service-rules"
