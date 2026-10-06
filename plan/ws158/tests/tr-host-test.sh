#!/bin/sh
# ws158-p002: the host test of libkeiland's translations (translate.c) and of the catalogs' tool (tools/i18n/tr.py).
# Catalogs are written to a temporary directory; tr-host-test.c reads them.  Last line "tr-host-test: PASS" or "FAIL".
#   sh plan/ws158/tests/tr-host-test.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
status=0
mkdir -p "$tmp/locale/ja" "$tmp/locale/fr-none"
tab=$(printf '\t')
# The program's catalog: plain texts, a context, a plural, escapes, a malformed line, a text given twice.
cat > "$tmp/locale/ja/files.tr" <<EOF2
# Files (ja)
msg${tab}Move to Trash${tab}ゴミ箱に移動
msg${tab}Cut${tab}切り取り（Files）
msg${tab}Open${tab}開く（既定）
ctx${tab}verb${tab}Open${tab}開く
ctx${tab}adjective${tab}Open${tab}開いている
plural${tab}{1} item${tab}{1} items${tab}{1} 個
msg${tab}Tab\\there${tab}タブ\\tと\\\\と\\n改行
msg${tab}Broken
msg${tab}Twice${tab}一度目
msg${tab}Twice${tab}二度目
EOF2
# The shared catalog (CRLF line ends, which are read too).
printf 'msg\tCopy\tコピー\r\nmsg\tCut\t切り取り\r\n' > "$tmp/locale/ja/keiland.tr"
cc -std=c89 -Wall -Wextra -Werror -pedantic -Wno-long-long -Wno-overlength-strings -D_GNU_SOURCE \
	-DKEILAND_DATADIR='"/nonexistent"' -I. -Iuserland/desktop/keiland \
	-o "$tmp/tr-host-test" plan/ws158/tests/tr-host-test.c userland/desktop/libkeiland/translate.c 2> "$tmp/cc.txt" ||
	{ cat "$tmp/cc.txt"; echo "tr-host-test: FAIL (build)"; exit 1; }
[ -s "$tmp/cc.txt" ] && { cat "$tmp/cc.txt"; status=1; }
"$tmp/tr-host-test" "$tmp/locale" > "$tmp/out.txt" 2>&1 || status=1
grep -v '^ok:' "$tmp/out.txt"
# The shipped catalogs: well formed, every text translated, every place kept, against the source that asks for them.
for catalog in userland/desktop/locale/*/*.tr; do
	[ -f "$catalog" ] || continue
	domain=$(basename "$catalog" .tr)
	case $domain in
	wayland) sources="userland/desktop/wayland userland/desktop/locale/wayland.keys" ;;
	settings) sources="userland/desktop/settings userland/desktop/locale/settings.keys" ;;
	files) sources="userland/desktop/files userland/desktop/locale/files.keys" ;;
	*) sources="" ;;
	esac
	# shellcheck disable=SC2086
	if python3 tools/i18n/tr.py check --strict "$catalog" $sources > "$tmp/catalog.txt" 2>&1; then
		echo "catalog $catalog: ok"
	else
		grep -v 'without string literals' "$tmp/catalog.txt"
		status=1
	fi
done
if [ -f tools/i18n/tr.py ]; then
	sh plan/ws158/tests/tr-tool-test.sh || status=1
fi
[ $status -eq 0 ] && echo "tr-host-test: PASS" || echo "tr-host-test: FAIL"
exit $status
