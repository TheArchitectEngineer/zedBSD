#!/bin/sh
# ws129-p003: the version's one source, on the host.
#  1. make version writes $(BUILD)/gen/os-release and zedbsd-version.h from VERSION: the nightly release
#     (VERSION+g<revision>), the release build's (ZEDBSD_RELEASE_BUILD=y: VERSION itself), a development version's
#     name (1.0.0-beta2.dev: "1.0.0 Beta 2 (development)"), +unknown without git, a VERSION that is no version
#     refused; a second make leaves the files as they were (their time unchanged).
#  2. libc's uname_field (userland/base/libc/posix.c, taken out with sed) reads the keys of os-release text:
#     quoted and plain values, a key that is a prefix of another, a missing key, a value cut to the field.
# The tree's VERSION is restored at the end.
#   sh plan/ws129/tests/host-version.sh [BUILD]   (default build/ws129-p003)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
build=${1:-build/ws129-p003}
mkdir -p "$build/test"
printf 'ZEDBSD_PLATFORM := amd64\n' > "$build/test/config.mk"
make_version() { timeout 300 make ZEDBSD_CONFIG="$build/test/config.mk" BUILD="$build" "$@" version >"$build/test/make.log" 2>&1; }
release=$build/gen/os-release
passed=0
failed=0
saved=$(cat VERSION)
revision=$(git rev-parse --short=7 HEAD)

# Checks that a file has a line.
expect() {
	if grep -Fqx -- "$2" "$1"; then
		passed=$((passed + 1))
	else
		echo "FAIL: $1 lacks: $2"
		failed=$((failed + 1))
	fi
}

# 1. The generated files.
make_version
expect "$release" 'NAME="zedBSD"'
expect "$release" "VERSION_ID=$saved"
expect "$release" "ZEDBSD_RELEASE=$saved+g$revision"
expect "$build/gen/zedbsd-version.h" "#define ZEDBSD_VERSION \"$saved\""
before=$(stat -c %Y "$release" "$build/gen/zedbsd-version.h")
sleep 1
make_version
after=$(stat -c %Y "$release" "$build/gen/zedbsd-version.h")
if [ "$before" = "$after" ]; then passed=$((passed + 1)); else echo "FAIL: a second make rewrote the files"; failed=$((failed + 1)); fi
make_version ZEDBSD_RELEASE_BUILD=y
expect "$release" "ZEDBSD_RELEASE=$saved"
GIT_DIR=/nonexistent make_version
expect "$release" "ZEDBSD_RELEASE=$saved+unknown"
expect "$release" 'BUILD_ID=unknown'
printf '1.0.0-beta2.dev\n' > VERSION
make_version
expect "$release" 'VERSION="1.0.0 Beta 2 (development)"'
expect "$release" 'PRETTY_NAME="Kei/zedBSD 1.0.0 Beta 2 (development)"'
expect "$build/gen/zedbsd-version.h" '#define ZEDBSD_VERSION "1.0.0-beta2.dev"'
printf '1.0.0-beta1\n' > VERSION
make_version
expect "$release" 'VERSION="1.0.0 Beta 1"'
expect "$release" 'PRETTY_NAME="Kei/zedBSD 1.0.0 Beta 1"'
printf 'one two\n' > VERSION
if make_version; then echo "FAIL: a bad VERSION was taken"; failed=$((failed + 1)); else passed=$((passed + 1)); fi
printf '%s\n' "$saved" > VERSION
make_version

# 2. uname_field, taken out of libc.
sed -n '/^uname_field($/,/^}$/p' userland/base/libc/posix.c > "$build/test/field.inc"
cat > "$build/test/field.c" <<'EOF2'
#include <stdio.h>
#include <string.h>
static void uname_field(const char *text, const char *key, char *value, size_t size);
static void
#include "field.inc"
static int failed;
static void check(const char *text, const char *key, size_t size, const char *wanted)
{
	char value[65];
	strcpy(value, "kept");
	uname_field(text, key, value, size);
	if (strcmp(value, wanted) != 0) {
		printf("FAIL: %s: '%s', not '%s'\n", key, value, wanted);
		failed++;
	}
}
int main(void)
{
	static const char text[] = "NAME=\"zedBSD\"\nZEDBSD_RELEASE_X=no\nZEDBSD_RELEASE=1.0.0-beta1+g1a2b3c4\n"
	    "ZEDBSD_VERSION=\"zedBSD 1.0.0-beta1+g1a2b3c4 (1a2b3c4 2026-10-05)\"\nEMPTY=\nLAST=end";
	check(text, "ZEDBSD_RELEASE", 65, "1.0.0-beta1+g1a2b3c4");
	check(text, "ZEDBSD_VERSION", 65, "zedBSD 1.0.0-beta1+g1a2b3c4 (1a2b3c4 2026-10-05)");
	check(text, "NAME", 65, "zedBSD");
	check(text, "ZEDBSD", 65, "kept");
	check(text, "MISSING", 65, "kept");
	check(text, "EMPTY", 65, "");
	check(text, "LAST", 65, "end");
	check(text, "ZEDBSD_VERSION", 7, "zedBSD");
	check("", "NAME", 65, "kept");
	printf("%d\n", failed);
	return failed != 0;
}
EOF2
if cc -std=c11 -Wall -Wextra -Werror -I"$build/test" "$build/test/field.c" -o "$build/test/field" && "$build/test/field" >/dev/null; then
	passed=$((passed + 1))
else
	"$build/test/field" 2>/dev/null
	echo "FAIL: uname_field"
	failed=$((failed + 1))
fi

echo "host-version: $passed passed, $failed failed"
[ "$failed" = 0 ]
