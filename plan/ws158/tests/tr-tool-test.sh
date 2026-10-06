#!/bin/sh
# ws158-p002: the host test of tools/i18n/tr.py: extract from a C file (literals joined, escapes, a context, a plural,
# a comment and a non-literal left out), update a catalog keeping its translation, check the places.
# Last line "tr-tool: PASS" or "tr-tool: FAIL".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
# The work directory stays in build/tmp (2026-10-06 user: deleting is Q1's step; plan/tools/q1-clean.sh removes it).
mkdir -p build/tmp
tmp=$(mktemp -d "$(pwd)/build/tmp/tr-tool-test.XXXXXX")
status=0
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1"; status=1; }
cat > "$tmp/a.c" <<'EOF2'
/* kl_tr("in a comment") is not a use. */
static void
draw(void)
{
	label = kl_tr("Move to " "Trash");
	verb = kl_trc("verb", "Open");
	many = kl_trn("{1} item", "{1} items", count);
	odd = kl_tr("Tab\there \"quoted\" \xe3\x81\x82");
	dynamic = kl_tr(name);
	again = kl_tr("Move to Trash");
}
EOF2
python3 tools/i18n/tr.py extract "$tmp/a.c" > "$tmp/out.tr" 2> "$tmp/err.txt"
tab=$(printf '\t')
grep -qx "msg${tab}Move to Trash${tab}" "$tmp/out.tr" && ok "joined literals" || bad "joined literals"
grep -qx "ctx${tab}verb${tab}Open${tab}" "$tmp/out.tr" && ok "context" || bad "context"
grep -qx "plural${tab}{1} item${tab}{1} items${tab}" "$tmp/out.tr" && ok "plural" || bad "plural"
grep -qF 'msg	Tab\there "quoted" あ	' "$tmp/out.tr" && ok "escapes" || bad "escapes: $(grep Tab "$tmp/out.tr")"
[ "$(grep -c '^msg' "$tmp/out.tr")" = 2 ] && ok "one entry a text, no comment's" || bad "entries: $(grep -c '^msg' "$tmp/out.tr")"
grep -q 'kl_tr without string literals' "$tmp/err.txt" && ok "non-literal reported" || bad "non-literal"
grep -q "$tmp/a.c:5, $tmp/a.c:10" "$tmp/out.tr" && ok "places of a text" || bad "places"
# A catalog: one translation kept, one text no longer used, then checks.
mkdir -p "$tmp/locale/ja"
printf '# Test catalog.\nmsg\tMove to Trash\tゴミ箱に移動\nmsg\tGone\t消えた\n' > "$tmp/locale/ja/a.tr"
python3 tools/i18n/tr.py update "$tmp/locale/ja/a.tr" "$tmp/a.c" > "$tmp/update.txt" 2>/dev/null
grep -qx '# Test catalog.' "$tmp/locale/ja/a.tr" && ok "header kept" || bad "header"
grep -qx "msg${tab}Move to Trash${tab}ゴミ箱に移動" "$tmp/locale/ja/a.tr" && ok "translation kept" || bad "translation kept"
grep -qx "# msg${tab}Gone${tab}消えた" "$tmp/locale/ja/a.tr" && ok "unused kept as a comment" || bad "unused"
grep -q '4 texts, 3 new, 1 no longer used' "$tmp/update.txt" && ok "update counts" || bad "update counts: $(cat "$tmp/update.txt")"
python3 tools/i18n/tr.py check "$tmp/locale/ja/a.tr" "$tmp/a.c" > /dev/null && ok "check" || bad "check"
python3 tools/i18n/tr.py check --strict "$tmp/locale/ja/a.tr" > /dev/null && bad "strict passes untranslated" || ok "strict fails untranslated"
printf 'plural\t{1} item\t{1} items\t個\n' > "$tmp/places.tr"
python3 tools/i18n/tr.py check "$tmp/places.tr" | grep -q 'places' && ok "a lost place" || bad "a lost place"
printf 'nonsense line\n' > "$tmp/bad.tr"
python3 tools/i18n/tr.py check "$tmp/bad.tr" > /dev/null && bad "malformed accepted" || ok "malformed refused"
[ $status -eq 0 ] && echo "tr-tool: PASS" || echo "tr-tool: FAIL"
exit $status
