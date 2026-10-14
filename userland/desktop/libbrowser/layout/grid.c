/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Grid layout (ws074-p072), the common part of CSS Grid Layout 1: the
 * explicit column and row templates (lengths, percentages, fr, auto,
 * minmax()'s minimum), items placed by line numbers (negative from the
 * end) and spans or else row after row in the first cells that are free
 * (with row dense backfilling earlier holes),
 * implicit auto tracks for what goes past the templates, row/column sparse
 * and dense auto-placement, the gaps, and
 * each item laid out as a block in its area and aligned in it
 * (align-self, align-items).  Columns are sized in three steps: the fixed
 * ones, then the auto ones at their items' content width, then the fr
 * ones sharing what is left (auto columns share it when there is no fr);
 * rows are as tall as their items unless the template fixes them.  Direct
 * absolute children with definite lines use their area's static origin in
 * horizontal LTR/RTL grids, without taking part in track sizing.  Not in
 * this pass: named lines and areas, full inline-axis
 * alignment, order, full baseline alignment, subgrid, and auto-fill and auto-fit.
 */

#include "layout/layout.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* The most implicit rows a grid places items in (items past them go on the last). */
#define GRID_ROWS_MAX	4096

/*
 * One item on its way through the algorithm: its box, and its area's
 * first column and row and how many of each it spans.
 */
struct grid_item {
	struct layout_box *box;
	int column;
	int column_span;
	int row;
	int row_span;
};

/*
 * The grid being laid out: its items, its columns' and rows' sizes and
 * offsets, and which cells are taken (rows of column_count cells).
 */
struct grid_state {
	struct grid_item *items;
	size_t item_count;
	int column_count;
	int row_count;
	layout_unit *columns;
	layout_unit *rows;
	unsigned char *taken;
	int taken_rows;
};

/*
 * The part of an item's style that its layout in its area overrides, kept
 * to be put back afterwards.
 */
struct grid_sizes {
	int box_sizing;
	struct css_length width;
	struct css_length min_width;
	struct css_length max_width;
	struct css_length height;
	struct css_length min_height;
	struct css_length max_height;
	struct css_length margin[4];
};

static int grid_collect(struct layout_box *box, struct grid_state *grid);
static int grid_vertical_fixed(struct layout_tree *tree, struct layout_box *box, int *handled);
static int grid_vertical_auto(struct layout_tree *tree, struct layout_box *box, int *handled);
static void grid_resolve(const struct css_grid_place *place, int explicit_count, int *start, int *span);
static void grid_static_position(struct layout_box *box, const struct grid_state *grid,
    layout_unit column_gap, layout_unit row_gap, layout_unit shift, layout_unit row_shift);
static void grid_static_vertical_start(struct layout_box *box);
static int grid_place(struct grid_state *grid, int auto_flow);
static int grid_place_row(struct grid_state *grid, int dense);
static int grid_fits(const struct grid_state *grid, int row, int column, int row_span, int column_span);
static int grid_take(struct grid_state *grid, int row, int column, int row_span, int column_span);
static const struct css_track *grid_column_track(const struct css_style *style, int column);
static const struct css_track *grid_row_track(const struct css_style *style, int row);
static int grid_size_columns(struct layout_tree *tree, struct layout_box *box, struct grid_state *grid, layout_unit gap);
static int grid_item_width(struct layout_tree *tree, struct layout_box *item, layout_unit *width);
static int grid_lay_item(struct layout_tree *tree, struct layout_box *item, layout_unit width, int justify);
static void grid_size_rows(struct layout_box *box, struct grid_state *grid, layout_unit gap);
static int grid_relay_item(struct layout_tree *tree, struct layout_box *item, layout_unit width, layout_unit height, int align);
static layout_unit grid_outer_height(const struct layout_box *item);
static int grid_fixed_row_baseline(const struct layout_box *box, const struct grid_item *item, layout_unit *baseline);
static int grid_fixed_column_baseline(const struct layout_box *box, const struct grid_item *item, layout_unit *right_extent);
static layout_unit grid_track_length(const struct css_length *length, layout_unit whole, int measuring);
static void grid_release(struct grid_state *grid);

/*
 * Lays out the content of a grid container: its in-flow children as grid
 * items in their areas; sets their positions (their border boxes relative
 * to the container's content box), the container's content height, and
 * the width its columns need at their content's sizes (for the
 * containers around it that shrink to fit).
 */
int
layout_grid(
	struct layout_tree *tree,
	struct layout_box *box)
{
	struct grid_state grid;
	struct grid_item *item;
	struct layout_box *child;
	layout_unit column_gap;
	layout_unit row_gap;
	layout_unit x;
	layout_unit y;
	layout_unit area_width;
	layout_unit area_height;
	layout_unit outer;
	layout_unit outer_width;
	layout_unit frame;
	layout_unit room;
	layout_unit used;
	layout_unit shift;
	layout_unit row_shift;
	layout_unit baseline;
	layout_unit peer_baseline;
	layout_unit shared_baseline;
	layout_unit right_extent;
	layout_unit shared_right_extent;
	layout_unit free_height;
	size_t index;
	size_t peer;
	int position;
	int justify;
	int align;
	int baseline_eligible;
	int peer_baseline_eligible;
	int auto_top;
	int auto_bottom;
	int auto_block_margin;
	int handled;
	int error;

	/* The children out of the flow have their static place at the content box's corner. */
	for (child = box->first_child; child != NULL; child = child->next) {
		if (!child->out_of_flow)
			continue;
		child->static_x = 0;
		child->static_y = 0;
		child->static_x_from_right = 0;
		child->static_x_area_width = 0;
		child->static_x_align = CSS_ALIGN_AUTO;
		child->static_x_align_from_right = 0;
		child->static_y_area_height = 0;
		child->static_y_align = CSS_ALIGN_AUTO;
	}

	/* Fixed vertical grids map their logical tracks to physical axes before laying out items. */
	handled = 0;
	error = grid_vertical_fixed(tree, box, &handled);
	if (error != 0)
		return error;
	if (handled)
		return 0;
	error = grid_vertical_auto(tree, box, &handled);
	if (error != 0)
		return error;
	if (handled)
		return 0;

	/* The items, placed in their cells. */
	memset(&grid, 0, sizeof(grid));
	error = grid_collect(box, &grid);
	if (error == 0)
		error = grid_place(&grid, box->style.grid_auto_flow);
	if (error != 0) {
		grid_release(&grid);
		return error;
	}

	/* The gaps between the tracks. */
	column_gap = 0;
	if (box->style.column_gap.unit == CSS_UNIT_PX)
		column_gap = layout_from_px(box->style.column_gap.value);
	if (box->style.column_gap.unit == CSS_UNIT_PERCENT)
		column_gap = (layout_unit)((float)box->width * box->style.column_gap.value / 100.0f);
	row_gap = 0;
	if (box->style.row_gap.unit == CSS_UNIT_PX)
		row_gap = layout_from_px(box->style.row_gap.value);

	/* The columns' widths. */
	error = grid_size_columns(tree, box, &grid, column_gap);
	if (error != 0) {
		grid_release(&grid);
		return error;
	}

	/* The columns side by side: where they start (justify-content moves a grid narrower than its box). */
	used = column_gap * (layout_unit)(grid.column_count - 1);
	for (position = 0; position < grid.column_count; position++)
		used += grid.columns[position];
	box->grid_content = used;
	shift = 0;
	room = box->width - used;
	if (room > 0 && box->style.justify_content == CSS_ALIGN_CENTER)
		shift = room / 2;
	if (room > 0) {
		if (box->style.justify_content == CSS_ALIGN_RIGHT ||
		    (box->style.justify_content == CSS_ALIGN_NORMAL &&
		    box->style.direction == CSS_DIRECTION_RTL &&
		    box->style.writing_mode == CSS_WRITING_HORIZONTAL_TB) ||
		    (box->style.justify_content == CSS_ALIGN_END &&
		    box->style.direction == CSS_DIRECTION_LTR) ||
		    (box->style.justify_content == CSS_ALIGN_START &&
		    box->style.direction == CSS_DIRECTION_RTL))
			shift = room;
	}

	/* Each item laid out in its area's width. */
	for (index = 0; index < grid.item_count; index++) {
		item = &grid.items[index];
		area_width = column_gap * (layout_unit)(item->column_span - 1);
		for (position = item->column; position < item->column + item->column_span; position++)
			area_width += grid.columns[position];
		justify = item->box->style.justify_self;
		if (justify == CSS_ALIGN_AUTO)
			justify = box->style.justify_items;
		if (justify == CSS_ALIGN_NORMAL)
			justify = item->box->replaced ? CSS_ALIGN_START : CSS_ALIGN_STRETCH;
		if (box->style.writing_mode != CSS_WRITING_HORIZONTAL_TB ||
		    item->box->style.writing_mode != CSS_WRITING_HORIZONTAL_TB ||
		    item->box->style.min_width.unit != CSS_UNIT_AUTO ||
		    item->box->style.max_width.unit != CSS_UNIT_NONE)
			justify = CSS_ALIGN_STRETCH;
		if (item->box->replaced &&
		    (item->box->style.margin[CSS_LEFT].unit == CSS_UNIT_AUTO ||
		    item->box->style.margin[CSS_RIGHT].unit == CSS_UNIT_AUTO))
			justify = CSS_ALIGN_STRETCH;
		else if (item->box->style.margin[CSS_LEFT].unit == CSS_UNIT_AUTO ||
		    item->box->style.margin[CSS_RIGHT].unit == CSS_UNIT_AUTO)
			justify = CSS_ALIGN_START;
		error = grid_lay_item(tree, item->box, area_width, justify);
		if (error != 0) {
			grid_release(&grid);
			return error;
		}
	}

