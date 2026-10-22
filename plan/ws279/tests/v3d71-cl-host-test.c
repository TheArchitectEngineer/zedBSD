/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The V3D 7.1 self-test command lists (v3d71-cl.c) printed for the packet
 * oracle (v3d71-cl-packet-check.py, ws279-p003).
 *
 * Builds the noop and the clear job at fixed GPU addresses, checks that a
 * refused job leaves zero lengths, and prints every stream in hex.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/bcm2711/v3d71-private.h"

/* The CPU storage of the three lists. */
static uint8_t bin_bytes[4096];
static uint8_t render_bytes[4096];
static uint8_t tile_bytes[4096];

static void lend(struct bcm2711_v3d_noop *lists);
static void print_stream(const char *prefix, const char *name, const struct bcm2711_v3d_cl *list);

/* Builds both jobs, checks a refusal and prints the streams. */
int
main(void)
{
	struct bcm2711_v3d_noop lists;
	struct bcm2711_v3d_clear clear;
	int error;

	/* Builds and prints the noop job. */
	lend(&lists);
	error = bcm2712_v3d_noop_prepare(&lists);
	if (error != 0) {
		printf("v3d71-cl-host-test: FAIL noop %d\n", error);
		return 1;
	}
	print_stream("noop", "bin", &lists.bin);
	print_stream("noop", "render", &lists.render);
	print_stream("noop", "tile", &lists.tile);

	/* Builds and prints the clear job. */
	memset(&clear, 0, sizeof(clear));
	lend(&clear.lists);
	clear.width = 64;
	clear.height = 64;
	clear.stride = 256;
	clear.target_address = 0x100000;
	clear.target_bytes = 16384;
	clear.color = 0xff317ce0U;
	error = bcm2712_v3d_clear_prepare(&clear);
	if (error != 0) {
		printf("v3d71-cl-host-test: FAIL clear %d\n", error);
		return 1;
	}
	print_stream("clear", "bin", &clear.lists.bin);
	print_stream("clear", "render", &clear.lists.render);
	print_stream("clear", "tile", &clear.lists.tile);

	/* A target over the pool is refused and publishes nothing. */
	clear.target_address = clear.lists.pool_address;
	error = bcm2712_v3d_clear_prepare(&clear);
	if (error != EINVAL || clear.lists.render.used != 0) {
		printf("v3d71-cl-host-test: FAIL overlapping target accepted\n");
		return 1;
	}

	/* A short render buffer is refused. */
	lend(&lists);
	lists.render.capacity = BCM2712_V3D_NOOP_RENDER_BYTES - 1U;
	error = bcm2712_v3d_noop_prepare(&lists);
	if (error != ENOSPC || lists.bin.used != 0) {
		printf("v3d71-cl-host-test: FAIL short render buffer accepted\n");
		return 1;
	}

	/* Succeeded: the oracle compares the printed streams. */
	printf("v3d71-cl-host-test: PASS\n");
	return 0;
}

/* Lends the lists their storage and fixed GPU addresses. */
static void
lend(
	struct bcm2711_v3d_noop *lists)
{
	/* The same layout as the self-test's diagnostic run. */
	memset(lists, 0, sizeof(*lists));
	lists->bin.bytes = bin_bytes;
	lists->bin.capacity = sizeof(bin_bytes);
	lists->bin.address = 0x100000 - 0x3000;
	lists->render.bytes = render_bytes;
	lists->render.capacity = sizeof(render_bytes);
	lists->render.address = 0x100000 - 0x2000;
	lists->tile.bytes = tile_bytes;
	lists->tile.capacity = sizeof(tile_bytes);
	lists->tile.address = 0x30000;
	lists->pool_address = 0x40000;
	lists->pool_bytes = BCM2711_V3D_NOOP_POOL_BYTES;
}

/* Prints one stream as "<job>-image <name> <hex>". */
static void
print_stream(
	const char *prefix,
	const char *name,
	const struct bcm2711_v3d_cl *list)
{
	uint32_t index;

	/* Prints the used bytes of the stream. */
	printf("%s-image %s ", prefix, name);
	for (index = 0; index < list->used; index++)
		printf("%02x", list->bytes[index]);
	printf("\n");
}
