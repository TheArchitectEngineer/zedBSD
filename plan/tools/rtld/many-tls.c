/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws140: one of the forty TLS libraries the dynamic loader's test opens
 * (rtld-many.c), each a TLS module of its own, so that a thread's TLS
 * vector has to grow past the 33 entries it starts with.  It holds one
 * thread-local variable, whose initial value is given when it is
 * compiled, and one function that returns the variable's address in the
 * calling thread: -DMANY_SYMBOL=tls_address_07 -DMANY_NUMBER=8.
 * build-many.sh compiles the even ones with TLSDESC (-mtls-dialect=gnu2)
 * and the odd ones with __tls_get_addr.
 */

#ifndef MANY_SYMBOL
#error MANY_SYMBOL names the function
#endif
#ifndef MANY_NUMBER
#error MANY_NUMBER is the variable's initial value
#endif

int *MANY_SYMBOL(void);

/*
 * The variable.  It is local to the library (all forty use the name), and
 * in a shared library its access still goes through the loader: the
 * module's TLSDESC or __tls_get_addr.
 */
static __thread int many_tls_value = MANY_NUMBER;

/*
 * Returns the address of the calling thread's copy of the variable.
 */
int *
MANY_SYMBOL(
	void)
{
	/* The thread's own copy. */
	return &many_tls_value;
}
