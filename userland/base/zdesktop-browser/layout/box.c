/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The box tree: built from the DOM and the computed styles, with anonymous
 * blocks where a block holds both block and inline children, then laid out
 * from the root and given absolute positions (relatively positioned boxes
 * shifted by their offsets), and its out-of-flow boxes placed.
 */

#include "layout/layout.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* The list markers of list-style-type, as UTF-16. */
#define BOX_BULLET_DISC		0x2022U
#define BOX_BULLET_CIRCLE	0x25e6U
#define BOX_BULLET_SQUARE	0x25aaU

static int box_build_element(struct layout_tree *tree, struct css_engine *css, struct dom_element *element, const struct css_style *parent_style, struct layout_box *parent, int depth);
static int box_build_children(struct layout_tree *tree, struct css_engine *css, struct dom_node *node, const struct css_style *style, struct layout_box *box, int depth);
static struct layout_box *box_new(struct layout_tree *tree, int kind, struct dom_node *node, const struct css_style *style);
static void box_append(struct layout_box *parent, struct layout_box *child);
static int box_is_inline_level(const struct layout_box *box);
static int box_is_whitespace(const struct layout_box *box);
static int box_fix_children(struct layout_tree *tree, struct layout_box *box);
static void box_anonymous_style(const struct css_style *parent, struct css_style *style);
static void box_marker(struct layout_box *box, int ordinal);
static void box_relative_offset(const struct layout_box *box, layout_unit *dx, layout_unit *dy);
static void box_static_inline(struct layout_box *box, layout_unit x, layout_unit y);

/*
 * Builds and lays out the box tree of a document for a viewport of width
 * by height pixels.
 */
int
layout_build(
	struct layout_tree *tree,
	struct css_engine *css,
	struct text_system *text,
	struct dom_document *document,
	int width,
	int height)
{
	struct dom_node *node;
	struct layout_box *root;
	layout_unit total;
	int error;

	/* Starts an empty tree for the viewport. */
	memset(tree, 0, sizeof(*tree));
	wb_arena_init(&tree->arena, 0);
	tree->text = text;
	tree->viewport_width = (layout_unit)width * LAYOUT_UNIT;
	tree->viewport_height = (layout_unit)height * LAYOUT_UNIT;
	css_engine_set_viewport(css, (float)width, (float)height);

	/* Builds the boxes of the root element. */
	for (node = document->node.first_child; node != NULL; node = node->next) {
		if (node->type != DOM_ELEMENT)
			continue;

		/* The root element's box has no parent. */
		error = box_build_element(tree, css, (struct dom_element *)node, NULL, NULL, 0);
		if (error != 0)
			return error;
		break;
	}

	/* A document without a rendered root has nothing to lay out. */
	root = tree->root;
	if (root == NULL)
		return 0;

	/* Lays the root out in the viewport and makes every position absolute. */
	error = layout_block(tree, root, tree->viewport_width);
	if (error != 0)
		return error;
	root->x = root->margin[CSS_LEFT];
	root->y = root->margin[CSS_TOP];
	layout_absolute(root, 0, 0);

	/* The document is as tall as the root's margin box. */
	total = root->margin[CSS_TOP] + root->border[CSS_TOP] + root->padding[CSS_TOP] + root->height +
	    root->padding[CSS_BOTTOM] + root->border[CSS_BOTTOM] + root->margin[CSS_BOTTOM];
	tree->document_height = total;

	/* The boxes out of the flow go to their containing blocks (the document grows to hold them). */
	error = layout_position(tree);
	if (error != 0)
		return error;

	/* Succeeded: the tree is laid out. */
	return 0;
}

/*
 * Frees a layout tree.
 */
void
layout_release(
	struct layout_tree *tree)
{
	/* Everything lives in the arena. */
	wb_arena_release(&tree->arena);
	tree->root = NULL;
}

/*
 * Converts pixels to layout units, rounding to the nearest unit.
 */
layout_unit
layout_from_px(
	float px)
{
	float units;

	/* Scales and rounds away from zero at the half. */
	units = px * (float)LAYOUT_UNIT;
	if (units < 0)
		return (layout_unit)(units - 0.5f);

	/* Positive and zero values. */
	return (layout_unit)(units + 0.5f);
}

/*
 * Converts layout units to pixels.
 */
float
layout_to_px(
	layout_unit value)
{
	/* Divides by the units in a pixel. */
	return (float)value / (float)LAYOUT_UNIT;
}