	/* The rows' heights from the items. */
	grid_size_rows(box, &grid, row_gap);
	row_shift = 0;
	if (box->style.writing_mode == CSS_WRITING_HORIZONTAL_TB &&
	    box->style.align_content == CSS_ALIGN_CENTER &&
	    box->style.height.unit == CSS_UNIT_PX) {
		layout_unit available_height;

		available_height = layout_from_px(box->style.height.value);
		if (box->style.box_sizing == CSS_BOX_SIZING_BORDER)
			available_height -= box->border[CSS_TOP] + box->padding[CSS_TOP] +
			    box->padding[CSS_BOTTOM] + box->border[CSS_BOTTOM];
		used = 0;
		if (grid.row_count > 0)
			used = row_gap * (layout_unit)(grid.row_count - 1);
		for (position = 0; position < grid.row_count; position++)
			used += grid.rows[position];
		if (available_height > used)
			row_shift = (available_height - used) / 2;
	}

	/* Absolute children use valid definite grid areas without sizing the tracks. */
	grid_static_position(box, &grid, column_gap, row_gap, shift, row_shift);

	/* Each item in its area, aligned in it across the rows. */
	for (index = 0; index < grid.item_count; index++) {
		item = &grid.items[index];
		child = item->box;

		/* The area's width, then its corner and height. */
		area_width = column_gap * (layout_unit)(item->column_span - 1);
		for (position = item->column; position < item->column + item->column_span; position++)
			area_width += grid.columns[position];
		x = shift;
		if (box->style.direction == CSS_DIRECTION_RTL &&
		    box->style.writing_mode == CSS_WRITING_HORIZONTAL_TB) {
			for (position = item->column + item->column_span;
			     position < grid.column_count; position++)
				x += grid.columns[position] + column_gap;
		} else {
			for (position = 0; position < item->column; position++)
				x += grid.columns[position] + column_gap;
		}
		y = row_shift;
		for (position = 0; position < item->row; position++)
			y += grid.rows[position] + row_gap;
		area_height = row_gap * (layout_unit)(item->row_span - 1);
		for (position = item->row; position < item->row + item->row_span; position++)
			area_height += grid.rows[position];
		/* The item's alignment: its align-self, or the container's align-items. */
		align = child->style.align_self;
		if (align == CSS_ALIGN_AUTO)
			align = box->style.align_items;
		if (align == CSS_ALIGN_NORMAL)
			align = child->replaced ? CSS_ALIGN_START : CSS_ALIGN_STRETCH;
		/* Definite horizontal rows distribute block-axis auto margins after sizing. */
		auto_top = child->style.margin[CSS_TOP].unit == CSS_UNIT_AUTO;
		auto_bottom = child->style.margin[CSS_BOTTOM].unit == CSS_UNIT_AUTO;
		auto_block_margin = 0;
		if ((auto_top || auto_bottom) && box->style.writing_mode == CSS_WRITING_HORIZONTAL_TB &&
		    item->row_span == 1 && item->row < box->style.row_count &&
		    box->style.rows[item->row].kind == CSS_TRACK_LENGTH) {
			if (box->style.rows[item->row].size.unit == CSS_UNIT_PX ||
			    (box->style.rows[item->row].size.unit == CSS_UNIT_PERCENT &&
			    box->style.height.unit == CSS_UNIT_PX))
				auto_block_margin = 1;
		}
		if (auto_block_margin)
			align = CSS_ALIGN_START;
		baseline_eligible = grid_fixed_row_baseline(box, item, &baseline);
		if (align == CSS_ALIGN_BASELINE && !baseline_eligible)
			align = CSS_ALIGN_START;

		/* A definite grid area must reach percentage descendants of a stretched item. */
		error = grid_relay_item(tree, child, area_width, area_height, align);
		if (error != 0) {
			grid_release(&grid);
			return error;
		}

		/* A stretching item without a height of its own fills the area's height. */
		frame = child->border[CSS_TOP] + child->padding[CSS_TOP] + child->padding[CSS_BOTTOM] + child->border[CSS_BOTTOM];
		if (align == CSS_ALIGN_STRETCH && child->style.height.unit == CSS_UNIT_AUTO) {
			child->height = area_height - child->margin[CSS_TOP] - child->margin[CSS_BOTTOM] - frame;
			if (child->height < 0)
				child->height = 0;
		}

		/* Its place, moved down by the room it leaves when it is at the end or the center. */
		child->x = x + child->margin[CSS_LEFT];
		if (box->style.writing_mode == CSS_WRITING_HORIZONTAL_TB &&
		    (child->style.width.unit != CSS_UNIT_AUTO ||
		    (child->style.writing_mode == CSS_WRITING_HORIZONTAL_TB &&
		    child->style.min_width.unit == CSS_UNIT_AUTO &&
		    child->style.max_width.unit == CSS_UNIT_NONE &&
		    child->style.margin[CSS_LEFT].unit != CSS_UNIT_AUTO &&
		    child->style.margin[CSS_RIGHT].unit != CSS_UNIT_AUTO))) {
			justify = child->style.justify_self;
			if (justify == CSS_ALIGN_AUTO)
				justify = box->style.justify_items;
			if (justify == CSS_ALIGN_NORMAL)
				justify = child->replaced ? CSS_ALIGN_START : CSS_ALIGN_STRETCH;
			if (justify == CSS_ALIGN_STRETCH)
				justify = CSS_ALIGN_START;
			if (justify == CSS_ALIGN_SELF_START)
				justify = child->style.direction == CSS_DIRECTION_RTL ? CSS_ALIGN_RIGHT : CSS_ALIGN_LEFT;
			if (justify == CSS_ALIGN_SELF_END)
				justify = child->style.direction == CSS_DIRECTION_RTL ? CSS_ALIGN_LEFT : CSS_ALIGN_RIGHT;
			outer_width = child->margin[CSS_LEFT] + child->border[CSS_LEFT] +
			    child->padding[CSS_LEFT] + child->width + child->padding[CSS_RIGHT] +
			    child->border[CSS_RIGHT] + child->margin[CSS_RIGHT];
			if (area_width > outer_width &&
			    (justify == CSS_ALIGN_RIGHT ||
			    (justify == CSS_ALIGN_END && box->style.direction == CSS_DIRECTION_LTR) ||
			    (justify == CSS_ALIGN_START && box->style.direction == CSS_DIRECTION_RTL)))
				child->x += area_width - outer_width;
			if (area_width > outer_width && justify == CSS_ALIGN_CENTER)
				child->x += (area_width - outer_width) / 2;
		}
		if (grid_fixed_column_baseline(box, item, &right_extent)) {
			/* Orthogonal empty items share a border-left baseline in this column. */
			shared_right_extent = 0;
			for (peer = 0; peer < grid.item_count; peer++) {
				if (grid.items[peer].column != item->column)
					continue;
				if (!grid_fixed_column_baseline(box, &grid.items[peer], &right_extent))
					continue;
				if (right_extent > shared_right_extent)
					shared_right_extent = right_extent;
			}
			child->x = x + area_width - shared_right_extent;
		}
		child->y = y + child->margin[CSS_TOP];
		if (auto_block_margin) {
			/* Auto margins absorb only positive space left in the final row. */
			free_height = area_height - grid_outer_height(child);
			if (free_height > 0 && auto_top)
				child->y += auto_bottom ? free_height / 2 : free_height;
		}
		if (align == CSS_ALIGN_BASELINE) {
			/* Members in this fixed row share their first or synthesized baseline. */
			shared_baseline = 0;
			/* Find the lowest baseline among participating siblings. */
			for (peer = 0; peer < grid.item_count; peer++) {
				if (grid.items[peer].row != item->row)
					continue;
				peer_baseline_eligible = grid_fixed_row_baseline(box, &grid.items[peer], &peer_baseline);
				if (!peer_baseline_eligible)
					continue;
				if (peer_baseline > shared_baseline)
					shared_baseline = peer_baseline;
			}
			child->y += shared_baseline - baseline;
		}
		outer = grid_outer_height(child);
		if (align == CSS_ALIGN_END && area_height > outer)
			child->y += area_height - outer;
		if (align == CSS_ALIGN_CENTER && area_height > outer)
			child->y += (area_height - outer) / 2;
	}

	/* The container's content height: its rows and the gaps between them. */
	box->height = row_gap * (layout_unit)(grid.row_count - 1);
	if (grid.row_count == 0)
		box->height = 0;
	for (position = 0; position < grid.row_count; position++)
		box->height += grid.rows[position];

	/* The state is no longer needed. */
	grid_release(&grid);

	/* Succeeded: the container's content is laid out. */
	return 0;
}

