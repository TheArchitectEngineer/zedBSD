#!/bin/sh
# Compiles camcheck's reading of the test pattern (userland/tests/camcheck/analyze.c) on the host,
# normally and with ASan/UBSan, and runs both on frames the test draws.
#
#   sh plan/ws282/tests/run-camcheck-host-test.sh
#
# The output goes to a new directory under build/tmp/ of the tree; nothing is removed.
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws282-camcheck.XXXXXX")
flags="-std=c11 -Wall -Wextra -Werror -I$repo"
sources="$repo/plan/ws282/tests/camcheck-host-test.c $repo/userland/tests/camcheck/analyze.c"

# shellcheck disable=SC2086
cc $flags -O2 $sources -lm -o "$work/ordinary"
# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $sources -lm -o "$work/sanitized"
echo "camcheck-host-test: built in $work"

"$work/ordinary"
UBSAN_OPTIONS=halt_on_error=1 "$work/sanitized"
