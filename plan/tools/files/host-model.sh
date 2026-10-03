#!/bin/sh
# ws093-p003: builds files' host tests (host-build.sh) and runs files-model (host-model.c) in a temporary folder.
#
#   sh plan/tools/files/host-model.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
sh plan/tools/files/host-build.sh || exit 1
temporary=$(mktemp -d)
# A second folder on another file system when the host has one (/dev/shm): the moves and trashes across volumes (ws127-p003).
other=""
if [ -d /dev/shm ] && [ "$(stat -c %d /dev/shm 2>/dev/null)" != "$(stat -c %d "$temporary" 2>/dev/null)" ]; then
	other=$(mktemp -d -p /dev/shm) || other=""
fi
# A third folder on a volume of the user's (a tmpfs mounted with sudo, when sudo needs no password) for the volume's
# trash (ws127-p003); /dev/shm is a system's mount, which keeps no trash of its own (ws127-p008).
volume=""
if [ -n "$other" ] && sudo -n true 2>/dev/null; then
	volume=$(pwd)/build/ws071-host/volume.$$
	mkdir -p "$volume"
	sudo -n mount -t tmpfs -o "uid=$(id -u),gid=$(id -g),mode=0755" tmpfs "$volume" || volume=""
fi
# The thumbnails kept on disk go to the temporary folder, not the user's cache (ws127-p002).
XDG_CACHE_HOME=$temporary/cache timeout 300 build/ws071-host/files-model "$temporary" $other $volume
status=$?
rm -rf "$temporary"
[ -n "$other" ] && rm -rf "$other"
if [ -n "$volume" ]; then
	sudo -n umount "$volume"
	rmdir "$volume"
fi
exit $status
