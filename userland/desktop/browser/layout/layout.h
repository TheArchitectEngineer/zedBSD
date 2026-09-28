/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Layout (plan/ws074/design.md §6): the box tree built from the DOM and its
 * computed styles, block and inline formatting, and the positions the
 * painting reads.
 *
 * Positions are in layout units, 1/64 of a pixel, so lengths add up without
 * the drift of floating point.  Normal flow stacks block boxes with their
 * margins collapsing and breaks inline content into line boxes; then
 * relatively positioned boxes are shifted, and absolutely positioned and
 * fixed boxes, taken out of the flow, are placed in their containing
 * blocks (position.c).  Floats, tables, flex and grid come later; until
 * then their boxes are laid out as blocks.  An <img> is a replaced box
 * (replaced.c): inline, an atomic piece of its line; otherwise a block
 * without content; either sized by its image and its width and height.
 * A form control (<input>, <textarea>, <select>) is a replaced box too,
 * sized by its text and attributes and standing on its text's baseline
 * (control.c).
 */

#ifndef KEILAND_BROWSER_LAYOUT_H
#define KEILAND_BROWSER_LAYOUT_H

#include "css/css.h"
#include "image/image.h"
#include "text/text.h"

/* How many layout units make a pixel. */
#define LAYOUT_UNIT		64

/* The deepest box nesting laid out (deeper boxes are dropped). */
#define LAYOUT_DEPTH_MAX	256

/*
 * A length in layout units.
 */
typedef int32_t layout_unit;

/*
 * The kinds of box.
 */
enum layout_box_kind {
	LAYOUT_BLOCK,
	LAYOUT_ANONYMOUS_BLOCK,
	LAYOUT_INLINE,
	LAYOUT_TEXT,
	LAYOUT_LINE_BREAK,
	LAYOUT_REPLACED
};

/*
 * Finds the decoded image an element shows (NULL when it has none); the
 * page supplies it, so that the layout needs no knowledge of loading.
 */
typedef const struct img_bitmap *(*layout_image_lookup)(void *context, const struct dom_element *element);

/*
 * Finds the decoded image a style's URL names (a background image; NULL
 * when it cannot be had), for the page to supply likewise.
 */
typedef const struct img_bitmap *(*layout_url_lookup)(void *context, const struct vm_string *url);

/*
 * A piece of text on a line: a run of a text box's characters, with its
 * font, position and width.  x is from the line's left, and the baseline
 * is the line's.
 */
struct layout_fragment {
	struct layout_box *box;
	const uint16_t *text;
	size_t length;
	struct text_font font;
	uint32_t color;
	int underline;
	layout_unit x;
	layout_unit width;
	layout_unit ascent;
	layout_unit descent;
};

/*
 * One line box of a block's inline content: where it is in the block's
 * content box, its height and baseline, and its fragments.
 */
struct layout_line {
	layout_unit y;
	layout_unit height;
	layout_unit baseline;
	layout_unit left;
	struct layout_fragment *fragments;
	size_t fragment_count;
};

/*
 * A box: a block (or anonymous block), an inline element, a run of text,
 * or a line break.
 *
 * x and y are the border box's position relative to the parent's content
 * box until the final pass makes them absolute; width and height are the
 * content box's.  A block whose children are inline has lines instead of
 * laid out children.  An absolutely positioned or fixed box is out of the
 * flow: its parent's layout passes it by and records its static position
 * (where it would have been, relative to the parent's content box, then
 * absolute), and position.c places it.
 */
struct layout_box {
	int kind;
	struct dom_node *node;
	struct css_style style;
	struct layout_box *parent;
	struct layout_box *first_child;
	struct layout_box *last_child;
	struct layout_box *next;
	int children_inline;
	int out_of_flow;
	int floating;
	layout_unit static_x;
	layout_unit static_y;

	/* The box model, resolved. */
	layout_unit x;
	layout_unit y;
	layout_unit width;
	layout_unit height;
	layout_unit margin[4];
	layout_unit border[4];
	layout_unit padding[4];

	/* The margins after collapsing with the first and last children. */
	layout_unit collapsed_top;
	layout_unit collapsed_bottom;

	/* The lines of a block with inline children. */
	struct layout_line *lines;
	size_t line_count;

	/* The text of a text box, whitespace kept as it came (the lines collapse it). */
	const uint16_t *text;
	size_t text_length;

	/* The list marker of a list item ("•" or "1."), empty when none. */
	uint16_t marker[8];
	size_t marker_length;

	/* A replaced box (an <img>), and the image it shows (NULL when it has none). */
	int replaced;
	const struct img_bitmap *image;

