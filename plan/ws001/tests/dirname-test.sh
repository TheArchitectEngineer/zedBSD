#!/bin/sh
set -eu

source_file=${1:-userland/base/dirname/main.c}
test_root=${TMPDIR:-/tmp}/ws001-dirname-test.$$
binary=$test_root/dirname
actual=$test_root/actual
expected=$test_root/expected

trap 'rm -rf "$test_root"' EXIT HUP INT TERM
mkdir -p "$test_root"
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -I. -Iinclude \
  "$source_file" userland/base/common/command.c -o "$binary"

check() {
  expected_text=$1
  shift
  printf '%s\n' "$expected_text" >"$expected"
  "$binary" "$@" >"$actual"
  cmp "$expected" "$actual"
}

check . foo
check . ''
check / /
check / //
check / '/////'
check / /foo
check / /foo/
check /usr /usr/bin
check /usr /usr//bin///
check a a/b
check a/b a/b/c
check . -- -dash
# Several strings, each result on its own line (GNU; the user's decision
# for WS045, 2026-09-27).
check "$(printf 'a\n/\n.\n/usr')" a/b / c /usr//bin
check "$(printf 'x\n.')" -- x/y -z
printf 'a\0.\0' >"$expected"
"$binary" -z a/b c >"$actual"
cmp "$expected" "$actual"

long_component=$(awk 'BEGIN { for (i = 0; i < 4096; i++) printf "x" }')
check "$long_component" "$long_component/value"

if "$binary" >/dev/null 2>&1; then
  echo "dirname accepted a missing operand" >&2
  exit 1
fi
if "$binary" -q a >/dev/null 2>&1; then
  echo "dirname accepted an unknown option" >&2
  exit 1
fi
if [ -e /dev/full ] && "$binary" value >/dev/full 2>/dev/null; then
  echo "dirname ignored a stdout failure" >&2
  exit 1
fi

echo "WS001 dirname: PASS"
