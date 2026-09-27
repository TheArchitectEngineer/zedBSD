/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The errno cell of zedBSD's libc, for the host build of the libm tests.
 *
 * The library under test is compiled against zedBSD's headers, where errno
 * is *__libc_errno_location().  On the host that cell is the host's errno,
 * which the runner reads.
 */

#include <errno.h>

int *__libc_errno_location(void);

/*
 * Returns the address of the calling thread's errno.
 */
int *
__libc_errno_location(void)
{
	/* The host's own cell serves as zedBSD's. */
	return &errno;
}
