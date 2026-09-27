/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display list: a laid out page walked in painting order into
 * rectangles and runs of glyphs, and its text dump for the tests.
 *
 * The walk is CSS 2's painting order simplified: the canvas, then each
 * block's background and borders before its content, a block's children
 * in document order, and a block's lines after its own background.  The
 * positioned boxes are painted apart, each with its descendants that are
 * not positioned, in the order layout_stacking_order gives: those with a
 * negative z-index before the normal flow, the others after it.  A box
 * that clips its overflow puts its content between a clip to its padding
 * box and the clip's end; a positioned box is clipped by the clipping
 * boxes around it that it does not escape (an absolute box escapes those
 * outside its containing block, a fixed one all of them).
 */

#include "paint/paint.h"

#include <errno.h>
#include <math.h>
#include <string.h>

/* The canvas color when neither the root nor the body has a background: white. */
#define LIST_CANVAS_DEFAULT	0xffffffffU

/* How many spaces a tab stands for in preserved whitespace (as in the layout). */
#define LIST_TAB_SPACES		8

/* The visibility value that hides a box's own painting. */
#define LIST_VISIBILITY_HIDDEN	1

/*
 * What a walk of the box tree carries: the list being filled, the text
 * system that measures the glyphs, the box whose background went to the
 * canvas (and is not painted again), and the first error.
 */
struct list_walk {
	struct paint_list *list;
	struct text_system *text;
	const struct layout_box *canvas_box;
	int error;
};

static const struct layout_box *list_canvas(const struct layout_tree *tree, uint32_t *color);
static void list_box(struct list_walk *walk, const struct layout_box *box, const struct layout_box *layer, int depth);
static void list_borders(struct list_walk *walk, const struct layout_box *box);
static void list_inline_floats(struct list_walk *walk, const struct layout_box *box, const struct layout_box *layer, int depth);
static void list_lines(struct list_walk *walk, const struct layout_box *box);
static void list_fragment(struct list_walk *walk, const struct layout_fragment *fragment, layout_unit x, layout_unit baseline);
static void list_rect(struct list_walk *walk, layout_unit x, layout_unit y, layout_unit width, layout_unit height, uint32_t color);
static void list_clip(struct list_walk *walk, const struct layout_box *box);
static void list_unclip(struct list_walk *walk);
static int list_layer_clips(struct list_walk *walk, const struct layout_box *layer, int push);
static void list_color(struct wb_buffer *out, uint32_t color);

/*
 * Builds the display list of a laid out page.
 */
int
paint_build(
	struct paint_list *list,
	const struct layout_tree *tree)
{
	struct list_walk walk;
	struct wb_vector order;
	const struct layout_box *layer;
	size_t flow_index;
	size_t index;
	int clips;
	int error;

	/* Starts an empty list the size of the document, at least the viewport. */
	memset(list, 0, sizeof(*list));
	wb_arena_init(&list->arena, 0);
	wb_vector_init(&list->items, sizeof(struct paint_item));
	list->width = tree->viewport_width;
	list->height = tree->document_height;
	if (list->height < tree->viewport_height)
		list->height = tree->viewport_height;

	/* The canvas takes the root's background, or the body's. */
	memset(&walk, 0, sizeof(walk));
	walk.list = list;
	walk.text = tree->text;
	walk.canvas_box = list_canvas(tree, &list->canvas_color);

	/* An empty document paints only the canvas. */
	if (tree->root == NULL)
		return 0;

	/* The positioned boxes in painting order, and where the normal flow goes among them. */
	wb_vector_init(&order, sizeof(const struct layout_box *));
	error = layout_stacking_order(tree, &order, &flow_index);
	if (error != 0) {
		wb_vector_release(&order);
		paint_release(list);
		return error;
	}

	/* The positioned boxes below the flow, the flow from the root, then the ones above it. */
	for (index = 0; index < order.count; index++) {
		if (index == flow_index)
			list_box(&walk, tree->root, NULL, 0);
		layer = *(const struct layout_box **)wb_vector_at(&order, index);
		clips = list_layer_clips(&walk, layer, 1);
		list_box(&walk, layer, layer, 0);
		while (clips > 0) {
			list_unclip(&walk);
			clips--;
		}
	}

	/* The flow is last when no box is above it. */
	if (flow_index == order.count)
		list_box(&walk, tree->root, NULL, 0);
	wb_vector_release(&order);
	if (walk.error != 0) {
		paint_release(list);
		return walk.error;
	}

	/* Succeeded: the list holds the page's painting. */
	return 0;
}

