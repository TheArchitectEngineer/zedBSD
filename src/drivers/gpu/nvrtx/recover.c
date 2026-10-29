/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The decisions of the renderer's recovery that touch no hardware: which
 * session's number a new channel takes on the runlist.
 *
 * The numbers are handed out after the one handed out last, wrapping, so a
 * number freed now is taken again only after every other free one: an
 * error report the GSP sends late for a channel just freed does not reach
 * the next channel that would have taken the same number.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/nvrtx/nvrtx-work.h"

/*
 * Picks the sessions' number (0 to count - 1) a new channel takes: the
 * first free one after last, wrapping, among the bits of used.  Returns
 * the number, or -1 when every one is in use.
 */
int
nvrtx_recover_chid_next(
	uint32_t used,
	uint32_t last,
	uint32_t count)
{
	uint32_t step;
	uint32_t number;

	/* No more numbers than the bits hold. */
	if (count == 0U || count > 32U)
		return -1;

	/* Each number after the last one, wrapping, the last one itself at the end. */
	for (step = 1U; step <= count; step++) {
		number = (last + step) % count;
		if ((used & (1U << number)) == 0U)
			return (int)number;
	}

	/* Every number is in use. */
	return -1;
}