/*
 * Turns positions relative to the parent's content box into absolute ones
 * for a box placed at x and y (its parent's content origin) and its
 * descendants: a relatively positioned box moves by its offsets, and a
 * box out of the flow gets its absolute static position (it is placed
 * later).
 */
void
layout_absolute(
	struct layout_box *box,
	layout_unit x,
	layout_unit y)
{
	struct layout_box *child;
	layout_unit content_x;
	layout_unit content_y;
	layout_unit dx;
	layout_unit dy;

	/* The box's border box moves by the parent's content origin, and by its offsets when it is relative. */
	box_relative_offset(box, &dx, &dy);
	box->x += x + dx;
	box->y += y + dy;

	/* Its children are relative to its content box. */
	content_x = box->x + box->border[CSS_LEFT] + box->padding[CSS_LEFT];
	content_y = box->y + box->border[CSS_TOP] + box->padding[CSS_TOP];

	/* The lines are relative to the block; boxes out of the flow among them start at its content's top. */
	if (box->children_inline) {
		box_static_inline(box, content_x, content_y);
		return;
	}

	/* A block child moves with the box; one out of the flow only learns where its static position is. */
	for (child = box->first_child; child != NULL; child = child->next) {
		if (child->out_of_flow) {
			child->static_x += content_x;
			child->static_y += content_y;
			continue;
		}

		/* A child in the flow. */
		layout_absolute(child, content_x, content_y);
	}
}

/*
 * Tells whether a box is positioned for the painting order: relative,
 * absolute or fixed (sticky is laid out as static in this pass).
 */
int
layout_is_positioned(
	const struct layout_box *box)
{
	/* Only blocks are painted as positioned boxes. */
	if (box->kind != LAYOUT_BLOCK)
		return 0;

	/* The three positioned schemes. */
	if (box->style.position == CSS_POSITION_RELATIVE)
		return 1;
	if (box->style.position == CSS_POSITION_ABSOLUTE)
		return 1;
	if (box->style.position == CSS_POSITION_FIXED)
		return 1;

	/* Static and sticky boxes. */
	return 0;
}

/*
 * Tells whether a box clips its content to its padding box: its overflow
 * is not visible, and is not the viewport's (the root's overflow, or the
 * body's when the root's is visible, goes to the viewport).
 */
int
layout_clips(
	const struct layout_box *box)
{
	const struct dom_element *element;
	int is_body;

	/* Visible on both axes clips nothing. */
	if (box->style.overflow_x == CSS_OVERFLOW_VISIBLE && box->style.overflow_y == CSS_OVERFLOW_VISIBLE)
		return 0;

	/* The root's overflow is the viewport's. */
	if (box->parent == NULL)
		return 0;

	/* So is the body's, when the root leaves its own visible. */
	is_body = 0;
	if (box->node != NULL && box->node->type == DOM_ELEMENT && box->parent->parent == NULL) {
		element = (const struct dom_element *)box->node;
		is_body = dom_element_is(&element->node, DOM_NS_HTML, DOM_TAG_BODY);
	}

	/* The body gives its overflow to the viewport when the root keeps visible. */
	if (is_body &&
	    box->parent->style.overflow_x == CSS_OVERFLOW_VISIBLE &&
	    box->parent->style.overflow_y == CSS_OVERFLOW_VISIBLE)
		return 0;

	/* Any other box clips. */
	return 1;
}

/*
 * Picks the font a style draws text with.
 */
void
layout_font_of(
	struct layout_tree *tree,
	const struct css_style *style,
	struct text_font *font)
{
	int monospace;

	/* The monospace family uses the monospace face. */
	monospace = 0;
	if (style->generic_family == CSS_FAMILY_MONOSPACE)
		monospace = 1;
	text_select_font(tree->text, monospace, style->font_size, style->font_weight, font);
}

