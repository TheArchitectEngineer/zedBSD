#!/bin/sh
# Compiles the xHCI high-bandwidth isochronous checks (the endpoint context
# encoder and the Isoch TRB's burst counts) on the host, normally and with
# ASan/UBSan, and runs both.
#
#   sh plan/ws282/tests/run-xhci-hb-host-test.sh
#
# The output goes to a new directory under build/tmp/ of the tree; nothing
# is removed.
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws282-xhci-hb.XXXXXX")
flags="-std=gnu11 -Wall -Wextra -Werror -I$repo/include"
source="$repo/plan/ws282/tests/xhci-hb-host-test.c"

# shellcheck disable=SC2086
cc $flags -O2 "$source" -o "$work/ordinary"
# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer "$source" -o "$work/sanitized"
echo "xhci-hb-host-test: built in $work"

"$work/ordinary"
UBSAN_OPTIONS=halt_on_error=1 "$work/sanitized"
