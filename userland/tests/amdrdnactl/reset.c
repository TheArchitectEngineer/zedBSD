/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The reset of the GPU (amdrdnactl reset): asks the driver for a mode-1
 * reset and the stages set up again, and prints one line with the result,
 * the time it took and the resets so far.  The driver refuses it while a
 * session of the GPU device is open.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <uapi/amdrdna.h>

#include "amdrdnactl.h"

/*
 * Resets the GPU of the control device at path; returns 0 when the GPU
 * was set up again, or 1 when the request was refused or the reset
 * failed.
 */
int
control_reset(
	const char *path)
{
	struct amdrdna_reset_ask ask;
	int descriptor;
	int saved;
	int result;

	/* The control device. */
	descriptor = open(path, O_RDONLY | O_CLOEXEC);
	if (descriptor < 0) {
		printf("RESET fail open %s %d\n", path, errno);
		return 1;
	}

	/* The request; the driver answers when the reset has run. */
	memset(&ask, 0, sizeof(ask));
	ask.version = AMDRDNA_INTERFACE_VERSION;
	ask.size = sizeof(ask);
	result = ioctl(descriptor, AMDRDNA_IOC_RESET, &ask);
	saved = errno;
	(void)close(descriptor);
	if (result != 0) {
		printf("RESET refused %d (%s)\n", saved, strerror(saved));
		return 1;
	}

	/* The reset's own result. */
	if (ask.result != 0) {
		printf("RESET fail result=%d ms=%u resets=%u\n", (int)ask.result, (unsigned)ask.milliseconds, (unsigned)ask.resets);
		return 1;
	}

	/* Succeeded: the GPU runs again. */
	printf("RESET ok ms=%u resets=%u\n", (unsigned)ask.milliseconds, (unsigned)ask.resets);
	return 0;
}
