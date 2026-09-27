/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The library of the rtld dlopen check (BUG-083, rtld-dlopen.sh): installed
 * as /usr/lib/libws074probe.so, a DT_NEEDED of rtld-dlopen.  Its counter
 * shows whether dlopen gives the copy already loaded or a second one.
 */

int probe_bump(void);
int probe_count(void);

/* How many times probe_bump ran in this copy of the library. */
static int probe_counter;

/* Counts one call; returns the new count. */
int
probe_bump(void)
{
	/* One more. */
	probe_counter++;
	return probe_counter;
}

/* Returns the count of this copy. */
int
probe_count(void)
{
	/* The counter as it is. */
	return probe_counter;
}
