/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The library's interface version, which a program compares with the
 * KUI_VERSION it was built against.
 */

#include <keiui.h>

/*
 * Reports the interface version of this library.
 */
unsigned
kui_version(void)
{
	/* Succeeded: the version this library was built as. */
	return KUI_VERSION;
}
