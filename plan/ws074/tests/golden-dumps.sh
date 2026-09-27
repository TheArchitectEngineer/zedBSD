#!/bin/sh
# ws074: compares browser's text dumps of the test pages with the reviewed golden files.
#
#   sh plan/ws074/tests/golden-dumps.sh [--update] [--program PATH] KIND...    (KIND: dom, style, layout, paint)
#
# For each plan/ws074/tests/pages/NAME.html and each KIND, `PROGRAM --dump=KIND` is compared with
# plan/ws074/tests/golden/NAME.KIND (a golden file that does not exist yet is skipped with a note).
# --update rewrites the golden files; review the difference before committing them.
# PROGRAM defaults to the host build, build/ws074-host/plain/browser.
# The layout is measured with the fonts of the guest image (build/ws035-fonts: Inter, JetBrains Mono,
# Droid Sans Fallback), passed with --font= and the like, so the host and the guest agree.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
program=build/ws074-host/plain/browser
update=0
while [ $# -gt 0 ]; do
	case $1 in
	--update) update=1; shift ;;
	--program) program=$2; shift 2 ;;
	*) break ;;
	esac
done
mkdir -p plan/ws074/tests/golden build/ws074-dumps
fonts=build/ws035-fonts
[ -f $fonts/Inter.ttf ] || { echo "golden-dumps: no $fonts (link it from the main checkout's build/)"; exit 1; }
failed=0
checked=0
for kind in "$@"; do
	for page in plan/ws074/tests/pages/*.html; do
		name=$(basename "$page" .html)
		golden=plan/ws074/tests/golden/$name.$kind
		out=build/ws074-dumps/$name.$kind
		"$program" --dump="$kind" --font=$fonts/Inter.ttf --mono-font=$fonts/JetBrainsMono-Regular.ttf \
		    --fallback-font=$fonts/DroidSansFallbackFull.ttf "$page" > "$out"
		if [ $update = 1 ]; then
			cp "$out" "$golden"
			echo "updated $golden"
			continue
		fi
		if [ ! -f "$golden" ]; then
			echo "skip $name.$kind: no golden file"
			continue
		fi
		checked=$((checked + 1))
		if ! cmp -s "$out" "$golden"; then
			echo "FAIL $name.$kind"
			diff "$golden" "$out" | head -20
			failed=$((failed + 1))
		fi
	done
done
echo "golden-dumps: $checked checked, $failed failed"
[ $failed = 0 ]
