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
 * outside its containing block, a fixed one all of them).  A replaced box
 * paints its background and borders, then its image over its content box:
 * a block one as a block does, an inline one where its fragment is.  An
 * inline block is painted as a block in its place among its line's text.
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

/* The most tiles one background image is painted in (a tiny tile over a huge page stops there). */
#define LIST_TILES_MAX		16384U

/*
 * The colors of the form controls' own look, as Chromium draws them
 * (0xAARRGGBB): a checkbox's frame and inside, a checked one's fill, and a
 * field's placeholder.
 */
#define LIST_FRAME_COLOR	0xff767676U
#define LIST_FIELD_COLOR	0xffffffffU
#define LIST_CHECKED_COLOR	0xff0075ffU
#define LIST_PLACEHOLDER_COLOR	0xff757575U

/* The character a password field shows for each of its characters. */
#define LIST_PASSWORD_BULLET	0x2022U

/* A select's arrow: how far its left is from the content box's right, its rows and its widest row, in pixels. */
#define LIST_ARROW_RIGHT	14
#define LIST_ARROW_ROWS		5
#define LIST_ARROW_WIDTH	9

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

/*
 * A rectangle of the page in layout units: a background's positioning
 * area or its painting area.
 */
struct list_area {
	layout_unit x;
	layout_unit y;
	layout_unit width;
	layout_unit height;
};

static const struct layout_box *list_canvas(const struct layout_tree *tree, uint32_t *color);
static void list_box(struct list_walk *walk, const struct layout_box *box, const struct layout_box *layer, int depth);
static void list_borders(struct list_walk *walk, const struct layout_box *box);
static void list_inline_floats(struct list_walk *walk, const struct layout_box *box, const struct layout_box *layer, int depth);
static void list_lines(struct list_walk *walk, const struct layout_box *box, const struct layout_box *layer, int depth);
static void list_fragment(struct list_walk *walk, const struct layout_fragment *fragment, layout_unit x, layout_unit baseline);
static void list_rect(struct list_walk *walk, layout_unit x, layout_unit y, layout_unit width, layout_unit height, uint32_t color);
static void list_replaced(struct list_walk *walk, const struct layout_box *box, layout_unit x, layout_unit y);
static void list_image(struct list_walk *walk, const struct layout_box *box, layout_unit x, layout_unit y);
static void list_control(struct list_walk *walk, const struct layout_box *box);
static int list_control_native(const struct layout_box *box);
static void list_check(struct list_walk *walk, const struct layout_box *box, layout_unit width, layout_unit height);
static void list_control_text(struct list_walk *walk, const struct layout_box *box);
static int list_control_shown(const struct layout_box *box, struct dom_element *element, const struct dom_control *control, struct wb_units *shown, uint32_t *color);
static int list_caret_offset(struct list_walk *walk, const struct text_font *font, const struct layout_box *box, struct dom_element *element, struct dom_control *control, layout_unit *offset);
static void list_textarea(struct list_walk *walk, const struct layout_box *box, struct dom_element *element, const struct list_area *content, const struct text_font *font, layout_unit line, layout_unit ascent);
static void list_select(struct list_walk *walk, const struct layout_box *box, struct dom_element *element, const struct list_area *content, const struct text_font *font, layout_unit line, layout_unit ascent);
static void list_control_record(struct list_walk *walk, const struct layout_box *box, struct dom_control *control, const struct list_area *content, const struct text_font *font, layout_unit caret_x, layout_unit line_top, layout_unit ascent);
static void list_style_font(struct list_walk *walk, const struct css_style *style, struct text_font *font);
static void list_text_run(struct list_walk *walk, const struct text_font *font, uint32_t color, const uint16_t *units, size_t length, layout_unit x, layout_unit baseline);
static void list_image_item(struct list_walk *walk, const struct img_bitmap *image, layout_unit x, layout_unit y, layout_unit width, layout_unit height);
static void list_box_background(struct list_walk *walk, const struct layout_box *box);
static void list_canvas_background(struct list_walk *walk, const struct layout_tree *tree);
static void list_background_image(struct list_walk *walk, const struct layout_box *box, const struct list_area *area, const struct list_area *painting);
static void list_background_size(const struct layout_box *box, const struct img_bitmap *image, const struct list_area *area, layout_unit *width, layout_unit *height);
static layout_unit list_background_length(const struct css_length *length, layout_unit whole);
static layout_unit list_background_offset(const struct css_length *position, layout_unit room);
static void list_clip_rect(struct list_walk *walk, const struct list_area *area);
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

	/* The canvas's background image, under everything. */
	list_canvas_background(&walk, tree);

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
 * Adds a ring around a rectangle on top of everything painted so far: four
 * filled rectangles of a thickness just outside it (the focus ring of the
 * focused element, ws074-p056).
 */
