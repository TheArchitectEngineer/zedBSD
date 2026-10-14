#!/bin/sh
# Fetch every file of the zedBSD-firmware inventory from its original
# upstream URL into a new directory and check its size and SHA-256.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: fetch-upstream.sh INVENTORY OUTDIR
# OUTDIR must not exist.  Files land at OUTDIR/PACKAGE/REPO-FILE.
set -eu

inventory=$1
out=$2
fetch='curl --fail --location --silent --show-error --retry 3 --retry-all-errors --retry-delay 5'

if test -e "$out"; then
	echo "fetch-upstream: $out exists" >&2
	exit 1
fi
mkdir -p "$out"

failed=0
grep -v '^#' "$inventory" | while IFS='|' read -r package name url size hash; do
	target="$out/$package/$name"
	mkdir -p "$(dirname "$target")"
	$fetch --output "$target" "$url"
	actual_size=$(wc -c <"$target" | tr -d '[:space:]')
	actual_hash=$(sha256sum "$target" | awk '{print $1}')
	if test "$actual_size" = "$size" && test "$actual_hash" = "$hash"; then
		echo "OK   $package/$name $size"
	else
		echo "FAIL $package/$name size=$actual_size sha256=$actual_hash" >&2
		exit 1
	fi
done
