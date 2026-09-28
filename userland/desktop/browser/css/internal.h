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

#ifndef KEILAND_BROWSER_CSS_INTERNAL_H
#define KEILAND_BROWSER_CSS_INTERNAL_H

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

/* The most arguments a min(), max() or clamp() keeps. */
#define CSS_CALC_ARGUMENTS	3

/*
 * A sum of lengths of the kinds calc() mixes, each the number of its unit:
 * pixels (the absolute units converted), percentages, font-relative and
 * viewport-relative lengths.  The cascade turns it into pixels and a
 * percentage the layout resolves.
 */
struct css_calc_sum {
	float px;
	float percent;
	float em;
	float ex;
	float rem;
	float vw;
	float vh;
	float vmin;
	float vmax;
};

/* What a calculation does with its sums. */
enum css_calc_operation {
	CSS_CALC_SUM,
	CSS_CALC_MIN,
	CSS_CALC_MAX,
	CSS_CALC_CLAMP
};

/*
 * A calc(), min(), max() or clamp() length (ws074-p061): one sum, or the
 * least or greatest of several, or the middle one of three.
 */
struct css_calc {
	int operation;
	size_t count;
	struct css_calc_sum sums[CSS_CALC_ARGUMENTS];
};

/*
 * A declared value, as parsed: a keyword, a length, a number, a color or
 * font families.  A length whose unit is CSS_DUNIT_CALC is the
 * calculation calc points at (in the arena the value was parsed into).
 */
struct css_value {
	int kind;
	int keyword;
	float number;
	int unit;
	uint32_t color;
	struct vm_string *families[CSS_DECLARED_FAMILIES];
	int family_count;
	struct vm_string *url;
	const struct css_calc *calc;
};

/* The kinds of declared value. */
enum css_value_kind {
	CSS_VALUE_KEYWORD,
	CSS_VALUE_LENGTH,
	CSS_VALUE_NUMBER,
	CSS_VALUE_COLOR,
	CSS_VALUE_FAMILIES,
	CSS_VALUE_URL,
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
	CSS_DUNIT_FONT_KEYWORD,
	CSS_DUNIT_VMIN,
	CSS_DUNIT_VMAX,
	CSS_DUNIT_CALC
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
	CSS_PROP_CLEAR,
	CSS_PROP_OVERFLOW,
	CSS_PROP_OVERFLOW_X,
	CSS_PROP_OVERFLOW_Y,
	CSS_PROP_BACKGROUND_IMAGE,
	CSS_PROP_BACKGROUND_REPEAT,
	CSS_PROP_BACKGROUND_POSITION_X,
	CSS_PROP_BACKGROUND_POSITION_Y,
	CSS_PROP_BACKGROUND_SIZE_WIDTH,
	CSS_PROP_BACKGROUND_SIZE_HEIGHT,
	CSS_PROP_BOX_SIZING,
	CSS_PROP_COUNT
};

/*
 * One declaration: a property, its value and whether it is !important.
 */
struct css_declaration {
	int property;
	int important;
	struct css_value value;
	struct vm_string *custom_name;
	const struct css_token *raw;
	size_t raw_count;
	int pending_property;
};

/*
 * The property of a custom property's declaration (--name: tokens): its
 * name is custom_name and its value the tokens raw (ws074-p061).
 */
#define CSS_PROP_CUSTOM		(-1)

/*
 * The property of a declaration whose value uses var(): its tokens raw
 * are parsed as pending_property (a longhand or a shorthand) once the
 * element's custom properties are known.
 */
#define CSS_PROP_PENDING	(-2)

/*
 * One custom property an element has (ws074-p061): its name, its value's
 * tokens with every var() replaced (none: the property is invalid), and
 * the next one of the element's list, which ends in its parent's list.
 */
struct css_custom {
	struct vm_string *name;
	const struct css_token *tokens;
	size_t count;
	const struct css_custom *next;
	int invalid;
};

