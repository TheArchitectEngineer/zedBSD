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
 * the drift of floating point.  The first pass lays out normal flow: block
 * boxes stacked with their margins collapsing, and inline content broken
 * into line boxes.  Floats, positioning, tables, flex and grid come later;
 * until then their boxes are laid out as blocks.
 */

#ifndef ZDESKTOP_BROWSER_LAYOUT_H
#define ZDESKTOP_BROWSER_LAYOUT_H

#include "css/css.h"
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
	LAYOUT_LINE_BREAK
};

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
 * laid out children.
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
};

/*
 * A laid out page: the box tree, the arena the boxes live in and the size
 * the layout was made for.
 */
struct layout_tree {
	struct wb_arena arena;
	struct layout_box *root;
	struct text_system *text;
	layout_unit viewport_width;
	layout_unit viewport_height;
	layout_unit document_height;
};

/* The layout (box.c, block.c, inline.c, dump.c, hit.c). */
int layout_build(struct layout_tree *tree, struct css_engine *css, struct text_system *text, struct dom_document *document, int width, int height);
void layout_release(struct layout_tree *tree);
int layout_block(struct layout_tree *tree, struct layout_box *box, layout_unit containing_width);
int layout_inline(struct layout_tree *tree, struct layout_box *box);
void layout_font_of(struct layout_tree *tree, const struct css_style *style, struct text_font *font);
int layout_dump(const struct layout_tree *tree, struct wb_buffer *out);
const struct layout_box *layout_hit(const struct layout_tree *tree, layout_unit x, layout_unit y);
struct dom_node *layout_hit_node(const struct layout_tree *tree, layout_unit x, layout_unit y);
layout_unit layout_from_px(float px);
float layout_to_px(layout_unit value);

#endif
