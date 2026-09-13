/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Runs the original backend ownership cases with the real optional display code.
 *
 * The shared console fixture supplies bounded PCI, scheduler and transport peers.
 * Keeping its complete production display implementation linked catches changes
 * to the registered callback table without replacing those operations with nulls.
 */

#define VENUS_CONSOLE_ENTRY venus_console_unused_main
#include "../../ws030/tests/venus-console.c"
#undef VENUS_CONSOLE_ENTRY

/*
 * Executes every original backend ownership assertion through the current peers.
 */
int
main(
	void)
{
	int status;

	/* The included backend entry retains all context, resource, display and failure cases. */
	status = venus_backend_unused_main();
	if (status != 0)
		return status;

	/* No display callback may leave an interrupt lock or modeled worker alive. */
	assert(console_observed_spin == NULL);
	assert(console_created == console_reaped);

	/* Succeeded: the complete optional display linkage preserves the backend assertions. */
	return 0;
}
