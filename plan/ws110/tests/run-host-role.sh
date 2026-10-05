#!/bin/sh
# ws110-p001: builds and runs the host test of the compositor's role (host-role.c with
# userland/desktop/wayland/role.c).  Last line: host-role: PASS.
#   sh plan/ws110/tests/run-host-role.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws110-host
mkdir -p "$out"
cc -std=c99 -Wall -Wextra -Werror -I. plan/ws110/tests/host-role.c userland/desktop/wayland/role.c -o "$out/host-role"
timeout 30 "$out/host-role"
