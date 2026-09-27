/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * CSS values: the property table, the parsing of a declaration's value
 * into one or more longhand declarations, colors and lengths.
 */

#include "css/internal.h"

#include <errno.h>
#include <string.h>

/* The shorthands this pass expands (numbered after the longhands). */
enum values_shorthand {
	SHORT_MARGIN = CSS_PROP_COUNT,
	SHORT_PADDING,
	SHORT_BORDER,
	SHORT_BORDER_TOP,
	SHORT_BORDER_RIGHT,
	SHORT_BORDER_BOTTOM,
	SHORT_BORDER_LEFT,
	SHORT_BORDER_WIDTH,
	SHORT_BORDER_STYLE,
	SHORT_BORDER_COLOR,
	SHORT_BACKGROUND,
	SHORT_FONT,
	SHORT_TEXT_DECORATION,
	SHORT_LIST_STYLE
};

/*
 * One property name and its number (a longhand or a shorthand).
 */
struct values_name {
	const char *name;
	int property;
};

/*
 * One keyword of a property and the value it stands for.
 */
struct values_keyword {
	const char *name;
	int value;
};

/*
 * A named color and its RGB value.
 */
struct values_color {
	const char *name;
	uint32_t rgb;
};

static int values_keyword(const struct values_keyword *table, const struct css_token *token, int *value);
static int values_unit(const struct css_token *token, int *unit);
static int values_length(const struct css_token *token, int allow_keywords, struct css_value *value);
static int values_single(struct vm_heap *heap, int property, const struct css_token *tokens, size_t count, struct css_value *value);
static int values_families(struct vm_heap *heap, const struct css_token *tokens, size_t count, struct css_value *value);
static size_t values_components(const struct css_token *tokens, size_t count, size_t *starts, size_t *lengths, size_t max);
static int values_four(struct vm_heap *heap, int first, const struct css_token *tokens, size_t count, struct css_declaration *out, size_t *made);
static int values_border(struct vm_heap *heap, int side, const struct css_token *tokens, size_t count, struct css_declaration *out, size_t *made);
static void values_add(struct css_declaration *out, size_t *made, int property, const struct css_value *value);
static int values_hex_digit(uint16_t unit);
static int values_rgb_function(const struct css_token *tokens, size_t count, uint32_t *color);
static void values_wide_keyword(int property, const struct css_value *value, struct css_declaration *out, size_t *made);
static void values_background(const struct css_token *tokens, size_t count, struct css_declaration *out, size_t *made);

/* The property names this pass knows. */
static const struct values_name values_names[] = {
	{ "display", CSS_PROP_DISPLAY },
	{ "position", CSS_PROP_POSITION },
	{ "float", CSS_PROP_FLOAT },
	{ "visibility", CSS_PROP_VISIBILITY },
	{ "width", CSS_PROP_WIDTH },
	{ "height", CSS_PROP_HEIGHT },
	{ "min-width", CSS_PROP_MIN_WIDTH },
	{ "max-width", CSS_PROP_MAX_WIDTH },
	{ "min-height", CSS_PROP_MIN_HEIGHT },
	{ "max-height", CSS_PROP_MAX_HEIGHT },
	{ "margin-top", CSS_PROP_MARGIN_TOP },
	{ "margin-right", CSS_PROP_MARGIN_RIGHT },
	{ "margin-bottom", CSS_PROP_MARGIN_BOTTOM },
	{ "margin-left", CSS_PROP_MARGIN_LEFT },
	{ "padding-top", CSS_PROP_PADDING_TOP },
	{ "padding-right", CSS_PROP_PADDING_RIGHT },
	{ "padding-bottom", CSS_PROP_PADDING_BOTTOM },
	{ "padding-left", CSS_PROP_PADDING_LEFT },
	{ "border-top-width", CSS_PROP_BORDER_TOP_WIDTH },
	{ "border-right-width", CSS_PROP_BORDER_RIGHT_WIDTH },
	{ "border-bottom-width", CSS_PROP_BORDER_BOTTOM_WIDTH },
	{ "border-left-width", CSS_PROP_BORDER_LEFT_WIDTH },
	{ "border-top-style", CSS_PROP_BORDER_TOP_STYLE },
	{ "border-right-style", CSS_PROP_BORDER_RIGHT_STYLE },
	{ "border-bottom-style", CSS_PROP_BORDER_BOTTOM_STYLE },
	{ "border-left-style", CSS_PROP_BORDER_LEFT_STYLE },
	{ "border-top-color", CSS_PROP_BORDER_TOP_COLOR },
	{ "border-right-color", CSS_PROP_BORDER_RIGHT_COLOR },
	{ "border-bottom-color", CSS_PROP_BORDER_BOTTOM_COLOR },
	{ "border-left-color", CSS_PROP_BORDER_LEFT_COLOR },
	{ "color", CSS_PROP_COLOR },
	{ "background-color", CSS_PROP_BACKGROUND_COLOR },
	{ "font-size", CSS_PROP_FONT_SIZE },
	{ "font-weight", CSS_PROP_FONT_WEIGHT },
	{ "font-style", CSS_PROP_FONT_STYLE },
	{ "font-family", CSS_PROP_FONT_FAMILY },
	{ "line-height", CSS_PROP_LINE_HEIGHT },
	{ "text-align", CSS_PROP_TEXT_ALIGN },
	{ "white-space", CSS_PROP_WHITE_SPACE },
	{ "text-decoration-line", CSS_PROP_TEXT_DECORATION_LINE },
	{ "list-style-type", CSS_PROP_LIST_STYLE_TYPE },
	{ "margin", SHORT_MARGIN },
	{ "padding", SHORT_PADDING },
	{ "border", SHORT_BORDER },
	{ "border-top", SHORT_BORDER_TOP },
	{ "border-right", SHORT_BORDER_RIGHT },
	{ "border-bottom", SHORT_BORDER_BOTTOM },
	{ "border-left", SHORT_BORDER_LEFT },
	{ "border-width", SHORT_BORDER_WIDTH },
	{ "border-style", SHORT_BORDER_STYLE },
	{ "border-color", SHORT_BORDER_COLOR },
	{ "background", SHORT_BACKGROUND },
	{ "font", SHORT_FONT },
	{ "text-decoration", SHORT_TEXT_DECORATION },
	{ "list-style", SHORT_LIST_STYLE },
	{ NULL, 0 }
};

