/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * CSS (plan/ws074/design.md §5): style sheets parsed into rules, selectors
 * matched against elements, and the cascade that gives every element its
 * computed style.
 *
 * The first pass covers the common case: type, class, id and attribute
 * selectors with the four combinators, the origins and specificity, the
 * inherited properties, and some thirty properties.  Names are atoms of
 * the document's heap, which live as long as the heap, so the style sheets
 * can keep them without being traced.
 */

#ifndef KEILAND_BROWSER_CSS_H
#define KEILAND_BROWSER_CSS_H

#include "dom/dom.h"

/* The four sides, in the order of the margin and padding shorthands. */
#define CSS_TOP		0
#define CSS_RIGHT	1
#define CSS_BOTTOM	2
#define CSS_LEFT	3

/* How many font families a computed style keeps. */
#define CSS_FAMILIES_MAX	8

/*
 * The units a length can have after the cascade: pixels, a percentage of
 * the containing block (resolved by the layout), or a keyword.
 */
enum css_unit {
	CSS_UNIT_PX,
	CSS_UNIT_PERCENT,
	CSS_UNIT_AUTO,
	CSS_UNIT_NONE,
	CSS_UNIT_NORMAL,
	CSS_UNIT_NUMBER
};

/*
 * A computed length: a value and its unit.
 */
struct css_length {
	float value;
	int unit;
};

/* The values of display. */
enum css_display {
	CSS_DISPLAY_INLINE,
	CSS_DISPLAY_BLOCK,
	CSS_DISPLAY_INLINE_BLOCK,
	CSS_DISPLAY_LIST_ITEM,
	CSS_DISPLAY_NONE,
	CSS_DISPLAY_TABLE,
	CSS_DISPLAY_TABLE_ROW,
	CSS_DISPLAY_TABLE_CELL,
	CSS_DISPLAY_FLEX,
	CSS_DISPLAY_CONTENTS
};

/* The values of position. */
enum css_position {
	CSS_POSITION_STATIC,
	CSS_POSITION_RELATIVE,
	CSS_POSITION_ABSOLUTE,
	CSS_POSITION_FIXED,
	CSS_POSITION_STICKY
};

/* The values of float, and the sides clear clears (both is the two together). */
enum css_float {
	CSS_FLOAT_NONE,
	CSS_FLOAT_LEFT,
	CSS_FLOAT_RIGHT,
	CSS_CLEAR_BOTH
};

/* The values of overflow-x and overflow-y. */
enum css_overflow {
	CSS_OVERFLOW_VISIBLE,
	CSS_OVERFLOW_HIDDEN,
	CSS_OVERFLOW_CLIP,
	CSS_OVERFLOW_SCROLL,
	CSS_OVERFLOW_AUTO
};

/* The values of text-align. */
enum css_text_align {
	CSS_TEXT_ALIGN_START,
	CSS_TEXT_ALIGN_LEFT,
	CSS_TEXT_ALIGN_RIGHT,
	CSS_TEXT_ALIGN_CENTER,
	CSS_TEXT_ALIGN_JUSTIFY
};

/* The values of white-space. */
enum css_white_space {
	CSS_WHITE_SPACE_NORMAL,
	CSS_WHITE_SPACE_PRE,
	CSS_WHITE_SPACE_NOWRAP,
	CSS_WHITE_SPACE_PRE_WRAP,
	CSS_WHITE_SPACE_PRE_LINE
};

/*
 * The values of border-style this pass tells apart (groove and ridge parse
 * as solid; every style is drawn solid, but a form control whose borders
 * are all inset or outset, as the user agent's sheet gives it, is drawn
 * the way the platform draws such a control).
 */
enum css_border_style {
	CSS_BORDER_NONE,
	CSS_BORDER_SOLID,
	CSS_BORDER_DASHED,
	CSS_BORDER_DOTTED,
	CSS_BORDER_DOUBLE,
	CSS_BORDER_INSET,
	CSS_BORDER_OUTSET
};