/*
 * Frees a display list.
 */
void
paint_release(
	struct paint_list *list)
{
	/* The items and the glyphs they point at. */
	wb_vector_release(&list->items);
	wb_arena_release(&list->arena);
	memset(list, 0, sizeof(*list));
}

/*
 * Writes a display list as text, one item a line, in pixels.
 */
int
paint_dump(
	const struct paint_list *list,
	struct wb_buffer *out)
{
	const struct paint_item *item;
	size_t index;
	size_t glyph;
	int error;

	/* The canvas: its size and color. */
	wb_buffer_printf(out, "canvas %.2f x %.2f ", (double)layout_to_px(list->width), (double)layout_to_px(list->height));
	list_color(out, list->canvas_color);
	wb_buffer_append_string(out, "\n");

	/* Each item in painting order. */
	for (index = 0; index < list->items.count; index++) {
		item = wb_vector_at(&list->items, index);

		/* A clip: the rectangle the items until its end are drawn inside. */
		if (item->kind == PAINT_CLIP) {
			wb_buffer_printf(out, "clip %.2f %.2f %.2f %.2f\n", (double)layout_to_px(item->x), (double)layout_to_px(item->y),
			    (double)layout_to_px(item->width), (double)layout_to_px(item->height));
			continue;
		}

		/* The end of the last clip. */
		if (item->kind == PAINT_UNCLIP) {
			wb_buffer_append_string(out, "unclip\n");
			continue;
		}

		/* A rectangle: its position, size and color. */
		if (item->kind == PAINT_RECT) {
			wb_buffer_printf(out, "rect %.2f %.2f %.2f %.2f ", (double)layout_to_px(item->x), (double)layout_to_px(item->y),
			    (double)layout_to_px(item->width), (double)layout_to_px(item->height));
			list_color(out, item->color);
			wb_buffer_append_string(out, "\n");
			continue;
		}

		/* A text run: its origin, font, color and characters. */
		wb_buffer_printf(out, "text %.2f %.2f %upx face %d bold %d ", (double)layout_to_px(item->x),
		    (double)layout_to_px(item->y), item->font.pixels, item->font.face, item->font.bold);
		list_color(out, item->color);
		wb_buffer_append_string(out, " \"");
		for (glyph = 0; glyph < item->glyph_count; glyph++)
			wb_buffer_append_utf8(out, item->glyphs[glyph].code_point);
		wb_buffer_append_string(out, "\"\n");
	}

	/* Reports a buffer that could not grow. */
	error = wb_buffer_reserve(out, 1);
	if (error != 0)
		return error;

	/* Succeeded: the dump is written. */
	return 0;
}

/*
 * Starts a clip stack for a target of a size: only the whole target.
 */
void
paint_clips_init(
	struct paint_clips *clips,
	int width,
	int height)
{
	struct paint_clip *whole;

	/* The target's rectangle is the first clip. */
	memset(clips, 0, sizeof(*clips));
	whole = &clips->stack[0];
	whole->right = (float)width;
	whole->bottom = (float)height;
	whole->pixel_right = width;
	whole->pixel_bottom = height;
}

/*
 * Starts a clip (a PAINT_CLIP item, the document scrolled up by
 * scroll_y): the top of the stack becomes its intersection with the clip
 * around it.
 */