/* The keywords of display. */
static const struct values_keyword values_display[] = {
	{ "inline", CSS_DISPLAY_INLINE },
	{ "block", CSS_DISPLAY_BLOCK },
	{ "inline-block", CSS_DISPLAY_INLINE_BLOCK },
	{ "list-item", CSS_DISPLAY_LIST_ITEM },
	{ "none", CSS_DISPLAY_NONE },
	{ "table", CSS_DISPLAY_TABLE },
	{ "inline-table", CSS_DISPLAY_TABLE },
	{ "table-row", CSS_DISPLAY_TABLE_ROW },
	{ "table-cell", CSS_DISPLAY_TABLE_CELL },
	{ "table-row-group", CSS_DISPLAY_BLOCK },
	{ "table-header-group", CSS_DISPLAY_BLOCK },
	{ "table-footer-group", CSS_DISPLAY_BLOCK },
	{ "table-caption", CSS_DISPLAY_BLOCK },
	{ "flex", CSS_DISPLAY_FLEX },
	{ "inline-flex", CSS_DISPLAY_FLEX },
	{ "grid", CSS_DISPLAY_BLOCK },
	{ "inline-grid", CSS_DISPLAY_BLOCK },
	{ "flow-root", CSS_DISPLAY_BLOCK },
	{ "contents", CSS_DISPLAY_CONTENTS },
	{ NULL, 0 }
};

/* The keywords of position (only static is laid out in this pass). */
static const struct values_keyword values_position[] = {
	{ "static", 0 },
	{ "relative", 1 },
	{ "absolute", 2 },
	{ "fixed", 3 },
	{ "sticky", 4 },
	{ NULL, 0 }
};

/* The keywords of float. */
static const struct values_keyword values_float[] = {
	{ "none", 0 },
	{ "left", 1 },
	{ "right", 2 },
	{ NULL, 0 }
};

/* The keywords of visibility. */
static const struct values_keyword values_visibility[] = {
	{ "visible", 0 },
	{ "hidden", 1 },
	{ "collapse", 1 },
	{ NULL, 0 }
};

/* The keywords of border-style. */
static const struct values_keyword values_border_style[] = {
	{ "none", CSS_BORDER_NONE },
	{ "hidden", CSS_BORDER_NONE },
	{ "solid", CSS_BORDER_SOLID },
	{ "dashed", CSS_BORDER_DASHED },
	{ "dotted", CSS_BORDER_DOTTED },
	{ "double", CSS_BORDER_DOUBLE },
	{ "groove", CSS_BORDER_SOLID },
	{ "ridge", CSS_BORDER_SOLID },
	{ "inset", CSS_BORDER_SOLID },
	{ "outset", CSS_BORDER_SOLID },
	{ NULL, 0 }
};

/* The keywords of border widths, in pixels. */
static const struct values_keyword values_border_width[] = {
	{ "thin", 1 },
	{ "medium", 3 },
	{ "thick", 5 },
	{ NULL, 0 }
};

/* The keywords of font-weight. */
static const struct values_keyword values_font_weight[] = {
	{ "normal", 400 },
	{ "bold", 700 },
	{ "bolder", 700 },
	{ "lighter", 300 },
	{ NULL, 0 }
};

/* The keywords of font-style. */
static const struct values_keyword values_font_style[] = {
	{ "normal", 0 },
	{ "italic", 1 },
	{ "oblique", 1 },
	{ NULL, 0 }
};

/* The keywords of font-size, in pixels at the default size of 16. */
static const struct values_keyword values_font_size[] = {
	{ "xx-small", 9 },
	{ "x-small", 10 },
	{ "small", 13 },
	{ "medium", 16 },
	{ "large", 18 },
	{ "x-large", 24 },
	{ "xx-large", 32 },
	{ "xxx-large", 48 },
	{ NULL, 0 }
};

/* The keywords of text-align. */
static const struct values_keyword values_text_align[] = {
	{ "start", CSS_TEXT_ALIGN_START },
	{ "left", CSS_TEXT_ALIGN_LEFT },
	{ "right", CSS_TEXT_ALIGN_RIGHT },
	{ "center", CSS_TEXT_ALIGN_CENTER },
	{ "justify", CSS_TEXT_ALIGN_JUSTIFY },
	{ "end", CSS_TEXT_ALIGN_RIGHT },
	{ "-webkit-center", CSS_TEXT_ALIGN_CENTER },
	{ NULL, 0 }
};

/* The keywords of white-space. */
static const struct values_keyword values_white_space[] = {
	{ "normal", CSS_WHITE_SPACE_NORMAL },
	{ "pre", CSS_WHITE_SPACE_PRE },
	{ "nowrap", CSS_WHITE_SPACE_NOWRAP },
	{ "pre-wrap", CSS_WHITE_SPACE_PRE_WRAP },
	{ "pre-line", CSS_WHITE_SPACE_PRE_LINE },
	{ NULL, 0 }
};

/* The keywords of text-decoration-line (only underline is drawn in this pass). */
static const struct values_keyword values_decoration[] = {
	{ "none", 0 },
	{ "underline", 1 },
	{ "overline", 0 },
	{ "line-through", 0 },
	{ NULL, 0 }
};

/* The keywords of list-style-type. */
static const struct values_keyword values_list_style[] = {
	{ "disc", CSS_LIST_DISC },
	{ "circle", CSS_LIST_CIRCLE },
	{ "square", CSS_LIST_SQUARE },
	{ "decimal", CSS_LIST_DECIMAL },
	{ "none", CSS_LIST_NONE },
	{ NULL, 0 }
};

/* The CSS-wide keywords. */
static const struct values_keyword values_wide[] = {
	{ "inherit", CSS_VALUE_INHERIT },
	{ "initial", CSS_VALUE_INITIAL },
	{ "unset", CSS_VALUE_UNSET },
	{ "revert", CSS_VALUE_UNSET },
	{ "revert-layer", CSS_VALUE_UNSET },
	{ NULL, 0 }
};

/* The keywords a length property may take instead of a length. */
static const struct values_keyword values_length_keywords[] = {
	{ "auto", CSS_UNIT_AUTO },
	{ "none", CSS_UNIT_NONE },
	{ "normal", CSS_UNIT_NORMAL },
	{ NULL, 0 }
};

