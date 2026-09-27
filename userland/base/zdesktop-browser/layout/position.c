/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Positioned boxes: the placing of absolutely positioned and fixed boxes
 * in their containing blocks (CSS 2 §10.3.7 and §10.6.4, simplified), and
 * the painting order of every positioned box.
 *
 * A box out of the flow is placed after the normal flow, in tree order, so
 * its containing block (the padding box of its nearest positioned
 * ancestor, or the viewport's rectangle at the top of the document) is
 * already placed.  An auto width with left or right auto shrinks to fit:
 * the box is laid out once very wide to measure its content, then at the
 * narrower of that and the room it has.  A fixed box is placed like an
 * absolute one against the viewport's rectangle and scrolls with the page
 * in this pass.
 *
 * The painting order flattens the stacking contexts: the positioned boxes
 * with a negative z-index (lowest first), the normal flow, then the others
 * with z-index auto or 0 in tree order and the positive ones (lowest
 * first), each painted with its descendants that are not positioned.
 */

#include "layout/layout.h"

#include <errno.h>
#include <string.h>

/* The width a box is laid out in to measure its content's widest line (262144 pixels). */
#define POSITION_MEASURE_WIDTH	((layout_unit)1 << 24)

/*
 * A containing block: the rectangle of a positioned box's padding box, or
 * of the viewport, in absolute layout units.
 */
struct position_block {
	layout_unit x;
	layout_unit y;
	layout_unit width;
	layout_unit height;
};

/*
 * One positioned box with its z-index, while the painting order is sorted.
 */
struct position_entry {
	const struct layout_box *box;
	int z;
};

static int position_walk(struct layout_tree *tree, struct layout_box *box, const struct position_block *containing, const struct position_block *viewport, int depth);
static int position_place(struct layout_tree *tree, struct layout_box *box, const struct position_block *containing);
static void position_padding_box(const struct layout_box *box, struct position_block *block);
static int position_offset(const struct css_length *length, layout_unit size, layout_unit *value);
static layout_unit position_content_width(const struct layout_box *box, int depth);
static layout_unit position_frame(const struct layout_box *box);
static layout_unit position_outer_height(const struct layout_box *box);
static int position_collect(const struct layout_box *box, struct wb_vector *entries, int depth);

/*
 * Places every box out of the flow of a laid out tree (whose normal flow
 * has absolute positions), and makes the document tall enough for those
 * that scroll with it.
 */
int
layout_position(
	struct layout_tree *tree)
{
	struct position_block viewport;
	int error;

	/* The initial containing block is the viewport's rectangle at the document's top left. */
	if (tree->root == NULL)
		return 0;
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = tree->viewport_width;
	viewport.height = tree->viewport_height;

	/* Walks from the root, whose own containing block is the viewport. */
	error = position_walk(tree, tree->root, &viewport, &viewport, 0);
	if (error != 0)
		return error;

	/* Succeeded: every box is placed. */
	return 0;
}

/*
 * Lists the positioned boxes of a tree in painting order, and reports at
 * which index of the list the normal flow is painted (after the boxes
 * with a negative z-index).  boxes holds const struct layout_box pointers.
 */
int
layout_stacking_order(
	const struct layout_tree *tree,
	struct wb_vector *boxes,
	size_t *flow_index)
{
	struct wb_vector entries;
	struct position_entry *entry;
	struct position_entry moved;
	size_t index;
	size_t place;
	int error;

	/* The positioned boxes in tree order. */
	*flow_index = 0;
	wb_vector_init(&entries, sizeof(struct position_entry));
	error = 0;
	if (tree->root != NULL)
		error = position_collect(tree->root, &entries, 0);
	if (error != 0) {
		wb_vector_release(&entries);
		return error;
	}

	/* A stable insertion sort by z-index keeps tree order among equals. */
	for (index = 1; index < entries.count; index++) {
		entry = wb_vector_at(&entries, index);
		moved = *entry;
		for (place = index; place > 0; place--) {
			entry = wb_vector_at(&entries, place - 1U);
			if (entry->z <= moved.z)
				break;
			*(struct position_entry *)wb_vector_at(&entries, place) = *entry;
		}

		/* The entry goes into the gap. */
		*(struct position_entry *)wb_vector_at(&entries, place) = moved;
	}

	/* The boxes in that order; the normal flow goes after the negative ones. */
	for (index = 0; index < entries.count; index++) {
		entry = wb_vector_at(&entries, index);
		if (entry->z < 0)
			*flow_index = index + 1U;
		error = wb_vector_push(boxes, &entry->box);
		if (error != 0) {
			wb_vector_release(&entries);
			return error;
		}
	}

	/* Succeeded: the order is listed. */
	wb_vector_release(&entries);
	return 0;
}

/* Places the boxes out of the flow under a box, in tree order, with the containing block its descendants have. */
static int
position_walk(
	struct layout_tree *tree,
	struct layout_box *box,
	const struct position_block *containing,
	const struct position_block *viewport,
	int depth)
{
	struct position_block own;
	struct layout_box *child;
	const struct position_block *inner;
	int positioned;
	int error;

	/* Stops at the depth the layout stops at. */
	if (depth > LAYOUT_DEPTH_MAX)
		return 0;

	/* A positioned block is the containing block of the boxes out of the flow inside it. */
	inner = containing;
	positioned = layout_is_positioned(box);
	if (positioned) {
		position_padding_box(box, &own);
		inner = &own;
	}

	/* Each child: one out of the flow is placed first (a fixed one against the viewport), then searched. */
	for (child = box->first_child; child != NULL; child = child->next) {
		if (child->out_of_flow && child->style.position == CSS_POSITION_FIXED) {
			error = position_place(tree, child, viewport);
		} else if (child->out_of_flow) {
			error = position_place(tree, child, inner);
		} else {
			error = 0;
		}

		/* A box that could not be laid out stops the walk. */
		if (error != 0)
			return error;

		/* Its descendants. */
		error = position_walk(tree, child, inner, viewport, depth + 1);
		if (error != 0)
			return error;
	}

	/* Succeeded: the boxes under this one are placed. */
	return 0;
}

/* Lays out and places one box out of the flow in its containing block. */
static int
position_place(
	struct layout_tree *tree,
	struct layout_box *box,
	const struct position_block *containing)
{
	const struct css_length *offset;
	layout_unit left;
	layout_unit right;
	layout_unit top;
	layout_unit bottom;
	layout_unit room;
	layout_unit content;
	layout_unit frame;
	layout_unit height;
	layout_unit x;
	layout_unit y;
	layout_unit bottom_edge;
	int has_left;
	int has_right;
	int has_top;
	int has_bottom;
	int error;

	/* The offsets that are not auto, against the containing block's size. */
	offset = box->style.offset;
	has_left = position_offset(&offset[CSS_LEFT], containing->width, &left);
	has_right = position_offset(&offset[CSS_RIGHT], containing->width, &right);
	has_top = position_offset(&offset[CSS_TOP], containing->height, &top);
	has_bottom = position_offset(&offset[CSS_BOTTOM], containing->height, &bottom);

	/* The room between the offsets that are set. */
	room = containing->width;
	if (has_left)
		room -= left;
	if (has_right)
		room -= right;
	if (room < 0)
		room = 0;

	/* Auto margins are zero unless both sides are set (they then center a box of a set width). */
	if (!has_left || !has_right) {
		if (box->style.margin[CSS_LEFT].unit == CSS_UNIT_AUTO) {
			box->style.margin[CSS_LEFT].unit = CSS_UNIT_PX;
			box->style.margin[CSS_LEFT].value = 0.0f;
		}

		/* The same on the right. */
		if (box->style.margin[CSS_RIGHT].unit == CSS_UNIT_AUTO) {
			box->style.margin[CSS_RIGHT].unit = CSS_UNIT_PX;
			box->style.margin[CSS_RIGHT].value = 0.0f;
		}
	}

	/* A width set, or both sides set, lays the box out in that room. */
	if (box->style.width.unit != CSS_UNIT_AUTO || (has_left && has_right)) {
		error = layout_block(tree, box, room);
		if (error != 0)
			return error;
	} else {
		/* Otherwise the box shrinks to its content: measured very wide, then laid out at the narrower width. */
		error = layout_block(tree, box, POSITION_MEASURE_WIDTH);
		if (error != 0)
			return error;
		content = position_content_width(box, 0);
		frame = position_frame(box);
		if (content > room - frame - box->margin[CSS_LEFT] - box->margin[CSS_RIGHT])
			content = room - frame - box->margin[CSS_LEFT] - box->margin[CSS_RIGHT];
		if (content < 0)
			content = 0;
		box->style.width.unit = CSS_UNIT_PX;
		box->style.width.value = layout_to_px(content);
		error = layout_block(tree, box, room);
		if (error != 0)
			return error;
	}

	/* A height left auto between a top and a bottom fills the room between them. */
	if (box->style.height.unit == CSS_UNIT_AUTO && has_top && has_bottom) {
		height = containing->height - top - bottom - box->margin[CSS_TOP] - box->margin[CSS_BOTTOM] -
		    box->border[CSS_TOP] - box->padding[CSS_TOP] - box->padding[CSS_BOTTOM] - box->border[CSS_BOTTOM];
		if (height < 0)
			height = 0;
		box->height = height;
	}

	/* Horizontally: from the left, from the right, or where it would have been. */
	frame = position_frame(box);
	if (has_left) {
		x = containing->x + left + box->margin[CSS_LEFT];
	} else if (has_right) {
		x = containing->x + containing->width - right - box->margin[CSS_RIGHT] - (box->width + frame);
	} else {
		x = box->static_x + box->margin[CSS_LEFT];
	}

	/* Vertically: from the top, from the bottom, or where it would have been. */
	if (has_top) {
		y = containing->y + top + box->margin[CSS_TOP];
	} else if (has_bottom) {
		y = containing->y + containing->height - bottom - box->margin[CSS_BOTTOM] - position_outer_height(box);
	} else {
		y = box->static_y + box->margin[CSS_TOP];
	}

	/* The box and its descendants take absolute positions from there. */
	box->x = 0;
	box->y = 0;
	layout_absolute(box, x, y);

	/* A box that scrolls with the document makes it tall enough to hold it. */
	bottom_edge = box->y + position_outer_height(box) + box->margin[CSS_BOTTOM];
	if (box->style.position != CSS_POSITION_FIXED && bottom_edge > tree->document_height)
		tree->document_height = bottom_edge;

	/* Succeeded: the box is placed. */
	return 0;
}

/* Reports a box's padding box as a containing block. */
static void
position_padding_box(
	const struct layout_box *box,
	struct position_block *block)
{
	/* Inside the borders, around the paddings. */
	block->x = box->x + box->border[CSS_LEFT];
	block->y = box->y + box->border[CSS_TOP];
	block->width = box->padding[CSS_LEFT] + box->width + box->padding[CSS_RIGHT];
	block->height = box->padding[CSS_TOP] + box->height + box->padding[CSS_BOTTOM];
}

/* Resolves an offset (top, right, bottom or left) against a size; zero when it is auto. */
static int
position_offset(
	const struct css_length *length,
	layout_unit size,
	layout_unit *value)
{
	/* Pixels. */
	*value = 0;
	if (length->unit == CSS_UNIT_PX) {
		*value = layout_from_px(length->value);
		return 1;
	}

	/* A percentage of the containing block. */
	if (length->unit == CSS_UNIT_PERCENT) {
		*value = (layout_unit)((float)size * length->value / 100.0f);
		return 1;
	}

	/* auto. */
	return 0;
}

/* Measures the width a laid out box's content needs: its longest line, or its widest child. */
static layout_unit
position_content_width(
	const struct layout_box *box,
	int depth)
{
	const struct layout_line *line;
	const struct layout_fragment *last;
	const struct layout_box *child;
	layout_unit widest;
	layout_unit width;
	size_t index;

	/* Stops at the depth the layout stops at. */
	widest = 0;
	if (depth > LAYOUT_DEPTH_MAX)
		return 0;

	/* Lines: the end of each line's last piece. */
	if (box->children_inline) {
		for (index = 0; index < box->line_count; index++) {
			line = &box->lines[index];
			if (line->fragment_count == 0)
				continue;
			last = &line->fragments[line->fragment_count - 1U];
			width = last->x + last->width;
			if (width > widest)
				widest = width;
		}

		/* The longest line. */
		return widest;
	}

	/* Blocks: each child's margin box, its content measured when its width is auto. */
	for (child = box->first_child; child != NULL; child = child->next) {
		if (child->out_of_flow)
			continue;
		width = child->width;
		if (child->style.width.unit == CSS_UNIT_AUTO)
			width = position_content_width(child, depth + 1);
		width += child->margin[CSS_LEFT] + position_frame(child) + child->margin[CSS_RIGHT];
		if (width > widest)
			widest = width;
	}

	/* The widest one. */
	return widest;
}

/* Reports the horizontal borders and paddings of a box. */
static layout_unit
position_frame(
	const struct layout_box *box)
{
	/* The four widths. */
	return box->border[CSS_LEFT] + box->padding[CSS_LEFT] + box->padding[CSS_RIGHT] + box->border[CSS_RIGHT];
}

/* Reports a box's border-box height. */
static layout_unit
position_outer_height(
	const struct layout_box *box)
{
	/* The content, paddings and borders. */
	return box->border[CSS_TOP] + box->padding[CSS_TOP] + box->height + box->padding[CSS_BOTTOM] + box->border[CSS_BOTTOM];
}

/* Adds the positioned boxes under a box (not the root) to the entries, in tree order. */
static int
position_collect(
	const struct layout_box *box,
	struct wb_vector *entries,
	int depth)
{
	struct position_entry entry;
	const struct layout_box *child;
	int positioned;
	int error;

	/* Stops at the depth the layout stops at. */
	if (depth > LAYOUT_DEPTH_MAX)
		return 0;

	/* Each child: a positioned one is listed with its z-index (auto is 0), then searched. */
	for (child = box->first_child; child != NULL; child = child->next) {
		positioned = layout_is_positioned(child);
		if (positioned) {
			entry.box = child;
			entry.z = child->style.z_index;
			if (child->style.z_index_auto)
				entry.z = 0;
			error = wb_vector_push(entries, &entry);
			if (error != 0)
				return error;
		}

		/* Its descendants. */
		error = position_collect(child, entries, depth + 1);
		if (error != 0)
			return error;
	}

	/* Succeeded: the boxes are listed. */
	return 0;
}