/* Builds the box of an element (none for display: none) and of its children. */
static int
box_build_element(
	struct layout_tree *tree,
	struct css_engine *css,
	struct dom_element *element,
	const struct css_style *parent_style,
	struct layout_box *parent,
	int depth)
{
	struct css_style *style;
	struct layout_box *box;
	int out_of_flow;
	int floating;
	int kind;
	int error;

	/* Boxes past the depth limit are dropped. */
	if (depth > LAYOUT_DEPTH_MAX)
		return 0;

	/* Computes the style into a temporary (the box keeps its own copy). */
	style = malloc(sizeof(*style));
	if (style == NULL)
		return ENOMEM;
	error = css_engine_compute(css, element, parent_style, style);
	if (error != 0) {
		free(style);
		return error;
	}

	/* display: none makes no box, for the element or its children. */
	if (style->display == CSS_DISPLAY_NONE) {
		free(style);
		return 0;
	}

	/* display: contents makes no box, but its children go to the parent. */
	if (style->display == CSS_DISPLAY_CONTENTS && parent != NULL) {
		error = box_build_children(tree, css, &element->node, style, parent, depth + 1);
		free(style);
		return error;
	}

	/* The kind of box: blocks for every block-level display in this pass, inline otherwise. */
	kind = LAYOUT_BLOCK;
	if (style->display == CSS_DISPLAY_INLINE)
		kind = LAYOUT_INLINE;
	if (element->ns == DOM_NS_HTML && element->tag == DOM_TAG_BR)
		kind = LAYOUT_LINE_BREAK;
	if (parent == NULL)
		kind = LAYOUT_BLOCK;

	/* An absolutely positioned or fixed box is a block out of the flow (the root stays in it). */
	out_of_flow = 0;
	if (parent != NULL && (style->position == CSS_POSITION_ABSOLUTE || style->position == CSS_POSITION_FIXED)) {
		kind = LAYOUT_BLOCK;
		out_of_flow = 1;
	}

	/* A float that is not out of the flow is a block beside the flow. */
	floating = CSS_FLOAT_NONE;
	if (parent != NULL && !out_of_flow && style->float_side != CSS_FLOAT_NONE) {
		kind = LAYOUT_BLOCK;
		floating = style->float_side;
	}

	/* Makes the box and places it in the tree. */
	box = box_new(tree, kind, &element->node, style);
	free(style);
	if (box == NULL)
		return ENOMEM;
	box->out_of_flow = out_of_flow;
	box->floating = floating;
	if (parent == NULL) {
		tree->root = box;
	} else {
		box_append(parent, box);
	}

	/* The children, then the fix-ups a block needs. */
	error = box_build_children(tree, css, &element->node, &box->style, box, depth + 1);
	if (error != 0)
		return error;
	error = box_fix_children(tree, box);
	if (error != 0)
		return error;

	/* Succeeded: the element's boxes are built. */
	return 0;
}

/* Builds the boxes of a node's children into a box. */
static int
box_build_children(
	struct layout_tree *tree,
	struct css_engine *css,
	struct dom_node *node,
	const struct css_style *style,
	struct layout_box *box,
	int depth)
{
	const struct dom_character_data *text;
	struct dom_node *child;
	struct layout_box *text_box;
	int ordinal;
	int error;

	/* Each child: an element's boxes, or a text box. */
	ordinal = 0;
	for (child = node->first_child; child != NULL; child = child->next) {
		/* Elements build their own boxes; list items number themselves. */
		if (child->type == DOM_ELEMENT) {
			error = box_build_element(tree, css, (struct dom_element *)child, style, box, depth);
			if (error != 0)
				return error;
			if (box->last_child != NULL && box->last_child->node == child && box->last_child->style.display == CSS_DISPLAY_LIST_ITEM) {
				ordinal++;
				box_marker(box->last_child, ordinal);
			}

			continue;
		}

		/* Only text nodes are rendered among the others. */
		if (child->type != DOM_TEXT)
			continue;
		text = (const struct dom_character_data *)child;
		if (text->data.length == 0)
			continue;

		/* A text box takes its element's style. */
		text_box = box_new(tree, LAYOUT_TEXT, child, style);
		if (text_box == NULL)
			return ENOMEM;
		text_box->text = text->data.data;
		text_box->text_length = text->data.length;
		box_append(box, text_box);
	}

	/* Succeeded: the children are built. */
	return 0;
}

/* Allocates a box in the tree's arena. */
static struct layout_box *
box_new(
	struct layout_tree *tree,
	int kind,
	struct dom_node *node,
	const struct css_style *style)
{
	struct layout_box *box;

	/* Allocates it zeroed. */
	box = wb_arena_zalloc(&tree->arena, sizeof(*box));
	if (box == NULL)
		return NULL;

	/* Records its kind, node and style. */
	box->kind = kind;
	box->node = node;
	box->style = *style;

	/* Succeeded: the box is detached. */
	return box;
}

/* Appends a box as the last child of another. */
static void
box_append(
	struct layout_box *parent,
	struct layout_box *child)
{
	/* Links it at the end. */
	child->parent = parent;
	if (parent->last_child != NULL) {
		parent->last_child->next = child;
	} else {
		parent->first_child = child;
	}

	/* The child is the parent's last one now. */
	parent->last_child = child;
}

