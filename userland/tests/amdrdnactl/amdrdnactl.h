/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the amdrdna control tool's files share.
 */

#ifndef AMDRDNACTL_H
#define AMDRDNACTL_H

/* gpu-check.c: the GPU device check (amdrdnactl gpu). */
int control_gpu(const char *path);

/* modes.c: the display's modes (amdrdnactl modes). */
int control_modes(const char *path);

#endif