/* Lays out two implicit auto tracks on each physical axis of a definite vertical Grid. */
static int
grid_vertical_auto(
	struct layout_tree *tree,
	struct layout_box *box,
	int *handled)
{
	struct grid_state grid;
	struct grid_item *placed;
	struct layout_box *child;
	struct layout_box *text_box;
	struct css_length saved_height;
	const struct css_track *track;
	const struct layout_line *line;
	const struct layout_fragment *last;
	layout_unit columns[2];
	layout_unit rows[2];
	layout_unit natural_height[4];
	layout_unit width;
	layout_unit height;
	layout_unit area_width;
	layout_unit area_height;
	layout_unit outer_width;
	layout_unit outer_height;
	layout_unit content_height;
	layout_unit remaining;
	int occupied[4];
	size_t index;
	size_t line_index;
	size_t text_index;
	int align;
	int justify;
	int side;
	int cell;
	int error;

	/* Leaves other Grid forms to the existing layout path. */
	*handled = 0;
	if (box->style.display != CSS_DISPLAY_INLINE_GRID ||
	    (box->style.writing_mode != CSS_WRITING_VERTICAL_LR &&
	    box->style.writing_mode != CSS_WRITING_VERTICAL_RL) ||
	    box->style.direction != CSS_DIRECTION_LTR ||
	    box->style.width.unit != CSS_UNIT_PX ||
	    box->style.height.unit != CSS_UNIT_PX ||
	    box->style.box_sizing != CSS_BOX_SIZING_CONTENT ||
	    box->style.column_count != 0 || box->style.row_count != 0 ||
	    box->style.column_gap.value != 0 || box->style.row_gap.value != 0)
		return 0;

	/* Padding and borders require a separate content-box mapping. */
	for (side = 0; side < 4; side++) {
		if (box->border[side] != 0 || box->padding[side] != 0)
			return 0;
	}

	/* Every item must use the bounded text and box model. */
	for (child = box->first_child; child != NULL; child = child->next) {
		if (child->out_of_flow || child->replaced ||
		    (child->style.writing_mode != box->style.writing_mode &&
		    child->style.writing_mode != CSS_WRITING_HORIZONTAL_TB) ||
		    child->style.width.unit != CSS_UNIT_AUTO ||
		    child->style.height.unit != CSS_UNIT_AUTO)
			return 0;

		/* The measured path uses one unwrapped sideways Latin text run. */
		text_box = child->first_child;
		if (text_box == NULL || text_box->kind != LAYOUT_TEXT ||
		    text_box->next != NULL ||
		    (child->style.writing_mode == box->style.writing_mode &&
		    child->style.text_orientation == CSS_TEXT_ORIENTATION_UPRIGHT))
			return 0;

		/* Other scripts need upright glyph metrics and shaping. */
		for (text_index = 0; text_index < text_box->text_length; text_index++) {
			if (text_box->text[text_index] > 0x7fU)
				return 0;
		}

		/* The physical frame and margins must have definite nonnegative sizes. */
		layout_box_model(child, 0);
		if (child->style.box_sizing != CSS_BOX_SIZING_CONTENT)
			return 0;
		for (side = 0; side < 4; side++) {
			if (child->style.margin[side].unit != CSS_UNIT_PX ||
			    child->style.padding[side].unit != CSS_UNIT_PX ||
			    child->margin[side] < 0 || child->border[side] < 0 ||
			    child->padding[side] < 0)
				return 0;
		}
	}

	/* Collects the items before the common placer resolves their implicit tracks. */
	memset(&grid, 0, sizeof(grid));
	error = grid_collect(box, &grid);
	if (error != 0) {
		grid_release(&grid);
		return error;
	}

	/* Resolves each explicit line against the two implicit auto tracks. */
	error = grid_place(&grid, box->style.grid_auto_flow);
	if (error != 0) {
		grid_release(&grid);
		return error;
	}

	/* A larger or incomplete grid belongs to the general path. */
	if (grid.column_count != 2 || grid.row_count != 2 || grid.item_count != 4) {
		grid_release(&grid);
		return 0;
	}

	/* Every implicit track must have the default auto sizing function. */
	for (cell = 0; cell < 2; cell++) {
		track = grid_column_track(&box->style, cell);
		if (track->kind != CSS_TRACK_AUTO) {
			grid_release(&grid);
			return 0;
		}
		track = grid_row_track(&box->style, cell);
		if (track->kind != CSS_TRACK_AUTO) {
			grid_release(&grid);
			return 0;
		}
	}

	/* One item per cell keeps the item and track contributions unambiguous. */
	memset(occupied, 0, sizeof(occupied));
	for (index = 0; index < grid.item_count; index++) {
		placed = &grid.items[index];
		if (placed->column_span != 1 || placed->row_span != 1 ||
		    placed->column < 0 || placed->column >= 2 ||
		    placed->row < 0 || placed->row >= 2) {
			grid_release(&grid);
			return 0;
		}
		cell = placed->row * 2 + placed->column;
		if (occupied[cell]) {
			grid_release(&grid);
			return 0;
		}
		occupied[cell] = 1;

		/* The bounded painter and placement handle only start and stretch. */
		child = placed->box;
		align = child->style.align_self;
		if (align == CSS_ALIGN_AUTO)
			align = box->style.align_items;
		if (align == CSS_ALIGN_NORMAL)
			align = CSS_ALIGN_STRETCH;
		justify = child->style.justify_self;
		if (justify == CSS_ALIGN_AUTO)
			justify = box->style.justify_items;
		if (justify == CSS_ALIGN_NORMAL)
			justify = CSS_ALIGN_STRETCH;
		if ((align != CSS_ALIGN_START && align != CSS_ALIGN_STRETCH) ||
		    (justify != CSS_ALIGN_START && justify != CSS_ALIGN_STRETCH)) {
			grid_release(&grid);
			return 0;
		}
	}

	/* Measure each item's physical content extents before sharing free track room. */
	columns[0] = 0;
	columns[1] = 0;
	rows[0] = 0;
	rows[1] = 0;
	for (index = 0; index < grid.item_count; index++) {
		placed = &grid.items[index];
		child = placed->box;
		saved_height = child->style.height;

		/* A definite inline room gives a vertical text run its physical line width. */
		if (child->style.writing_mode == box->style.writing_mode) {
			child->style.height.unit = CSS_UNIT_PX;
			content_height = layout_from_px(box->style.height.value) -
			    child->margin[CSS_TOP] - child->margin[CSS_BOTTOM] -
			    child->border[CSS_TOP] - child->border[CSS_BOTTOM] -
			    child->padding[CSS_TOP] - child->padding[CSS_BOTTOM];
			if (content_height < 0)
				content_height = 0;
			child->style.height.value = layout_to_px(content_height);
			child->style.height.offset = 0;
			child->max_content_known = 0;
		}
		error = layout_max_content(tree, child, &width);
		child->style.height = saved_height;
		if (error != 0) {
			grid_release(&grid);
			return error;
		}
		height = child->height;
		if (child->style.writing_mode == box->style.writing_mode) {
			height = 0;

			/* The last glyph of each line gives its natural inline advance. */
			for (line_index = 0; line_index < child->line_count; line_index++) {
				line = &child->lines[line_index];
				if (line->fragment_count == 0)
					continue;
				last = &line->fragments[line->fragment_count - 1U];
				if (last->x + last->width > height)
					height = last->x + last->width;
			}
		}
		natural_height[index] = height;
		outer_width = width + child->margin[CSS_LEFT] + child->margin[CSS_RIGHT] +
		    child->border[CSS_LEFT] + child->border[CSS_RIGHT] +
		    child->padding[CSS_LEFT] + child->padding[CSS_RIGHT];
		outer_height = height + child->margin[CSS_TOP] + child->margin[CSS_BOTTOM] +
		    child->border[CSS_TOP] + child->border[CSS_BOTTOM] +
		    child->padding[CSS_TOP] + child->padding[CSS_BOTTOM];
		if (outer_width > rows[placed->row])
			rows[placed->row] = outer_width;
		if (outer_height > columns[placed->column])
			columns[placed->column] = outer_height;
	}

	/* Each auto track receives an equal share after its content minimum. */
	remaining = box->width - rows[0] - rows[1];
	if (remaining > 0) {
		rows[0] += remaining / 2;
		rows[1] += remaining - remaining / 2;
	}
	remaining = layout_from_px(box->style.height.value) - columns[0] - columns[1];
	if (remaining > 0) {
		columns[0] += remaining / 2;
		columns[1] += remaining - remaining / 2;
	}
	box->height = layout_from_px(box->style.height.value);
	box->grid_content = rows[0] + rows[1];

	/* Lay each item out in its physical row width and column height. */
	for (index = 0; index < grid.item_count; index++) {
		placed = &grid.items[index];
		child = placed->box;
		area_width = rows[placed->row];
		area_height = columns[placed->column];
		align = child->style.align_self;
		if (align == CSS_ALIGN_AUTO)
			align = box->style.align_items;
		if (align == CSS_ALIGN_NORMAL)
			align = CSS_ALIGN_STRETCH;
		justify = child->style.justify_self;
		if (justify == CSS_ALIGN_AUTO)
			justify = box->style.justify_items;
		if (justify == CSS_ALIGN_NORMAL)
			justify = CSS_ALIGN_STRETCH;
		saved_height = child->style.height;
		if (child->style.writing_mode == box->style.writing_mode) {
			child->style.height.unit = CSS_UNIT_PX;
			content_height = area_height - child->margin[CSS_TOP] -
			    child->margin[CSS_BOTTOM] - child->border[CSS_TOP] -
			    child->border[CSS_BOTTOM] - child->padding[CSS_TOP] -
			    child->padding[CSS_BOTTOM];
			if (content_height < 0)
				content_height = 0;
			child->style.height.value = layout_to_px(content_height);
			child->style.height.offset = 0;
		}
		error = grid_lay_item(tree, child, area_width, align);
		if (error == 0 && child->style.writing_mode == CSS_WRITING_HORIZONTAL_TB)
			error = grid_relay_item(tree, child, area_width, area_height, justify);
		child->style.height = saved_height;
		if (error != 0) {
			grid_release(&grid);
			return error;
		}
		if (child->style.writing_mode == box->style.writing_mode && justify == CSS_ALIGN_START)
			child->height = natural_height[index];
		if (box->style.writing_mode == CSS_WRITING_VERTICAL_RL) {
			/* The row's block-start edge is on the physical right. */
			child->x = box->width - (placed->row == 0 ? 0 : rows[0]) -
			    child->margin[CSS_RIGHT] - child->border[CSS_LEFT] -
			    child->padding[CSS_LEFT] - child->width -
			    child->padding[CSS_RIGHT] - child->border[CSS_RIGHT];
		} else {
			child->x = (placed->row == 0 ? 0 : rows[0]) + child->margin[CSS_LEFT];
		}
		child->y = (placed->column == 0 ? 0 : columns[0]) + child->margin[CSS_TOP];
	}

	/* The common Grid state no longer owns any output. */
	grid_release(&grid);
	*handled = 1;

	/* Succeeded: the physical axis mapping consumed this Grid. */
	return 0;
}