void
paint_clips_push(
	struct paint_clips *clips,
	const struct paint_item *item,
	layout_unit scroll_y)
{
	const struct paint_clip *outer;
	struct paint_clip *inner;
	float value;

	/* A clip past the stack's depth is only counted. */
	if (clips->depth >= PAINT_CLIP_DEPTH) {
		clips->ignored++;
		return;
	}

	/* The new clip starts as the one around it. */
	outer = &clips->stack[clips->depth];
	clips->depth++;
	inner = &clips->stack[clips->depth];
	*inner = *outer;

	/* Each edge moves in to the item's edge when that is further in. */
	value = layout_to_px(item->x);
	if (value > inner->left)
		inner->left = value;
	value = layout_to_px(item->y - scroll_y);
	if (value > inner->top)
		inner->top = value;
	value = layout_to_px(item->x + item->width);
	if (value < inner->right)
		inner->right = value;
	value = layout_to_px(item->y - scroll_y + item->height);
	if (value < inner->bottom)
		inner->bottom = value;

	/* The same edges on whole pixels (rounded to the nearest), for glyphs. */
	inner->pixel_left = (int)floorf(inner->left + 0.5f);
	inner->pixel_top = (int)floorf(inner->top + 0.5f);
	inner->pixel_right = (int)floorf(inner->right + 0.5f);
	inner->pixel_bottom = (int)floorf(inner->bottom + 0.5f);
}

/*
 * Ends the last clip started (a PAINT_UNCLIP item).
 */
void
paint_clips_pop(
	struct paint_clips *clips)
{
	/* A clip past the depth was only counted. */
	if (clips->ignored > 0) {
		clips->ignored--;
		return;
	}

	/* The clip around it is the top again. */
	if (clips->depth > 0)
		clips->depth--;
}

/*
 * Reports the clip the items are drawn inside now.
 */
const struct paint_clip *
paint_clips_top(
	const struct paint_clips *clips)
{
	/* The top of the stack. */
	return &clips->stack[clips->depth];
}

/*
 * Finds the canvas color: the root's background, or when the root has
 * none, the body's.  Reports the box whose background it took, so the walk
 * does not paint that background again, or NULL.
 */
static const struct layout_box *
list_canvas(
	const struct layout_tree *tree,
	uint32_t *color)
{
	const struct layout_box *body;
	const struct dom_element *element;
	int is_body;

	/* White unless a background is found. */
	*color = LIST_CANVAS_DEFAULT;
	if (tree->root == NULL)
		return NULL;

	/* The root's own background wins. */
	if ((tree->root->style.background_color >> 24) != 0) {
		*color = tree->root->style.background_color;
		return tree->root;
	}

	/* Otherwise the body's, when the root's first block is the body. */
	body = tree->root->first_child;
	while (body != NULL && body->node == NULL)
		body = body->next;
	if (body == NULL || body->node->type != DOM_ELEMENT)
		return NULL;

	/* Only the body element gives the canvas its background. */
	element = (const struct dom_element *)body->node;
	is_body = vm_string_equal_ascii(element->local_name, "body");
	if (!is_body)
		return NULL;

	/* A body without a background leaves the canvas white. */
	if ((body->style.background_color >> 24) == 0)
		return NULL;

	/* The body's background is the canvas's. */
	*color = body->style.background_color;
	return body;
}