int
paint_add_ring(
	struct paint_list *list,
	layout_unit x,
	layout_unit y,
	layout_unit width,
	layout_unit height,
	layout_unit thickness,
	uint32_t color)
{
	struct paint_item edges[4];
	size_t index;
	int error;

	/* The four edges, all of one color. */
	memset(edges, 0, sizeof(edges));
	for (index = 0; index < 4U; index++) {
		edges[index].kind = PAINT_RECT;
		edges[index].color = color;
	}

	/* The top edge, across the corners. */
	edges[0].x = x - thickness;
	edges[0].y = y - thickness;
	edges[0].width = width + 2 * thickness;
	edges[0].height = thickness;

	/* The bottom edge, across the corners. */
	edges[1].x = x - thickness;
	edges[1].y = y + height;
	edges[1].width = width + 2 * thickness;
	edges[1].height = thickness;

	/* The left edge, between the top and the bottom. */
	edges[2].x = x - thickness;
	edges[2].y = y;
	edges[2].width = thickness;
	edges[2].height = height;

	/* The right edge, between the top and the bottom. */
	edges[3].x = x + width;
	edges[3].y = y;
	edges[3].width = thickness;
	edges[3].height = height;

	/* The items, last in the list so they are drawn over the page. */
	for (index = 0; index < 4U; index++) {
		error = wb_vector_push(&list->items, &edges[index]);
		if (error != 0)
			return error;
	}

	/* Succeeded: the ring is in the list. */
	return 0;
}

/*
 * Adds a filled rectangle on top of everything painted so far (the caret
 * of a focused text control, ws074-p032).
 */