/* Lays out bounded fixed vertical tracks in physical x/y axes. */
static int
grid_vertical_fixed(
	struct layout_tree *tree,
	struct layout_box *box,
	int *handled)
{
	struct grid_state grid;
	struct grid_item *placed;
	struct layout_box *child;
	layout_unit columns[CSS_TRACKS + 1];
	layout_unit rows[CSS_TRACKS + 1];
	layout_unit area_width;
	layout_unit area_height;
	size_t index;
	int column;
	int row;
	int align;
	int justify;
	int side;
	int error;

	/* Other writing modes and Grid forms retain their existing layout path. */
	*handled = 0;
	if ((box->style.writing_mode != CSS_WRITING_VERTICAL_LR &&
	    box->style.writing_mode != CSS_WRITING_VERTICAL_RL) ||
	    box->style.direction != CSS_DIRECTION_LTR ||
	    box->style.display != CSS_DISPLAY_INLINE_GRID ||
	    box->style.height.unit != CSS_UNIT_AUTO ||
	    box->style.column_count == 0 || box->style.row_count == 0 ||
	    box->style.column_gap.value != 0 || box->style.row_gap.value != 0)
		return 0;

	/* The explicit tracks must have definite pixel sizes. */
	for (column = 0; column < box->style.column_count; column++) {
		if (box->style.columns[column].kind != CSS_TRACK_LENGTH ||
		    box->style.columns[column].size.unit != CSS_UNIT_PX)
			return 0;
	}
	for (row = 0; row < box->style.row_count; row++) {
		if (box->style.rows[row].kind != CSS_TRACK_LENGTH ||
		    box->style.rows[row].size.unit != CSS_UNIT_PX)
			return 0;
	}

	/* An inline grid can be laid out again at its shrink-to-fit physical width. */
	area_width = 0;
	for (row = 0; row < box->style.row_count; row++)
		area_width += layout_from_px(box->style.rows[row].size.value);
	if (box->style.width.unit != CSS_UNIT_AUTO &&
	    (box->style.width.unit != CSS_UNIT_PX ||
	    layout_from_px(box->style.width.value) != area_width))
		return 0;

	/* Out-of-flow or separately sized children still use the established path. */
	for (child = box->first_child; child != NULL; child = child->next) {
		if (child->out_of_flow || child->replaced ||
		    (child->style.writing_mode != box->style.writing_mode &&
		    child->style.writing_mode != CSS_WRITING_HORIZONTAL_TB) ||
		    child->style.width.unit != CSS_UNIT_AUTO ||
		    child->style.height.unit != CSS_UNIT_AUTO)
			return 0;
		for (side = 0; side < 4; side++) {
			if (child->style.margin[side].unit == CSS_UNIT_AUTO)
				return 0;
		}
	}

	/* Placement remains the common logical grid algorithm. */
	memset(&grid, 0, sizeof(grid));
	error = grid_collect(box, &grid);
	if (error == 0)
		error = grid_place(&grid, box->style.grid_auto_flow);
	if (error != 0) {
		grid_release(&grid);
		return error;
	}
	if (grid.column_count != box->style.column_count ||
	    grid.row_count != box->style.row_count) {
		grid_release(&grid);
		return 0;
	}

	/* This first physical mapping covers one track per item and start/stretch only. */
	for (index = 0; index < grid.item_count; index++) {
		placed = &grid.items[index];
		if (placed->column_span != 1 || placed->row_span != 1) {
			grid_release(&grid);
			return 0;
		}
		child = placed->box;
		align = child->style.align_self;
		if (align == CSS_ALIGN_AUTO)
			align = box->style.align_items;
		if (align == CSS_ALIGN_NORMAL)
			align = CSS_ALIGN_STRETCH;
		justify = child->style.justify_self;
		if (justify == CSS_ALIGN_AUTO)
			justify = box->style.justify_items;
		if (justify == CSS_ALIGN_NORMAL)
			justify = CSS_ALIGN_STRETCH;
		if ((align != CSS_ALIGN_START && align != CSS_ALIGN_STRETCH) ||
		    (justify != CSS_ALIGN_START && justify != CSS_ALIGN_STRETCH)) {
			grid_release(&grid);
			return 0;
		}
	}

	/* Columns advance downwards; rows advance in the writing mode's block direction. */
	columns[0] = 0;
	for (column = 0; column < grid.column_count; column++)
		columns[column + 1] = columns[column] + layout_from_px(box->style.columns[column].size.value);
	rows[0] = 0;
	for (row = 0; row < grid.row_count; row++)
		rows[row + 1] = rows[row] + layout_from_px(box->style.rows[row].size.value);
	box->width = rows[grid.row_count];
	box->height = columns[grid.column_count];
	box->grid_content = box->width;

	/* Each item receives the physical width of its row and height of its column. */
	for (index = 0; index < grid.item_count; index++) {
		placed = &grid.items[index];
		child = placed->box;
		area_width = rows[placed->row + 1] - rows[placed->row];
		area_height = columns[placed->column + 1] - columns[placed->column];
		align = child->style.align_self;
		if (align == CSS_ALIGN_AUTO)
			align = box->style.align_items;
		if (align == CSS_ALIGN_NORMAL)
			align = CSS_ALIGN_STRETCH;
		justify = child->style.justify_self;
		if (justify == CSS_ALIGN_AUTO)
			justify = box->style.justify_items;
		if (justify == CSS_ALIGN_NORMAL)
			justify = CSS_ALIGN_STRETCH;
		error = grid_lay_item(tree, child, area_width, align);
		if (error != 0) {
			grid_release(&grid);
			return error;
		}
		error = grid_relay_item(tree, child, area_width, area_height, justify);
		if (error != 0) {
			grid_release(&grid);
			return error;
		}
		if (box->style.writing_mode == CSS_WRITING_VERTICAL_RL) {
			/* The row's block-start edge is on the physical right. */
			child->x = box->width - rows[placed->row] - child->margin[CSS_RIGHT] -
			    child->border[CSS_LEFT] - child->padding[CSS_LEFT] - child->width -
			    child->padding[CSS_RIGHT] - child->border[CSS_RIGHT];
		} else {
			child->x = rows[placed->row] + child->margin[CSS_LEFT];
		}
		child->y = columns[placed->column] + child->margin[CSS_TOP];
	}

	/* Succeeded: the physical Grid path consumed this bounded case. */
	grid_release(&grid);
	*handled = 1;
	return 0;
}

/* Gathers a container's in-flow children as items, with the places their styles give on each axis. */
static int
grid_collect(
	struct layout_box *box,
	struct grid_state *grid)
{
	struct layout_box *child;
	struct grid_item *item;
	size_t count;

	/* Counts the in-flow children. */
	count = 0;
	for (child = box->first_child; child != NULL; child = child->next) {
		if (!child->out_of_flow)
			count++;
	}

	/* The array (one place at least, so that none is not NULL). */
	grid->items = calloc(count + 1U, sizeof(*grid->items));
	if (grid->items == NULL)
		return ENOMEM;

	/* The columns and rows the templates make. */
	grid->column_count = box->style.column_count;
	grid->row_count = box->style.row_count;

	/* Each child, its places resolved against the templates. */
	for (child = box->first_child; child != NULL; child = child->next) {
		if (child->out_of_flow)
			continue;
		item = &grid->items[grid->item_count];
		item->box = child;
		grid_resolve(&child->style.grid_column, box->style.column_count, &item->column, &item->column_span);
		grid_resolve(&child->style.grid_row, box->style.row_count, &item->row, &item->row_span);
		grid->item_count++;
	}

	/* Succeeded: the items are gathered. */
	return 0;
}

/*
 * Resolves an item's place on one axis to its first track (-1 for auto)
 * and its span: lines count from 1, negative lines from the end of the
 * explicit tracks; a start and an end line give the span between them.
 */
static void
grid_resolve(
	const struct css_grid_place *place,
	int explicit_count,
	int *start,
	int *span)
{
	int first;
	int last;

	/* The start line's track, or none. */
	first = -1;
	if (place->start > 0)
		first = place->start - 1;
	if (place->start < 0)
		first = explicit_count + 1 + place->start;
	if (place->start < 0 && first < 0)
		first = 0;

	/* The end line's track (the one after the area), or none. */
	last = -1;
	if (place->end > 0)
		last = place->end - 1;
	if (place->end < 0)
		last = explicit_count + 1 + place->end;
	if (place->end < 0 && last < 0)
		last = 0;

	/* The span: between the lines, or the one given, or one. */
	*span = place->start_span;
	if (place->end_span > *span)
		*span = place->end_span;
	if (*span < 1)
		*span = 1;
	if (first >= 0 && last >= 0) {
		if (last < first) {
			*span = first - last;
			first = last;
		} else if (last > first) {
			*span = last - first;
		}
	} else if (first < 0 && last >= 0) {
		/* Only the end is known: the area ends there. */
		first = last - *span;
		if (first < 0)
			first = 0;
	}

	/* The first track, or -1 for auto. */
	*start = first;
}

/* Gives bounded vertical Grid absolute items their physical area and alignment. */
static void
grid_static_vertical_start(
	struct layout_box *box)
{
	struct layout_box *child;
	layout_unit x;
	layout_unit y;
	layout_unit width;
	int column;
	int column_span;
	int row;
	int row_span;
	int align;
	int justify;
	int index;

	/* Only fixed two-by-two LTR tracks have this definite physical mapping. */
	if (box->style.position != CSS_POSITION_RELATIVE ||
	    box->style.display != CSS_DISPLAY_INLINE_GRID ||
	    box->style.direction != CSS_DIRECTION_LTR ||
	    box->style.column_count != 2 || box->style.row_count != 2 ||
	    box->style.column_gap.value != 0 || box->style.row_gap.value != 0)
		return;
	for (index = 0; index < 2; index++) {
		if (box->style.columns[index].kind != CSS_TRACK_LENGTH ||
		    box->style.columns[index].size.unit != CSS_UNIT_PX ||
		    box->style.rows[index].kind != CSS_TRACK_LENGTH ||
		    box->style.rows[index].size.unit != CSS_UNIT_PX)
			return;
	}
	for (index = 0; index < 4; index++) {
		if (box->border[index] != 0 || box->padding[index] != 0)
			return;
	}
	width = layout_from_px(box->style.rows[0].size.value + box->style.rows[1].size.value);
	if (box->width != width)
		return;

