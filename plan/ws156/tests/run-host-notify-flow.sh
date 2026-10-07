#!/bin/sh
# ws156-p003: builds the popup's movement (userland/desktop/wayland/notify-flow.c) with the model (notify.c) and
# host-notify-flow.c for the host (plain, and ASan+UBSan) and runs it.
#   sh plan/ws156/tests/run-host-notify-flow.sh [OUTPUT]   (default build/ws156-notify-flow)
# Each run gets a new directory behind OUTPUT (plan/tools/fresh-out.sh); nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws156-notify-flow}
cc=${CC:-cc}
. plan/tools/fresh-out.sh
fresh_out "$out"
status=0
for variant in plain sanitized; do
	flags="-std=c99 -pedantic -O1 -g -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I."
	if [ "$variant" = sanitized ]; then
		flags="$flags -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
	fi
	# shellcheck disable=SC2086
	"$cc" $flags userland/desktop/wayland/notify-flow.c userland/desktop/wayland/notify.c plan/ws156/tests/host-notify-flow.c -o "$out/host-notify-flow-$variant"
	if "$out/host-notify-flow-$variant" > "$out/flow-$variant.txt" 2>&1; then
		echo "host-notify-flow $variant: $(tail -1 "$out/flow-$variant.txt")"
	else
		grep -v '^ok' "$out/flow-$variant.txt"
		status=1
	fi
done
[ $status -eq 0 ] && echo "run-host-notify-flow: PASS" || echo "run-host-notify-flow: FAIL"
exit $status
