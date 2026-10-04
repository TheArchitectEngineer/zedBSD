/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The decoding of a BCM2711 compositor display list.
 *
 * The compositor composes each output channel from a display list: a run of
 * planes in the compositor's own list memory, each plane a few 32-bit words
 * that start with a control word, and the run closed by an end word.  This
 * file only reads a list; it touches no register and allocates nothing, so
 * the host test drives it with an ordinary array.
 *
 * The word layout is the BCM2711 compositor's (the fifth generation of the
 * VideoCore compositor); the bit positions are hardware facts.
 */

#include <stdbool.h>
#include <stdint.h>

#include "drivers/gpu/bcm2711/bcm2711-private.h"

/* The control word: set on the word that closes the list. */
#define LIST_CONTROL_END		0x80000000U

/* The control word: set on the first word of every plane. */
#define LIST_CONTROL_VALID		0x40000000U

/* The control word: the plane's length in words, bits 29:24. */
#define LIST_CONTROL_WORDS_SHIFT	24U
#define LIST_CONTROL_WORDS_MASK		0x3fU

/* The control word: set when the plane is shown at its own size. */
#define LIST_CONTROL_UNSCALED		0x00008000U

/* The control word: the order of the colour components, bits 14:13. */
#define LIST_CONTROL_ORDER_SHIFT	13U
#define LIST_CONTROL_ORDER_MASK		0x3U

/* The control word: the pixel format, bits 4:0. */
#define LIST_CONTROL_FORMAT_MASK	0x1fU

/* The last pixel format that keeps all colour in one plane of memory. */
#define LIST_FORMAT_LAST_SINGLE		7U

/* The position word: x in bits 13:0, y in bits 27:16. */
#define LIST_POSITION_X_MASK		0x3fffU
#define LIST_POSITION_Y_SHIFT		16U
#define LIST_POSITION_Y_MASK		0xfffU

/* The source size word: width in bits 12:0, height in bits 28:16. */
#define LIST_SIZE_WIDTH_MASK		0x1fffU
#define LIST_SIZE_HEIGHT_SHIFT		16U
#define LIST_SIZE_HEIGHT_MASK		0x1fffU

/* The pitch word of a single-plane format: bytes per line, bits 15:0. */
#define LIST_PITCH_MASK			0xffffU

/*
 * Where the words of an unscaled plane sit, counted from its control word.
 * A scaled plane inserts one word (its output size) after the alpha word, so
 * every later word moves down by one.
 */
#define LIST_WORD_POSITION		1U
#define LIST_WORD_SOURCE_SIZE		3U
#define LIST_WORD_POINTER		5U
#define LIST_WORD_PITCH			7U

/* The shortest unscaled plane: control, position, alpha, size, context, pointer, context, pitch. */
#define LIST_PLANE_MIN_WORDS		8U

/* The most planes one list may hold before the walk calls it malformed. */
#define LIST_PLANE_LIMIT		256U

static void decode_plane(const volatile uint32_t *memory, uint32_t first, uint32_t words, struct bcm2711_list_plane *plane);

/*
 * Decodes the display list that starts at a word of the list memory.
 *
 * memory is the list memory, BCM2711_LIST_WORDS words long.  The walk follows
 * the planes' lengths to the end word; it stops and leaves valid false at a
 * word that is neither a plane nor the end, at a plane that would run past
 * the memory, and after LIST_PLANE_LIMIT planes.
 */
void
bcm2711_list_decode(
	const volatile uint32_t *memory,
	uint32_t start,
	struct bcm2711_list *list)
{
	uint32_t index;
	uint32_t control;
	uint32_t words;

	/* Starts from an empty, invalid list. */
	list->valid = false;
	list->start = start;
	list->end = start;
	list->plane_count = 0;

	/* Walks the planes until the end word. */
	index = start;
	while (index < BCM2711_LIST_WORDS) {
		/* Ends the walk at the word that closes the list. */
		control = memory[index];
		if ((control & LIST_CONTROL_END) != 0) {
			list->end = index;
			list->valid = true;
			return;
		}

		/* Refuses a word that does not start a plane. */
		if ((control & LIST_CONTROL_VALID) == 0)
			return;

		/* Refuses a plane with no length or one that runs past the memory. */
		words = (control >> LIST_CONTROL_WORDS_SHIFT) & LIST_CONTROL_WORDS_MASK;
		if (words == 0)
			return;
		if (words > BCM2711_LIST_WORDS - index)
			return;

		/* Refuses a list with more planes than any real one has. */
		if (list->plane_count == LIST_PLANE_LIMIT)
			return;

		/* Keeps the first planes in full; the rest are only counted. */
		if (list->plane_count < BCM2711_LIST_PLANES)
			decode_plane(memory, index, words, &list->planes[list->plane_count]);

		/* Moves past the plane. */
		list->plane_count++;
		index += words;
	}
}

/* Decodes the words of one plane that starts at a control word. */
static void
decode_plane(
	const volatile uint32_t *memory,
	uint32_t first,
	uint32_t words,
	struct bcm2711_list_plane *plane)
{
	uint32_t control;
	uint32_t shift;
	uint32_t position;
	uint32_t size;

	/* Records what the control word says. */
	control = memory[first];
	plane->control = control;
	plane->words = words;
	plane->format = control & LIST_CONTROL_FORMAT_MASK;
	plane->order = (control >> LIST_CONTROL_ORDER_SHIFT) & LIST_CONTROL_ORDER_MASK;
	plane->x = 0;
	plane->y = 0;
	plane->width = 0;
	plane->height = 0;
	plane->pointer = 0;
	plane->pitch = 0;

	/* A scaled plane carries its output size after the alpha word. */
	if ((control & LIST_CONTROL_UNSCALED) != 0) {
		plane->scaled = false;
		shift = 0;
	} else {
		plane->scaled = true;
		shift = 1;
	}

	/* Leaves the rest empty for a plane too short to hold the usual words. */
	if (words < LIST_PLANE_MIN_WORDS + shift)
		return;

	/* Reads where the plane sits on the output. */
	position = memory[first + LIST_WORD_POSITION];
	plane->x = position & LIST_POSITION_X_MASK;
	plane->y = (position >> LIST_POSITION_Y_SHIFT) & LIST_POSITION_Y_MASK;

	/* Reads the size of the image the plane shows. */
	size = memory[first + LIST_WORD_SOURCE_SIZE + shift];
	plane->width = size & LIST_SIZE_WIDTH_MASK;
	plane->height = (size >> LIST_SIZE_HEIGHT_SHIFT) & LIST_SIZE_HEIGHT_MASK;

	/* Reads the bus address of the image's first pixel. */
	plane->pointer = memory[first + LIST_WORD_POINTER + shift];

	/* Reads the pitch, which follows one pointer only in a single-plane format. */
	if (plane->format <= LIST_FORMAT_LAST_SINGLE)
		plane->pitch = memory[first + LIST_WORD_PITCH + shift] & LIST_PITCH_MASK;
}