/* The units of lengths. */
static const struct values_keyword values_units[] = {
	{ "px", CSS_DUNIT_PX },
	{ "em", CSS_DUNIT_EM },
	{ "rem", CSS_DUNIT_REM },
	{ "ex", CSS_DUNIT_EX },
	{ "ch", CSS_DUNIT_EX },
	{ "pt", CSS_DUNIT_PT },
	{ "pc", CSS_DUNIT_PC },
	{ "in", CSS_DUNIT_IN },
	{ "cm", CSS_DUNIT_CM },
	{ "mm", CSS_DUNIT_MM },
	{ "vw", CSS_DUNIT_VW },
	{ "vh", CSS_DUNIT_VH },
	{ NULL, 0 }
};

/* The relative font sizes, as percentages of the parent's. */
static const struct values_keyword values_relative_size[] = {
	{ "smaller", 83 },
	{ "larger", 120 },
	{ NULL, 0 }
};

/* The words of the font shorthand this pass reads past. */
static const struct values_keyword values_font_ignored[] = {
	{ "normal", 0 },
	{ "small-caps", 0 },
	{ NULL, 0 }
};

/* The color keywords that are not named colors (CSS_CURRENT_COLOR stands for currentcolor). */
static const struct values_keyword values_color_keywords[] = {
	{ "transparent", 0 },
	{ "currentcolor", (int)CSS_CURRENT_COLOR },
	{ NULL, 0 }
};

/* The named colors of CSS Color 4. */
static const struct values_color values_colors[] = {
	{ "aliceblue", 0xf0f8ffU },
	{ "antiquewhite", 0xfaebd7U },
	{ "aqua", 0x00ffffU },
	{ "aquamarine", 0x7fffd4U },
	{ "azure", 0xf0ffffU },
	{ "beige", 0xf5f5dcU },
	{ "bisque", 0xffe4c4U },
	{ "black", 0x000000U },
	{ "blanchedalmond", 0xffebcdU },
	{ "blue", 0x0000ffU },
	{ "blueviolet", 0x8a2be2U },
	{ "brown", 0xa52a2aU },
	{ "burlywood", 0xdeb887U },
	{ "cadetblue", 0x5f9ea0U },
	{ "chartreuse", 0x7fff00U },
	{ "chocolate", 0xd2691eU },
	{ "coral", 0xff7f50U },
	{ "cornflowerblue", 0x6495edU },
	{ "cornsilk", 0xfff8dcU },
	{ "crimson", 0xdc143cU },
	{ "cyan", 0x00ffffU },
	{ "darkblue", 0x00008bU },
	{ "darkcyan", 0x008b8bU },
	{ "darkgoldenrod", 0xb8860bU },
	{ "darkgray", 0xa9a9a9U },
	{ "darkgreen", 0x006400U },
	{ "darkgrey", 0xa9a9a9U },
	{ "darkkhaki", 0xbdb76bU },
	{ "darkmagenta", 0x8b008bU },
	{ "darkolivegreen", 0x556b2fU },
	{ "darkorange", 0xff8c00U },
	{ "darkorchid", 0x9932ccU },
	{ "darkred", 0x8b0000U },
	{ "darksalmon", 0xe9967aU },
	{ "darkseagreen", 0x8fbc8fU },
	{ "darkslateblue", 0x483d8bU },
	{ "darkslategray", 0x2f4f4fU },
	{ "darkslategrey", 0x2f4f4fU },
	{ "darkturquoise", 0x00ced1U },
	{ "darkviolet", 0x9400d3U },
	{ "deeppink", 0xff1493U },
	{ "deepskyblue", 0x00bfffU },
	{ "dimgray", 0x696969U },
	{ "dimgrey", 0x696969U },
	{ "dodgerblue", 0x1e90ffU },
	{ "firebrick", 0xb22222U },
	{ "floralwhite", 0xfffaf0U },
	{ "forestgreen", 0x228b22U },
	{ "fuchsia", 0xff00ffU },
	{ "gainsboro", 0xdcdcdcU },
	{ "ghostwhite", 0xf8f8ffU },
	{ "gold", 0xffd700U },
	{ "goldenrod", 0xdaa520U },
	{ "gray", 0x808080U },
	{ "green", 0x008000U },
	{ "greenyellow", 0xadff2fU },
	{ "grey", 0x808080U },
	{ "honeydew", 0xf0fff0U },
	{ "hotpink", 0xff69b4U },
	{ "indianred", 0xcd5c5cU },
	{ "indigo", 0x4b0082U },
	{ "ivory", 0xfffff0U },
	{ "khaki", 0xf0e68cU },
	{ "lavender", 0xe6e6faU },
	{ "lavenderblush", 0xfff0f5U },
	{ "lawngreen", 0x7cfc00U },
	{ "lemonchiffon", 0xfffacdU },
	{ "lightblue", 0xadd8e6U },
	{ "lightcoral", 0xf08080U },
	{ "lightcyan", 0xe0ffffU },
	{ "lightgoldenrodyellow", 0xfafad2U },
	{ "lightgray", 0xd3d3d3U },
	{ "lightgreen", 0x90ee90U },
	{ "lightgrey", 0xd3d3d3U },
	{ "lightpink", 0xffb6c1U },
	{ "lightsalmon", 0xffa07aU },
	{ "lightseagreen", 0x20b2aaU },
	{ "lightskyblue", 0x87cefaU },
	{ "lightslategray", 0x778899U },
	{ "lightslategrey", 0x778899U },
	{ "lightsteelblue", 0xb0c4deU },
	{ "lightyellow", 0xffffe0U },
	{ "lime", 0x00ff00U },
	{ "limegreen", 0x32cd32U },
	{ "linen", 0xfaf0e6U },
	{ "magenta", 0xff00ffU },
	{ "maroon", 0x800000U },
	{ "mediumaquamarine", 0x66cdaaU },
	{ "mediumblue", 0x0000cdU },
	{ "mediumorchid", 0xba55d3U },
	{ "mediumpurple", 0x9370dbU },
	{ "mediumseagreen", 0x3cb371U },
	{ "mediumslateblue", 0x7b68eeU },
	{ "mediumspringgreen", 0x00fa9aU },
	{ "mediumturquoise", 0x48d1ccU },
	{ "mediumvioletred", 0xc71585U },
	{ "midnightblue", 0x191970U },
	{ "mintcream", 0xf5fffaU },
	{ "mistyrose", 0xffe4e1U },
	{ "moccasin", 0xffe4b5U },
	{ "navajowhite", 0xffdeadU },
	{ "navy", 0x000080U },
	{ "oldlace", 0xfdf5e6U },
	{ "olive", 0x808000U },
	{ "olivedrab", 0x6b8e23U },
	{ "orange", 0xffa500U },
	{ "orangered", 0xff4500U },
	{ "orchid", 0xda70d6U },
	{ "palegoldenrod", 0xeee8aaU },
	{ "palegreen", 0x98fb98U },
	{ "paleturquoise", 0xafeeeeU },
	{ "palevioletred", 0xdb7093U },
	{ "papayawhip", 0xffefd5U },
	{ "peachpuff", 0xffdab9U },
	{ "peru", 0xcd853fU },
	{ "pink", 0xffc0cbU },
	{ "plum", 0xdda0ddU },
	{ "powderblue", 0xb0e0e6U },
	{ "purple", 0x800080U },
	{ "rebeccapurple", 0x663399U },
	{ "red", 0xff0000U },
	{ "rosybrown", 0xbc8f8fU },
	{ "royalblue", 0x4169e1U },
	{ "saddlebrown", 0x8b4513U },
	{ "salmon", 0xfa8072U },
	{ "sandybrown", 0xf4a460U },
	{ "seagreen", 0x2e8b57U },
	{ "seashell", 0xfff5eeU },
	{ "sienna", 0xa0522dU },
	{ "silver", 0xc0c0c0U },
	{ "skyblue", 0x87ceebU },
	{ "slateblue", 0x6a5acdU },
	{ "slategray", 0x708090U },
	{ "slategrey", 0x708090U },
	{ "snow", 0xfffafaU },
	{ "springgreen", 0x00ff7fU },
	{ "steelblue", 0x4682b4U },
	{ "tan", 0xd2b48cU },
	{ "teal", 0x008080U },
	{ "thistle", 0xd8bfd8U },
	{ "tomato", 0xff6347U },
	{ "turquoise", 0x40e0d0U },
	{ "violet", 0xee82eeU },
	{ "wheat", 0xf5deb3U },
	{ "white", 0xffffffU },
	{ "whitesmoke", 0xf5f5f5U },
	{ "yellow", 0xffff00U },
	{ "yellowgreen", 0x9acd32U },
	{ NULL, 0 }
};

