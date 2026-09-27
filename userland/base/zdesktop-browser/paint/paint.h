/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Painting (plan/ws074/design.md §8): the display list made from a laid
 * out page, and the CPU reference renderer that draws it into memory.
 *
 * The display list is the one definition of what a page looks like: the
 * GPU renderer of the window and the CPU renderer of the headless mode and
 * the tests both draw it, with the same coverage rules, so that the two
 * can be compared pixel by pixel.  Positions are the layout's units (1/64
 * of a pixel) in document coordinates; the renderers subtract the scroll.
 *
 * The first pass paints normal flow: the canvas color, each block's
 * background and borders (every style drawn solid), and the text of the
 * lines with its underline.  Rounded corners, images, clipping, opacity,
 * shadows and the stacking order of positioned boxes come later.
 */

#ifndef ZDESKTOP_BROWSER_PAINT_H
#define ZDESKTOP_BROWSER_PAINT_H

#include "layout/layout.h"

/*
 * The kinds of display item.
 */
enum paint_kind {
	PAINT_RECT,
	PAINT_TEXT
};

/*
 * One glyph of a text item: the code point it draws (the text system
 * finds its face and bitmap) and its pen position from the item's origin.
 */
struct paint_glyph {
	uint32_t code_point;
	layout_unit x;
};

/*
 * One display item.
 *
 * A rectangle is filled with a color, its edges covering the pixels they
 * cross in proportion.  A text item draws its glyphs from x along the
 * baseline y in its font and color.
 */
struct paint_item {
	int kind;
	layout_unit x;
	layout_unit y;
	layout_unit width;
	layout_unit height;
	uint32_t color;
	struct text_font font;
	const struct paint_glyph *glyphs;
	size_t glyph_count;
};

/*
 * A display list: the items in painting order, the arena their glyphs
 * live in, the color under everything and the size of the document.
 */
struct paint_list {
	struct wb_arena arena;
	struct wb_vector items;
	uint32_t canvas_color;
	layout_unit width;
	layout_unit height;
};

/*
 * A bitmap the CPU renderer draws into: width by height pixels, each
 * 0xAARRGGBB with the alpha always opaque, rows top to bottom.
 */
struct paint_bitmap {
	uint32_t *pixels;
	int width;
	int height;
};

/* The display list (list.c). */
int paint_build(struct paint_list *list, const struct layout_tree *tree);
void paint_release(struct paint_list *list);
int paint_dump(const struct paint_list *list, struct wb_buffer *out);

/* The CPU reference renderer (software.c). */
int paint_bitmap_create(struct paint_bitmap *bitmap, int width, int height);
void paint_bitmap_release(struct paint_bitmap *bitmap);
int paint_software(const struct paint_list *list, struct text_system *text, layout_unit scroll_y, struct paint_bitmap *bitmap);
int paint_write_ppm(const struct paint_bitmap *bitmap, const char *path);

#endif
