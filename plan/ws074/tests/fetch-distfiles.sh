#!/bin/sh
# ws074: downloads the lists zdesktop-browser generates tables from into
# userland/base/zdesktop-browser/distfiles/ and checks their SHA-256, as the package's Makefile does
# (the URLs and the digests are read from it), for the host build.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
makefile=userland/base/zdesktop-browser/Makefile
distdir=userland/base/zdesktop-browser/distfiles
mkdir -p "$distdir"

# fetch NAME FILE: the list NAME of the Makefile (NAME_URL, NAME_SHA256) into FILE.
fetch() {
	url=$(sed -n "s/^ZDESKTOP_BROWSER_$1_URL := //p" $makefile)
	sum=$(sed -n "s/^ZDESKTOP_BROWSER_$1_SHA256 := //p" $makefile)
	file=$distdir/$2
	if [ ! -f "$file" ]; then
		curl --fail --location --silent --show-error --output "$file.part" "$url"
		mv "$file.part" "$file"
	fi
	actual=$(sha256sum "$file" | cut -d' ' -f1)
	if [ "$actual" != "$sum" ]; then
		echo "fetch-distfiles: $file has SHA-256 $actual, not the pinned $sum" >&2
		exit 1
	fi
}

fetch ENTITIES entities.json
fetch UNICODE_DATA UnicodeData-16.0.0.txt
fetch SPECIAL_CASING SpecialCasing-16.0.0.txt
