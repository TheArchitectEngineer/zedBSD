/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The account where the backend does not offer it yet (ws160-p002): Linux
 * and FreeBSD.  A password change answers ENOTSUP.
 */

#include "userland/desktop/libkeiland-backend/keiland-backend.h"

#include <errno.h>
#include <stddef.h>

/* Refuses a password change. */
int
kl_backend_account_set_password(
	const char *current,
	const char *fresh)
{
	/* Nothing is offered here. */
	(void)current;
	(void)fresh;
	return ENOTSUP;
}
