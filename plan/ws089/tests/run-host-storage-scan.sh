#!/bin/sh
# ws089-p023: builds and runs the host test of the Storage page's analysis (host-storage-scan.c with
# userland/desktop/settings/storage-scan.c) on a tree made here: folders of files of several sizes, a file of two
# links, a link to a large file outside the tree, a folder that cannot be read, and a large tree of 30000 files
# for the stop.  The total is compared with GNU du -sx -B1.  Last line: host-storage-scan: PASS.
#   sh plan/ws089/tests/run-host-storage-scan.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
work=$(mktemp -d)
trap 'chmod -R u+rwx "$work" 2>/dev/null; rm -rf "$work"' EXIT
cc -std=gnu89 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -I. \
    plan/ws089/tests/host-storage-scan.c userland/desktop/settings/storage-scan.c -o "$work/host-storage-scan"

# The tree.
root="$work/home"
mkdir -p "$root/Big/a/b/c" "$root/Small" "$root/Locked" "$root/Empty" "$work/outside"
head -c 3000000 /dev/urandom > "$root/Big/a/b/c/blob"
head -c 500000 /dev/urandom > "$root/Big/a/one"
ln "$root/Big/a/one" "$root/Big/two-links"
head -c 12345 /dev/urandom > "$root/Small/note"
head -c 100 /dev/urandom > "$root/top.txt"
head -c 9000000 /dev/urandom > "$work/outside/huge"
ln -s "$work/outside/huge" "$root/Small/huge-link"
head -c 4000 /dev/urandom > "$root/Locked/secret"
chmod 000 "$root/Locked"
du_bytes=$(du -sx -B1 "$root" 2>/dev/null | awk '{print $1}')

# The large tree.
large="$work/large"
i=0
while [ $i -lt 30 ]; do
	mkdir -p "$large/d$i"
	(cd "$large/d$i" && seq 1 1000 | xargs touch)
	i=$((i + 1))
done

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$work/host-storage-scan" "$root" "$du_bytes" Big "$large"