	/* Absolute children keep their sizing and are positioned after Grid layout. */
	for (child = box->first_child; child != NULL; child = child->next) {
		if (child->style.position != CSS_POSITION_ABSOLUTE ||
		    child->style.writing_mode != box->style.writing_mode ||
		    child->style.width.unit != CSS_UNIT_AUTO ||
		    child->style.height.unit != CSS_UNIT_AUTO)
			continue;
		for (index = 0; index < 4; index++) {
			if (child->style.offset[index].unit != CSS_UNIT_AUTO)
				break;
		}
		if (index != 4)
			continue;
		grid_resolve(&child->style.grid_column, 2, &column, &column_span);
		grid_resolve(&child->style.grid_row, 2, &row, &row_span);
		if (column < 0 || column >= 2 || row < 0 || row >= 2 ||
		    column_span != 1 || row_span != 1)
			continue;
		align = child->style.align_self;
		if (align == CSS_ALIGN_AUTO)
			align = box->style.align_items;
		justify = child->style.justify_self;
		if (justify == CSS_ALIGN_AUTO)
			justify = box->style.justify_items;
		/* Invalid align-self:left/right falls back to normal, the static start edge. */
		if (align == CSS_ALIGN_NORMAL)
			align = CSS_ALIGN_START;
		if (justify == CSS_ALIGN_NORMAL)
			justify = CSS_ALIGN_START;
		if ((align != CSS_ALIGN_START && align != CSS_ALIGN_END &&
		    align != CSS_ALIGN_CENTER && align != CSS_ALIGN_LEFT &&
		    align != CSS_ALIGN_RIGHT) ||
		    (justify != CSS_ALIGN_START && justify != CSS_ALIGN_END &&
		    justify != CSS_ALIGN_CENTER && justify != CSS_ALIGN_LEFT &&
		    justify != CSS_ALIGN_RIGHT && justify != CSS_ALIGN_SELF_START &&
		    justify != CSS_ALIGN_SELF_END))
			continue;

		/* Logical columns descend; logical rows go right or left. */
		y = column == 0 ? 0 : layout_from_px(box->style.columns[0].size.value);
		x = row == 0 ? 0 : layout_from_px(box->style.rows[0].size.value);
		if (box->style.writing_mode == CSS_WRITING_VERTICAL_RL) {
			child->static_x = box->width - x;
			child->static_x_from_right = 1;
		} else {
			child->static_x = x;
		}
		child->static_y = y;
		child->static_x_align = align;
		child->static_x_area_width = layout_from_px(box->style.rows[row].size.value);
		child->static_x_align_from_right =
		    align == CSS_ALIGN_RIGHT ||
		    (align == CSS_ALIGN_END &&
		    box->style.writing_mode == CSS_WRITING_VERTICAL_LR);
		child->static_y_align =
		    justify == CSS_ALIGN_RIGHT ? CSS_ALIGN_END :
		    justify == CSS_ALIGN_LEFT ? CSS_ALIGN_START : justify;
		child->static_y_area_height =
		    layout_from_px(box->style.columns[column].size.value);
	}
}

/* Gives a direct absolute child the origin of its definite grid area. */
static void
grid_static_position(
	struct layout_box *box,
	const struct grid_state *grid,
	layout_unit column_gap,
	layout_unit row_gap,
	layout_unit shift,
	layout_unit row_shift)
{
	struct layout_box *child;
	layout_unit x;
	layout_unit y;
	int column;
	int column_span;
	int row;
	int row_span;
	int index;
	int justify;
	int align;
	int full_inline;
	int full_block;
	int auto_start_row;
	int auto_end_row;
	layout_unit available_height;

	/* Other writing directions retain their existing static-position path. */
	if (box->style.position != CSS_POSITION_RELATIVE)
		return;
	if (box->style.writing_mode != CSS_WRITING_HORIZONTAL_TB) {
		grid_static_vertical_start(box);
		return;
	}
	if (box->style.direction != CSS_DIRECTION_LTR &&
	    box->style.direction != CSS_DIRECTION_RTL)
		return;

	/* A direct absolute child can use a definite explicit area without becoming a grid item. */
	for (child = box->first_child; child != NULL; child = child->next) {
		if (child->style.position != CSS_POSITION_ABSOLUTE)
			continue;
		if (child->style.offset[CSS_LEFT].unit != CSS_UNIT_AUTO)
			continue;
		if (child->style.offset[CSS_RIGHT].unit != CSS_UNIT_AUTO)
			continue;
		if (child->style.offset[CSS_TOP].unit != CSS_UNIT_AUTO)
			continue;
		if (child->style.offset[CSS_BOTTOM].unit != CSS_UNIT_AUTO)
			continue;

		/* An absolute box never creates a track or enters auto-placement. */
		grid_resolve(&child->style.grid_column, box->style.column_count,
		    &column, &column_span);
		grid_resolve(&child->style.grid_row, box->style.row_count,
		    &row, &row_span);
		available_height = 0;
		if (box->style.align_content == CSS_ALIGN_CENTER &&
		    box->style.height.unit == CSS_UNIT_PX &&
		    box->padding[CSS_TOP] == 0 && box->padding[CSS_BOTTOM] == 0 &&
		    box->border[CSS_TOP] == 0 && box->border[CSS_BOTTOM] == 0)
			available_height = layout_from_px(box->style.height.value);
		full_block = available_height > 0 && child->style.grid_row.start == 0 &&
		    child->style.grid_row.end == 0 &&
		    child->style.grid_row.start_span == 0 &&
		    child->style.grid_row.end_span == 0;
		auto_start_row = available_height > 0 && child->style.grid_row.start == 0 &&
		    child->style.grid_row.end == 1;
		auto_end_row = available_height > 0 && child->style.grid_row.start == 2 &&
		    child->style.grid_row.end == 0;
		full_inline = child->style.grid_column.start == 0 &&
		    child->style.grid_column.end == 0 &&
		    child->style.grid_column.start_span == 0 &&
		    child->style.grid_column.end_span == 0;
		if (full_inline && (box->padding[CSS_LEFT] != 0 ||
		    box->padding[CSS_RIGHT] != 0 || box->border[CSS_LEFT] != 0 ||
		    box->border[CSS_RIGHT] != 0))
			continue;
		if (column < 0 && !full_inline)
			continue;
		if (row < 0 && !full_block)
			continue;
		if (column_span < 1)
			continue;
		if (row_span < 1)
			continue;
		if (!full_inline && column + column_span > box->style.column_count)
			continue;
		if (!full_block && row + row_span > box->style.row_count)
			continue;
		justify = child->style.justify_self;
		if (justify == CSS_ALIGN_AUTO)
			justify = box->style.justify_items;
		if (box->style.direction == CSS_DIRECTION_RTL &&
		    justify != CSS_ALIGN_START && justify != CSS_ALIGN_END &&
		    justify != CSS_ALIGN_SELF_END && justify != CSS_ALIGN_CENTER &&
		    justify != CSS_ALIGN_LEFT && justify != CSS_ALIGN_RIGHT &&
		    justify != CSS_ALIGN_SELF_START)
			continue;

		/* Track offsets include the container's content distribution and gaps. */
		x = shift;
		if (full_inline) {
			x = box->style.direction == CSS_DIRECTION_RTL ? box->width : 0;
			child->static_x_from_right = box->style.direction == CSS_DIRECTION_RTL;
		} else if (box->style.direction == CSS_DIRECTION_RTL) {
			/* The right edge includes the current column and every track after it. */
			for (index = column; index < grid->column_count; index++) {
				x += grid->columns[index];
				if (index + 1 < grid->column_count)
					x += column_gap;
			}
			child->static_x_from_right = 1;
		} else {
			for (index = 0; index < column; index++)
				x += grid->columns[index] + column_gap;
		}
		/* An auto start line reaches the padding edge, before content distribution. */
		if (child->style.grid_column.start == 0 &&
		    child->style.grid_column.end == 1)
			x = box->style.direction == CSS_DIRECTION_RTL ? box->width : 0;
		y = child->style.grid_row.start > 0 ? row_shift : 0;
		for (index = 0; !full_block && index < row; index++)
			y += grid->rows[index] + row_gap;
		child->static_x = x;
		child->static_y = y;
		if (child->style.writing_mode == CSS_WRITING_HORIZONTAL_TB &&
		    (justify == CSS_ALIGN_END || justify == CSS_ALIGN_SELF_END ||
		    justify == CSS_ALIGN_CENTER || justify == CSS_ALIGN_LEFT ||
		    justify == CSS_ALIGN_RIGHT || justify == CSS_ALIGN_SELF_START)) {
			child->static_x_align = justify;
			if (justify == CSS_ALIGN_SELF_END)
				child->static_x_align_from_right =
				    child->style.direction == CSS_DIRECTION_LTR;
			else if (justify == CSS_ALIGN_SELF_START)
				child->static_x_align_from_right =
				    child->style.direction == CSS_DIRECTION_RTL;
			else
				child->static_x_align_from_right =
				    justify == CSS_ALIGN_RIGHT ||
				    (justify == CSS_ALIGN_END && box->style.direction == CSS_DIRECTION_LTR);
			if (full_inline) {
				child->static_x_area_width = box->width;
			} else {
				for (index = column; index < column + column_span; index++) {
					child->static_x_area_width += grid->columns[index];
					if (index + 1 < column + column_span)
						child->static_x_area_width += column_gap;
				}
			}
			/* An auto end line reaches the opposite padding edge. */
			if (child->style.grid_column.start == 2 &&
			    child->style.grid_column.end == 0)
				child->static_x_area_width = box->style.direction == CSS_DIRECTION_RTL ?
				    x : box->width - x;
			if (child->static_x_area_width < 0)
				child->static_x_area_width = 0;
		}
		align = child->style.align_self;
		if (align == CSS_ALIGN_AUTO)
			align = box->style.align_items;
		if (child->style.writing_mode == CSS_WRITING_HORIZONTAL_TB &&
		    (align == CSS_ALIGN_END || align == CSS_ALIGN_SELF_END ||
		    align == CSS_ALIGN_CENTER)) {
			child->static_y_align = align;
			if (full_block) {
				child->static_y_area_height = available_height;
			} else if (auto_start_row) {
				child->static_y_area_height = row_shift;
			} else if (auto_end_row) {
				child->static_y_area_height = available_height - y;
			} else {
				for (index = row; index < row + row_span; index++) {
					child->static_y_area_height += grid->rows[index];
					if (index + 1 < row + row_span)
						child->static_y_area_height += row_gap;
				}
			}
			if (child->static_y_area_height < 0)
				child->static_y_area_height = 0;
		}
	}
}

/*
 * Places every item in the cells: the ones with both lines given first,
 * then the ones with a row, then the rest row after row in the first free
 * cells after the last one placed.  The grid grows to hold them.
 */
