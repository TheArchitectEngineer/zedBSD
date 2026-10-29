#!/bin/sh
# Builds and runs preview-test.c (keiland-preview's callers' side on Linux) against a Linux build's keiland-preview.
#
#   sh plan/ws314/tests/preview-test.sh LINUX_BUILD     # e.g. build/ws314-p014-linux
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
build=$1
out=build/ws314-preview-test
mkdir -p "$out/libexec"
dir=$(mktemp -d "$PWD/$out/run.XXXXXX")
# The program runs with an empty environment: a wrapper gives it the build's libraries.
printf '#!/bin/sh\nLD_LIBRARY_PATH=%s exec %s "$@"\n' "$PWD/$build/lib" "$PWD/$build/libexec/keiland-preview" > "$out/libexec/keiland-preview"
chmod 0755 "$out/libexec/keiland-preview"
cc -std=gnu17 -D_GNU_SOURCE -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
	-I. -Iuserland/desktop/include "-DKEILAND_LIBEXECDIR=\"$PWD/$out/libexec\"" \
	plan/ws314/tests/preview-test.c userland/desktop/preview/client.c userland/desktop/preview/linux/spawn.c \
	-o "$out/preview-test" || exit 1
XDG_RUNTIME_DIR=$dir timeout 120 "$out/preview-test" docs/howto/keiland-examples/01-app.png "$dir"