/* Adds a box's painting and its descendants' to the list; positioned boxes other than the layer being painted wait for their turn. */
static void
list_box(
	struct list_walk *walk,
	const struct layout_box *box,
	const struct layout_box *layer,
	int depth)
{
	const struct layout_box *child;
	layout_unit width;
	layout_unit height;
	int positioned;
	int visible;
	int clips;

	/* Stops at the depth the layout stops at, or after an error. */
	if (depth > LAYOUT_DEPTH_MAX || walk->error != 0)
		return;

	/* Only blocks paint a box of their own; inline content is painted through their lines. */
	if (box->kind != LAYOUT_BLOCK && box->kind != LAYOUT_ANONYMOUS_BLOCK)
		return;

	/* A positioned box (not the root) is painted in its own turn. */
	positioned = layout_is_positioned(box);
	if (positioned && box != layer && box->parent != NULL)
		return;

	/* A hidden box paints nothing of its own, but its children may be visible. */
	visible = 1;
	if (box->style.visibility == LIST_VISIBILITY_HIDDEN)
		visible = 0;

	/* The background fills the border box, unless it went to the canvas. */
	width = box->border[CSS_LEFT] + box->padding[CSS_LEFT] + box->width + box->padding[CSS_RIGHT] + box->border[CSS_RIGHT];
	height = box->border[CSS_TOP] + box->padding[CSS_TOP] + box->height + box->padding[CSS_BOTTOM] + box->border[CSS_BOTTOM];
	if (visible && box != walk->canvas_box && (box->style.background_color >> 24) != 0)
		list_rect(walk, box->x, box->y, width, height, box->style.background_color);

	/* The borders go over the background. */
	if (visible)
		list_borders(walk, box);

	/* A box that clips its overflow clips its content to its padding box. */
	clips = layout_clips(box);
	if (clips)
		list_clip(walk, box);

	/* A block of lines paints the floats among its content, then its text. */
	if (box->children_inline) {
		list_inline_floats(walk, box, layer, depth + 1);
		list_lines(walk, box);
	} else {
		/* A block of blocks paints its children in order, the floats after the others. */
		for (child = box->first_child; child != NULL; child = child->next) {
			if (child->floating == CSS_FLOAT_NONE)
				list_box(walk, child, layer, depth + 1);
		}

		/* Then the floats, over the backgrounds of the blocks beside them. */
		for (child = box->first_child; child != NULL; child = child->next) {
			if (child->floating != CSS_FLOAT_NONE)
				list_box(walk, child, layer, depth + 1);
		}
	}

	/* The clip ends with the content. */
	if (clips)
		list_unclip(walk);
}

/* Paints the floats among a block's inline content (inline boxes are searched through). */
static void
list_inline_floats(
	struct list_walk *walk,
	const struct layout_box *box,
	const struct layout_box *layer,
	int depth)
{
	const struct layout_box *child;

	/* Stops at the depth the layout stops at. */
	if (depth > LAYOUT_DEPTH_MAX)
		return;

	/* A float paints itself; an inline box is searched; a box out of the flow is painted in its own turn. */
	for (child = box->first_child; child != NULL; child = child->next) {
		if (child->out_of_flow)
			continue;
		if (child->floating != CSS_FLOAT_NONE) {
			list_box(walk, child, layer, depth + 1);
			continue;
		}

		/* An inline box's content. */
		list_inline_floats(walk, child, layer, depth + 1);
	}
}

/*
 * Adds a box's borders as four rectangles: the top and bottom across the
 * whole border box, the left and right between them.
 */
static void
list_borders(
	struct list_walk *walk,
	const struct layout_box *box)
{
	layout_unit width;
	layout_unit height;
	layout_unit inner;

	/* The border box, and the height between the top and bottom borders. */
	width = box->border[CSS_LEFT] + box->padding[CSS_LEFT] + box->width + box->padding[CSS_RIGHT] + box->border[CSS_RIGHT];
	height = box->border[CSS_TOP] + box->padding[CSS_TOP] + box->height + box->padding[CSS_BOTTOM] + box->border[CSS_BOTTOM];
	inner = height - box->border[CSS_TOP] - box->border[CSS_BOTTOM];

	/* The top border. */
	if (box->border[CSS_TOP] > 0)
		list_rect(walk, box->x, box->y, width, box->border[CSS_TOP], box->style.border_color[CSS_TOP]);

	/* The bottom border. */
	if (box->border[CSS_BOTTOM] > 0) {
		list_rect(
			walk,
			box->x,
			box->y + height - box->border[CSS_BOTTOM],
			width,
			box->border[CSS_BOTTOM],
			box->style.border_color[CSS_BOTTOM]);
	}

	/* The left border. */
	if (box->border[CSS_LEFT] > 0 && inner > 0)
		list_rect(walk, box->x, box->y + box->border[CSS_TOP], box->border[CSS_LEFT], inner, box->style.border_color[CSS_LEFT]);

	/* The right border. */
	if (box->border[CSS_RIGHT] > 0 && inner > 0) {
		list_rect(
			walk,
			box->x + width - box->border[CSS_RIGHT],
			box->y + box->border[CSS_TOP],
			box->border[CSS_RIGHT],
			inner,
			box->style.border_color[CSS_RIGHT]);
	}
}

