/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * libpasskey's operating system layer (ws161-p004; plan/ws161/phase001
 * section 9.4): the security keys' raw HID nodes found, opened, and given
 * to the CTAPHID layer as report functions.
 *
 *   zedBSD  /dev/input/hidrawN, a node whose HIDRAW_GET_INFO says FIDO's
 *           usage page (0xF1D0) and usage 1 (CTAPHID); HIDRAW_GRAB takes it
 *           for one open (os-zedbsd.c)
 *   Linux   /dev/hidrawN, whose report descriptor's first application
 *           collection is FIDO's (os-linux.c; no grab)
 *
 * The report functions on an open node are the same on both (os-posix.c).
 * The report descriptor's reading is a pure function (descriptor.c), so the
 * host tests try it alone.  Each system's file is built only for its own
 * system.
 */

#ifndef LIBPASSKEY_OS_H
#define LIBPASSKEY_OS_H

#include <stddef.h>
#include <stdint.h>

#include "hid.h"

/* The longest node path and product name kept (with the NUL), and the most keys listed. */
#define PK_OS_PATH_MAX		64U
#define PK_OS_NAME_MAX		64U
#define PK_OS_DEVICES_MAX	16U

/* A security key found: its node, its product's name, its vendor and product. */
struct pk_os_device {
	char path[PK_OS_PATH_MAX];
	char name[PK_OS_NAME_MAX];
	uint16_t vendor;
	uint16_t product;
};

/* An open key: the node's descriptor (-1 when closed). */
struct pk_os_hid {
	int descriptor;
};

int pk_os_list(struct pk_os_device *devices, size_t capacity, size_t *count);
int pk_os_open(struct pk_os_hid *handle, const char *path, int grab, struct pk_hid_io *io);
void pk_os_close(struct pk_os_hid *handle);
void pk_os_posix_io(struct pk_os_hid *handle, struct pk_hid_io *io);
int pk_os_descriptor_is_fido(const uint8_t *descriptor, size_t size);

#endif
