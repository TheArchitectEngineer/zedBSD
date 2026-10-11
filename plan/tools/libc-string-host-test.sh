#!/bin/sh
# Checks the libc memcpy, memmove and memset (src/libc/string.c) on the host (BUG-291).
#
#   sh plan/tools/libc-string-host-test.sh [CC...]
#
# For each compiler (default: clang and gcc, whichever exist) string.c is built the way the target
# libc builds it (-ffreestanding -fno-builtin, strict aliasing, zedBSD headers) with its functions
# renamed to zedbsd_*, at -Os and -O2, with AddressSanitizer and UndefinedBehaviorSanitizer, and
# linked with plan/tools/libc-string-host-test.c, which compares every result with a reference.
# Then, when the tree's LLVM, aarch64-linux-gnu-gcc and qemu-aarch64 are present, string.c is
# built with the arm64 target libc's flags (-march=armv8-a -Os -ffreestanding -fno-builtin -fPIC,
# so the word loops become ldp/stp q) and the harness is run under qemu-aarch64 (user mode, no
# sanitizers); a missing tool skips that pass with a note.
# Output goes to build/tmp/libc-string-host-test (a fresh run directory; nothing is removed).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

ROOT=$(cd "$(dirname -- "$0")/../.." && pwd)
. "$ROOT/plan/tools/fresh-out.sh"
fresh_out "$ROOT/build/tmp/libc-string-host-test"
OUT=$fresh_dir

COMPILERS=${*:-}
if [ -z "$COMPILERS" ]; then
	for candidate in clang gcc; do
		if command -v "$candidate" >/dev/null 2>&1; then
			COMPILERS="$COMPILERS $candidate"
		fi
	done
fi

RENAMES=
for name in memcpy memmove memset memchr memcmp strlen strnlen strcmp strncmp strcpy strncpy \
	strcat strncat strchr strrchr strstr strdup strerror strerror_r; do
	RENAMES="$RENAMES -D$name=zedbsd_$name"
done

SANITIZE="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
STATUS=0
for cc in $COMPILERS; do
	for level in -Os -O2; do
		tag=$(basename -- "$cc")$level
		# shellcheck disable=SC2086
		"$cc" $level -g $SANITIZE -ffreestanding -fno-builtin -fstrict-aliasing \
			-Wall -Wextra -Werror -nostdinc -isystem "$ROOT/include/libc" -I "$ROOT/include" \
			-I "$ROOT" -DKERN_UAPI_NATIVE $RENAMES \
			-c "$ROOT/src/libc/string.c" -o "$OUT/string-$tag.o"
		printf 'char *heap_strdup_active(const char *s) { (void)s; return 0; }\n' > "$OUT/stub.c"
		# shellcheck disable=SC2086
		"$cc" $level -g $SANITIZE -Wall -Wextra -Werror \
			"$ROOT/plan/tools/libc-string-host-test.c" "$OUT/stub.c" "$OUT/string-$tag.o" \
			-o "$OUT/test-$tag"
		printf '%s: ' "$tag"
		if ! "$OUT/test-$tag"; then
			STATUS=1
		fi
	done
done

# The arm64 pass: the target compiler's code generation, run by user-mode QEMU.
TARGET_CC=$ROOT/build/llvm/bin/clang
if [ ! -x "$TARGET_CC" ] || ! command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 ||
	! command -v qemu-aarch64 >/dev/null 2>&1; then
	echo "arm64: skipped (needs build/llvm/bin/clang, aarch64-linux-gnu-gcc and qemu-aarch64)"
	exit $STATUS
fi
# shellcheck disable=SC2086
"$TARGET_CC" --target=aarch64-linux-gnu -march=armv8-a -Os -ffreestanding -fno-builtin -fPIC \
	-fno-stack-protector -Wall -Wextra -Werror -nostdinc -isystem "$ROOT/include/libc" \
	-I "$ROOT/include" -I "$ROOT" -DKERN_UAPI_NATIVE $RENAMES \
	-c "$ROOT/src/libc/string.c" -o "$OUT/string-arm64.o"
printf 'char *heap_strdup_active(const char *s) { (void)s; return 0; }\n' > "$OUT/stub.c"
aarch64-linux-gnu-gcc -O2 -static -Wall -Wextra -Werror \
	"$ROOT/plan/tools/libc-string-host-test.c" "$OUT/stub.c" "$OUT/string-arm64.o" \
	-o "$OUT/test-arm64"
printf 'arm64-Os (qemu-aarch64): '
if ! qemu-aarch64 "$OUT/test-arm64"; then
	STATUS=1
fi
exit $STATUS
