#!/bin/sh
# ws089-p025: builds and runs the host test of the zedBSD backend's Remote Login (host-sharing.c with
# userland/desktop/libkeiland-backend-zedbsd/sharing-zedbsd.c and userland/base/common/sha256.c), with a host key
# made here by ssh-keygen and its fingerprint as ssh-keygen -l prints it.  Last line: host-sharing: PASS.
#   sh plan/ws089/tests/run-host-sharing.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/ssh"
ssh-keygen -q -t ed25519 -N '' -f "$work/ssh/ssh_host_ed25519_key"
expected=$(ssh-keygen -l -E sha256 -f "$work/ssh/ssh_host_ed25519_key.pub" | awk '{print $2}')
cc -std=gnu89 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -fsanitize=address,undefined -fno-omit-frame-pointer -I. \
    -DSHARING_KEY_DIR="\"$work/ssh\"" \
    plan/ws089/tests/host-sharing.c userland/desktop/libkeiland-backend-zedbsd/sharing-zedbsd.c userland/base/common/sha256.c \
    -o "$work/host-sharing"
UBSAN_OPTIONS=halt_on_error=1 "$work/host-sharing" "$expected"
