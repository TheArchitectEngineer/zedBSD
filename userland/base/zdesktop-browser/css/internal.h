/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The inside of the CSS module, shared by its files: the tokens of CSS
 * Syntax 3, the parsed rules, selectors and declarations, and the
 * property table.
 */

#ifndef ZDESKTOP_BROWSER_CSS_INTERNAL_H
#define ZDESKTOP_BROWSER_CSS_INTERNAL_H

#include "css/css.h"

/* The color value that stands for currentcolor (zero alpha, so no real color has it). */
#define CSS_CURRENT_COLOR	0x00000001U

/* How many families a declared font-family keeps. */
#define CSS_DECLARED_FAMILIES	CSS_FAMILIES_MAX

/*
 * The token types of CSS Syntax 3.
 */
enum css_token_type {
	CSS_TOKEN_IDENT,
	CSS_TOKEN_FUNCTION,
	CSS_TOKEN_AT_KEYWORD,
	CSS_TOKEN_HASH,
	CSS_TOKEN_STRING,
	CSS_TOKEN_BAD_STRING,
	CSS_TOKEN_URL,
	CSS_TOKEN_BAD_URL,
	CSS_TOKEN_DELIM,
	CSS_TOKEN_NUMBER,
	CSS_TOKEN_PERCENTAGE,
	CSS_TOKEN_DIMENSION,
	CSS_TOKEN_WHITESPACE,
	CSS_TOKEN_CDO,
	CSS_TOKEN_CDC,
	CSS_TOKEN_COLON,
	CSS_TOKEN_SEMICOLON,
	CSS_TOKEN_COMMA,
	CSS_TOKEN_OPEN_SQUARE,
	CSS_TOKEN_CLOSE_SQUARE,
	CSS_TOKEN_OPEN_PAREN,
	CSS_TOKEN_CLOSE_PAREN,
	CSS_TOKEN_OPEN_CURLY,
	CSS_TOKEN_CLOSE_CURLY,
	CSS_TOKEN_EOF
};

/*
 * One token: its type, its text (the name of an ident, function,
 * at-keyword or hash, the value of a string or URL, the unit of a
 * dimension; unescaped, in the sheet's arena), its number and, for a
 * delimiter, its code point.
 */
struct css_token {
	int type;
	const uint16_t *text;
	size_t length;
	double number;
	int integer;
	uint32_t delim;
	int hash_is_id;
};

/*
 * The kinds of simple selector.
 */
enum css_simple_kind {
	CSS_SIMPLE_TYPE,
	CSS_SIMPLE_UNIVERSAL,
	CSS_SIMPLE_ID,
	CSS_SIMPLE_CLASS,
	CSS_SIMPLE_ATTRIBUTE,
	CSS_SIMPLE_PSEUDO_CLASS,
	CSS_SIMPLE_NEVER
};

/*
 * How an attribute selector compares the value.
 */
enum css_attribute_match {
	CSS_MATCH_EXISTS,
	CSS_MATCH_EQUAL,
	CSS_MATCH_INCLUDES,
	CSS_MATCH_DASH,
	CSS_MATCH_PREFIX,
	CSS_MATCH_SUFFIX,
	CSS_MATCH_SUBSTRING
};

/*
 * The pseudo-classes this pass understands.
 */
enum css_pseudo_class {
	CSS_PSEUDO_ROOT,
	CSS_PSEUDO_FIRST_CHILD,
	CSS_PSEUDO_LAST_CHILD,
	CSS_PSEUDO_ONLY_CHILD,
	CSS_PSEUDO_EMPTY,
	CSS_PSEUDO_LINK,
	CSS_PSEUDO_NEVER
};

/*
 * How a compound selector relates to the one on its left.
 */
enum css_combinator {
	CSS_COMBINATOR_NONE,
	CSS_COMBINATOR_DESCENDANT,
	CSS_COMBINATOR_CHILD,
	CSS_COMBINATOR_NEXT,
	CSS_COMBINATOR_SUBSEQUENT
};

/*
 * One simple selector: a name (an atom), and for attributes a value (an
 * atom) and a comparison.
 */
struct css_simple {
	int kind;
	struct vm_string *name;
	struct vm_string *value;
	int match;
	int case_insensitive;
	int pseudo;
};

/*
 * One compound selector and the combinator that joins it to the compound
 * on its left.
 */
struct css_compound {
	struct css_simple *simples;
	size_t count;
	int combinator;
};

/*
 * A complex selector, compounds from left to right, and its specificity
 * (ids << 16 | classes << 8 | types).
 */
struct css_selector {
	struct css_compound *compounds;
	size_t count;
	uint32_t specificity;
};

/*
 * A declared value, as parsed: a keyword, a length, a number, a color or
 * font families.
 */
struct css_value {
	int kind;
	int keyword;
	float number;
	int unit;
	uint32_t color;
	struct vm_string *families[CSS_DECLARED_FAMILIES];
	int family_count;
};

/* The kinds of declared value. */
enum css_value_kind {
	CSS_VALUE_KEYWORD,
	CSS_VALUE_LENGTH,
	CSS_VALUE_NUMBER,
	CSS_VALUE_COLOR,
	CSS_VALUE_FAMILIES,
	CSS_VALUE_INHERIT,
	CSS_VALUE_INITIAL,
	CSS_VALUE_UNSET
};

