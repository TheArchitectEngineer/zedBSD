#!/bin/sh
# ws074: downloads the lists zdesktop-browser generates tables from into
# userland/base/zdesktop-browser/distfiles/ and checks their SHA-256, as the package's Makefile does
# (the URL and the digest are read from it), for the host build.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
makefile=userland/base/zdesktop-browser/Makefile
url=$(sed -n 's/^ZDESKTOP_BROWSER_ENTITIES_URL := //p' $makefile)
sum=$(sed -n 's/^ZDESKTOP_BROWSER_ENTITIES_SHA256 := //p' $makefile)
file=userland/base/zdesktop-browser/distfiles/entities.json
mkdir -p "$(dirname "$file")"
if [ ! -f "$file" ]; then
	curl --fail --location --silent --show-error --output "$file.part" "$url"
	mv "$file.part" "$file"
fi
actual=$(sha256sum "$file" | cut -d' ' -f1)
if [ "$actual" != "$sum" ]; then
	echo "fetch-distfiles: $file has SHA-256 $actual, not the pinned $sum" >&2
	exit 1
fi
