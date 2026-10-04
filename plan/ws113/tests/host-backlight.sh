#!/bin/sh
# ws113-p013: host test of the kernel's backlight class (src/drivers/generic/backlight.c) with the real character
# device registry (src/kern/cdev.c), and of the backend's stub (libkeiland-backend/unsupported/backlight-unsupported.c).
#   sh plan/ws113/tests/host-backlight.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0

# The class and the registry, with the fixture's allocator, locks and copies.
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -ffunction-sections -fdata-sections \
    -DKERN_USER_ABI_LP64 -I. -Iinclude -Isrc -Iinclude/libc -DKERN_UAPI_NATIVE \
    -include include/libc/sys/ioctl.h \
    plan/ws113/tests/host-backlight.c src/drivers/generic/backlight.c src/kern/cdev.c \
    -Wl,--gc-sections -o "$work/host-backlight" || exit 1
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$work/host-backlight" || status=1

# The backend's stub answers ENOTSUP and opens nothing.
cat > "$work/stub.c" <<'STUB'
#include "userland/desktop/libkeiland-backend/keiland-backend.h"
#include <errno.h>
#include <stdio.h>
int main(void)
{
	struct kl_backend_backlight *backlight = (void *)1;
	unsigned percent = 0;
	int bad = 0;
	if (kl_backend_backlight_open(&backlight) != ENOTSUP || backlight != NULL) bad = 1;
	if (kl_backend_backlight_get(NULL, &percent) != ENOTSUP) bad = 1;
	if (kl_backend_backlight_set(NULL, 50) != ENOTSUP) bad = 1;
	kl_backend_backlight_close(NULL);
	printf("%s: the backend's stub answers ENOTSUP\n", bad ? "FAIL" : "ok");
	return bad;
}
STUB
cc -std=gnu17 -Wall -Wextra -Werror -D_GNU_SOURCE -I. "$work/stub.c" \
    userland/desktop/libkeiland-backend/unsupported/backlight-unsupported.c -o "$work/stub" || exit 1
"$work/stub" || status=1

[ $status = 0 ] && echo "host-backlight.sh: PASS" || echo "host-backlight.sh: FAIL"
exit $status
