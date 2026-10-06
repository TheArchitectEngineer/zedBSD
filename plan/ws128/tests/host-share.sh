#!/bin/sh
# ws128-p005: builds and runs host-share.c (Image Viewer's share.c with Files' trash.c, apps.c and the mount table's reader,
# libkeiland's settings under the host stand-in) in a temporary folder.
#   sh plan/ws128/tests/host-share.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws128-share-host
mkdir -p "$out/include"
ln -sf "$(pwd)/userland/desktop/keiland/truetype.h" "$out/include/truetype.h"
ln -sf "$(pwd)/userland/desktop/keiland/keiland.h" "$out/include/keiland.h"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
cc=${CC:-cc}
"$cc" -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -D_GNU_SOURCE -I"$out/include" -I. -Iuserland/desktop/libkeiland \
    -o "$out/host-share" plan/ws128/tests/host-share.c userland/desktop/imageview/share.c userland/desktop/files/trash.c \
    userland/desktop/files/apps.c userland/desktop/files/mntent/mounts-mntent.c \
    userland/desktop/libkeiland/settings-cache.c userland/desktop/libkeiland/settings-app.c \
    userland/desktop/settings-keys/settings-keys.c plan/tools/settings/host-kl-settings.c
temporary=$(mktemp -d)
status=0
timeout 60 "$out/host-share" "$temporary" || status=$?
rm -rf "$temporary"
exit $status
