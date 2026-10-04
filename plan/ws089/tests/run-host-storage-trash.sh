#!/bin/sh
# ws089-p023: builds and runs the host test of the Storage page's Trash (host-storage-trash.c with
# userland/desktop/settings/storage-trash.c) on a trash made here: files, a folder of folders, a link to a file
# outside, and their .trashinfo.  Last line: host-storage-trash: PASS.
#   sh plan/ws089/tests/run-host-storage-trash.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=gnu89 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -I. \
    plan/ws089/tests/host-storage-trash.c userland/desktop/settings/storage-trash.c -o "$work/host-storage-trash"
trash="$work/home/.local/share/Trash"
mkdir -p "$trash/files/Project/src/deep" "$trash/info" "$work/outside"
head -c 5000 /dev/urandom > "$work/outside/keep"
head -c 1000 /dev/urandom > "$trash/files/report.pdf"
head -c 2000 /dev/urandom > "$trash/files/Project/src/deep/main.c"
ln -s "$work/outside/keep" "$trash/files/link"
for name in report.pdf Project link; do
	printf '[Trash Info]\nPath=/home/user/%s\nDeletionDate=2026-10-05T08:00:00\n' "$name" > "$trash/info/$name.trashinfo"
done
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$work/host-storage-trash" "$work/home" "$work/outside/keep"
