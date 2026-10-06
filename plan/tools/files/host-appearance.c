/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws090-p010: the stand-in for libkeiland's appearance on the host, which
 * libkeiland's theme asks (no compositor: the light look), for files'
 * host tests that draw libkeiland's widgets (the field of the name being
 * changed).
 */

#include <keiland/keiland.h>

/* Reports the light appearance. */
unsigned
kl_appearance_get(
	const struct kl_appearance *appearance)
{
	/* No desktop to ask. */
	(void)appearance;
	return KL_APPEARANCE_LIGHT;
}