/* Adds the text of a block's lines. */
static void
list_lines(
	struct list_walk *walk,
	const struct layout_box *box)
{
	const struct layout_line *line;
	const struct layout_fragment *fragment;
	layout_unit left;
	layout_unit top;
	size_t index;
	size_t item;

	/* The content box's origin, which the lines are placed from. */
	left = box->x + box->border[CSS_LEFT] + box->padding[CSS_LEFT];
	top = box->y + box->border[CSS_TOP] + box->padding[CSS_TOP];

	/* Each fragment of each line, on the line's baseline. */
	for (index = 0; index < box->line_count; index++) {
		line = &box->lines[index];
		for (item = 0; item < line->fragment_count; item++) {
			fragment = &line->fragments[item];
			list_fragment(walk, fragment, left + line->left + fragment->x, top + line->y + line->baseline);
		}
	}
}

/* Adds a fragment's glyphs as a text item, and its underline. */
static void
list_fragment(
	struct list_walk *walk,
	const struct layout_fragment *fragment,
	layout_unit x,
	layout_unit baseline)
{
	struct paint_item item;
	struct paint_glyph *glyphs;
	struct text_glyph glyph;
	uint32_t code_point;
	layout_unit pen;
	layout_unit offset;
	layout_unit thickness;
	size_t count;
	size_t used;
	size_t position;
	int error;

	/* A fragment of a hidden box paints nothing. */
	if (fragment->box->style.visibility == LIST_VISIBILITY_HIDDEN)
		return;

	/* There are at most as many glyphs as UTF-16 units. */
	glyphs = NULL;
	if (fragment->length != 0) {
		glyphs = wb_arena_alloc(&walk->list->arena, fragment->length * sizeof(struct paint_glyph));
		if (glyphs == NULL) {
			walk->error = ENOMEM;
			return;
		}
	}

	/* Sets each character's glyph at the pen, which moves by the advances the layout measured. */
	count = 0;
	pen = 0;
	position = 0;
	while (position < fragment->length) {
		used = wb_utf16_decode(fragment->text + position, fragment->length - position, &code_point);
		position += used;

		/* A preserved tab is as wide as several spaces and draws nothing. */
		if (code_point == 0x09U) {
			error = text_glyph(walk->text, &fragment->font, 0x20U, 0, &glyph);
			if (error != 0) {
				walk->error = error;
				return;
			}

			/* Moves the pen past the tab. */
			pen += (layout_unit)glyph.advance_units * LIST_TAB_SPACES;
			continue;
		}

		/* Measures the glyph. */
		error = text_glyph(walk->text, &fragment->font, code_point, 0, &glyph);
		if (error != 0) {
			walk->error = error;
			return;
		}

		/* Places it and moves the pen on. */
		glyphs[count].code_point = code_point;
		glyphs[count].x = pen;
		count++;
		pen += (layout_unit)glyph.advance_units;
	}

	/* The text item. */
	memset(&item, 0, sizeof(item));
	item.kind = PAINT_TEXT;
	item.x = x;
	item.y = baseline;
	item.width = fragment->width;
	item.color = fragment->color;
	item.font = fragment->font;
	item.glyphs = glyphs;
	item.glyph_count = count;
	error = wb_vector_push(&walk->list->items, &item);
	if (error != 0) {
		walk->error = error;
		return;
	}

	/* An underline runs under the fragment, a tenth of the size below the baseline. */
	if (fragment->underline) {
		offset = layout_from_px(fragment->font.size / 10.0f + 0.5f);
		offset = (offset / LAYOUT_UNIT) * LAYOUT_UNIT;
		if (offset < LAYOUT_UNIT)
			offset = LAYOUT_UNIT;
		thickness = layout_from_px(fragment->font.size / 16.0f + 0.5f);
		thickness = (thickness / LAYOUT_UNIT) * LAYOUT_UNIT;
		if (thickness < LAYOUT_UNIT)
			thickness = LAYOUT_UNIT;
		list_rect(walk, x, baseline + offset, fragment->width, thickness, fragment->color);
	}
}