/*
 * Finds a property's number by its name, or -1 for an unknown property.
 */
int
css_property_lookup(
	const struct css_token *name)
{
	size_t index;
	int same;

	/* Compares with each known name. */
	for (index = 0; values_names[index].name != NULL; index++) {
		same = css_ident_equal(name, values_names[index].name);
		if (same)
			return values_names[index].property;
	}

	/* The property is not one this pass knows. */
	return -1;
}

/*
 * Parses a declaration's value into longhand declarations.
 *
 * Returns EINVAL for an unknown property or an invalid value (the
 * declaration is then dropped) and ENOMEM when memory runs out.
 */
int
css_parse_value(
	struct vm_heap *heap,
	const struct css_token *tokens,
	size_t count,
	const struct css_token *name,
	struct css_declaration *out,
	size_t *out_count,
	size_t out_capacity)
{
	struct css_value value;
	int property;
	int error;
	int first;
	int wide;

	UNUSED_PARAMETER(out_capacity);

	/* Finds the property. */
	*out_count = 0;
	property = css_property_lookup(name);
	if (property < 0 || count == 0)
		return EINVAL;

	/* The CSS-wide keywords apply to every longhand a shorthand stands for. */
	memset(&value, 0, sizeof(value));
	wide = values_keyword(values_wide, &tokens[0], &value.kind);
	if (count == 1 && wide) {
		values_wide_keyword(property, &value, out, out_count);
		return 0;
	}

	/* The shorthands. */
	switch (property) {
	case SHORT_MARGIN:
		return values_four(heap, CSS_PROP_MARGIN_TOP, tokens, count, out, out_count);
	case SHORT_PADDING:
		return values_four(heap, CSS_PROP_PADDING_TOP, tokens, count, out, out_count);
	case SHORT_BORDER_WIDTH:
		return values_four(heap, CSS_PROP_BORDER_TOP_WIDTH, tokens, count, out, out_count);
	case SHORT_BORDER_STYLE:
		return values_four(heap, CSS_PROP_BORDER_TOP_STYLE, tokens, count, out, out_count);
	case SHORT_BORDER_COLOR:
		return values_four(heap, CSS_PROP_BORDER_TOP_COLOR, tokens, count, out, out_count);
	case SHORT_BORDER:
		return values_border(heap, -1, tokens, count, out, out_count);
	case SHORT_BORDER_TOP:
		return values_border(heap, CSS_TOP, tokens, count, out, out_count);
	case SHORT_BORDER_RIGHT:
		return values_border(heap, CSS_RIGHT, tokens, count, out, out_count);
	case SHORT_BORDER_BOTTOM:
		return values_border(heap, CSS_BOTTOM, tokens, count, out, out_count);
	case SHORT_BORDER_LEFT:
		return values_border(heap, CSS_LEFT, tokens, count, out, out_count);
	case SHORT_BACKGROUND:
		values_background(tokens, count, out, out_count);
		return 0;
	case SHORT_TEXT_DECORATION:
		return css_parse_value_as(heap, CSS_PROP_TEXT_DECORATION_LINE, tokens, 1, out, out_count);
	case SHORT_LIST_STYLE:
		for (first = 0; first < (int)count; first++) {
			error = values_single(heap, CSS_PROP_LIST_STYLE_TYPE, &tokens[first], 1, &value);
			if (error == 0) {
				values_add(out, out_count, CSS_PROP_LIST_STYLE_TYPE, &value);
				return 0;
			}
		}

		/* A list style without a type is not used in this pass. */
		return EINVAL;
	case SHORT_FONT:
		return css_parse_font(heap, tokens, count, out, out_count);
	default:
		break;
	}

	/* A longhand. */
	error = values_single(heap, property, tokens, count, &value);
	if (error != 0)
		return error;
	values_add(out, out_count, property, &value);

	/* Succeeded: one declaration. */
	return 0;
}

/*
 * Parses a longhand's value from tokens as a given property (used by the
 * shorthands).
 */
int
css_parse_value_as(
	struct vm_heap *heap,
	int property,
	const struct css_token *tokens,
	size_t count,
	struct css_declaration *out,
	size_t *out_count)
{
	struct css_value value;
	int error;

	/* Parses the value and adds the declaration. */
	memset(&value, 0, sizeof(value));
	error = values_single(heap, property, tokens, count, &value);
	if (error != 0)
		return error;
	values_add(out, out_count, property, &value);

	/* Succeeded: one declaration. */
	return 0;
}