/* Tells whether a box takes part in inline formatting. */
static int
box_is_inline_level(
	const struct layout_box *box)
{
	/* Inline boxes, text and line breaks. */
	if (box->kind == LAYOUT_INLINE || box->kind == LAYOUT_TEXT || box->kind == LAYOUT_LINE_BREAK)
		return 1;

	/* Blocks are block-level. */
	return 0;
}

/* Tells whether a box is text of nothing but collapsible whitespace. */
static int
box_is_whitespace(
	const struct layout_box *box)
{
	size_t index;
	int space;

	/* Only text can be whitespace, and only where whitespace collapses. */
	if (box->kind != LAYOUT_TEXT)
		return 0;
	if (box->style.white_space == CSS_WHITE_SPACE_PRE || box->style.white_space == CSS_WHITE_SPACE_PRE_WRAP)
		return 0;

	/* Every character must be a space, tab or line feed. */
	for (index = 0; index < box->text_length; index++) {
		space = text_is_space(box->text[index]);
		if (!space)
			return 0;
	}

	/* The text is all whitespace. */
	return 1;
}

/*
 * Fixes a box's children after they are built: an inline box with block
 * children becomes a block, and a block with both kinds of children wraps
 * each run of inline children in an anonymous block.
 */
static int
box_fix_children(
	struct layout_tree *tree,
	struct layout_box *box)
{
	struct layout_box *child;
	struct layout_box *next;
	struct layout_box *anonymous;
	struct css_style style;
	int has_block;
	int has_inline;
	int inline_level;
	int whitespace;

	/* Looks at the kinds of children (those out of the flow and floats count as neither). */
	has_block = 0;
	has_inline = 0;
	for (child = box->first_child; child != NULL; child = child->next) {
		if (child->out_of_flow || child->floating != CSS_FLOAT_NONE)
			continue;
		inline_level = box_is_inline_level(child);
		whitespace = box_is_whitespace(child);
		if (!inline_level)
			has_block = 1;
		if (inline_level && !whitespace)
			has_inline = 1;
	}

	/* An inline box holding blocks is laid out as a block in this pass. */
	if (box->kind == LAYOUT_INLINE && has_block)
		box->kind = LAYOUT_BLOCK;

	/* Only inline children: the box lays them out in lines. */
	if (!has_block) {
		box->children_inline = 1;
		return 0;
	}

	/* Only block children (and whitespace between them): the whitespace goes. */
	box_anonymous_style(&box->style, &style);
	child = box->first_child;
	box->first_child = NULL;
	box->last_child = NULL;
	anonymous = NULL;
	while (child != NULL) {
		next = child->next;
		child->next = NULL;
		inline_level = box_is_inline_level(child);

		/* A box out of the flow or a float stays where it is among the others, and ends nothing. */
		if (child->out_of_flow || child->floating != CSS_FLOAT_NONE) {
			if (anonymous != NULL) {
				box_append(anonymous, child);
			} else {
				box_append(box, child);
			}

			/* On to the next child. */
			child = next;
			continue;
		}

		/* A block ends the anonymous block before it. */
		if (!inline_level) {
			anonymous = NULL;
			box_append(box, child);
			child = next;
			continue;
		}

		/* Whitespace between blocks is dropped; other inline content goes into an anonymous block. */
		whitespace = box_is_whitespace(child);
		if (anonymous == NULL && (whitespace || !has_inline)) {
			child = next;
			continue;
		}

		/* Inline content after a block starts a new anonymous block. */
		if (anonymous == NULL) {
			anonymous = box_new(tree, LAYOUT_ANONYMOUS_BLOCK, NULL, &style);
			if (anonymous == NULL)
				return ENOMEM;
			anonymous->children_inline = 1;
			box_append(box, anonymous);
		}

		/* The inline content goes into the anonymous block. */
		box_append(anonymous, child);
		child = next;
	}

	/* Succeeded: the children are all blocks. */
	return 0;
}

/* Makes the style of an anonymous block: the parent's inherited properties, initial values otherwise. */
static void
box_anonymous_style(
	const struct css_style *parent,
	struct css_style *style)
{
	/* The initial values, then what inherits. */
	css_initial_style(style);
	style->display = CSS_DISPLAY_BLOCK;
	style->color = parent->color;
	style->font_size = parent->font_size;
	style->font_size_keyword = parent->font_size_keyword;
	style->font_weight = parent->font_weight;
	style->font_italic = parent->font_italic;
	style->generic_family = parent->generic_family;
	memcpy(style->families, parent->families, sizeof(style->families));
	style->family_count = parent->family_count;
	style->line_height = parent->line_height;
	style->text_align = parent->text_align;
	style->white_space = parent->white_space;
	style->visibility = parent->visibility;
	style->underline = parent->underline;
}