/* Adds a filled rectangle. */
static void
list_rect(
	struct list_walk *walk,
	layout_unit x,
	layout_unit y,
	layout_unit width,
	layout_unit height,
	uint32_t color)
{
	struct paint_item item;
	int error;

	/* Nothing to add after an error, for an empty rectangle, or for a transparent color. */
	if (walk->error != 0)
		return;
	if (width <= 0 || height <= 0)
		return;
	if ((color >> 24) == 0)
		return;

	/* The item. */
	memset(&item, 0, sizeof(item));
	item.kind = PAINT_RECT;
	item.x = x;
	item.y = y;
	item.width = width;
	item.height = height;
	item.color = color;
	error = wb_vector_push(&walk->list->items, &item);
	if (error != 0)
		walk->error = error;
}

/* Writes a color as #AARRGGBB. */
static void
list_color(
	struct wb_buffer *out,
	uint32_t color)
{
	/* Eight hexadecimal digits, alpha first. */
	wb_buffer_printf(out, "#%08x", (unsigned)color);
}

/* Adds the start of a clip to a box's padding box. */
static void
list_clip(
	struct list_walk *walk,
	const struct layout_box *box)
{
	struct paint_item item;
	int error;

	/* Nothing to add after an error. */
	if (walk->error != 0)
		return;

	/* The padding box: inside the borders. */
	memset(&item, 0, sizeof(item));
	item.kind = PAINT_CLIP;
	item.x = box->x + box->border[CSS_LEFT];
	item.y = box->y + box->border[CSS_TOP];
	item.width = box->padding[CSS_LEFT] + box->width + box->padding[CSS_RIGHT];
	item.height = box->padding[CSS_TOP] + box->height + box->padding[CSS_BOTTOM];
	error = wb_vector_push(&walk->list->items, &item);
	if (error != 0)
		walk->error = error;
}

/* Adds the end of the last clip. */
static void
list_unclip(
	struct list_walk *walk)
{
	struct paint_item item;
	int error;

	/* Nothing to add after an error. */
	if (walk->error != 0)
		return;

	/* The item. */
	memset(&item, 0, sizeof(item));
	item.kind = PAINT_UNCLIP;
	error = wb_vector_push(&walk->list->items, &item);
	if (error != 0)
		walk->error = error;
}

/*
 * Adds the clips of the clipping boxes around a positioned box that it
 * does not escape (an absolute box escapes those inside its containing
 * block), the outermost first, and reports how many were added (push zero
 * only counts them).
 */
static int
list_layer_clips(
	struct list_walk *walk,
	const struct layout_box *layer,
	int push)
{
	const struct layout_box *around[PAINT_CLIP_DEPTH];
	const struct layout_box *walk_up;
	int count;
	int reached;
	int clips;
	int positioned;

	/* A fixed box escapes every clip; an absolute one those between it and its containing block. */
	count = 0;
	if (layer->style.position == CSS_POSITION_FIXED)
		return 0;
	reached = 1;
	if (layer->style.position == CSS_POSITION_ABSOLUTE)
		reached = 0;

	/* The clipping ancestors, nearest first, from the containing block up for an absolute box. */
	for (walk_up = layer->parent; walk_up != NULL && count < PAINT_CLIP_DEPTH; walk_up = walk_up->parent) {
		clips = layout_clips(walk_up);
		positioned = layout_is_positioned(walk_up);

		/* The containing block (the nearest positioned ancestor) and the boxes around it clip an absolute box. */
		if (positioned)
			reached = 1;
		if (clips && reached)
			around[count++] = walk_up;
	}

	/* The clips, the outermost first. */
	for (clips = count; push && clips > 0; clips--)
		list_clip(walk, around[clips - 1]);

	/* Reports how many there are. */
	return count;
}