/*
 * Parses the font shorthand: [style] [weight] size[/line-height] family.
 */
int
css_parse_font(
	struct vm_heap *heap,
	const struct css_token *tokens,
	size_t count,
	struct css_declaration *out,
	size_t *out_count)
{
	struct css_value value;
	size_t index;
	int keyword;
	int found;
	int error;

	/* Style and weight keywords come before the size. */
	index = 0;
	for (;;) {
		while (index < count && tokens[index].type == CSS_TOKEN_WHITESPACE)
			index++;
		if (index >= count)
			return EINVAL;
		memset(&value, 0, sizeof(value));
		found = values_keyword(values_font_style, &tokens[index], &keyword);
		if (found && keyword != 0) {
			value.kind = CSS_VALUE_KEYWORD;
			value.keyword = keyword;
			values_add(out, out_count, CSS_PROP_FONT_STYLE, &value);
			index++;
			continue;
		}

		/* A weight: a keyword or a number from 1 to 1000. */
		found = values_keyword(values_font_weight, &tokens[index], &keyword);
		if (!found && tokens[index].type == CSS_TOKEN_NUMBER && tokens[index].number >= 1 && tokens[index].number <= 1000) {
			found = 1;
			keyword = (int)tokens[index].number;
		}

		/* The weight is declared. */
		if (found) {
			value.kind = CSS_VALUE_KEYWORD;
			value.keyword = keyword;
			values_add(out, out_count, CSS_PROP_FONT_WEIGHT, &value);
			index++;
			continue;
		}

		/* The variants this pass does not draw are read past. */
		found = values_keyword(values_font_ignored, &tokens[index], &keyword);
		if (found) {
			index++;
			continue;
		}

		/* Anything else is the size. */
		break;
	}

	/* The size, and an optional line height after a slash. */
	error = css_parse_value_as(heap, CSS_PROP_FONT_SIZE, &tokens[index], 1, out, out_count);
	if (error != 0)
		return error;
	index++;
	if (index < count && tokens[index].type == CSS_TOKEN_DELIM && tokens[index].delim == '/') {
		index++;
		while (index < count && tokens[index].type == CSS_TOKEN_WHITESPACE)
			index++;
		if (index >= count)
			return EINVAL;
		error = css_parse_value_as(heap, CSS_PROP_LINE_HEIGHT, &tokens[index], 1, out, out_count);
		if (error != 0)
			return error;
		index++;
	}

	/* The families take the rest. */
	while (index < count && tokens[index].type == CSS_TOKEN_WHITESPACE)
		index++;
	if (index >= count)
		return EINVAL;
	error = css_parse_value_as(heap, CSS_PROP_FONT_FAMILY, &tokens[index], count - index, out, out_count);
	if (error != 0)
		return error;

	/* Succeeded: the longhands are declared. */
	return 0;
}

/*
 * Parses a color: a named color, transparent, currentcolor, #rgb, #rgba,
 * #rrggbb, #rrggbbaa, rgb() or rgba().
 *
 * currentcolor is reported as 0x00000001, a value no real color has with
 * zero alpha, which the cascade replaces with the color property.
 */
int
css_parse_color(
	const struct css_token *tokens,
	size_t count,
	uint32_t *color)
{
	const struct css_token *token;
	uint32_t value;
	size_t index;
	int keyword;
	int digit;
	int same;

	/* Skips leading whitespace. */
	while (count > 0 && tokens[0].type == CSS_TOKEN_WHITESPACE) {
		tokens++;
		count--;
	}

	/* Nothing is not a color. */
	if (count == 0)
		return EINVAL;
	token = &tokens[0];

	/* Keywords and named colors. */
	if (token->type == CSS_TOKEN_IDENT) {
		same = values_keyword(values_color_keywords, token, &keyword);
		if (same) {
			*color = (uint32_t)keyword;
			return 0;
		}

		/* The named colors. */
		for (index = 0; values_colors[index].name != NULL; index++) {
			same = css_ident_equal(token, values_colors[index].name);
			if (same) {
				*color = 0xff000000U | values_colors[index].rgb;
				return 0;
			}
		}

		/* An unknown word is not a color. */
		return EINVAL;
	}

	/* Hex colors of 3, 4, 6 or 8 digits. */
	if (token->type == CSS_TOKEN_HASH) {
		value = 0;
		for (index = 0; index < token->length; index++) {
			digit = values_hex_digit(token->text[index]);
			if (digit < 0)
				return EINVAL;
			value = value * 16U + (uint32_t)digit;
		}

		/* The number of digits picks the form. */
		switch (token->length) {
		case 3:
			*color = 0xff000000U |
			    ((value >> 8) & 0xfU) * 0x110000U |
			    ((value >> 4) & 0xfU) * 0x1100U |
			    (value & 0xfU) * 0x11U;
			return 0;
		case 4:
			*color = ((value & 0xfU) * 0x11U) << 24 |
			    ((value >> 12) & 0xfU) * 0x110000U |
			    ((value >> 8) & 0xfU) * 0x1100U |
			    ((value >> 4) & 0xfU) * 0x11U;
			return 0;
		case 6:
			*color = 0xff000000U | value;
			return 0;
		case 8:
			*color = (value << 24) | (value >> 8);
			return 0;
		default:
			return EINVAL;
		}
	}

	/* rgb() and rgba(). */
	if (token->type == CSS_TOKEN_FUNCTION) {
		same = css_ident_equal(token, "rgb");
		if (!same)
			same = css_ident_equal(token, "rgba");
		if (!same)
			return EINVAL;
		return values_rgb_function(tokens + 1, count - 1U, color);
	}

	/* Anything else is not a color. */
	return EINVAL;
}

/* Looks a keyword up in a table; returns 1 and its value when it is there. */
static int
values_keyword(
	const struct values_keyword *table,
	const struct css_token *token,
	int *value)
{
	size_t index;
	int same;

	/* Only identifiers are keywords. */
	if (token->type != CSS_TOKEN_IDENT)
		return 0;

	/* Compares with each keyword. */
	for (index = 0; table[index].name != NULL; index++) {
		same = css_ident_equal(token, table[index].name);
		if (same) {
			*value = table[index].value;
			return 1;
		}
	}

	/* The word is not one of the table's. */
	return 0;
}

