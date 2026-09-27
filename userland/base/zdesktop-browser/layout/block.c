/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Block layout: the widths of CSS 2's visual formatting model, block boxes
 * stacked in normal flow, and the collapsing of their vertical margins
 * (between siblings, through empty blocks, and between a block and its
 * first and last children).  Boxes out of the flow are passed by, with the
 * static position they would have had.
 */

#include "layout/layout.h"

static layout_unit block_resolve(const struct css_length *length, layout_unit containing_width);
static void block_box_model(struct layout_box *box, layout_unit containing_width);
static void block_width(struct layout_box *box, layout_unit containing_width);
static int block_children(struct layout_tree *tree, struct layout_box *box);
static layout_unit block_collapse(layout_unit first, layout_unit second);
static layout_unit block_outer_height(const struct layout_box *box);

/*
 * Lays out a block box in a containing block of a width: its box model,
 * its content (lines or blocks) and its height.  The box's position is
 * set by its parent.
 */
int
layout_block(
	struct layout_tree *tree,
	struct layout_box *box,
	layout_unit containing_width)
{
	layout_unit height;
	int error;

	/* The margins, borders, paddings and the content width. */
	block_box_model(box, containing_width);
	block_width(box, containing_width);

	/* Lays out the content: lines of inline content, or the child blocks. */
	box->collapsed_top = box->margin[CSS_TOP];
	box->collapsed_bottom = box->margin[CSS_BOTTOM];
	if (box->children_inline) {
		error = layout_inline(tree, box);
	} else {
		error = block_children(tree, box);
	}

	/* Propagates a content that could not be laid out. */
	if (error != 0)
		return error;

	/* An explicit height replaces the content's. */
	if (box->style.height.unit == CSS_UNIT_PX) {
		height = layout_from_px(box->style.height.value);
		box->height = height;
	}

	/* min-height raises a shorter box. */
	if (box->style.min_height.unit == CSS_UNIT_PX) {
		height = layout_from_px(box->style.min_height.value);
		if (box->height < height)
			box->height = height;
	}

	/* max-height lowers a taller box. */
	if (box->style.max_height.unit == CSS_UNIT_PX) {
		height = layout_from_px(box->style.max_height.value);
		if (box->height > height)
			box->height = height;
	}

	/* Succeeded: the box has its size. */
	return 0;
}

/* Resolves a computed length against the containing block's width (auto and none resolve to 0). */
static layout_unit
block_resolve(
	const struct css_length *length,
	layout_unit containing_width)
{
	layout_unit value;

	/* Pixels, a percentage of the width, or nothing. */
	value = 0;
	if (length->unit == CSS_UNIT_PX)
		value = layout_from_px(length->value);
	if (length->unit == CSS_UNIT_PERCENT)
		value = (layout_unit)((float)containing_width * length->value / 100.0f);

	/* Reports the length. */
	return value;
}

/* Resolves a box's margins, borders and paddings. */
static void
block_box_model(
	struct layout_box *box,
	layout_unit containing_width)
{
	int side;

	/* Every side: percentages of the containing width, borders only where they are drawn. */
	for (side = 0; side < 4; side++) {
		box->margin[side] = block_resolve(&box->style.margin[side], containing_width);
		box->padding[side] = block_resolve(&box->style.padding[side], containing_width);
		box->border[side] = 0;
		if (box->style.border_style[side] != CSS_BORDER_NONE)
			box->border[side] = layout_from_px(box->style.border_width[side]);
	}
}

/* Computes a block's content width and its horizontal margins (auto margins center a sized box). */
static void
block_width(
	struct layout_box *box,
	layout_unit containing_width)
{
	layout_unit frame;
	layout_unit room;
	layout_unit width;
	int left_auto;
	int right_auto;

	/* The borders and paddings around the content. */
	frame = box->border[CSS_LEFT] + box->padding[CSS_LEFT] + box->padding[CSS_RIGHT] + box->border[CSS_RIGHT];

	/* An auto width fills the containing block. */
	if (box->style.width.unit == CSS_UNIT_AUTO) {
		width = containing_width - box->margin[CSS_LEFT] - box->margin[CSS_RIGHT] - frame;
	} else {
		width = block_resolve(&box->style.width, containing_width);
	}

	/* min-width and max-width. */
	if (box->style.max_width.unit == CSS_UNIT_PX || box->style.max_width.unit == CSS_UNIT_PERCENT) {
		room = block_resolve(&box->style.max_width, containing_width);
		if (width > room)
			width = room;
	}

	/* min-width wins over max-width, and no width is negative. */
	room = block_resolve(&box->style.min_width, containing_width);
	if (width < room)
		width = room;
	if (width < 0)
		width = 0;
	box->width = width;

	/* Auto margins share what is left of a sized box: both center it, one takes it all. */
	left_auto = 0;
	if (box->style.margin[CSS_LEFT].unit == CSS_UNIT_AUTO)
		left_auto = 1;
	right_auto = 0;
	if (box->style.margin[CSS_RIGHT].unit == CSS_UNIT_AUTO)
		right_auto = 1;
	room = containing_width - width - frame;
	if (room < 0)
		room = 0;
	if (left_auto && right_auto) {
		box->margin[CSS_LEFT] = room / 2;
		box->margin[CSS_RIGHT] = room - room / 2;
	} else if (left_auto) {
		box->margin[CSS_LEFT] = room - box->margin[CSS_RIGHT];
	} else if (right_auto) {
		box->margin[CSS_RIGHT] = room - box->margin[CSS_LEFT];
	}
}

