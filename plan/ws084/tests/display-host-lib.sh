#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# Sourced by plan/ws084/tests/run-native-decide-host-test.sh (ws084-p004, restored from the WS031 tool removed in
# 1e867fbf): builds one host display test against the production display sources (src/drivers/gpu/i915/display/*.c,
# every file, each its own translation unit because the Linux environments of the display are mutually exclusive), the
# device trace (trace.c), the host stand-ins of the kernel services and the test's own sources
# (src/drivers/gpu/i915/tests/display/), under ASan/UBSan.
#
# The link drops every function the test does not reach (--gc-sections), so host-kernel.c provides only the kernel
# services the tested paths call.  The objects go to a new directory each run (plan/tools/fresh-out.sh: nothing is
# removed here; Q1's plan/tools/q1-clean.sh removes the old runs).
#
# Usage (from the repository root):
#   . plan/ws084/tests/display-host-lib.sh
#   i915_display_host_build <output> <test and fixture sources under tests/display/>...
. plan/tools/fresh-out.sh

# shift-base is off: the Linux text has `(1 << 31)` register bits (the kernel is built with wrapping semantics).
I915_HOST_BASE="-std=gnu11 -O1 -g -fno-omit-frame-pointer -DKERN_USER_ABI_LP64 -Iinclude -Isrc -I. -idirafter libc/include \
-ffunction-sections -fdata-sections -fsanitize=address,undefined -fno-sanitize=alignment -fno-sanitize=shift-base"
# The production text is compiled as the kernel compiles it; GCC adds a packed-member warning on the DMC headers
# (dmc.c) that clang does not have.
I915_HOST_PRODUCTION_WARNINGS="-Wall -Wextra -Wno-address-of-packed-member"
I915_HOST_TEST_WARNINGS="-Wall -Wextra -Werror -Wdeclaration-after-statement"

i915_display_host_build() {
	out=$1
	shift
	fresh_out "$out.objects"
	objects=$out.objects
	: > "$objects/commands"
	: > "$objects/list"
	for source in src/drivers/gpu/i915/display/*.c src/drivers/gpu/i915/trace.c ${I915_HOST_EXTRA_PRODUCTION:-}; do
		object=$objects/production-$(basename "$source" .c).o
		echo "${CC:-cc} $I915_HOST_BASE $I915_HOST_PRODUCTION_WARNINGS -c $source -o $object" >> "$objects/commands"
		echo "$object" >> "$objects/list"
	done
	for source in src/drivers/gpu/i915/tests/display/host-kernel.c src/drivers/gpu/i915/tests/display/host-test.c "$@"; do
		object=$objects/test-$(basename "$source" .c).o
		echo "${CC:-cc} $I915_HOST_BASE $I915_HOST_TEST_WARNINGS -c $source -o $object" >> "$objects/commands"
		echo "$object" >> "$objects/list"
	done
	tr '\n' '\0' < "$objects/commands" | xargs -0 -P "$(nproc 2>/dev/null || echo 4)" -n 1 sh -c
	while read -r object; do
		test -f "$object" || { echo "compile failed: $object" >&2; return 1; }
	done < "$objects/list"
	# shellcheck disable=SC2046
	${CC:-cc} -fsanitize=address,undefined -Wl,--gc-sections -o "$out" $(cat "$objects/list")
}