/* Looks a dimension's unit up; returns 1 and the unit when it is a length unit. */
static int
values_unit(
	const struct css_token *token,
	int *unit)
{
	size_t index;
	int same;

	/* Only dimensions have units. */
	if (token->type != CSS_TOKEN_DIMENSION)
		return 0;

	/* Compares the unit with each length unit. */
	for (index = 0; values_units[index].name != NULL; index++) {
		same = css_ident_equal(token, values_units[index].name);
		if (same) {
			*unit = values_units[index].value;
			return 1;
		}
	}

	/* The unit is not a length unit. */
	return 0;
}

/* Parses a length or percentage (and, when allowed, auto, none and normal). */
static int
values_length(
	const struct css_token *token,
	int allow_keywords,
	struct css_value *value)
{
	int found;

	/* Keywords. */
	value->kind = CSS_VALUE_LENGTH;
	if (token->type == CSS_TOKEN_IDENT && allow_keywords) {
		found = values_keyword(values_length_keywords, token, &value->keyword);
		if (!found)
			return EINVAL;
		value->kind = CSS_VALUE_KEYWORD;
		return 0;
	}

	/* Zero, percentages and dimensions. */
	if (token->type == CSS_TOKEN_NUMBER && token->number == 0) {
		value->number = 0;
		value->unit = CSS_DUNIT_PX;
		return 0;
	}

	/* A percentage. */
	if (token->type == CSS_TOKEN_PERCENTAGE) {
		value->number = (float)token->number;
		value->unit = CSS_DUNIT_PERCENT;
		return 0;
	}

	/* Otherwise a dimension with a known unit. */
	if (token->type != CSS_TOKEN_DIMENSION)
		return EINVAL;
	value->number = (float)token->number;
	found = values_unit(token, &value->unit);
	if (!found)
		return EINVAL;

	/* Succeeded: the length and its unit. */
	return 0;
}

/* Parses a longhand's value. */
static int
values_single(
	struct vm_heap *heap,
	int property,
	const struct css_token *tokens,
	size_t count,
	struct css_value *value)
{
	const struct values_keyword *table;
	int keyword;
	int found;
	int error;

	/* Skips surrounding whitespace. */
	while (count > 0 && tokens[0].type == CSS_TOKEN_WHITESPACE) {
		tokens++;
		count--;
	}
	while (count > 0 && tokens[count - 1U].type == CSS_TOKEN_WHITESPACE)
		count--;
	if (count == 0)
		return EINVAL;
	memset(value, 0, sizeof(*value));

	/* Families take a list; colors may be a function of several tokens. */
	if (property == CSS_PROP_FONT_FAMILY)
		return values_families(heap, tokens, count, value);
	if (property == CSS_PROP_COLOR || property == CSS_PROP_BACKGROUND_COLOR ||
	    (property >= CSS_PROP_BORDER_TOP_COLOR && property <= CSS_PROP_BORDER_LEFT_COLOR)) {
		error = css_parse_color(tokens, count, &value->color);
		if (error != 0)
			return error;
		value->kind = CSS_VALUE_COLOR;
		return 0;
	}

	/* The other properties take one token. */
	if (count != 1)
		return EINVAL;

	/* The keyword properties. */
	table = NULL;
	switch (property) {
	case CSS_PROP_DISPLAY:
		table = values_display;
		break;
	case CSS_PROP_POSITION:
		table = values_position;
		break;
	case CSS_PROP_FLOAT:
		table = values_float;
		break;
	case CSS_PROP_VISIBILITY:
		table = values_visibility;
		break;
	case CSS_PROP_BORDER_TOP_STYLE:
	case CSS_PROP_BORDER_RIGHT_STYLE:
	case CSS_PROP_BORDER_BOTTOM_STYLE:
	case CSS_PROP_BORDER_LEFT_STYLE:
		table = values_border_style;
		break;
	case CSS_PROP_FONT_STYLE:
		table = values_font_style;
		break;
	case CSS_PROP_TEXT_ALIGN:
		table = values_text_align;
		break;
	case CSS_PROP_WHITE_SPACE:
		table = values_white_space;
		break;
	case CSS_PROP_TEXT_DECORATION_LINE:
		table = values_decoration;
		break;
	case CSS_PROP_LIST_STYLE_TYPE:
		table = values_list_style;
		break;
	default:
		break;
	}

	/* A keyword property takes a keyword of its table. */
	if (table != NULL) {
		found = values_keyword(table, &tokens[0], &keyword);
		if (!found)
			return EINVAL;
		value->kind = CSS_VALUE_KEYWORD;
		value->keyword = keyword;
		return 0;
	}

	/* font-weight: a keyword or a number from 1 to 1000. */
	if (property == CSS_PROP_FONT_WEIGHT) {
		found = values_keyword(values_font_weight, &tokens[0], &keyword);
		if (!found && tokens[0].type == CSS_TOKEN_NUMBER && tokens[0].number >= 1 && tokens[0].number <= 1000) {
			found = 1;
			keyword = (int)tokens[0].number;
		}

		/* Anything else is not a weight. */
		if (!found)
			return EINVAL;
		value->kind = CSS_VALUE_KEYWORD;
		value->keyword = keyword;
		return 0;
	}

	/* font-size: a keyword or a length. */
	if (property == CSS_PROP_FONT_SIZE) {
		found = values_keyword(values_font_size, &tokens[0], &keyword);
		if (found) {
			/* A keyword's size at the default size, marked as a keyword so the monospace family can rescale it. */
			value->kind = CSS_VALUE_LENGTH;
			value->number = (float)keyword;
			value->unit = CSS_DUNIT_FONT_KEYWORD;
			return 0;
		}

		/* The relative sizes. */
		found = values_keyword(values_relative_size, &tokens[0], &keyword);
		if (found) {
			value->kind = CSS_VALUE_LENGTH;
			value->number = (float)keyword;
			value->unit = CSS_DUNIT_PERCENT;
			return 0;
		}

		/* Otherwise a length (no keywords). */
		return values_length(&tokens[0], 0, value);
	}

	/* line-height: normal, a number or a length. */
	if (property == CSS_PROP_LINE_HEIGHT) {
		if (tokens[0].type == CSS_TOKEN_NUMBER) {
			value->kind = CSS_VALUE_NUMBER;
			value->number = (float)tokens[0].number;
			return 0;
		}

		/* Or a length, or normal. */
		return values_length(&tokens[0], 1, value);
	}