/*
 * Lays out a block's block children one under another, collapsing the
 * margins that meet.
 */
static int
block_children(
	struct layout_tree *tree,
	struct layout_box *box)
{
	struct layout_box *child;
	layout_unit cursor;
	layout_unit pending;
	layout_unit child_top;
	layout_unit child_bottom;
	int collapse_top;
	int collapse_bottom;
	int first;
	int empty;
	int error;

	/*
	 * The box's own top margin meets its first child's unless a border,
	 * padding, or the root separates them; likewise at the bottom when the
	 * height is auto.
	 */
	collapse_top = 0;
	if (box->parent != NULL && box->border[CSS_TOP] == 0 && box->padding[CSS_TOP] == 0)
		collapse_top = 1;
	collapse_bottom = 0;
	if (box->parent != NULL && box->border[CSS_BOTTOM] == 0 && box->padding[CSS_BOTTOM] == 0 &&
	    box->style.height.unit == CSS_UNIT_AUTO)
		collapse_bottom = 1;

	/* Stacks the children, carrying the margin that has not been placed yet. */
	cursor = 0;
	pending = 0;
	first = 1;
	for (child = box->first_child; child != NULL; child = child->next) {
		/* A box out of the flow takes no room; its static position is where the next block would start. */
		if (child->out_of_flow) {
			child->static_x = 0;
			child->static_y = cursor + pending;
			if (first && collapse_top)
				child->static_y = 0;
			continue;
		}

		/* Lays the child out in this box's content width. */
		error = layout_block(tree, child, box->width);
		if (error != 0)
			return error;
		child->x = child->margin[CSS_LEFT];
		child_top = child->collapsed_top;
		child_bottom = child->collapsed_bottom;

		/* An empty block's margins collapse through it into the margin carried on. */
		empty = 0;
		if (child->height == 0 && child->border[CSS_TOP] == 0 && child->border[CSS_BOTTOM] == 0 &&
		    child->padding[CSS_TOP] == 0 && child->padding[CSS_BOTTOM] == 0)
			empty = 1;
		if (empty) {
			/*
			 * Its border edge sits where the margins met so far and its own top
			 * margin put it, as if a bottom border held its bottom margin back.
			 */
			if (first && collapse_top) {
				child->y = 0;
			} else {
				child->y = cursor + block_collapse(pending, child_top);
			}

			/* All of its margins join the margin carried on. */
			pending = block_collapse(pending, block_collapse(child_top, child_bottom));
			continue;
		}

		/* The first child's top margin joins the box's own when they meet; otherwise it joins the carried one. */
		if (first && collapse_top) {
			box->collapsed_top = block_collapse(box->collapsed_top, block_collapse(pending, child_top));
			child->y = 0;
		} else {
			child->y = cursor + block_collapse(pending, child_top);
		}

		/* The children after this one are not the first to meet the box's top margin. */
		first = 0;

		/* The cursor moves past the child's border box; its bottom margin is carried to the next. */
		cursor = child->y + block_outer_height(child);
		pending = child_bottom;
	}

	/* The last margin joins the box's own bottom margin, or stays inside the box. */
	if (collapse_bottom) {
		box->collapsed_bottom = block_collapse(box->collapsed_bottom, pending);
		box->height = cursor;
	} else {
		box->height = cursor + pending;
	}

	/* A box whose children were all empty collapses its own margins too. */
	if (first && collapse_top && collapse_bottom)
		box->collapsed_top = block_collapse(box->collapsed_top, pending);

	/* Succeeded: the children are placed. */
	return 0;
}

/* Collapses two margins: the largest positive plus the most negative. */
static layout_unit
block_collapse(
	layout_unit first,
	layout_unit second)
{
	layout_unit positive;
	layout_unit negative;

	/* The largest of the positive ones. */
	positive = 0;
	if (first > positive)
		positive = first;
	if (second > positive)
		positive = second;

	/* The most negative of the negative ones. */
	negative = 0;
	if (first < negative)
		negative = first;
	if (second < negative)
		negative = second;

	/* Their sum is the collapsed margin. */
	return positive + negative;
}

/* Reports a box's border-box height. */
static layout_unit
block_outer_height(
	const struct layout_box *box)
{
	/* The content, paddings and borders. */
	return box->border[CSS_TOP] + box->padding[CSS_TOP] + box->height + box->padding[CSS_BOTTOM] + box->border[CSS_BOTTOM];
}
