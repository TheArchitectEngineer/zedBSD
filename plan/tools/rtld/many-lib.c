/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws140: one shared library of the many the dynamic loader's test loads
 * (rtld-many.c).  It holds one function, whose name and value are given
 * when it is compiled: -DMANY_SYMBOL=many_value_07 -DMANY_NUMBER=8 makes
 * many_value_07() return 8.  build-many.sh compiles it once a library.
 */

#ifndef MANY_SYMBOL
#error MANY_SYMBOL names the function
#endif
#ifndef MANY_NUMBER
#error MANY_NUMBER is the value it returns
#endif

int MANY_SYMBOL(void);

/*
 * Returns this library's number.
 */
int
MANY_SYMBOL(
	void)
{
	/* The value the loader's test expects of this library. */
	return MANY_NUMBER;
}