	/* Border widths: a keyword or a length. */
	if (property >= CSS_PROP_BORDER_TOP_WIDTH && property <= CSS_PROP_BORDER_LEFT_WIDTH) {
		found = values_keyword(values_border_width, &tokens[0], &keyword);
		if (found) {
			value->kind = CSS_VALUE_LENGTH;
			value->number = (float)keyword;
			value->unit = CSS_DUNIT_PX;
			return 0;
		}

		/* Or a length. */
		return values_length(&tokens[0], 0, value);
	}

	/* The other lengths: sizes, margins and paddings. */
	return values_length(&tokens[0], 1, value);
}

/* Parses font-family: names (identifiers joined by spaces, or strings) separated by commas. */
static int
values_families(
	struct vm_heap *heap,
	const struct css_token *tokens,
	size_t count,
	struct css_value *value)
{
	struct wb_units name;
	size_t index;
	int error;

	/* Reads each family between the commas. */
	value->kind = CSS_VALUE_FAMILIES;
	value->family_count = 0;
	wb_units_init(&name);
	index = 0;
	while (index <= count) {
		/* A comma or the end finishes the name gathered. */
		if (index == count || tokens[index].type == CSS_TOKEN_COMMA) {
			while (name.length > 0 && name.data[name.length - 1U] == ' ')
				name.length--;
			if (name.length != 0 && value->family_count < CSS_DECLARED_FAMILIES) {
				value->families[value->family_count] = vm_atom_from_units(heap, name.data, name.length);
				if (value->families[value->family_count] == NULL) {
					wb_units_release(&name);
					return ENOMEM;
				}

				/* The family is kept. */
				value->family_count++;
			}

			/* The next name starts empty. */
			wb_units_clear(&name);
			index++;
			continue;
		}

		/* Identifiers and strings make the name; whitespace between words is one space. */
		if (tokens[index].type == CSS_TOKEN_IDENT || tokens[index].type == CSS_TOKEN_STRING) {
			error = wb_units_append(&name, tokens[index].text, tokens[index].length);
			if (error != 0) {
				wb_units_release(&name);
				return ENOMEM;
			}
		} else if (tokens[index].type == CSS_TOKEN_WHITESPACE && name.length != 0) {
			error = wb_units_append_code_point(&name, ' ');
			if (error != 0) {
				wb_units_release(&name);
				return ENOMEM;
			}
		} else if (tokens[index].type != CSS_TOKEN_WHITESPACE) {
			wb_units_release(&name);
			return EINVAL;
		}

		/* The next token. */
		index++;
	}

	/* The gathered name is no longer needed. */
	wb_units_release(&name);

	/* A family list needs a family. */
	if (value->family_count == 0)
		return EINVAL;

	/* Succeeded: the families in order. */
	return 0;
}

/* Splits a value into its space-separated components (functions whole); returns how many. */
static size_t
values_components(
	const struct css_token *tokens,
	size_t count,
	size_t *starts,
	size_t *lengths,
	size_t max)
{
	size_t index;
	size_t made;
	size_t start;
	int depth;

	/* Walks the tokens, keeping function arguments with their function. */
	made = 0;
	index = 0;
	while (index < count && made < max) {
		if (tokens[index].type == CSS_TOKEN_WHITESPACE) {
			index++;
			continue;
		}

		/* The component starts here and runs to whitespace outside a function. */
		start = index;
		depth = 0;
		do {
			if (tokens[index].type == CSS_TOKEN_FUNCTION || tokens[index].type == CSS_TOKEN_OPEN_PAREN)
				depth++;
			if (tokens[index].type == CSS_TOKEN_CLOSE_PAREN)
				depth--;
			index++;
		} while (index < count && (depth > 0 || tokens[index].type != CSS_TOKEN_WHITESPACE));
		starts[made] = start;
		lengths[made] = index - start;
		made++;
	}

	/* Reports how many components there are. */
	return made;
}

/* Parses a four-sided shorthand (margin, padding, border-width/style/color). */
static int
values_four(
	struct vm_heap *heap,
	int first,
	const struct css_token *tokens,
	size_t count,
	struct css_declaration *out,
	size_t *made)
{
	struct css_value values[4];
	size_t starts[5];
	size_t lengths[5];
	size_t components;
	size_t index;
	int error;

	/* One to four components. */
	components = values_components(tokens, count, starts, lengths, 5);
	if (components == 0 || components > 4)
		return EINVAL;
	for (index = 0; index < components; index++) {
		error = values_single(heap, first, tokens + starts[index], lengths[index], &values[index]);
		if (error != 0)
			return error;
	}

	/* Top, right, bottom, left, the missing ones copied from their opposites. */
	if (components < 2)
		values[1] = values[0];
	if (components < 3)
		values[2] = values[0];
	if (components < 4)
		values[3] = values[1];
	for (index = 0; index < 4; index++)
		values_add(out, made, first + (int)index, &values[index]);

	/* Succeeded: four longhands. */
	return 0;
}

/* Parses a border shorthand for one side, or all four when side is -1. */
static int
values_border(
	struct vm_heap *heap,
	int side,
	const struct css_token *tokens,
	size_t count,
	struct css_declaration *out,
	size_t *made)
{
	struct css_value width;
	struct css_value style;
	struct css_value color;
	struct css_value candidate;
	size_t starts[4];
	size_t lengths[4];
	size_t components;
	size_t index;
	int error;
	int first;
	int last;

	/* The defaults: medium, none, currentcolor. */
	memset(&width, 0, sizeof(width));
	width.kind = CSS_VALUE_LENGTH;
	width.number = 3;
	width.unit = CSS_DUNIT_PX;
	memset(&style, 0, sizeof(style));
	style.kind = CSS_VALUE_KEYWORD;
	style.keyword = CSS_BORDER_NONE;
	memset(&color, 0, sizeof(color));
	color.kind = CSS_VALUE_COLOR;
	color.color = CSS_CURRENT_COLOR;

	/* Each component is a width, a style or a color, in any order. */
	components = values_components(tokens, count, starts, lengths, 4);
	if (components == 0 || components > 3)
		return EINVAL;
	for (index = 0; index < components; index++) {
		error = values_single(heap, CSS_PROP_BORDER_TOP_STYLE, tokens + starts[index], lengths[index], &candidate);
		if (error == 0) {
			style = candidate;
			continue;
		}

		/* Or a width. */
		error = values_single(heap, CSS_PROP_BORDER_TOP_WIDTH, tokens + starts[index], lengths[index], &candidate);
		if (error == 0) {
			width = candidate;
			continue;
		}

		/* Or a color. */
		error = values_single(heap, CSS_PROP_BORDER_TOP_COLOR, tokens + starts[index], lengths[index], &candidate);
		if (error == 0) {
			color = candidate;
			continue;
		}

		/* Anything else spoils the shorthand. */
		return EINVAL;
	}

	/* Declares the three longhands on the side or sides. */
	first = side;
	last = side;
	if (side < 0) {
		first = CSS_TOP;
		last = CSS_LEFT;
	}

	/* Each side gets the three. */
	for (index = (size_t)first; index <= (size_t)last; index++) {
		values_add(out, made, CSS_PROP_BORDER_TOP_WIDTH + (int)index, &width);
		values_add(out, made, CSS_PROP_BORDER_TOP_STYLE + (int)index, &style);
		values_add(out, made, CSS_PROP_BORDER_TOP_COLOR + (int)index, &color);
	}

	/* Succeeded: the longhands are declared. */
	return 0;
}

