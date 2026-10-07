/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The preview on FreeBSD (WS168 p004; client.h): until keiland-preview
 * confines itself with Capsicum (plan/ws177/backlog-p2.md), the preview is
 * made in the caller's own process, as the callers decoded pictures before
 * (Q1, 2026-10-07: no loss of the previews on FreeBSD meanwhile).
 */

#include "../client.h"

/*
 * Makes the preview here and now.  Returns 0 with no child and the status.
 */
int
preview_spawn(
	int input,
	int output,
	const struct preview_request *request,
	pid_t *pid,
	int *status)
{
	/* In this process. */
	*pid = 0;
	*status = preview_make(input, output, request);
	return 0;
}