static int
grid_place(
	struct grid_state *grid,
	int auto_flow)
{
	struct grid_item *item;
	size_t index;
	int exchanged;
	int error;

	/* Column flow is row flow with the grid's two axes exchanged. */
	if (!(auto_flow & CSS_GRID_FLOW_COLUMN))
		return grid_place_row(grid, auto_flow & CSS_GRID_FLOW_DENSE);
	for (index = 0; index < grid->item_count; index++) {
		item = &grid->items[index];
		exchanged = item->column;
		item->column = item->row;
		item->row = exchanged;
		exchanged = item->column_span;
		item->column_span = item->row_span;
		item->row_span = exchanged;
	}
	exchanged = grid->column_count;
	grid->column_count = grid->row_count;
	grid->row_count = exchanged;

	/* The occupancy map is only needed inside placement, in its transposed axes. */
	error = grid_place_row(grid, auto_flow & CSS_GRID_FLOW_DENSE);
	exchanged = grid->column_count;
	grid->column_count = grid->row_count;
	grid->row_count = exchanged;
	for (index = 0; index < grid->item_count; index++) {
		item = &grid->items[index];
		exchanged = item->column;
		item->column = item->row;
		item->row = exchanged;
		exchanged = item->column_span;
		item->column_span = item->row_span;
		item->row_span = exchanged;
	}
	return error;
}

/* Places items in row order, optionally starting each search at the first cell. */
static int
grid_place_row(
	struct grid_state *grid,
	int dense)
{
	struct grid_item *item;
	size_t index;
	int cursor_row;
	int cursor_column;
	int error;
	int pass;
	int fits;

	/* The columns: the template's, and as many more as the items given a column need. */
	for (index = 0; index < grid->item_count; index++) {
		item = &grid->items[index];
		if (item->column >= 0 && item->column + item->column_span > grid->column_count)
			grid->column_count = item->column + item->column_span;
		if (item->column_span > grid->column_count)
			grid->column_count = item->column_span;
	}

	/* A grid has one column at least. */
	if (grid->column_count < 1)
		grid->column_count = 1;

	/* An auto item's span is at most the columns there are. */
	for (index = 0; index < grid->item_count; index++) {
		item = &grid->items[index];
		if (item->column_span > grid->column_count)
			item->column_span = grid->column_count;
	}

	/* The items with a column and a row, then those with a row only (in the first free columns of their rows). */
	for (pass = 0; pass < 2; pass++) {
		for (index = 0; index < grid->item_count; index++) {
			item = &grid->items[index];
			if (item->row < 0)
				continue;
			if (pass == 0 && item->column < 0)
				continue;
			if (pass == 1 && item->column >= 0)
				continue;

			/* An item with a row only takes the first columns free in it. */
			if (item->column < 0) {
				item->column = 0;
				fits = grid_fits(grid, item->row, item->column, item->row_span, item->column_span);
				while (!fits && item->column + item->column_span < grid->column_count) {
					item->column++;
					fits = grid_fits(grid, item->row, item->column, item->row_span, item->column_span);
				}
			}

			/* The item takes its cells. */
			error = grid_take(grid, item->row, item->column, item->row_span, item->column_span);
			if (error != 0)
				return error;
		}
	}

	/* The rest, row after row from the last one placed. */
	cursor_row = 0;
	cursor_column = 0;
	for (index = 0; index < grid->item_count; index++) {
		item = &grid->items[index];
		if (item->row >= 0)
			continue;
		if (dense) {
			cursor_row = 0;
			cursor_column = 0;
		}

		/* A column given: the first row from the cursor where the area is free. */
		if (item->column >= 0) {
			if (item->column < cursor_column)
				cursor_row++;
			fits = grid_fits(grid, cursor_row, item->column, item->row_span, item->column_span);
			while (!fits && cursor_row < GRID_ROWS_MAX) {
				cursor_row++;
				fits = grid_fits(grid, cursor_row, item->column, item->row_span, item->column_span);
			}

			/* The cursor is at the item's column. */
			cursor_column = item->column;
		} else {
			/* No column: the first free cells from the cursor, row after row. */
			fits = 0;
			while (!fits && cursor_row < GRID_ROWS_MAX) {
				if (cursor_column + item->column_span > grid->column_count) {
					cursor_row++;
					cursor_column = 0;
					continue;
				}

				/* The area at the cursor, or the next column. */
				fits = grid_fits(grid, cursor_row, cursor_column, item->row_span, item->column_span);
				if (!fits)
					cursor_column++;
			}
		}

		/* The item takes its cells, and the cursor moves past them. */
		if (item->column < 0)
			item->column = cursor_column;
		item->row = cursor_row;
		error = grid_take(grid, item->row, item->column, item->row_span, item->column_span);
		if (error != 0)
			return error;
		cursor_column = item->column + item->column_span;
	}

	/* The rows: the template's, and as many more as the items took. */
	if (grid->taken_rows > grid->row_count)
		grid->row_count = grid->taken_rows;

	/* Succeeded: every item has its area. */
	return 0;
}

/* Tells whether an area's cells are all free (rows past the grid's are free). */
static int
grid_fits(
	const struct grid_state *grid,
	int row,
	int column,
	int row_span,
	int column_span)
{
	int y;
	int x;

	/* Each cell of the area inside the rows there are. */
	for (y = row; y < row + row_span && y < grid->taken_rows; y++) {
		for (x = column; x < column + column_span; x++) {
			if (grid->taken[(size_t)y * (size_t)grid->column_count + (size_t)x])
				return 0;
		}
	}

	/* The area is free. */
	return 1;
}

/* Marks an area's cells taken, growing the rows to hold it (an area past the most rows is held in the last). */
static int
grid_take(
	struct grid_state *grid,
	int row,
	int column,
	int row_span,
	int column_span)
{
	unsigned char *taken;
	int rows;
	int y;
	int x;

	/* The area stays inside the most rows. */
	if (row >= GRID_ROWS_MAX)
		row = GRID_ROWS_MAX - 1;
	if (row + row_span > GRID_ROWS_MAX)
		row_span = GRID_ROWS_MAX - row;
	if (column + column_span > grid->column_count)
		column_span = grid->column_count - column;

	/* More rows when the area reaches past them. */
	rows = row + row_span;
	if (rows > grid->taken_rows) {
		taken = realloc(grid->taken, (size_t)rows * (size_t)grid->column_count);
		if (taken == NULL)
			return ENOMEM;
		memset(taken + (size_t)grid->taken_rows * (size_t)grid->column_count, 0,
		    (size_t)(rows - grid->taken_rows) * (size_t)grid->column_count);
		grid->taken = taken;
		grid->taken_rows = rows;
	}

	/* The cells. */
	for (y = row; y < row + row_span; y++) {
		for (x = column; x < column + column_span; x++)
			grid->taken[(size_t)y * (size_t)grid->column_count + (size_t)x] = 1;
	}

	/* Succeeded: the area is taken. */
	return 0;
}

/* Returns a declared column track, cycling implicit sizes after the explicit grid. */
static const struct css_track *
grid_column_track(
	const struct css_style *style,
	int column)
{
	if (column < style->column_count)
		return &style->columns[column];
	return &style->auto_columns[(column - style->column_count) % style->auto_column_count];
}

/* Returns a declared row track, cycling implicit sizes after the explicit grid. */
static const struct css_track *
grid_row_track(
	const struct css_style *style,
	int row)
{
	if (row < style->row_count)
		return &style->rows[row];
	return &style->auto_rows[(row - style->row_count) % style->auto_row_count];
}

/*
 * Sizes the columns: fixed tracks at their lengths, auto tracks at their
 * one-column items' content widths (widened for the items spanning them),
 * then the fr tracks sharing what is left of the container's width (at
 * least their minimum), or the auto tracks sharing it when there is no fr.
 * While a container is measured its fr tracks are sized as auto ones.
 */