	/*
	 * A form control's box (control.c): its kind (DOM_CONTROL_*, NONE for
	 * other boxes), its natural size, and whether its text has a baseline
	 * and how far below its content box's top that is.
	 */
	int control;
	layout_unit natural_width;
	layout_unit natural_height;
	int has_baseline;
	layout_unit control_baseline;

	/* The background image the style names, decoded (NULL when there is none, or it could not be had). */
	const struct img_bitmap *background;
};

/*
 * A rectangle of the page in layout units from the document's top left:
 * the place a node takes (layout_node_bounds).
 */
struct layout_rect {
	layout_unit x;
	layout_unit y;
	layout_unit width;
	layout_unit height;
};

/*
 * A laid out page: the box tree, the arena the boxes live in and the size
 * the layout was made for.  While a layout runs, floats is the current
 * block formatting context's list of floats (float.c) and origin_x,
 * origin_y the content box of the block being laid out in that context's
 * coordinates.  image_lookup and image_context find the image of an
 * <img>, url_lookup a background image's.
 */
struct layout_tree {
	struct wb_arena arena;
	struct layout_box *root;
	struct text_system *text;
	layout_unit viewport_width;
	layout_unit viewport_height;
	layout_unit document_height;
	struct wb_vector *floats;
	layout_unit origin_x;
	layout_unit origin_y;
	layout_image_lookup image_lookup;
	layout_url_lookup url_lookup;
	void *image_context;
};

/*
 * The block formatting context a box's layout started a new one inside of
 * (float.c): its floats and the origin of the box's content box in it.
 */
struct layout_context {
	struct wb_vector *floats;
	layout_unit origin_x;
	layout_unit origin_y;
};

/* The layout (box.c, block.c, inline.c, float.c, position.c, replaced.c, dump.c, hit.c). */
int layout_build(struct layout_tree *tree, struct css_engine *css, struct text_system *text, struct dom_document *document, layout_image_lookup image_lookup, layout_url_lookup url_lookup, void *image_context, int width, int height);
void layout_box_model(struct layout_box *box, layout_unit containing_width);
void layout_auto_margins(struct layout_box *box, layout_unit containing_width);
void layout_replaced_size(struct layout_box *box, layout_unit containing_width);
void layout_absolute(struct layout_box *box, layout_unit x, layout_unit y);
int layout_is_positioned(const struct layout_box *box);
int layout_clips(const struct layout_box *box);
int layout_position(struct layout_tree *tree);
layout_unit layout_content_width(const struct layout_box *box, int depth);
void layout_context_begin(struct layout_tree *tree, struct layout_context *context, struct wb_vector *floats);
void layout_context_end(struct layout_tree *tree, const struct layout_context *context);
int layout_place_float(struct layout_tree *tree, struct layout_box *box, layout_unit y_min, layout_unit width);
void layout_line_room(const struct layout_tree *tree, layout_unit top, layout_unit bottom, layout_unit width, layout_unit *left, layout_unit *right);
layout_unit layout_below_float(const struct layout_tree *tree, layout_unit top);
layout_unit layout_clearance(const struct layout_tree *tree, int clear);
layout_unit layout_floats_bottom(const struct layout_tree *tree);
int layout_shrink_to_fit(struct layout_tree *tree, struct layout_box *box, layout_unit room);
int layout_stacking_order(const struct layout_tree *tree, struct wb_vector *boxes, size_t *flow_index);
void layout_release(struct layout_tree *tree);
int layout_block(struct layout_tree *tree, struct layout_box *box, layout_unit containing_width);
int layout_inline(struct layout_tree *tree, struct layout_box *box);
void layout_font_of(struct layout_tree *tree, const struct css_style *style, struct text_font *font);
int layout_dump(const struct layout_tree *tree, struct wb_buffer *out);
const struct layout_box *layout_hit(const struct layout_tree *tree, layout_unit x, layout_unit y);
struct dom_node *layout_hit_node(const struct layout_tree *tree, layout_unit x, layout_unit y);
int layout_node_bounds(const struct layout_tree *tree, const struct dom_node *node, struct layout_rect *rect);
const struct layout_box *layout_box_of(const struct layout_tree *tree, const struct dom_node *node);
layout_unit layout_from_px(float px);
float layout_to_px(layout_unit value);

/* Form controls (control.c). */
int layout_control_measure(struct layout_tree *tree, struct layout_box *box);
int layout_control_line(struct text_system *text, const struct css_style *style, layout_unit *line, layout_unit *ascent);
int layout_units_width(struct text_system *text, const struct text_font *font, const uint16_t *units, size_t length, layout_unit *width);

#endif
