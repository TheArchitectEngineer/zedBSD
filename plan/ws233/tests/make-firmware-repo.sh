#!/bin/sh
# Lay out the contents of the zedBSD-firmware repository from the files
# fetch-upstream.sh fetched and checked.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: make-firmware-repo.sh INVENTORY UPSTREAM-DIR README-HEAD DIRECTORIES OUTDIR
# OUTDIR must not exist, or be the git checkout of the repository, whose files
# are then overwritten.  Each file is checked again against the inventory
# after it is copied.  DIRECTORIES gives each directory's notice (what the
# firmware is and its copyright, the text make shows before it asks about the
# license), license files and terms, for the directory's README.md.
set -eu

inventory=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
upstream=$2
head=$3
directories=$(cd "$(dirname "$4")" && pwd)/$(basename "$4")
tree=$(pwd)
out=$5

if test -e "$out" && ! test -d "$out/.git"; then
	echo "make-firmware-repo: $out exists" >&2
	exit 1
fi
mkdir -p "$out"

grep -v '^#' "$inventory" | while IFS='|' read -r package name url size hash; do
	target="$out/$package/$name"
	mkdir -p "$(dirname "$target")"
	cp -- "$upstream/$package/$name" "$target"
	chmod 0644 "$target"
	actual_size=$(wc -c <"$target" | tr -d '[:space:]')
	actual_hash=$(sha256sum "$target" | awk '{print $1}')
	if test "$actual_size" != "$size" || test "$actual_hash" != "$hash"; then
		echo "make-firmware-repo: $package/$name differs from the inventory" >&2
		exit 1
	fi
done

grep -v '^#' "$directories" | while IFS='|' read -r directory notice licenses terms; do
	{
		printf '# %s\n\n```\n' "$directory"
		cat "$tree/$notice"
		printf '```\n\n## License\n\n'
		for license in $licenses; do
			printf -- '- [`%s`](%s)\n' "$license" "$license"
		done
		printf '\n%s\n\n' "$terms"
		printf 'The files are unmodified copies of the upstream files named above. Read the license before you\n'
		printf 'redistribute or use them.\n\n## Files\n\n| File | Size | SHA-256 |\n| --- | ---: | --- |\n'
		grep "^$directory|" "$inventory" | while IFS='|' read -r package name url size hash; do
			printf '| `%s` | %s | `%s` |\n' "$name" "$size" "$hash"
		done
	} >"$out/$directory/README.md"
done

{
	cat "$head"
	printf '\n| File | Size | SHA-256 | Upstream URL |\n| --- | ---: | --- | --- |\n'
	grep -v '^#' "$inventory" | while IFS='|' read -r package name url size hash; do
		printf '| `%s/%s` | %s | `%s` | <%s> |\n' "$package" "$name" "$size" "$hash" "$url"
	done
} >"$out/README.md"

(cd "$out" && grep -v '^#' "$inventory" | while IFS='|' read -r package name url size hash; do
	printf '%s  %s/%s\n' "$hash" "$package" "$name"
done >SHA256SUMS && sha256sum --check --quiet SHA256SUMS)