static int
grid_size_columns(
	struct layout_tree *tree,
	struct layout_box *box,
	struct grid_state *grid,
	layout_unit gap)
{
	const struct css_track *track;
	struct grid_item *item;
	layout_unit available;
	layout_unit width;
	layout_unit used;
	layout_unit share;
	layout_unit remainder;
	layout_unit spanned;
	float fr_total;
	size_t index;
	int column;
	int autos;
	int last_auto;
	int error;

	/* The row of widths. */
	grid->columns = calloc((size_t)grid->column_count, sizeof(*grid->columns));
	if (grid->columns == NULL)
		return ENOMEM;

	/* An empty implicit grid has no track for its auto-column size to occupy. */
	if (grid->item_count == 0 && box->style.column_count == 0)
		return 0;

	/* The fixed tracks, and the least of the fr tracks. */
	fr_total = 0;
	for (column = 0; column < grid->column_count; column++) {
		track = grid_column_track(&box->style, column);
		if (track->kind == CSS_TRACK_LENGTH)
			grid->columns[column] = grid_track_length(&track->size, box->width, tree->measuring);
		if (track->kind == CSS_TRACK_FR && tree->measuring == 0) {
			fr_total += track->fr;
			grid->columns[column] = grid_track_length(&track->minimum, box->width, tree->measuring);
		}
	}

	/* The auto tracks (and the fr ones while measuring): their one-column items' widths. */
	for (index = 0; index < grid->item_count; index++) {
		item = &grid->items[index];
		if (item->column_span != 1)
			continue;
		column = item->column;
		track = grid_column_track(&box->style, column);
		if (track->kind == CSS_TRACK_LENGTH)
			continue;
		if (track->kind == CSS_TRACK_FR && tree->measuring == 0)
			continue;

		/* The item's margin box at its content's width. */
		error = grid_item_width(tree, item->box, &width);
		if (error != 0)
			return error;
		if (width > grid->columns[column])
			grid->columns[column] = width;
	}

	/* An item spanning auto tracks widens them, equally, to hold it. */
	for (index = 0; index < grid->item_count; index++) {
		item = &grid->items[index];
		if (item->column_span < 2)
			continue;

		/* The spanned width and how many auto tracks share any more. */
		spanned = gap * (layout_unit)(item->column_span - 1);
		autos = 0;
		for (column = item->column; column < item->column + item->column_span; column++) {
			spanned += grid->columns[column];
			track = grid_column_track(&box->style, column);
			if (track->kind == CSS_TRACK_AUTO)
				autos++;
		}

		/* Spanning only fixed or fr tracks widens nothing. */
		if (autos == 0)
			continue;

		/* The item's width against it. */
		error = grid_item_width(tree, item->box, &width);
		if (error != 0)
			return error;
		if (width <= spanned)
			continue;
		share = (width - spanned) / autos;
		for (column = item->column; column < item->column + item->column_span; column++) {
			track = grid_column_track(&box->style, column);
			if (track->kind == CSS_TRACK_AUTO)
				grid->columns[column] += share;
		}
	}

	/* What is left of the container's width after the tracks and the gaps. */
	available = box->width - gap * (layout_unit)(grid->column_count - 1);
	used = 0;
	for (column = 0; column < grid->column_count; column++) {
		track = grid_column_track(&box->style, column);
		if (track->kind == CSS_TRACK_FR && tree->measuring == 0)
			continue;
		used += grid->columns[column];
	}

	/* The fr tracks share it by their factors (a sum under one shares only that much of it), each at least its minimum. */
	if (fr_total > 0) {
		if (fr_total < 1.0f)
			fr_total = 1.0f;
		for (column = 0; column < grid->column_count; column++) {
			track = grid_column_track(&box->style, column);
			if (track->kind != CSS_TRACK_FR)
				continue;
			width = (layout_unit)((float)(available - used) * track->fr / fr_total);
			if (width > grid->columns[column])
				grid->columns[column] = width;
		}

		/* The fr tracks took the room. */
		return 0;
	}

	/* Without fr tracks, the auto tracks share the room left (not while the container is measured). */
	if (tree->measuring != 0 || available <= used)
		return 0;
	autos = 0;
	last_auto = -1;
	for (column = 0; column < grid->column_count; column++) {
		track = grid_column_track(&box->style, column);
		if (track->kind == CSS_TRACK_AUTO) {
			autos++;
			last_auto = column;
		}
	}

	/* Without auto tracks the room stays empty. */
	if (autos == 0)
		return 0;
	share = (available - used) / autos;
	/* Give the last auto track the subpixel remainder so the grid fills its box exactly. */
	remainder = available - used - share * autos;
	for (column = 0; column < grid->column_count; column++) {
		track = grid_column_track(&box->style, column);
		if (track->kind == CSS_TRACK_AUTO)
			grid->columns[column] += share;
	}
	grid->columns[last_auto] += remainder;

	/* Succeeded: the columns are sized. */
	return 0;
}

/* Measures an item's margin box at its content's width (or at its own width, when it has one in pixels). */
static int
grid_item_width(
	struct layout_tree *tree,
	struct layout_box *item,
	layout_unit *width)
{
	layout_unit content;
	layout_unit outside;
	int error;

	/* Its margins, borders and paddings. */
	layout_box_model(item, 0);
	outside = item->margin[CSS_LEFT] + item->border[CSS_LEFT] + item->padding[CSS_LEFT] +
	    item->padding[CSS_RIGHT] + item->border[CSS_RIGHT] + item->margin[CSS_RIGHT];

	/* A width of its own in pixels. */
	if (item->style.width.unit == CSS_UNIT_PX) {
		content = layout_from_px(item->style.width.value);
		if (item->style.box_sizing == CSS_BOX_SIZING_BORDER)
			content -= item->border[CSS_LEFT] + item->padding[CSS_LEFT] + item->padding[CSS_RIGHT] + item->border[CSS_RIGHT];
		*width = content + outside;
		return 0;
	}

	/* Otherwise its content's width without a limit. */
	error = layout_max_content(tree, item, &content);
	if (error != 0)
		return error;
	layout_box_model(item, 0);
	*width = content + outside;
	return 0;
}

/*
 * Lays an item out as a block in its area's width. An auto-width item
 * stretches by default; non-stretch self-alignment uses its max-content
 * width capped by the area. Its style is put back afterwards.
 */
static int
grid_lay_item(
	struct layout_tree *tree,
	struct layout_box *item,
	layout_unit width,
	int justify)
{
	struct grid_sizes saved;
	layout_unit content;
	layout_unit intrinsic;
	layout_unit minimum;
	layout_unit remaining;
	int left_auto;
	int right_auto;
	int side;
	int error;

	/* An item with a width of its own is laid out in the area as a block is. */
	if (item->style.width.unit != CSS_UNIT_AUTO) {
		error = layout_block(tree, item, width);
		return error;
	}
	if (item->replaced && justify != CSS_ALIGN_STRETCH) {
		error = layout_block(tree, item, width);
		return error;
	}
	intrinsic = 0;
	if (justify != CSS_ALIGN_STRETCH) {
		error = layout_max_content(tree, item, &intrinsic);
		if (error != 0)
			return error;
	}

	/* Keeps the style's sizes and margins, which the layout at the area's width overrides. */
	saved.box_sizing = item->style.box_sizing;
	saved.width = item->style.width;
	saved.min_width = item->style.min_width;
	saved.max_width = item->style.max_width;
	memcpy(saved.margin, item->style.margin, sizeof(saved.margin));
	left_auto = item->style.margin[CSS_LEFT].unit == CSS_UNIT_AUTO;
	right_auto = item->style.margin[CSS_RIGHT].unit == CSS_UNIT_AUTO;

	/* The content width that fills the area: the area less the margins (auto ones are none), borders and paddings. */
	layout_box_model(item, width);
	for (side = CSS_RIGHT; side <= CSS_LEFT; side += 2) {
		if (item->style.margin[side].unit == CSS_UNIT_AUTO) {
			item->style.margin[side].unit = CSS_UNIT_PX;
			item->style.margin[side].value = 0;
			item->style.margin[side].offset = 0;
			item->margin[side] = 0;
		}
	}

	/* What is left of the area for the content. */
	content = width - item->margin[CSS_LEFT] - item->margin[CSS_RIGHT] - item->border[CSS_LEFT] - item->padding[CSS_LEFT] -
	    item->padding[CSS_RIGHT] - item->border[CSS_RIGHT];
	if (content < 0)
		content = 0;
	if (justify != CSS_ALIGN_STRETCH && content > intrinsic)
		content = intrinsic;
	if (justify != CSS_ALIGN_STRETCH && content < intrinsic) {
		/* A line that cannot break may be wider than its grid area. */
		error = layout_min_content(tree, item, &minimum);
		if (error != 0) {
			item->style.box_sizing = saved.box_sizing;
			item->style.width = saved.width;
			item->style.min_width = saved.min_width;
			item->style.max_width = saved.max_width;
			memcpy(item->style.margin, saved.margin, sizeof(saved.margin));
			return error;
		}
		if (content < minimum)
			content = minimum;
		if (content > intrinsic)
			content = intrinsic;
	}

	/* The layout at that width. */
	item->style.box_sizing = CSS_BOX_SIZING_CONTENT;
	item->style.width.unit = CSS_UNIT_PX;
	item->style.width.value = layout_to_px(content);
	item->style.width.offset = 0;
	item->style.min_width.unit = CSS_UNIT_PX;
	item->style.min_width.value = 0;
	item->style.max_width.unit = CSS_UNIT_NONE;
	error = layout_block(tree, item, width);
	if (error == 0 && (left_auto || right_auto)) {
		/* Auto margins absorb only the area left after the item's content-sized box. */
		remaining = width - item->margin[CSS_LEFT] - item->border[CSS_LEFT] -
		    item->padding[CSS_LEFT] - item->width - item->padding[CSS_RIGHT] -
		    item->border[CSS_RIGHT] - item->margin[CSS_RIGHT];
		if (remaining < 0)
			remaining = 0;
		if (left_auto && right_auto) {
			item->margin[CSS_LEFT] = remaining / 2;
			item->margin[CSS_RIGHT] = remaining - item->margin[CSS_LEFT];
		} else if (left_auto) {
			item->margin[CSS_LEFT] = remaining;
		} else {
			item->margin[CSS_RIGHT] = remaining;
		}
	}

	/* The style is the item's own again. */
	item->style.box_sizing = saved.box_sizing;
	item->style.width = saved.width;
	item->style.min_width = saved.min_width;
	item->style.max_width = saved.max_width;
	memcpy(item->style.margin, saved.margin, sizeof(saved.margin));

	/* Reports an item that could not be laid out. */
	if (error != 0)
		return error;

	/* Succeeded: the item's width follows its self-alignment. */
	return 0;
}

/*
 * Sizes the rows: a fixed track at its length, the others as tall as
 * their one-row items' margin boxes; an item spanning rows makes the last
 * of them taller when they do not hold it.
 */