/* The values of box-sizing: what width and height size. */
enum css_box_sizing {
	CSS_BOX_SIZING_CONTENT,
	CSS_BOX_SIZING_BORDER
};

/* The values of list-style-type. */
enum css_list_style {
	CSS_LIST_DISC,
	CSS_LIST_CIRCLE,
	CSS_LIST_SQUARE,
	CSS_LIST_DECIMAL,
	CSS_LIST_NONE
};

/* The values of background-repeat (the two axes together). */
enum css_background_repeat {
	CSS_REPEAT_BOTH,
	CSS_REPEAT_X,
	CSS_REPEAT_Y,
	CSS_REPEAT_NONE
};

/*
 * The keywords of background-size (a size of lengths is none of them);
 * numbered past the units, as the declared width's keyword carries them.
 */
enum css_background_size {
	CSS_BACKGROUND_SIZE_LENGTHS = 0,
	CSS_BACKGROUND_SIZE_CONTAIN = 100,
	CSS_BACKGROUND_SIZE_COVER = 101
};

/* The generic font families. */
enum css_generic_family {
	CSS_FAMILY_SERIF,
	CSS_FAMILY_SANS_SERIF,
	CSS_FAMILY_MONOSPACE
};

/*
 * The computed style of one element: every property this pass knows, with
 * lengths in pixels where they do not depend on the layout.  Colors are
 * 0xAARRGGBB, not premultiplied.
 */
struct css_style {
	/* The box. */
	int display;
	int position;
	int float_side;
	int clear;
	int overflow_x;
	int overflow_y;
	int visibility;
	struct css_length width;
	struct css_length height;
	struct css_length min_width;
	struct css_length max_width;
	struct css_length min_height;
	struct css_length max_height;
	struct css_length margin[4];
	struct css_length padding[4];
	struct css_length offset[4];
	int box_sizing;
	int z_index;
	int z_index_auto;
	float border_width[4];
	int border_style[4];
	uint32_t border_color[4];

	/* The colors. */
	uint32_t color;
	uint32_t background_color;

	/*
	 * The background image: its URL as the style sheet wrote it (an atom,
	 * which lives as long as the heap; NULL for none), how it repeats,
	 * where it sits (x and y: pixels or a percentage of the room left in the
	 * padding box) and its size (contain, cover, or a width and height, each
	 * pixels, a percentage of the padding box or auto).
	 */
	struct vm_string *background_image;
	int background_repeat;
	struct css_length background_position[2];
	int background_size_keyword;
	struct css_length background_size[2];

	/*
	 * The font.  font_size_keyword is 1 while the size still follows the
	 * keyword scale (medium by default, or an em or a percentage of such a
	 * size), which a change to or from the monospace family rescales.
	 */
	float font_size;
	int font_size_keyword;
	int font_weight;
	int font_italic;
	int generic_family;
	struct vm_string *families[CSS_FAMILIES_MAX];
	int family_count;

	/* The text. */
	struct css_length line_height;
	int text_align;
	int white_space;
	int underline;
	int list_style;
};

/*
 * The style sheets of a document and what the cascade needs: the user
 * agent's sheet and the author's, in document order.
 */
struct css_engine;

/* The engine (cascade.c). */
int css_engine_create(struct css_engine **engine, struct vm_heap *heap);
void css_engine_destroy(struct css_engine *engine);
int css_engine_add_sheet(struct css_engine *engine, const uint16_t *units, size_t length);
int css_engine_add_sheet_origin(struct css_engine *engine, const uint16_t *units, size_t length, int origin);
void css_engine_set_viewport(struct css_engine *engine, float width, float height);
int css_engine_compute(struct css_engine *engine, struct dom_element *element, const struct css_style *parent, struct css_style *style);
void css_initial_style(struct css_style *style);

#endif