/* The units a declared length can have (converted to pixels by the cascade). */
enum css_declared_unit {
	CSS_DUNIT_PX,
	CSS_DUNIT_EM,
	CSS_DUNIT_REM,
	CSS_DUNIT_EX,
	CSS_DUNIT_PERCENT,
	CSS_DUNIT_PT,
	CSS_DUNIT_PC,
	CSS_DUNIT_IN,
	CSS_DUNIT_CM,
	CSS_DUNIT_MM,
	CSS_DUNIT_VW,
	CSS_DUNIT_VH,
	CSS_DUNIT_FONT_KEYWORD
};

/*
 * The properties this pass knows (longhands; shorthands expand into
 * these while parsing).
 */
enum css_property {
	CSS_PROP_DISPLAY,
	CSS_PROP_POSITION,
	CSS_PROP_FLOAT,
	CSS_PROP_VISIBILITY,
	CSS_PROP_WIDTH,
	CSS_PROP_HEIGHT,
	CSS_PROP_MIN_WIDTH,
	CSS_PROP_MAX_WIDTH,
	CSS_PROP_MIN_HEIGHT,
	CSS_PROP_MAX_HEIGHT,
	CSS_PROP_MARGIN_TOP,
	CSS_PROP_MARGIN_RIGHT,
	CSS_PROP_MARGIN_BOTTOM,
	CSS_PROP_MARGIN_LEFT,
	CSS_PROP_PADDING_TOP,
	CSS_PROP_PADDING_RIGHT,
	CSS_PROP_PADDING_BOTTOM,
	CSS_PROP_PADDING_LEFT,
	CSS_PROP_BORDER_TOP_WIDTH,
	CSS_PROP_BORDER_RIGHT_WIDTH,
	CSS_PROP_BORDER_BOTTOM_WIDTH,
	CSS_PROP_BORDER_LEFT_WIDTH,
	CSS_PROP_BORDER_TOP_STYLE,
	CSS_PROP_BORDER_RIGHT_STYLE,
	CSS_PROP_BORDER_BOTTOM_STYLE,
	CSS_PROP_BORDER_LEFT_STYLE,
	CSS_PROP_BORDER_TOP_COLOR,
	CSS_PROP_BORDER_RIGHT_COLOR,
	CSS_PROP_BORDER_BOTTOM_COLOR,
	CSS_PROP_BORDER_LEFT_COLOR,
	CSS_PROP_COLOR,
	CSS_PROP_BACKGROUND_COLOR,
	CSS_PROP_FONT_SIZE,
	CSS_PROP_FONT_WEIGHT,
	CSS_PROP_FONT_STYLE,
	CSS_PROP_FONT_FAMILY,
	CSS_PROP_LINE_HEIGHT,
	CSS_PROP_TEXT_ALIGN,
	CSS_PROP_WHITE_SPACE,
	CSS_PROP_TEXT_DECORATION_LINE,
	CSS_PROP_LIST_STYLE_TYPE,
	CSS_PROP_TOP,
	CSS_PROP_RIGHT,
	CSS_PROP_BOTTOM,
	CSS_PROP_LEFT,
	CSS_PROP_Z_INDEX,
	CSS_PROP_COUNT
};

/*
 * One declaration: a property, its value and whether it is !important.
 */
struct css_declaration {
	int property;
	int important;
	struct css_value value;
};

/*
 * A style rule: its selectors and its declarations (in the sheet's arena).
 */
struct css_rule {
	struct css_selector *selectors;
	size_t selector_count;
	struct css_declaration *declarations;
	size_t declaration_count;
};

/*
 * A parsed style sheet: its rules and the arena all of them live in.
 */
struct css_sheet {
	struct wb_arena arena;
	struct css_rule *rules;
	size_t rule_count;
	int origin;
};

/* The origins of style sheets. */
#define CSS_ORIGIN_USER_AGENT	0
#define CSS_ORIGIN_AUTHOR	1

/* The tokenizer (tokenizer.c). */
int css_tokenize(struct wb_arena *arena, const uint16_t *units, size_t length, struct css_token **tokens, size_t *count);

/* The parser (parser.c). */
int css_parse_sheet(struct vm_heap *heap, const uint16_t *units, size_t length, int origin, struct css_sheet *sheet);
int css_parse_declarations(struct vm_heap *heap, struct wb_arena *arena, const uint16_t *units, size_t length, struct css_declaration **declarations, size_t *count);
void css_sheet_release(struct css_sheet *sheet);
int css_ident_equal(const struct css_token *token, const char *ascii);
int css_units_equal_ascii(const uint16_t *units, size_t length, const char *ascii);

/* Values (values.c). */
int css_property_lookup(const struct css_token *name);
int css_parse_value(struct vm_heap *heap, const struct css_token *tokens, size_t count, const struct css_token *name, struct css_declaration *out, size_t *out_count, size_t out_capacity);
int css_parse_color(const struct css_token *tokens, size_t count, uint32_t *color);
int css_parse_value_as(struct vm_heap *heap, int property, const struct css_token *tokens, size_t count, struct css_declaration *out, size_t *out_count);
int css_parse_font(struct vm_heap *heap, const struct css_token *tokens, size_t count, struct css_declaration *out, size_t *out_count);

/* The user agent's style sheet (ua.c). */
extern const char css_user_agent_sheet[];

#endif