static void
grid_size_rows(
	struct layout_box *box,
	struct grid_state *grid,
	layout_unit gap)
{
	const struct css_track *track;
	struct grid_item *item;
	layout_unit available;
	layout_unit height;
	layout_unit minimum;
	layout_unit spanned;
	layout_unit used;
	float fr_total;
	size_t index;
	int row;

	/* The column of heights (none when the grid has no rows). */
	grid->rows = calloc((size_t)grid->row_count + 1U, sizeof(*grid->rows));
	if (grid->rows == NULL) {
		grid->row_count = 0;
		return;
	}

	/* A pixel height is the definite content height available to the tracks. */
	available = -1;
	if (box->style.height.unit == CSS_UNIT_PX) {
		available = layout_from_px(box->style.height.value);
		if (box->style.box_sizing == CSS_BOX_SIZING_BORDER) {
			available -= box->border[CSS_TOP] + box->padding[CSS_TOP] +
			    box->padding[CSS_BOTTOM] + box->border[CSS_BOTTOM];
		}

		/* No available content height is negative. */
		if (available < 0)
			available = 0;
	}

	/* The fixed rows, and each fractional row's minimum. */
	fr_total = 0;
	for (row = 0; row < grid->row_count; row++) {
		track = grid_row_track(&box->style, row);
		if (track->kind == CSS_TRACK_LENGTH)
			grid->rows[row] = grid_track_length(&track->size, available, available < 0);
		if (track->kind == CSS_TRACK_FR && available >= 0) {
			fr_total += track->fr;
			grid->rows[row] = grid_track_length(&track->minimum, available, 0);
		}
	}

	/* The auto rows, and fractional rows without a definite height: their one-row items. */
	for (index = 0; index < grid->item_count; index++) {
		item = &grid->items[index];
		if (item->row_span != 1 || item->row >= grid->row_count)
			continue;
		track = grid_row_track(&box->style, item->row);
		if (track->kind == CSS_TRACK_LENGTH || (track->kind == CSS_TRACK_FR && available >= 0))
			continue;

		/* The item's outer height raises its auto row. */
		height = grid_outer_height(item->box);
		if (height > grid->rows[item->row])
			grid->rows[item->row] = height;
	}

	/* The items spanning rows. */
	for (index = 0; index < grid->item_count; index++) {
		item = &grid->items[index];
		if (item->row_span < 2)
			continue;
		spanned = gap * (layout_unit)(item->row_span - 1);
		for (row = item->row; row < item->row + item->row_span && row < grid->row_count; row++)
			spanned += grid->rows[row];
		height = grid_outer_height(item->box);
		row = item->row + item->row_span - 1;
		if (height > spanned && row < grid->row_count)
			grid->rows[row] += height - spanned;
	}

	/* Fractional rows share the definite room left after fixed and auto rows and the gaps. */
	if (fr_total > 0) {
		used = gap * (layout_unit)(grid->row_count - 1);
		for (row = 0; row < grid->row_count; row++) {
			track = grid_row_track(&box->style, row);
			if (track->kind == CSS_TRACK_FR)
				continue;
			used += grid->rows[row];
		}

		/* A sum below one leaves the corresponding share unused. */
		if (fr_total < 1.0f)
			fr_total = 1.0f;
		for (row = 0; row < grid->row_count; row++) {
			track = grid_row_track(&box->style, row);
			if (track->kind != CSS_TRACK_FR)
				continue;
			minimum = (layout_unit)((float)(available - used) * track->fr / fr_total);
			if (minimum > grid->rows[row])
				grid->rows[row] = minimum;
		}
	}
}

/*
 * Lays a grid item out again at the area's definite cross size when it is
 * stretched, or when its percentage height can use the area.  Placement
 * used to change only item->height, leaving its percentage descendants at
 * the intrinsic height from the first pass.
 */
static int
grid_relay_item(
	struct layout_tree *tree,
	struct layout_box *item,
	layout_unit width,
	layout_unit height,
	int align)
{
	struct grid_sizes saved;
	layout_unit frame;
	layout_unit content;
	int error;

	/* The target content height, from the percentage or the area's stretch. */
	frame = item->border[CSS_TOP] + item->padding[CSS_TOP] +
	    item->padding[CSS_BOTTOM] + item->border[CSS_BOTTOM];
	if (item->style.height.unit == CSS_UNIT_PERCENT) {
		content = (layout_unit)((float)height * item->style.height.value / 100.0f) +
		    layout_from_px(item->style.height.offset);
		if (item->style.box_sizing == CSS_BOX_SIZING_BORDER)
			content -= frame;
	} else if (item->style.height.unit == CSS_UNIT_AUTO && align == CSS_ALIGN_STRETCH) {
		content = height - item->margin[CSS_TOP] - item->margin[CSS_BOTTOM] - frame;
	} else {
		return 0;
	}

	/* No content size is negative. */
	if (content < 0)
		content = 0;

	/* Keep the item's style while the definite content box is laid out. */
	saved.box_sizing = item->style.box_sizing;
	saved.width = item->style.width;
	saved.min_width = item->style.min_width;
	saved.max_width = item->style.max_width;
	saved.height = item->style.height;
	saved.min_height = item->style.min_height;
	saved.max_height = item->style.max_height;
	memcpy(saved.margin, item->style.margin, sizeof(saved.margin));
	item->style.box_sizing = CSS_BOX_SIZING_CONTENT;
	item->style.width.unit = CSS_UNIT_PX;
	item->style.width.value = layout_to_px(item->width);
	item->style.width.offset = 0;
	item->style.min_width.unit = CSS_UNIT_PX;
	item->style.min_width.value = 0;
	item->style.max_width.unit = CSS_UNIT_NONE;
	item->style.height.unit = CSS_UNIT_PX;
	item->style.height.value = layout_to_px(content);
	item->style.height.offset = 0;
	item->style.min_height.unit = CSS_UNIT_PX;
	item->style.min_height.value = 0;
	item->style.max_height.unit = CSS_UNIT_NONE;
	item->style.margin[CSS_LEFT].unit = CSS_UNIT_PX;
	item->style.margin[CSS_LEFT].value = layout_to_px(item->margin[CSS_LEFT]);
	item->style.margin[CSS_LEFT].offset = 0;
	item->style.margin[CSS_RIGHT].unit = CSS_UNIT_PX;
	item->style.margin[CSS_RIGHT].value = layout_to_px(item->margin[CSS_RIGHT]);
	item->style.margin[CSS_RIGHT].offset = 0;
	error = layout_block(tree, item, width);
	item->style.box_sizing = saved.box_sizing;
	item->style.width = saved.width;
	item->style.min_width = saved.min_width;
	item->style.max_width = saved.max_width;
	item->style.height = saved.height;
	item->style.min_height = saved.min_height;
	item->style.max_height = saved.max_height;
	memcpy(item->style.margin, saved.margin, sizeof(saved.margin));
	return error;
}

/* Measures an item's margin box's height. */
static layout_unit
grid_outer_height(
	const struct layout_box *item)
{
	/* The content, paddings, borders and margins. */
	return item->margin[CSS_TOP] + item->border[CSS_TOP] + item->padding[CSS_TOP] + item->height +
	    item->padding[CSS_BOTTOM] + item->border[CSS_BOTTOM] + item->margin[CSS_BOTTOM];
}

/* Finds a fixed-row Grid item's first line or synthesized lower-border baseline. */
static int
grid_fixed_row_baseline(
	const struct layout_box *box,
	const struct grid_item *item,
	layout_unit *baseline)
{
	const struct layout_box *child;
	int align;

	child = item->box;
	/* The current group shares a horizontal block axis. */
	if (box->style.writing_mode != CSS_WRITING_HORIZONTAL_TB)
		return 0;
	if (child->style.writing_mode != CSS_WRITING_HORIZONTAL_TB)
		return 0;

	/* The row's fixed pixel size leaves baseline alignment out of track sizing. */
	if (item->row_span != 1 || item->row >= box->style.row_count)
		return 0;
	if (box->style.rows[item->row].kind != CSS_TRACK_LENGTH)
		return 0;
	if (box->style.rows[item->row].size.unit != CSS_UNIT_PX)
		return 0;
	if (child->style.margin[CSS_TOP].unit == CSS_UNIT_AUTO ||
	    child->style.margin[CSS_BOTTOM].unit == CSS_UNIT_AUTO)
		return 0;

	/* Only members with a baseline preference join the row's group. */
	align = child->style.align_self;
	if (align == CSS_ALIGN_AUTO)
		align = box->style.align_items;
	if (align != CSS_ALIGN_BASELINE)
		return 0;

	/* An inline content block exports its first formatted line. */
	if (child->children_inline && child->line_count > 0) {
		*baseline = child->margin[CSS_TOP] + child->border[CSS_TOP] +
		    child->padding[CSS_TOP] + child->lines[0].y + child->lines[0].baseline;
		return 1;
	}

	/* An empty definite-height block synthesizes at its lower border edge. */
	if (child->first_child != NULL || child->style.height.unit != CSS_UNIT_PX)
		return 0;
	*baseline = grid_outer_height(child) - child->margin[CSS_BOTTOM];

	/* This child has a baseline in the fixed row. */
	return 1;
}

/* Finds an empty orthogonal item's extent right of its border-left baseline. */
static int
grid_fixed_column_baseline(
	const struct layout_box *box,
	const struct grid_item *item,
	layout_unit *right_extent)
{
	const struct layout_box *child;
	int justify;

	child = item->box;
	if (box->style.writing_mode != CSS_WRITING_HORIZONTAL_TB ||
	    child->style.writing_mode != CSS_WRITING_VERTICAL_RL)
		return 0;
	if (box->style.column_count != 1 || item->column != 0 || item->column_span != 1 ||
	    box->style.columns[0].kind != CSS_TRACK_LENGTH ||
	    box->style.columns[0].size.unit != CSS_UNIT_PX)
		return 0;
	if (child->first_child != NULL || child->line_count != 0 ||
	    child->style.width.unit != CSS_UNIT_PX ||
	    child->style.margin[CSS_LEFT].unit == CSS_UNIT_AUTO ||
	    child->style.margin[CSS_RIGHT].unit == CSS_UNIT_AUTO)
		return 0;

	justify = child->style.justify_self;
	if (justify == CSS_ALIGN_AUTO)
		justify = box->style.justify_items;
	if (justify != CSS_ALIGN_BASELINE)
		return 0;

	*right_extent = child->border[CSS_LEFT] + child->padding[CSS_LEFT] +
	    child->width + child->padding[CSS_RIGHT] + child->border[CSS_RIGHT] +
	    child->margin[CSS_RIGHT];
	return 1;
}

/* Resolves a track's nonnegative length: pixels or a percentage of the definite whole. */
static layout_unit
grid_track_length(
	const struct css_length *length,
	layout_unit whole,
	int measuring)
{
	layout_unit resolved;

	/* Pixels. */
	if (length->unit == CSS_UNIT_PX) {
		if (length->value < 0)
			return 0;
		return layout_from_px(length->value);
	}

	/* A percentage of a definite whole. */
	if (length->unit == CSS_UNIT_PERCENT && measuring == 0) {
		resolved = (layout_unit)((float)whole * length->value / 100.0f) + layout_from_px(length->offset);
		if (resolved < 0)
			return 0;
		return resolved;
	}

	/* Nothing definite. */
	return 0;
}

/* Frees what a layout's state holds. */
static void
grid_release(
	struct grid_state *grid)
{
	/* The arrays. */
	free(grid->items);
	free(grid->columns);
	free(grid->rows);
	free(grid->taken);
}