/* Adds one longhand declaration to the expansion. */
static void
values_add(
	struct css_declaration *out,
	size_t *made,
	int property,
	const struct css_value *value)
{
	/* Fills the next place. */
	out[*made].property = property;
	out[*made].important = 0;
	out[*made].value = *value;
	(*made)++;
}

/* Reads a hexadecimal digit, or reports -1. */
static int
values_hex_digit(
	uint16_t unit)
{
	/* The three ranges of hex digits. */
	if (unit >= '0' && unit <= '9')
		return unit - '0';
	if (unit >= 'a' && unit <= 'f')
		return unit - 'a' + 10;
	if (unit >= 'A' && unit <= 'F')
		return unit - 'A' + 10;

	/* Anything else is not a hex digit. */
	return -1;
}

/* Parses the arguments of rgb() or rgba(): three numbers or percentages and an optional alpha. */
static int
values_rgb_function(
	const struct css_token *tokens,
	size_t count,
	uint32_t *color)
{
	double channels[4];
	size_t index;
	int made;
	double value;

	/* Reads up to four numeric arguments, separated by commas, spaces or a slash. */
	made = 0;
	channels[3] = 1.0;
	for (index = 0; index < count && tokens[index].type != CSS_TOKEN_CLOSE_PAREN; index++) {
		if (tokens[index].type == CSS_TOKEN_NUMBER || tokens[index].type == CSS_TOKEN_PERCENTAGE) {
			if (made >= 4)
				return EINVAL;
			value = tokens[index].number;
			if (tokens[index].type == CSS_TOKEN_PERCENTAGE && made == 3) {
				/* An alpha percentage is a fraction of one. */
				value = value / 100.0;
			} else if (tokens[index].type == CSS_TOKEN_PERCENTAGE) {
				/* A channel percentage is a fraction of 255. */
				value = value * 255.0 / 100.0;
			}

			/* The channel is kept. */
			channels[made] = value;
			made++;
			continue;
		}

		/* Separators are skipped. */
		if (tokens[index].type == CSS_TOKEN_WHITESPACE || tokens[index].type == CSS_TOKEN_COMMA)
			continue;
		if (tokens[index].type == CSS_TOKEN_DELIM && tokens[index].delim == '/')
			continue;
		return EINVAL;
	}

	/* Three channels are needed. */
	if (made < 3)
		return EINVAL;

	/* Clamps and packs the channels. */
	for (index = 0; index < 3; index++) {
		if (channels[index] < 0)
			channels[index] = 0;
		if (channels[index] > 255)
			channels[index] = 255;
	}

	/* The alpha between zero and one. */
	if (channels[3] < 0)
		channels[3] = 0;
	if (channels[3] > 1)
		channels[3] = 1;
	*color = (uint32_t)(channels[3] * 255.0 + 0.5) << 24 |
	    (uint32_t)(channels[0] + 0.5) << 16 |
	    (uint32_t)(channels[1] + 0.5) << 8 |
	    (uint32_t)(channels[2] + 0.5);

	/* Succeeded: the color is packed. */
	return 0;
}

/* Declares a CSS-wide keyword for a longhand, or for every longhand of a four-sided shorthand. */
static void
values_wide_keyword(
	int property,
	const struct css_value *value,
	struct css_declaration *out,
	size_t *made)
{
	int first;

	/* A longhand takes it itself. */
	if (property < CSS_PROP_COUNT) {
		values_add(out, made, property, value);
		return;
	}

	/* The four-sided shorthands give it to their four longhands. */
	first = -1;
	switch (property) {
	case SHORT_MARGIN:
		first = CSS_PROP_MARGIN_TOP;
		break;
	case SHORT_PADDING:
		first = CSS_PROP_PADDING_TOP;
		break;
	case SHORT_BORDER_WIDTH:
		first = CSS_PROP_BORDER_TOP_WIDTH;
		break;
	case SHORT_BORDER_STYLE:
		first = CSS_PROP_BORDER_TOP_STYLE;
		break;
	case SHORT_BORDER_COLOR:
		first = CSS_PROP_BORDER_TOP_COLOR;
		break;
	default:
		break;
	}

	/* The other shorthands are left alone in this pass. */
	if (first < 0)
		return;

	/* Declares the four. */
	values_add(out, made, first, value);
	values_add(out, made, first + 1, value);
	values_add(out, made, first + 2, value);
	values_add(out, made, first + 3, value);
}

/* Parses the background shorthand for its color (the first component that is one; transparent without one). */
static void
values_background(
	const struct css_token *tokens,
	size_t count,
	struct css_declaration *out,
	size_t *made)
{
	struct css_value value;
	size_t index;
	size_t rest;
	int error;

	/* Tries each token as the start of a color. */
	memset(&value, 0, sizeof(value));
	value.kind = CSS_VALUE_COLOR;
	for (index = 0; index < count; index++) {
		/* A function takes its arguments with it. */
		rest = 1;
		if (tokens[index].type == CSS_TOKEN_FUNCTION)
			rest = count - index;
		error = css_parse_color(&tokens[index], rest, &value.color);
		if (error == 0) {
			values_add(out, made, CSS_PROP_BACKGROUND_COLOR, &value);
			return;
		}
	}

	/* No color means transparent. */
	value.color = 0;
	values_add(out, made, CSS_PROP_BACKGROUND_COLOR, &value);
}
