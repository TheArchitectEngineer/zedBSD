#!/bin/sh
# Fetches (once) and checks the Mesa release whose Valhall ISA tables and assembler cases the Mali compiler's host test
# reads, and prints the extracted tree's path.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws313/tests/mali-mesa-source.sh   (from anywhere)
# The tarball is kept in build/distfiles and checked against its pinned size and SHA-256 on every run; only
# src/panfrost and the licence are extracted, into build/mesa-26.2.6-src, through a new directory that is renamed into
# place once the extraction is whole.  Nothing is removed.  The only network access is one HTTPS GET of the tarball.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
version=26.2.6
url=https://archive.mesa3d.org/mesa-$version.tar.xz
size=68703420
sha256=b9436b3e1a8be76fbd4823dc884f90596ab7c6ac5e1867a305e66ffccfb31a92
distfiles=$repo/build/distfiles
tarball=$distfiles/mesa-$version.tar.xz
tree=$repo/build/mesa-$version-src
stamp=$tree/.zedbsd-extracted

check() {
	actual_size=$(wc -c < "$1" | tr -d '[:space:]')
	actual_hash=$(sha256sum "$1" | awk '{print $1}')
	[ "$actual_size" = "$size" ] && [ "$actual_hash" = "$sha256" ]
}

mkdir -p "$distfiles"
if [ ! -f "$tarball" ]; then
	part=$(mktemp "$distfiles/.mesa-$version.XXXXXX")
	timeout 600 curl --fail --location --silent --show-error --output "$part" "$url"
	if ! check "$part"; then
		echo "mali-mesa-source: $url does not have the pinned size and SHA-256 (left at $part)" >&2
		exit 1
	fi
	mv -- "$part" "$tarball"
fi
if ! check "$tarball"; then
	echo "mali-mesa-source: $tarball does not have the pinned size and SHA-256" >&2
	exit 1
fi

if [ ! -f "$stamp" ] || [ "$(cat "$stamp")" != "$sha256" ]; then
	if [ -e "$tree" ]; then
		echo "mali-mesa-source: $tree exists without a matching stamp; move it aside" >&2
		exit 1
	fi
	mkdir -p "$repo/build"
	work=$(mktemp -d "$repo/build/.mesa-$version-src.XXXXXX")
	timeout 600 tar -xJf "$tarball" -C "$work" "mesa-$version/src/panfrost" "mesa-$version/docs/license.rst"
	printf '%s\n' "$sha256" > "$work/.zedbsd-extracted"
	mv -- "$work" "$tree"
fi
echo "$tree/mesa-$version"
