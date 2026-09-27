/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Hit testing: the box under a point of the laid out page.
 *
 * The first pass looks at the text: a point on a line's fragment hits the
 * text box the fragment was cut from, which is what a click on a link
 * needs.  Without floats or positioning nothing overlaps, so the first
 * fragment found is the one on top.
 */

#include "layout/layout.h"

static const struct layout_box *hit_box(const struct layout_box *box, layout_unit x, layout_unit y, int depth);
static const struct layout_box *hit_lines(const struct layout_box *box, layout_unit x, layout_unit y);

/*
 * Finds the box of the text under a point in document coordinates (layout
 * units); NULL when the point is on no text.
 */
const struct layout_box *
layout_hit(
	const struct layout_tree *tree,
	layout_unit x,
	layout_unit y)
{
	const struct layout_box *found;

	/* An empty page has nothing to hit. */
	if (tree->root == NULL)
		return NULL;

	/* Searches from the root. */
	found = hit_box(tree->root, x, y, 0);

	/* Reports the box, or NULL. */
	return found;
}

/* Searches a block and its descendants for the text under a point. */
static const struct layout_box *
hit_box(
	const struct layout_box *box,
	layout_unit x,
	layout_unit y,
	int depth)
{
	const struct layout_box *child;
	const struct layout_box *found;

	/* Stops at the depth the layout stops at. */
	if (depth > LAYOUT_DEPTH_MAX)
		return NULL;

	/* Only blocks hold lines or blocks. */
	if (box->kind != LAYOUT_BLOCK && box->kind != LAYOUT_ANONYMOUS_BLOCK)
		return NULL;

	/* A block of lines: the fragment under the point. */
	if (box->children_inline) {
		found = hit_lines(box, x, y);
		return found;
	}

	/* A block of blocks: the first child with text under the point. */
	for (child = box->first_child; child != NULL; child = child->next) {
		found = hit_box(child, x, y, depth + 1);
		if (found != NULL)
			return found;
	}

	/* Nothing under the point in this block. */
	return NULL;
}

/* Finds the fragment of a block's lines under a point and reports its box. */
static const struct layout_box *
hit_lines(
	const struct layout_box *box,
	layout_unit x,
	layout_unit y)
{
	const struct layout_line *line;
	const struct layout_fragment *fragment;
	layout_unit left;
	layout_unit top;
	layout_unit start;
	size_t index;
	size_t item;

	/* The content box's origin, which the lines are placed from. */
	left = box->x + box->border[CSS_LEFT] + box->padding[CSS_LEFT];
	top = box->y + box->border[CSS_TOP] + box->padding[CSS_TOP];

	/* The line whose band holds the point. */
	for (index = 0; index < box->line_count; index++) {
		line = &box->lines[index];
		if (y < top + line->y || y >= top + line->y + line->height)
			continue;

		/* The fragment of that line whose run holds the point. */
		for (item = 0; item < line->fragment_count; item++) {
			fragment = &line->fragments[item];
			start = left + line->left + fragment->x;
			if (x >= start && x < start + fragment->width)
				return fragment->box;
		}

		/* The point is on the line but on no fragment. */
		return NULL;
	}

	/* The point is on none of the lines. */
	return NULL;
}
