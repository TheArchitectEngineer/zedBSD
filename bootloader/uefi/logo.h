/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The boot logo: a binary PPM on the boot volume, drawn in the middle of
 * the screen (ws035-p096).
 */

#ifndef KERN_UEFI_LOGO_H
#define KERN_UEFI_LOGO_H

#include <stddef.h>
#include <stdint.h>

#include "bootloader/include/amd64-handoff.h"

/* The longest logo path, and the largest logo file, the loader takes. */
#define ZBL_UEFI_LOGO_PATH_MAX		63U
#define ZBL_UEFI_LOGO_FILE_MAX		(16U * 1024U * 1024U)

int zbl_uefi_logo_path(const char *text, size_t length, char *path, size_t capacity);
int zbl_uefi_parameter_present(const char *text, size_t length, const char *token);
int zbl_uefi_logo_draw(const uint8_t *data, size_t size, const struct zbl6_framebuffer *framebuffer);

#endif
