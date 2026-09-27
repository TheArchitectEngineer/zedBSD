/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The text dump of a laid out tree: one line per box with its border box
 * in pixels, and one line per line box and text fragment.  The tests
 * compare it with golden files, and the Chrome comparison with Chrome's
 * element rectangles.
 */

#include "layout/layout.h"

static int dump_box(const struct layout_box *box, int depth, struct wb_buffer *out);
static void dump_indent(struct wb_buffer *out, int depth);
static void dump_lines(const struct layout_box *box, int depth, struct wb_buffer *out);
static int dump_out_of_flow(const struct layout_box *box, int depth, struct wb_buffer *out);

/* The names of the box kinds, in enum layout_box_kind order. */
static const char *const dump_kinds[] = {
	"block",
	"anonymous",
	"inline",
	"text",
	"br"
};

/*
 * Writes the layout tree as text.
 */
int
layout_dump(
	const struct layout_tree *tree,
	struct wb_buffer *out)
{
	int error;

	/* The viewport and the document height, then the boxes. */
	wb_buffer_printf(out, "viewport %.2f x %.2f, document height %.2f\n", (double)layout_to_px(tree->viewport_width),
	    (double)layout_to_px(tree->viewport_height), (double)layout_to_px(tree->document_height));
	if (tree->root == NULL)
		return 0;
	error = dump_box(tree->root, 0, out);
	if (error != 0)
		return error;

	/* Succeeded: the tree is written. */
	return 0;
}

/* Writes a box and, below it, its lines or its children. */
static int
dump_box(
	const struct layout_box *box,
	int depth,
	struct wb_buffer *out)
{
	const struct dom_element *element;
	const struct layout_box *child;
	layout_unit width;
	layout_unit height;
	int error;

	/* Text and inline boxes are shown through the lines of their block. */
	if (box->kind == LAYOUT_TEXT || box->kind == LAYOUT_INLINE || box->kind == LAYOUT_LINE_BREAK)
		return 0;

	/* The box: its kind, its element's name and its border box. */
	dump_indent(out, depth);
	wb_buffer_append_string(out, dump_kinds[box->kind]);
	if (box->node != NULL && box->node->type == DOM_ELEMENT) {
		element = (const struct dom_element *)box->node;
		wb_buffer_append_string(out, " <");
		vm_string_to_utf8(element->local_name, out);
		wb_buffer_append_string(out, ">");
	}

	/* The border box's position and size in pixels. */
	width = box->border[CSS_LEFT] + box->padding[CSS_LEFT] + box->width + box->padding[CSS_RIGHT] + box->border[CSS_RIGHT];
	height = box->border[CSS_TOP] + box->padding[CSS_TOP] + box->height + box->padding[CSS_BOTTOM] + box->border[CSS_BOTTOM];
	wb_buffer_printf(out, " %.2f %.2f %.2f %.2f\n", (double)layout_to_px(box->x), (double)layout_to_px(box->y),
	    (double)layout_to_px(width), (double)layout_to_px(height));

	/* Its lines, and the boxes out of the flow among its inline content; or its children. */
	if (box->children_inline) {
		dump_lines(box, depth + 1, out);
		error = dump_out_of_flow(box, depth + 1, out);
		return error;
	}

	/* The child boxes, one level deeper. */
	for (child = box->first_child; child != NULL; child = child->next) {
		error = dump_box(child, depth + 1, out);
		if (error != 0)
			return error;
	}

	/* Succeeded: the box is written. */
	return 0;
}

/* Writes two spaces a level. */
static void
dump_indent(
	struct wb_buffer *out,
	int depth)
{
	int level;

	/* The indentation. */
	for (level = 0; level < depth; level++)
		wb_buffer_append_string(out, "  ");
}

/* Writes a block's lines and their fragments, in absolute pixels. */
static void
dump_lines(
	const struct layout_box *box,
	int depth,
	struct wb_buffer *out)
{
	const struct layout_line *line;
	const struct layout_fragment *fragment;
	layout_unit left;
	layout_unit top;
	size_t index;
	size_t item;

	/* The content box's origin. */
	left = box->x + box->border[CSS_LEFT] + box->padding[CSS_LEFT];
	top = box->y + box->border[CSS_TOP] + box->padding[CSS_TOP];

	/* Each line, then each fragment on it. */
	for (index = 0; index < box->line_count; index++) {
		line = &box->lines[index];
		dump_indent(out, depth);
		wb_buffer_printf(out, "line %.2f %.2f height %.2f baseline %.2f\n", (double)layout_to_px(left + line->left),
		    (double)layout_to_px(top + line->y), (double)layout_to_px(line->height),
		    (double)layout_to_px(top + line->y + line->baseline));
		for (item = 0; item < line->fragment_count; item++) {
			fragment = &line->fragments[item];
			dump_indent(out, depth + 1);
			wb_buffer_printf(out, "text %.2f w %.2f %upx \"", (double)layout_to_px(left + line->left + fragment->x),
			    (double)layout_to_px(fragment->width), fragment->font.pixels);
			wb_units_to_utf8(fragment->text, fragment->length, out);
			wb_buffer_append_string(out, "\"\n");
		}
	}
}

/* Writes the boxes out of the flow found among a block's inline content, in tree order. */
static int
dump_out_of_flow(
	const struct layout_box *box,
	int depth,
	struct wb_buffer *out)
{
	const struct layout_box *child;
	int error;

	/* Inline boxes are searched through; a box out of the flow is written with its own content. */
	for (child = box->first_child; child != NULL; child = child->next) {
		if (child->out_of_flow) {
			error = dump_box(child, depth, out);
		} else {
			error = dump_out_of_flow(child, depth, out);
		}

		/* A failure stops the dump. */
		if (error != 0)
			return error;
	}

	/* Succeeded: the boxes are written. */
	return 0;
}
