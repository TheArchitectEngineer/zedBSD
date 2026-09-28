/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Kei mark as coverage masks (ws035-p108), for the programs that draw it
 * themselves: the compositor (the greeter and the lock screen) and the file
 * manager (its Welcome and its empty folders).  The source is compiled into
 * each of them.
 */

#ifndef KEILAND_ARTWORK_MARK_H
#define KEILAND_ARTWORK_MARK_H

#include <stddef.h>
#include <stdint.h>

/*
 * The layers of the mark, drawn in this order, each in its own colour: the
 * bar, the bar's shade (deeper towards its foot), the leaf, and the leaf's
 * shade (deeper towards its lower point).
 */
enum keiland_mark_layer {
	KEILAND_MARK_BAR,
	KEILAND_MARK_BAR_SHADE,
	KEILAND_MARK_LEAF,
	KEILAND_MARK_LEAF_SHADE,
	KEILAND_MARK_LAYERS
};

void keiland_mark_raster(unsigned layer, unsigned pixels, uint8_t *coverage, size_t stride);

#endif
