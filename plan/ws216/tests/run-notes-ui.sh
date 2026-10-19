#!/bin/sh
# ws216-p006: builds Notes' toolbar (ui.c) with libtruetype and a stand-in for libkeiland's theme with the host's C
# compiler (plain, ASan and UBSan) into host-notes-ui, and runs the acceptance of plan/ws216/phase006/phase.md section 8
# (U2, U3) with the desktop's font:
#   sh plan/ws216/tests/run-notes-ui.sh
# Output: build/ws216-p006-host (a new run each time behind the name; nothing is removed, Q1 cleans the old runs).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws216-p006-host
cc=${CC:-cc}
. plan/tools/fresh-out.sh
fresh_out "$out"
out=$(cd "$out" && pwd -P)
mkdir -p "$out/include"
for header in pdf.h; do
	ln -sf "$(pwd)/include/libc/$header" "$out/include/$header"
done
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
for variant in plain asan ubsan; do
	flags="-std=c99 -pedantic -O1 -g -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I$out/include -I. -Iuserland/desktop/include"
	if [ "$variant" = asan ]; then
		flags="$flags -fsanitize=address -fno-omit-frame-pointer"
	fi
	if [ "$variant" = ubsan ]; then
		flags="$flags -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all"
	fi
	loose=$(echo "$flags" | sed 's/-std=c99 -pedantic/-std=gnu11/; s/-Werror//')
	objects=
	for file in userland/desktop/libtruetype/*.c; do
		object="$out/$(basename "$file" .c)-$variant.o"
		"$cc" $loose -w -I"$(dirname "$file")" -c "$file" -o "$object"
		objects="$objects $object"
	done
	# shellcheck disable=SC2086
	"$cc" $flags -Iuserland/desktop/notes userland/desktop/notes/ui.c plan/ws216/tests/host-notes-ui.c $objects -lm -o "$out/host-notes-ui-$variant"
	timeout 300 "$out/host-notes-ui-$variant" userland/desktop/fonts/Mahora-Regular.ttf "$out/ui-$variant.log"
done
echo "run-notes-ui: ok"