int
paint_add_rect(
	struct paint_list *list,
	layout_unit x,
	layout_unit y,
	layout_unit width,
	layout_unit height,
	uint32_t color)
{
	struct paint_item item;
	int error;

	/* The rectangle. */
	memset(&item, 0, sizeof(item));
	item.kind = PAINT_RECT;
	item.x = x;
	item.y = y;
	item.width = width;
	item.height = height;
	item.color = color;

	/* The item, last in the list so it is drawn over the page. */
	error = wb_vector_push(&list->items, &item);
	if (error != 0)
		return error;

	/* Succeeded: the rectangle is in the list. */
	return 0;
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

		/* An image: its rectangle and its image's size. */
		if (item->kind == PAINT_IMAGE) {
			wb_buffer_printf(out, "image %.2f %.2f %.2f %.2f %dx%d\n", (double)layout_to_px(item->x), (double)layout_to_px(item->y),
			    (double)layout_to_px(item->width), (double)layout_to_px(item->height), item->image->width, item->image->height);
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

	/* The root's own background (a color or an image) wins. */
	if ((tree->root->style.background_color >> 24) != 0 || tree->root->background != NULL) {
		*color = tree->root->style.background_color;
		if ((*color >> 24) == 0)
			*color = LIST_CANVAS_DEFAULT;
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
	if ((body->style.background_color >> 24) == 0 && body->background == NULL)
		return NULL;

	/* The body's background is the canvas's. */
	*color = body->style.background_color;
	if ((*color >> 24) == 0)
		*color = LIST_CANVAS_DEFAULT;
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

	/* A form control standing as a block draws itself from its state. */
	if (visible && box->control != DOM_CONTROL_NONE) {
		list_control(walk, box);
		return;
	}

	/* The background fills the border box, unless it went to the canvas. */
	width = box->border[CSS_LEFT] + box->padding[CSS_LEFT] + box->width + box->padding[CSS_RIGHT] + box->border[CSS_RIGHT];
	height = box->border[CSS_TOP] + box->padding[CSS_TOP] + box->height + box->padding[CSS_BOTTOM] + box->border[CSS_BOTTOM];
	if (visible && box != walk->canvas_box && (box->style.background_color >> 24) != 0)
		list_rect(walk, box->x, box->y, width, height, box->style.background_color);

	/* The background image over the color, unless it went to the canvas. */
	if (visible && box != walk->canvas_box)
		list_box_background(walk, box);

	/* The borders go over the background. */
	if (visible)
		list_borders(walk, box);

	/* A replaced block's image fills its content box. */
	if (visible && box->replaced) {
		list_image(walk, box, box->x + box->border[CSS_LEFT] + box->padding[CSS_LEFT], box->y + box->border[CSS_TOP] + box->padding[CSS_TOP]);
		return;
	}

	/* A box that clips its overflow clips its content to its padding box. */
	clips = layout_clips(box);
	if (clips)
		list_clip(walk, box);

	/* A block of lines paints the floats among its content, then its text and inline blocks. */
	if (box->children_inline) {
		list_inline_floats(walk, box, layer, depth + 1);
		list_lines(walk, box, layer, depth + 1);
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

	/*
	 * A float paints itself; an inline box is searched; a box out of the
	 * flow is painted in its own turn, and an inline block with its line.
	 */
	for (child = box->first_child; child != NULL; child = child->next) {
		if (child->out_of_flow || child->atomic)
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

/* Adds the text of a block's lines, and the inline blocks on them. */
static void
list_lines(
	struct list_walk *walk,
	const struct layout_box *box,
	const struct layout_box *layer,
	int depth)
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

	/* Each fragment of each line, on the line's baseline moved by its vertical alignment. */
	for (index = 0; index < box->line_count; index++) {
		line = &box->lines[index];
		for (item = 0; item < line->fragment_count; item++) {
			fragment = &line->fragments[item];

			/* An inline block paints itself where the line put it. */
			if (fragment->box->atomic) {
				list_box(walk, fragment->box, layer, depth);
				continue;
			}

			/* Text and replaced boxes. */
			list_fragment(walk, fragment, left + line->left + fragment->x, top + line->y + line->baseline + fragment->shift);
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

	/* An inline replaced box's margin box stands on the baseline. */
	if (fragment->box->kind == LAYOUT_REPLACED) {
		list_replaced(walk, fragment->box, x, baseline - fragment->ascent);
		return;
	}

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

/*
 * Paints an inline replaced box whose margin box starts at a point: its
 * background, its borders and its image.
 */
static void
list_replaced(
	struct list_walk *walk,
	const struct layout_box *box,
	layout_unit x,
	layout_unit y)
{
	struct layout_box placed;
	layout_unit width;
	layout_unit height;

	/* The box as if its border box were placed there (the layout places only blocks). */
	placed = *box;
	placed.x = x + box->margin[CSS_LEFT];
	placed.y = y + box->margin[CSS_TOP];

	/* A form control draws itself from its state. */
	if (box->control != DOM_CONTROL_NONE) {
		list_control(walk, &placed);
		return;
	}

	/* The background under the border box. */
	width = box->border[CSS_LEFT] + box->padding[CSS_LEFT] + box->width + box->padding[CSS_RIGHT] + box->border[CSS_RIGHT];
	height = box->border[CSS_TOP] + box->padding[CSS_TOP] + box->height + box->padding[CSS_BOTTOM] + box->border[CSS_BOTTOM];
	if ((box->style.background_color >> 24) != 0)
		list_rect(walk, placed.x, placed.y, width, height, box->style.background_color);

	/* The borders, then the image in the content box. */
	list_borders(walk, &placed);
	list_image(walk, box, placed.x + box->border[CSS_LEFT] + box->padding[CSS_LEFT], placed.y + box->border[CSS_TOP] + box->padding[CSS_TOP]);
}

/*
 * Paints a form control whose border box is placed (ws074-p032): its
 * frame, then its text, and it records where it drew its text and where
 * the caret goes in the control's state for the page.
 *
 * A control keeps the look the user agent's sheet gives it -- all four
 * borders inset or outset -- is drawn the way Chromium's controls look: a
 * one pixel frame in the border's color around the background.  One the
 * page styled otherwise is drawn from its style like any box.
 */
static void
list_control(
	struct list_walk *walk,
	const struct layout_box *box)
{
	struct list_area painting;
	struct list_area area;
	layout_unit width;
	layout_unit height;
	layout_unit pixel;
	int native;

	/* The border box. */
	width = box->border[CSS_LEFT] + box->padding[CSS_LEFT] + box->width + box->padding[CSS_RIGHT] + box->border[CSS_RIGHT];
	height = box->border[CSS_TOP] + box->padding[CSS_TOP] + box->height + box->padding[CSS_BOTTOM] + box->border[CSS_BOTTOM];

	/* A checkbox or a radio button is drawn as the platform draws one. */
	if (box->control == DOM_CONTROL_CHECKBOX || box->control == DOM_CONTROL_RADIO) {
		list_check(walk, box, width, height);
		return;
	}

	/* The background color under the border box. */
	if ((box->style.background_color >> 24) != 0)
		list_rect(walk, box->x, box->y, width, height, box->style.background_color);

	/* The background image, placed in the padding box and painted over the border box. */
	painting.x = box->x;
	painting.y = box->y;
	painting.width = width;
	painting.height = height;
	area.x = box->x + box->border[CSS_LEFT];
	area.y = box->y + box->border[CSS_TOP];
	area.width = box->padding[CSS_LEFT] + box->width + box->padding[CSS_RIGHT];
	area.height = box->padding[CSS_TOP] + box->height + box->padding[CSS_BOTTOM];
	list_background_image(walk, box, &area, &painting);

	/* The platform's one pixel frame, or the style's borders. */
	native = list_control_native(box);
	if (native) {
		pixel = LAYOUT_UNIT;
		list_rect(walk, box->x, box->y, width, pixel, box->style.border_color[CSS_TOP]);
		list_rect(walk, box->x, box->y + height - pixel, width, pixel, box->style.border_color[CSS_BOTTOM]);
		list_rect(walk, box->x, box->y + pixel, pixel, height - 2 * pixel, box->style.border_color[CSS_LEFT]);
		list_rect(walk, box->x + width - pixel, box->y + pixel, pixel, height - 2 * pixel, box->style.border_color[CSS_RIGHT]);
	} else {
		list_borders(walk, box);
	}

	/* The text in the content box. */
	list_control_text(walk, box);
}

/* Tells whether a control keeps the user agent's look: all four borders inset (a field) or outset (a button). */
static int
list_control_native(
	const struct layout_box *box)
{
	int side;
	int style;

	/* Every side must be inset or outset. */
	for (side = 0; side < 4; side++) {
		style = box->style.border_style[side];
		if (style != CSS_BORDER_INSET && style != CSS_BORDER_OUTSET)
			return 0;
	}

	/* The user agent's look. */
	return 1;
}

/*
 * Draws a checkbox or a radio button in its border box: a white square
 * with a gray frame, filled with blue and marked in white when it is
 * checked, as Chromium's look.
 */
static void
list_check(
	struct list_walk *walk,
	const struct layout_box *box,
	layout_unit width,
	layout_unit height)
{
	const struct dom_element *element;
	layout_unit pixel;
	layout_unit mark;
	int checked;

	/* Whether it is checked. */
	checked = 0;
	if (box->node != NULL && box->node->type == DOM_ELEMENT) {
		element = (const struct dom_element *)box->node;
		checked = dom_control_checked(element);
	}

	/* The frame and the inside: gray around white, or blue all over when checked. */
	pixel = LAYOUT_UNIT;
	if (checked) {
		list_rect(walk, box->x, box->y, width, height, LIST_CHECKED_COLOR);
	} else {
		list_rect(walk, box->x, box->y, width, height, LIST_FRAME_COLOR);
		list_rect(walk, box->x + pixel, box->y + pixel, width - 2 * pixel, height - 2 * pixel, LIST_FIELD_COLOR);
	}

	/* A checked control's white mark in its middle, a third of its size. */
	if (checked) {
		mark = width / 3;
		list_rect(walk, box->x + (width - mark) / 2, box->y + (height - mark) / 2, mark, mark, LIST_FIELD_COLOR);
	}
}

/*
 * Draws a control's text in its content box and records where it went: a
 * field's value (bullets for a password, the placeholder in gray when it
 * is empty) scrolled to keep the caret in view and clipped to the content
 * box, a button's label centered, a textarea's lines from the top.
 */
static void
list_control_text(
	struct list_walk *walk,
	const struct layout_box *box)
{
	struct dom_element *element;
	struct dom_control *control;
	struct list_area content;
	struct text_font font;
	struct wb_units shown;
	layout_unit line;
	layout_unit ascent;
	layout_unit width;
	layout_unit caret;
	layout_unit left;
	layout_unit top;
	uint32_t color;
	int editable;
	int error;

	/* Only an element's control has text. */
	if (walk->error != 0 || box->node == NULL || box->node->type != DOM_ELEMENT)
		return;
	element = (struct dom_element *)box->node;

	/* The content box. */
	content.x = box->x + box->border[CSS_LEFT] + box->padding[CSS_LEFT];
	content.y = box->y + box->border[CSS_TOP] + box->padding[CSS_TOP];
	content.width = box->width;
	content.height = box->height;

	/* The font and the line the text is set in. */
	list_style_font(walk, &box->style, &font);
	error = layout_control_line(walk->text, &box->style, &line, &ascent);
	if (error != 0) {
		walk->error = error;
		return;
	}

	/* A textarea's lines go their own way. */
	if (box->control == DOM_CONTROL_TEXTAREA) {
		list_textarea(walk, box, element, &content, &font, line, ascent);
		return;
	}

	/* So does a select's chosen option and its arrow. */
	if (box->control == DOM_CONTROL_SELECT) {
		list_select(walk, box, element, &content, &font, line, ascent);
		return;
	}

	/* A field keeps its state for the caret; a button only shows its label. */
	editable = 0;
	if (box->control == DOM_CONTROL_TEXT || box->control == DOM_CONTROL_PASSWORD)
		editable = 1;
	control = NULL;
	if (editable) {
		control = dom_control_of(element);
		if (control == NULL) {
			walk->error = ENOMEM;
			return;
		}
	}

	/* The text shown, and its color. */
	wb_units_init(&shown);
	color = box->style.color;
	error = list_control_shown(box, element, control, &shown, &color);
	if (error == 0)
		error = layout_units_width(walk->text, &font, shown.data, shown.length, &width);
	if (error != 0) {
		wb_units_release(&shown);
		walk->error = error;
		return;
	}

	/* The line is centered in the content box's height. */
	top = content.y + (content.height - line) / 2;

	/* A button's label is placed by text-align (centered by the user agent's sheet). */
	if (!editable) {
		left = content.x;
		if (box->style.text_align == CSS_TEXT_ALIGN_CENTER)
			left = content.x + (content.width - width) / 2;
		if (box->style.text_align == CSS_TEXT_ALIGN_RIGHT)
			left = content.x + content.width - width;
		list_text_run(walk, &font, color, shown.data, shown.length, left, top + ascent);
		wb_units_release(&shown);
		return;
	}

	/* The caret's place in the text (at the start while the placeholder shows). */
	error = list_caret_offset(walk, &font, box, element, control, &caret);
	if (error != 0) {
		wb_units_release(&shown);
		walk->error = error;
		return;
	}

	/* The scroll keeps the caret inside the content box, and no more of the box empty than it must. */
	if (caret - control->scroll_x > content.width - LAYOUT_UNIT)
		control->scroll_x = caret - content.width + LAYOUT_UNIT;
	if (caret < control->scroll_x)
		control->scroll_x = caret;
	if (control->scroll_x > 0 && width - control->scroll_x < content.width - LAYOUT_UNIT) {
		control->scroll_x = width - content.width + LAYOUT_UNIT;
		if (control->scroll_x < 0)
			control->scroll_x = 0;
	}

	/* The text, scrolled and clipped to the content box. */
	list_clip_rect(walk, &content);
	list_text_run(walk, &font, color, shown.data, shown.length, content.x - control->scroll_x, top + ascent);
	list_unclip(walk);
	wb_units_release(&shown);

	/*
	 * What the page needs of this drawing: drawn says the rest is current,
	 * and the caret stands at its offset, as tall as the font's glyphs.
	 */
	list_control_record(walk, box, control, &content, &font, content.x + caret - control->scroll_x, top, ascent);
}

/*
 * Writes the text a field or a button shows: a button's label, a field's
 * value (a password's as bullets), or the placeholder in gray when the
 * field is empty.
 */
static int
list_control_shown(
	const struct layout_box *box,
	struct dom_element *element,
	const struct dom_control *control,
	struct wb_units *shown,
	uint32_t *color)
{
	struct vm_string *placeholder;
	struct wb_units value;
	size_t index;
	uint16_t unit;
	int error;

	/* A button shows its label. */
	if (control == NULL) {
		error = dom_control_label(element, shown);
		return error;
	}

	/* A field's value. */
	wb_units_init(&value);
	error = dom_control_value(element, &value);
	if (error != 0) {
		wb_units_release(&value);
		return error;
	}

	/* An empty field shows its placeholder in gray. */
	placeholder = dom_attribute_ascii(element, "placeholder");
	if (value.length == 0 && placeholder != NULL) {
		wb_units_release(&value);
		*color = LIST_PLACEHOLDER_COLOR;
		for (index = 0; index < placeholder->length; index++) {
			unit = vm_string_at(placeholder, index);
			error = wb_units_append(shown, &unit, 1);
			if (error != 0)
				return error;
		}

		/* The placeholder is all that shows. */
		return 0;
	}

	/* A password shows a bullet for each unit, anything else its value. */
	if (box->control == DOM_CONTROL_PASSWORD) {
		unit = LIST_PASSWORD_BULLET;
		for (index = 0; index < value.length && error == 0; index++)
			error = wb_units_append(shown, &unit, 1);
	} else {
		error = wb_units_append(shown, value.data, value.length);
	}

	/* The value is no longer needed. */
	wb_units_release(&value);
	if (error != 0)
		return error;

	/* Succeeded: the text is written. */
	return 0;
}

/*
 * Measures where a field's caret is from its text's start: the width of the
 * value (or its bullets) before the caret; 0 while the field is empty.
 */
static int
list_caret_offset(
	struct list_walk *walk,
	const struct text_font *font,
	const struct layout_box *box,
	struct dom_element *element,
	struct dom_control *control,
	layout_unit *offset)
{
	struct wb_units value;
	size_t index;
	uint16_t bullet;
	layout_unit one;
	int error;

	/* The value, and a caret that fell past its end moves back to it. */
	*offset = 0;
	wb_units_init(&value);
	error = dom_control_value(element, &value);
	if (error != 0) {
		wb_units_release(&value);
		return error;
	}

	/* A caret past the value's end comes back to it. */
	if (control->caret > value.length)
		control->caret = value.length;

	/* A password's caret is after as many bullets. */
	if (box->control == DOM_CONTROL_PASSWORD) {
		bullet = LIST_PASSWORD_BULLET;
		error = layout_units_width(walk->text, font, &bullet, 1, &one);
		for (index = 0; index < control->caret && error == 0; index++)
			*offset += one;
	} else {
		error = layout_units_width(walk->text, font, value.data, control->caret, offset);
	}

	/* The value is no longer needed. */
	wb_units_release(&value);
	if (error != 0)
		return error;

	/* Succeeded: the caret's offset. */
	return 0;
}

/*
 * Draws a textarea's value line by line from its content box's top,
 * clipped to the content box, and records its caret (lines do not wrap in
 * this pass).
 */
static void
list_textarea(
	struct list_walk *walk,
	const struct layout_box *box,
	struct dom_element *element,
	const struct list_area *content,
	const struct text_font *font,
	layout_unit line,
	layout_unit ascent)
{
	struct dom_control *control;
	struct wb_units value;
	layout_unit caret_x;
	layout_unit caret_top;
	layout_unit y;
	size_t start;
	size_t index;
	int error;

	/* The state that the caret is kept in, and the value. */
	control = dom_control_of(element);
	if (control == NULL) {
		walk->error = ENOMEM;
		return;
	}

	/* The value. */
	wb_units_init(&value);
	error = dom_control_value(element, &value);
	if (error != 0) {
		wb_units_release(&value);
		walk->error = error;
		return;
	}

	/* A caret past the value's end comes back to it. */
	if (control->caret > value.length)
		control->caret = value.length;

	/* Each line, and the caret on the line it is in. */
	control->scroll_x = 0;
	caret_x = content->x;
	caret_top = content->y;
	list_clip_rect(walk, content);
	y = content->y;
	start = 0;
	for (index = 0; index <= value.length && walk->error == 0; index++) {
		/* A line ends at a line feed or at the end of the value. */
		if (index < value.length && value.data[index] != 0x0aU)
			continue;

		/* The caret is on this line when it falls within it. */
		if (control->caret >= start && control->caret <= index) {
			error = layout_units_width(walk->text, font, value.data + start, control->caret - start, &caret_x);
			if (error != 0)
				walk->error = error;
			caret_x += content->x;
			caret_top = y;
		}

		/* The line's text, then the next line below it. */
		list_text_run(walk, font, box->style.color, value.data + start, index - start, content->x, y + ascent);
		y += line;
		start = index + 1U;
	}

	/* The clip ends with the lines, and the value is no longer needed. */
	list_unclip(walk);
	wb_units_release(&value);

	/* What the page needs for the caret. */
	list_control_record(walk, box, control, content, font, caret_x, caret_top, ascent);
}

/*
 * Draws a select: its chosen option's label from the content box's left,
 * on a line centered in its height, and a small arrow pointing down in the
 * room kept at its right.
 */
static void
list_select(
	struct list_walk *walk,
	const struct layout_box *box,
	struct dom_element *element,
	const struct list_area *content,
	const struct text_font *font,
	layout_unit line,
	layout_unit ascent)
{
	struct dom_element *option;
	struct wb_units label;
	layout_unit top;
	layout_unit arrow_x;
	layout_unit arrow_y;
	layout_unit row;
	int step;
	int error;

	/* The chosen option's label, if there is an option. */
	wb_units_init(&label);
	option = dom_select_chosen(element);
	error = 0;
	if (option != NULL)
		error = dom_option_text(option, &label);
	if (error != 0) {
		wb_units_release(&label);
		walk->error = error;
		return;
	}

	/* The label on the line, clipped to the room left of the arrow. */
	top = content->y + (content->height - line) / 2;
	list_clip_rect(walk, content);
	list_text_run(walk, font, box->style.color, label.data, label.length, content->x, top + ascent);
	list_unclip(walk);
	wb_units_release(&label);

	/* The arrow: rows a pixel tall, each two pixels narrower, from a row nine pixels wide. */
	row = LAYOUT_UNIT;
	arrow_x = content->x + content->width - LIST_ARROW_RIGHT * LAYOUT_UNIT;
	arrow_y = content->y + (content->height - LIST_ARROW_ROWS * row) / 2;
	for (step = 0; step < LIST_ARROW_ROWS; step++) {
		list_rect(
			walk,
			arrow_x + (layout_unit)step * row,
			arrow_y + (layout_unit)step * row,
			(layout_unit)(LIST_ARROW_WIDTH - 2 * step) * row,
			row,
			box->style.color);
	}
}

/*
 * Records in a control's state where it was drawn and where its caret
 * goes: the content box, and a caret as tall as the font's glyphs on the
 * line whose top is given, in the text's color.
 */
static void
list_control_record(
	struct list_walk *walk,
	const struct layout_box *box,
	struct dom_control *control,
	const struct list_area *content,
	const struct text_font *font,
	layout_unit caret_x,
	layout_unit line_top,
	layout_unit ascent)
{
	struct text_metrics metrics;
	int error;

	/* The glyphs' extent, which the caret spans. */
	error = text_font_metrics(walk->text, font, &metrics);
	if (error != 0) {
		walk->error = error;
		return;
	}

	/*
	 * drawn tells the page that the geometry below is the display list's;
	 * it stays set as long as the control keeps a box.
	 */
	control->drawn = 1;
	control->content_x = content->x;
	control->content_y = content->y;
	control->content_width = content->width;
	control->content_height = content->height;
	control->caret_x = caret_x;
	control->caret_top = line_top + ascent - (layout_unit)metrics.ascent * LAYOUT_UNIT;
	control->caret_height = (layout_unit)(metrics.ascent + metrics.descent) * LAYOUT_UNIT;
	control->caret_color = box->style.color;
}

/* Picks the font a style draws text with (as the layout picks it). */
static void
list_style_font(
	struct list_walk *walk,
	const struct css_style *style,
	struct text_font *font)
{
	int monospace;

	/* The monospace family uses the monospace face. */
	monospace = 0;
	if (style->generic_family == CSS_FAMILY_MONOSPACE)
		monospace = 1;
	text_select_font(walk->text, monospace, style->font_size, style->font_weight, font);
}

/* Adds a run of UTF-16 text as a text item from a pen position on a baseline. */
static void
list_text_run(
	struct list_walk *walk,
	const struct text_font *font,
	uint32_t color,
	const uint16_t *units,
	size_t length,
	layout_unit x,
	layout_unit baseline)
{
	struct paint_item item;
	struct paint_glyph *glyphs;
	struct text_glyph glyph;
	uint32_t code_point;
	layout_unit pen;
	size_t count;
	size_t used;
	size_t position;
	int error;

	/* Nothing to add after an error, or for no text. */
	if (walk->error != 0 || length == 0)
		return;

	/* There are at most as many glyphs as UTF-16 units. */
	glyphs = wb_arena_alloc(&walk->list->arena, length * sizeof(struct paint_glyph));
	if (glyphs == NULL) {
		walk->error = ENOMEM;
		return;
	}

	/* Sets each character's glyph at the pen, which moves by its advance. */
	count = 0;
	pen = 0;
	position = 0;
	while (position < length) {
		used = wb_utf16_decode(units + position, length - position, &code_point);
		position += used;
		error = text_glyph(walk->text, font, code_point, 0, &glyph);
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
	item.width = pen;
	item.color = color;
	item.font = *font;
	item.glyphs = glyphs;
	item.glyph_count = count;
	error = wb_vector_push(&walk->list->items, &item);
	if (error != 0)
		walk->error = error;
}

/* Adds a replaced box's image over its content box, whose top left is at a point. */
static void
list_image(
	struct list_walk *walk,
	const struct layout_box *box,
	layout_unit x,
	layout_unit y)
{
	/* The image over the content box. */
	list_image_item(walk, box->image, x, y, box->width, box->height);
}

/* Adds an image item: an image stretched over a rectangle. */
static void
list_image_item(
	struct list_walk *walk,
	const struct img_bitmap *image,
	layout_unit x,
	layout_unit y,
	layout_unit width,
	layout_unit height)
{
	struct paint_item item;
	int error;

	/* Nothing to add after an error, without an image, or for an empty rectangle. */
	if (walk->error != 0 || image == NULL)
		return;
	if (width <= 0 || height <= 0)
		return;

	/* The item. */
	memset(&item, 0, sizeof(item));
	item.kind = PAINT_IMAGE;
	item.x = x;
	item.y = y;
	item.width = width;
	item.height = height;
	item.image = image;
	error = wb_vector_push(&walk->list->items, &item);
	if (error != 0)
		walk->error = error;
}

/* Paints a box's background image in its padding box, over its border box. */
static void
list_box_background(
	struct list_walk *walk,
	const struct layout_box *box)
{
	struct list_area area;
	struct list_area painting;

	/* A box without an image paints none. */
	if (box->background == NULL)
		return;

	/* The border box paints it; the padding box places it. */
	painting.x = box->x;
	painting.y = box->y;
	painting.width = box->border[CSS_LEFT] + box->padding[CSS_LEFT] + box->width + box->padding[CSS_RIGHT] + box->border[CSS_RIGHT];
	painting.height = box->border[CSS_TOP] + box->padding[CSS_TOP] + box->height + box->padding[CSS_BOTTOM] + box->border[CSS_BOTTOM];
	area.x = box->x + box->border[CSS_LEFT];
	area.y = box->y + box->border[CSS_TOP];
	area.width = box->padding[CSS_LEFT] + box->width + box->padding[CSS_RIGHT];
	area.height = box->padding[CSS_TOP] + box->height + box->padding[CSS_BOTTOM];
	list_background_image(walk, box, &area, &painting);
}

/*
 * Paints the canvas's background image (the root's, or the body's): placed
 * in the root's padding box and painted over the whole canvas.
 */
static void
list_canvas_background(
	struct list_walk *walk,
	const struct layout_tree *tree)
{
	const struct layout_box *root;
	struct list_area area;
	struct list_area painting;

	/* A canvas without an image paints none. */
	if (walk->canvas_box == NULL || walk->canvas_box->background == NULL)
		return;

	/* The whole canvas paints it; the root's padding box places it. */
	root = tree->root;
	painting.x = 0;
	painting.y = 0;
	painting.width = walk->list->width;
	painting.height = walk->list->height;
	area.x = root->x + root->border[CSS_LEFT];
	area.y = root->y + root->border[CSS_TOP];
	area.width = root->padding[CSS_LEFT] + root->width + root->padding[CSS_RIGHT];
	area.height = root->padding[CSS_TOP] + root->height + root->padding[CSS_BOTTOM];
	list_background_image(walk, walk->canvas_box, &area, &painting);
}

/*
 * Paints a box's background image: sized in its positioning area (the
 * padding box, or the root's for the canvas), placed there by
 * background-position, and repeated as background-repeat says over the
 * painting area (the border box, or the canvas), which clips it.
 */
static void
list_background_image(
	struct list_walk *walk,
	const struct layout_box *box,
	const struct list_area *area,
	const struct list_area *painting)
{
	const struct img_bitmap *image;
	layout_unit tile_width;
	layout_unit tile_height;
	layout_unit start_x;
	layout_unit start_y;
	layout_unit end_x;
	layout_unit end_y;
	layout_unit x;
	layout_unit y;
	size_t tiles;
	int repeat_x;
	int repeat_y;

	/* Nothing without an image, or in an area without room. */
	image = box->background;
	if (walk->error != 0 || image == NULL)
		return;
	if (area->width <= 0 || area->height <= 0 || painting->width <= 0 || painting->height <= 0)
		return;

	/* The size of one tile; an empty one paints nothing. */
	list_background_size(box, image, area, &tile_width, &tile_height);
	if (tile_width <= 0 || tile_height <= 0)
		return;

	/* The first tile's place, from the position in the room the area leaves. */
	start_x = area->x + list_background_offset(&box->style.background_position[0], area->width - tile_width);
	start_y = area->y + list_background_offset(&box->style.background_position[1], area->height - tile_height);

	/* A repeating axis starts at the tile before the painting area and ends past it; another has one tile. */
	repeat_x = box->style.background_repeat == CSS_REPEAT_BOTH || box->style.background_repeat == CSS_REPEAT_X;
	repeat_y = box->style.background_repeat == CSS_REPEAT_BOTH || box->style.background_repeat == CSS_REPEAT_Y;
	end_x = start_x + tile_width;
	end_y = start_y + tile_height;
	if (repeat_x) {
		start_x -= ((start_x - painting->x + tile_width - 1) / tile_width) * tile_width;
		end_x = painting->x + painting->width;
	}

	/* The vertical axis likewise. */
	if (repeat_y) {
		start_y -= ((start_y - painting->y + tile_height - 1) / tile_height) * tile_height;
		end_y = painting->y + painting->height;
	}

	/* The tiles, inside the painting area, up to a number no page needs. */
	list_clip_rect(walk, painting);
	tiles = 0;
	for (y = start_y; y < end_y && tiles < LIST_TILES_MAX; y += tile_height) {
		for (x = start_x; x < end_x && tiles < LIST_TILES_MAX; x += tile_width) {
			list_image_item(walk, image, x, y, tile_width, tile_height);
			tiles++;
		}
	}

	/* The painting area's clip ends. */
	list_unclip(walk);
}

/*
 * Works out the size of one tile of a background image in its area:
 * contain and cover scale the image to fit inside or to cover the area,
 * lengths and percentages size a side, and an auto side follows the other
 * through the image's ratio (or is the image's own).
 */
static void
list_background_size(
	const struct layout_box *box,
	const struct img_bitmap *image,
	const struct list_area *area,
	layout_unit *width,
	layout_unit *height)
{
	const struct css_length *sizes;
	float natural_width;
	float natural_height;
	float scale;
	float scale_height;
	int width_auto;
	int height_auto;

	/* The image's own size. */
	natural_width = (float)image->width * LAYOUT_UNIT;
	natural_height = (float)image->height * LAYOUT_UNIT;

	/* contain: the largest size that fits; cover: the smallest that covers. */
	if (box->style.background_size_keyword != CSS_BACKGROUND_SIZE_LENGTHS) {
		scale = (float)area->width / natural_width;
		scale_height = (float)area->height / natural_height;
		if (box->style.background_size_keyword == CSS_BACKGROUND_SIZE_CONTAIN && scale_height < scale)
			scale = scale_height;
		if (box->style.background_size_keyword == CSS_BACKGROUND_SIZE_COVER && scale_height > scale)
			scale = scale_height;
		*width = (layout_unit)(natural_width * scale + 0.5f);
		*height = (layout_unit)(natural_height * scale + 0.5f);
		return;
	}

	/* Each side given, or auto. */
	sizes = box->style.background_size;
	width_auto = sizes[0].unit != CSS_UNIT_PX && sizes[0].unit != CSS_UNIT_PERCENT;
	height_auto = sizes[1].unit != CSS_UNIT_PX && sizes[1].unit != CSS_UNIT_PERCENT;
	*width = (layout_unit)natural_width;
	*height = (layout_unit)natural_height;
	if (!width_auto)
		*width = list_background_length(&sizes[0], area->width);
	if (!height_auto)
		*height = list_background_length(&sizes[1], area->height);

	/* An auto side follows the other through the ratio. */
	if (width_auto && !height_auto)
		*width = (layout_unit)((float)*height * natural_width / natural_height);
	if (height_auto && !width_auto)
		*height = (layout_unit)((float)*width * natural_height / natural_width);
}

/* Resolves a length of the background against a length of its area (a percentage of it, or pixels). */
static layout_unit
list_background_length(
	const struct css_length *length,
	layout_unit whole)
{
	layout_unit value;

	/* A percentage of the whole. */
	if (length->unit == CSS_UNIT_PERCENT) {
		value = (layout_unit)((float)whole * length->value / 100.0f);
		return value;
	}

	/* Pixels. */
	value = layout_from_px(length->value);
	return value;
}

/* Resolves one axis of background-position: a percentage of the room the tile leaves, or pixels. */
static layout_unit
list_background_offset(
	const struct css_length *position,
	layout_unit room)
{
	layout_unit offset;

	/* A percentage of the room (which is negative for a tile larger than its area). */
	if (position->unit == CSS_UNIT_PERCENT) {
		offset = (layout_unit)((float)room * position->value / 100.0f);
		return offset;
	}

	/* Pixels from the area's edge. */
	offset = layout_from_px(position->value);
	return offset;
}

/* Starts a clip to a rectangle (the items until its end are drawn inside it). */
static void
list_clip_rect(
	struct list_walk *walk,
	const struct list_area *area)
{
	struct paint_item item;
	int error;

	/* Nothing to add after an error. */
	if (walk->error != 0)
		return;

	/* The rectangle. */
	memset(&item, 0, sizeof(item));
	item.kind = PAINT_CLIP;
	item.x = area->x;
	item.y = area->y;
	item.width = area->width;
	item.height = area->height;
	error = wb_vector_push(&walk->list->items, &item);
	if (error != 0)
		walk->error = error;
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
