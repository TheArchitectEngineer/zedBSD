/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The keyboard's XKB keymap (ws035-p078, keymap.c).
 */

#ifndef ZWL_KEYMAP_H
#define ZWL_KEYMAP_H

#include <stdint.h>

int zwl_keymap_open(void);
int zwl_keymap_descriptor(uint32_t *size);
const char *zwl_keymap_text(void);

#endif