/* Gives a list item its marker: a bullet, or its number and a period. */
static void
box_marker(
	struct layout_box *item,
	int ordinal)
{
	struct layout_box *box;
	struct layout_box *child;
	char digits[16];
	size_t length;
	size_t index;

	/*
	 * The marker goes on the first line of the item: its own when its
	 * children are inline, otherwise that of its first block (anonymous or
	 * not) with lines, as long as that block is not a list item itself.
	 */
	box = item;
	while (!box->children_inline && box->first_child != NULL) {
		child = box->first_child;

		/* A nested list item keeps its own marker. */
		if (child->style.display == CSS_DISPLAY_LIST_ITEM)
			break;

		/* The marker moves one block down. */
		box = child;
	}

	/* A box that ends up without lines cannot show the marker; the item keeps it. */
	if (!box->children_inline)
		box = item;

	/* The bullets are one character. */
	box->marker_length = 0;
	switch (item->style.list_style) {
	case CSS_LIST_DISC:
		box->marker[0] = BOX_BULLET_DISC;
		box->marker_length = 1;
		break;
	case CSS_LIST_CIRCLE:
		box->marker[0] = BOX_BULLET_CIRCLE;
		box->marker_length = 1;
		break;
	case CSS_LIST_SQUARE:
		box->marker[0] = BOX_BULLET_SQUARE;
		box->marker_length = 1;
		break;
	case CSS_LIST_DECIMAL:
		/* The number and a period. */
		length = 0;
		while (ordinal > 0 && length < 6) {
			digits[length] = (char)('0' + ordinal % 10);
			ordinal /= 10;
			length++;
		}

		/* The digits were gathered from the lowest; the marker reads from the highest. */
		for (index = 0; index < length; index++)
			box->marker[index] = (uint16_t)digits[length - 1U - index];
		box->marker[length] = '.';
		box->marker_length = length + 1U;
		break;
	default:
		break;
	}
}

/* Resolves a relatively positioned box's offsets: left (or minus right), top (or minus bottom); zero otherwise. */
static void
box_relative_offset(
	const struct layout_box *box,
	layout_unit *dx,
	layout_unit *dy)
{
	const struct css_length *offset;
	layout_unit width;

	/* Only a relative box moves. */
	*dx = 0;
	*dy = 0;
	if (box->style.position != CSS_POSITION_RELATIVE)
		return;

	/* Percentages are of the containing block's width (and the height's are left at zero in this pass). */
	width = 0;
	if (box->parent != NULL)
		width = box->parent->width;
	offset = box->style.offset;

	/* Horizontally: left wins over right. */
	if (offset[CSS_LEFT].unit == CSS_UNIT_PX) {
		*dx = layout_from_px(offset[CSS_LEFT].value);
	} else if (offset[CSS_LEFT].unit == CSS_UNIT_PERCENT) {
		*dx = (layout_unit)((float)width * offset[CSS_LEFT].value / 100.0f);
	} else if (offset[CSS_RIGHT].unit == CSS_UNIT_PX) {
		*dx = -layout_from_px(offset[CSS_RIGHT].value);
	} else if (offset[CSS_RIGHT].unit == CSS_UNIT_PERCENT) {
		*dx = -(layout_unit)((float)width * offset[CSS_RIGHT].value / 100.0f);
	}

	/* Vertically: top wins over bottom. */
	if (offset[CSS_TOP].unit == CSS_UNIT_PX) {
		*dy = layout_from_px(offset[CSS_TOP].value);
	} else if (offset[CSS_BOTTOM].unit == CSS_UNIT_PX) {
		*dy = -layout_from_px(offset[CSS_BOTTOM].value);
	}
}

/*
 * Gives the boxes out of the flow inside a block's inline content their
 * static position (the content's top left), and makes the floats there
 * absolute (they were placed relative to the content box).
 */
static void
box_static_inline(
	struct layout_box *box,
	layout_unit x,
	layout_unit y)
{
	struct layout_box *child;

	/* The inline boxes are searched through; a box out of the flow or a float is not entered. */
	for (child = box->first_child; child != NULL; child = child->next) {
		if (child->out_of_flow) {
			child->static_x = x;
			child->static_y = y;
			continue;
		}

		/* A float moves with the content box. */
		if (child->floating != CSS_FLOAT_NONE) {
			layout_absolute(child, x, y);
			continue;
		}

		/* An inline box's content. */
		box_static_inline(child, x, y);
	}
}