/*
 * One test of a media query: the feature it measures (media.c), how it
 * compares, and the value (pixels, a ratio, dppx, or 0 or 1).
 */
struct css_media_test {
	int feature;
	int comparison;
	float value;
};

/*
 * One media query: whether its media type is this browser's, its tests,
 * and whether not turns the answer round.
 */
struct css_media_query {
	int negate;
	int type_matches;
	struct css_media_test *tests;
	size_t test_count;
};

/*
 * A media query list: one of its queries must hold, and so must the list
 * it is nested in (parent, NULL at the top).
 */
struct css_media {
	struct css_media_query *queries;
	size_t query_count;
	const struct css_media *parent;
};

/*
 * What parsing a value needs: the heap the names are atoms of, and the
 * arena a calculation is kept in.
 */
struct css_parse {
	struct vm_heap *heap;
	struct wb_arena *arena;
};

/*
 * A style rule: its selectors and its declarations (in the sheet's arena).
 */
struct css_rule {
	struct css_selector *selectors;
	size_t selector_count;
	struct css_declaration *declarations;
	size_t declaration_count;
	const struct css_media *media;
};

/*
 * One selector of the rule index: the rule and which of its selectors.
 */
struct css_index_entry {
	uint32_t rule;
	uint32_t selector;
};

/*
 * The run of index entries filed under one key (an atom; NULL marks an
 * empty slot of the table).
 */
struct css_index_bucket {
	struct vm_string *key;
	uint32_t start;
	uint32_t count;
};

/*
 * An open-addressed table of buckets by key; capacity is a power of two
 * (zero for a table with no key).
 */
struct css_index_table {
	struct css_index_bucket *slots;
	size_t capacity;
};

/*
 * The rule index of a sheet (index.c): its selectors grouped by the key
 * of their rightmost compound, the groups found by id, class or type, and
 * the run of selectors any element may match.  Each run is in rule order.
 */
struct css_rule_index {
	struct css_index_entry *entries;
	size_t entry_count;
	struct css_index_table ids;
	struct css_index_table classes;
	struct css_index_table tags;
	uint32_t universal_start;
	uint32_t universal_count;
};

/*
 * A parsed style sheet: its rules (each with the @media lists it is
 * nested in), the URLs its @import rules name (atoms, in order) and their
 * media lists, its rule index and the arena all of them live in.
 */
struct css_sheet {
	struct wb_arena arena;
	struct css_rule *rules;
	size_t rule_count;
	struct vm_string **imports;
	struct css_media **import_media;
	size_t import_count;
	struct css_rule_index index;
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

/* The rule index (index.c). */
int css_index_build(struct css_sheet *sheet);
const struct css_index_bucket *css_index_find(const struct css_index_table *table, const struct vm_string *key);

/* Media queries (media.c). */
int css_media_parse(struct wb_arena *arena, const struct css_token *tokens, size_t count, const struct css_media *parent, struct css_media **media);
int css_media_matches(const struct css_media *media, float width, float height);

/* Values (values.c). */
int css_property_lookup(const struct css_token *name);
int css_parse_value(struct css_parse *parse, const struct css_token *tokens, size_t count, const struct css_token *name, struct css_declaration *out, size_t *out_count, size_t out_capacity);
int css_parse_property(struct css_parse *parse, int property, const struct css_token *tokens, size_t count, struct css_declaration *out, size_t *out_count);
int css_parse_color(const struct css_token *tokens, size_t count, uint32_t *color);
int css_parse_value_as(struct css_parse *parse, int property, const struct css_token *tokens, size_t count, struct css_declaration *out, size_t *out_count);
int css_parse_font(struct css_parse *parse, const struct css_token *tokens, size_t count, struct css_declaration *out, size_t *out_count);

/* The user agent's style sheet (ua.c). */
extern const char css_user_agent_sheet[];

#endif
