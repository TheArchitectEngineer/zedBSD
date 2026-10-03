/*
 * BUG-050 check, run in the guest: prints the description strerror gives
 * each error number from 1 to ETIME, the highest in <uapi/errno.h>, and
 * exits with the number of them that are "Unknown error".
 *
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

int
main(
	void)
{
	const char *text;
	int number;
	int unknown;
	int same;

	unknown = 0;

	/* Asks for every number and counts the ones without a description. */
	for (number = 1; number <= ETIME; number++) {
		text = strerror(number);
		printf("%d: %s\n", number, text);
		same = strncmp(text, "Unknown error", 13);
		if (same == 0)
			unknown++;
	}

	/* Reports the count, which is also the exit status. */
	printf("numbers %d unknown %d\n", ETIME, unknown);
	return unknown;
}
