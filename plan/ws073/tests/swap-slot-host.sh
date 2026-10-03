#!/bin/sh
# ws073-p046 (BUG-053): the host test of the swap slot search (source_find_free_locked() of src/kern/swap.c).
# The function's text is cut out of swap.c and compiled with the real struct swap_backend_source against a
# random bitmap model: every search finds a clear bit when one exists (from any hint, any size), the first
# clear one at or after the hint in wrap-around order, and ENOSPC when the source is full.
# Last line: swap-slot-host: PASS.
#   sh plan/ws073/tests/swap-slot-host.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws073-host/swap-slot
rm -rf "$out"
mkdir -p "$out"
awk '/^source_find_free_locked\(/{p=1; print "static int"} p{print} p&&/^}/{exit}' src/kern/swap.c > "$out/find.inc"
cat > "$out/main.c" <<'EOT'
#include "kern/swap.h"
#include <uapi/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "find.inc"

static int bit(const uint8_t *map, uint32_t i) { return (map[i >> 3] >> (i & 7U)) & 1U; }

int
main(void)
{
	static uint8_t map[4096];
	struct swap_backend_source source;
	uint32_t count, hint, got, want, i, k;
	int error, failures = 0, round;
	unsigned fill;

	srand(52053);
	for (round = 0; round < 20000; round++) {
		count = 1U + (uint32_t)(rand() % 300);
		if (round % 50 == 0)
			count = 1U + (uint32_t)(rand() % 30000);
		fill = (unsigned)(rand() % 101);
		memset(map, 0, sizeof(map));
		for (i = 0; i < count; i++)
			if ((unsigned)(rand() % 100) < fill)
				map[i >> 3] |= (uint8_t)(1U << (i & 7U));
		/* bits past the end are set, as a careless bitmap might be */
		for (i = count; i < ((count + 7U) & ~7U); i++)
			map[i >> 3] |= (uint8_t)(1U << (i & 7U));
		memset(&source, 0, sizeof(source));
		source.bitmap = map;
		source.slot_count = count;
		hint = (uint32_t)(rand() % (count + 10U));
		source.next_slot = hint;
		want = UINT32_MAX;
		for (k = 0; k < count; k++) {
			i = ((hint >= count ? 0 : hint) + k) % count;
			if (!bit(map, i)) { want = i; break; }
		}
		got = UINT32_MAX;
		error = source_find_free_locked(&source, &got);
		if (want == UINT32_MAX) {
			if (error != ENOSPC) { printf("round %d: full source error %d\n", round, error); failures++; }
		} else if (error != 0 || got != want) {
			printf("round %d: count %u hint %u fill %u: got %u error %d, want %u\n", round, count, hint, fill, got, error, want);
			failures++;
		}
	}
	if (failures != 0) { printf("swap-slot-host: FAIL (%d)\n", failures); return 1; }
	printf("swap-slot-host: PASS\n");
	return 0;
}
EOT
cc -std=gnu99 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -I. -Iinclude -Isrc -I"$out" \
    -fsanitize=address,undefined "$out/main.c" -o "$out/swap-slot-host"
exec "$out/swap-slot-host"
