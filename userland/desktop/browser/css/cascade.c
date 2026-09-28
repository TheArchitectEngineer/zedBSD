/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The cascade: the engine that holds a document's style sheets, selector
 * matching, and the computation of an element's style from the matching
 * declarations, its style attribute and its parent's style.
 */

#include "css/internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* The font size of the root before any style sets it, in pixels. */
#define CASCADE_DEFAULT_FONT_SIZE	16.0f

/*
 * The size medium stands for in the monospace family, as in Chromium: a
 * size on the keyword scale is scaled by it over the default size when the
 * family becomes monospace alone.
 */
#define CASCADE_MONOSPACE_FONT_SIZE	13.0f

/* The viewport the vw and vh units measure until the layout sets one. */
#define CASCADE_DEFAULT_VIEWPORT_WIDTH	1024.0f
#define CASCADE_DEFAULT_VIEWPORT_HEIGHT	768.0f

/* How deep var() references are followed before a cycle is assumed. */
#define CASCADE_VAR_DEPTH	16

/* How deep :has() looks below an element (the parser caps nesting too). */
#define CASCADE_HAS_DEPTH	512

/* The most declarations one pending declaration expands into (a shorthand's longhands). */
#define CASCADE_EXPANSION_MAX	16U

/* The precedence of a declaration by its origin and importance (higher wins). */
#define RANK_USER_AGENT		0
#define RANK_AUTHOR		1
#define RANK_AUTHOR_IMPORTANT	2
#define RANK_USER_AGENT_IMPORTANT	3

/*
 * One sheet of an engine, in cascade order: the sheet, and the same sheet
 * again in owned when the engine parsed it itself (the user agent's, and
 * text added with css_engine_add_sheet) and frees it; a sheet lent by the
 * page (css_engine_add_parsed) has owned NULL and outlives the engine.
 * media are the lists the sheet applies under (its <link>'s and its
 * @import rules'), all of which must hold.
 */
struct cascade_sheet {
	const struct css_sheet *sheet;
	struct css_sheet *owned;
	const struct css_media *media[CSS_MEDIA_CHAIN_MAX];
	size_t media_count;
};

/*
 * The style sheets of a document, the atoms the matching looks up, and
 * the buffers the matching of one element reuses: the atoms of its
 * classes, the index entries that may match it, and a class name's
 * characters on their way to an atom.  The arena holds what lives as long
 * as the engine: the media lists of <link> elements and the elements'
 * custom properties, which the computed styles point at.  pseudo_wanted is
 * the pseudo-element being styled (CSS_PSEUDO_ELEMENT_NONE for the element
 * itself), and pseudo_seen collects, while an element is styled, the bits
 * of the pseudo-elements some rule gives it.
 */
struct css_engine {
	struct vm_heap *heap;
	struct wb_vector sheets;
	struct vm_string *atom_id;
	struct vm_string *atom_class;
	struct vm_string *atom_style;
	struct vm_string *atom_href;
	float root_font_size;
	float viewport_width;
	float viewport_height;
	struct wb_vector class_keys;
	struct wb_vector candidates;
	struct wb_units word;
	struct wb_arena arena;
	int pseudo_wanted;
	int pseudo_seen;
};

/*
 * One declaration that applies to the element being styled, with what
 * orders it among the others: its sheet's place in the cascade, its
 * rule's place in the sheet and its own place in the rule, packed.
 */
struct cascade_match {
	const struct css_declaration *declaration;
	int rank;
	uint32_t specificity;
	uint64_t order;
};

static int cascade_collect(struct css_engine *engine, struct dom_element *element, struct wb_vector *matches, struct wb_arena *scratch);
static int cascade_element_keys(struct css_engine *engine, struct dom_element *element, struct vm_string **id);
static int cascade_add_bucket(struct css_engine *engine, const struct css_rule_index *index, const struct css_index_bucket *bucket);
static int cascade_collect_sheet(struct css_engine *engine, struct dom_element *element, const struct css_sheet *sheet, uint64_t sheet_number, struct vm_string *id, struct wb_vector *matches);
static int cascade_entry_compare(const void *left, const void *right);
static int cascade_add_declarations(struct wb_vector *matches, const struct css_declaration *declarations, size_t count, int origin, uint32_t specificity, uint64_t order);
static int cascade_compare(const void *left, const void *right);
static int cascade_customs(struct css_engine *engine, struct css_style *style, const struct cascade_match *matches, size_t count);
static int cascade_substitute(const struct css_custom *list, const struct css_token *tokens, size_t count, int depth, struct wb_vector *out);
static const struct css_custom *cascade_custom_find(const struct css_custom *list, const struct css_token *name);
static int cascade_keep_tokens(struct css_engine *engine, const struct css_token *tokens, size_t count, const struct css_token **kept);
static int cascade_resolve_pending(struct css_engine *engine, const struct css_style *style, struct wb_vector *list, struct wb_arena *scratch);
static struct css_length cascade_calc(struct css_engine *engine, const struct css_calc *calc, float font_size);
static int cascade_candidate_matches(struct css_engine *engine, struct dom_element *element, const struct css_selector *selector);
static int cascade_selector_matches(struct css_engine *engine, struct dom_element *element, const struct css_selector *selector, size_t index, struct dom_element *anchor);
static int cascade_anchored(struct dom_element *element, int combinator, struct dom_element *anchor);
static int cascade_pseudo_class(struct css_engine *engine, struct dom_element *element, const struct css_simple *simple);
static int cascade_any_matches(struct css_engine *engine, struct dom_element *element, const struct css_simple *simple);
static int cascade_has(struct css_engine *engine, struct dom_element *element, const struct css_simple *simple);
static int cascade_has_below(struct css_engine *engine, struct dom_node *node, const struct css_simple *simple, struct dom_element *anchor, int depth);
static int cascade_any_relative(struct css_engine *engine, struct dom_element *element, const struct css_simple *simple, struct dom_element *anchor);
static int cascade_nth(struct dom_element *element, const struct css_simple *simple);
static int cascade_is_form_element(const struct dom_element *element);
static int cascade_compute(struct css_engine *engine, struct dom_element *element, const struct css_style *parent, struct css_style *style, int pseudo);
static int cascade_compound_matches(struct css_engine *engine, struct dom_element *element, const struct css_compound *compound);
static int cascade_simple_matches(struct css_engine *engine, struct dom_element *element, const struct css_simple *simple);
static int cascade_attribute_matches(const struct vm_string *value, const struct css_simple *simple);
static int cascade_has_class(const struct vm_string *classes, const struct vm_string *name);
static struct dom_element *cascade_parent_element(struct dom_element *element);
static struct dom_element *cascade_previous_element(struct dom_element *element);
static struct dom_element *cascade_next_element(struct dom_element *element);
static void cascade_apply(struct css_engine *engine, struct css_style *style, const struct css_style *parent, const struct css_declaration *declaration);
static void cascade_inherit(struct css_style *style, const struct css_style *parent, int property);
static struct css_length cascade_length(struct css_engine *engine, const struct css_value *value, float font_size);
static float cascade_font_size(struct css_engine *engine, const struct css_value *value, float parent_size);
static int cascade_font_size_keyword(const struct css_value *value, int parent_keyword);
static void cascade_families(struct css_style *style, const struct css_value *value);
static int cascade_monospace_only(const struct css_style *style);
static void cascade_monospace_size(struct css_style *style, const struct css_style *parent, const struct css_declaration *font_size);

/*
 * Makes an engine with the user agent's style sheet.
 */
int
css_engine_create(
	struct css_engine **engine,
	struct vm_heap *heap)
{
	struct css_engine *created;
	struct wb_units units;
	int error;

	/* Allocates the engine and interns the attribute names it looks up. */
	created = calloc(1, sizeof(*created));
	if (created == NULL)
		return ENOMEM;
	created->heap = heap;
	created->root_font_size = CASCADE_DEFAULT_FONT_SIZE;
	created->viewport_width = CASCADE_DEFAULT_VIEWPORT_WIDTH;
	created->viewport_height = CASCADE_DEFAULT_VIEWPORT_HEIGHT;
	wb_vector_init(&created->sheets, sizeof(struct cascade_sheet));
	wb_vector_init(&created->class_keys, sizeof(struct vm_string *));
	wb_vector_init(&created->candidates, sizeof(struct css_index_entry));
	wb_units_init(&created->word);
	wb_arena_init(&created->arena, 0);
	created->atom_id = vm_atom_from_ascii(heap, "id");
	created->atom_class = vm_atom_from_ascii(heap, "class");
	created->atom_style = vm_atom_from_ascii(heap, "style");
	created->atom_href = vm_atom_from_ascii(heap, "href");
	if (created->atom_id == NULL || created->atom_class == NULL || created->atom_style == NULL || created->atom_href == NULL) {
		free(created);
		return ENOMEM;
	}

	/* Parses the user agent's sheet as the first sheet. */
	wb_units_init(&units);
	error = wb_utf8_to_units((const unsigned char *)css_user_agent_sheet, strlen(css_user_agent_sheet), &units);
	if (error == 0)
		error = css_engine_add_sheet_origin(created, units.data, units.length, CSS_ORIGIN_USER_AGENT);
	wb_units_release(&units);
	if (error != 0) {
		css_engine_destroy(created);
		return error;
	}

	/* Succeeded: the engine is ready for the author's sheets. */
	*engine = created;
	return 0;
}

/*
 * Destroys an engine and its style sheets.
 */
void
css_engine_destroy(
	struct css_engine *engine)
{
	struct cascade_sheet *sheets;
	size_t index;

	/* A NULL engine is nothing to destroy. */
	if (engine == NULL)
		return;

	/* Frees every sheet the engine parsed itself (the lent ones are their lender's). */
	sheets = engine->sheets.items;
	for (index = 0; index < engine->sheets.count; index++)
		css_sheet_destroy(sheets[index].owned);

	/* Frees the lists, the buffers and the engine. */
	wb_vector_release(&engine->sheets);
	wb_vector_release(&engine->class_keys);
	wb_vector_release(&engine->candidates);
	wb_units_release(&engine->word);
	wb_arena_release(&engine->arena);
	free(engine);
}

/*
 * Adds an author style sheet (a <style> element's text), after the ones
 * added before it.
 */
int
css_engine_add_sheet(
	struct css_engine *engine,
	const uint16_t *units,
	size_t length)
{
	int error;

	/* An author sheet. */
	error = css_engine_add_sheet_origin(engine, units, length, CSS_ORIGIN_AUTHOR);
	if (error != 0)
		return error;

	/* Succeeded: the sheet takes part in the cascade. */
	return 0;
}

/*
 * Adds a style sheet of an origin.
 */
int
css_engine_add_sheet_origin(
	struct css_engine *engine,
	const uint16_t *units,
	size_t length,
	int origin)
{
	struct cascade_sheet entry;
	struct css_sheet *sheet;
	int error;

	/* Parses the sheet. */
	sheet = calloc(1, sizeof(*sheet));
	if (sheet == NULL)
		return ENOMEM;
	error = css_parse_sheet(engine->heap, units, length, origin, sheet);
	if (error != 0) {
		css_sheet_destroy(sheet);
		return error;
	}

	/* Appends it, owned by the engine, under no media list. */
	memset(&entry, 0, sizeof(entry));
	entry.sheet = sheet;
	entry.owned = sheet;
	error = wb_vector_push(&engine->sheets, &entry);
	if (error != 0) {
		css_sheet_destroy(sheet);
		return error;
	}

	/* Succeeded: the sheet is the engine's last. */
	return 0;
}

/*
 * Adds an author sheet the caller parsed (and keeps) after the ones added
 * before it, applying under every one of media_count media lists (NULL
 * ones hold); the engine only borrows the sheet and the lists.
 */
int
css_engine_add_parsed(
	struct css_engine *engine,
	const struct css_sheet *sheet,
	const struct css_media *const *media,
	size_t media_count)
{
	struct cascade_sheet entry;
	size_t index;
	int error;

	/* The lent sheet and its lists (the deepest ones beyond the room are left out). */
	memset(&entry, 0, sizeof(entry));
	entry.sheet = sheet;
	entry.owned = NULL;
	for (index = 0; index < media_count && entry.media_count < CSS_MEDIA_CHAIN_MAX; index++) {
		if (media[index] == NULL)
			continue;
		entry.media[entry.media_count] = media[index];
		entry.media_count++;
	}

	/* Appends it. */
	error = wb_vector_push(&engine->sheets, &entry);
	if (error != 0)
		return error;

	/* Succeeded: the sheet takes part in the cascade. */
	return 0;
}

/*
 * Reads a media query list (a <link>'s media attribute) into the engine's
 * arena, for css_engine_add_parsed.
 */
int
css_engine_parse_media(
	struct css_engine *engine,
	const uint16_t *units,
	size_t length,
	const struct css_media **media)
{
	struct css_token *tokens;
	struct css_media *parsed;
	size_t count;
	int error;

	/* The attribute's tokens (without the end-of-file token). */
	error = css_tokenize(&engine->arena, units, length, &tokens, &count);
	if (error != 0)
		return error;
	if (count > 0)
		count--;

	/* The list. */
	error = css_media_parse(&engine->arena, tokens, count, NULL, &parsed);
	if (error != 0)
		return error;

	/* Succeeded: the list lives as long as the engine. */
	*media = parsed;
	return 0;
}

/*
 * Sets the viewport the vw and vh units measure.
 */
void
css_engine_set_viewport(
	struct css_engine *engine,
	float width,
	float height)
{
	/* Remembers the size. */
	engine->viewport_width = width;
	engine->viewport_height = height;
}

/*
 * Computes an element's style from the cascade and its parent's style
 * (NULL for the root).
 */
int
css_engine_compute(
	struct css_engine *engine,
	struct dom_element *element,
	const struct css_style *parent,
	struct css_style *style)
{
	int error;

	/* The element itself. */
	error = cascade_compute(engine, element, parent, style, CSS_PSEUDO_ELEMENT_NONE);
	if (error != 0)
		return error;

	/* Succeeded: the style is computed. */
	return 0;
}

/*
 * Computes the style of an element's ::before or ::after (pseudo) from
 * the rules for it and the element's own style, which it inherits from.
 */
int
css_engine_compute_pseudo(
	struct css_engine *engine,
	struct dom_element *element,
	int pseudo,
	const struct css_style *element_style,
	struct css_style *style)
{
	int error;

	/* The pseudo-element's rules. */
	error = cascade_compute(engine, element, element_style, style, pseudo);
	if (error != 0)
		return error;

	/* Succeeded: the pseudo-element's style is computed. */
	return 0;
}

/* Computes the style of an element, or of one of its pseudo-elements, in the cascade. */
static int
cascade_compute(
	struct css_engine *engine,
	struct dom_element *element,
	const struct css_style *parent,
	struct css_style *style,
	int pseudo)
{
	struct cascade_match *matches;
	struct wb_vector list;
	struct wb_arena scratch;
	const struct css_declaration *font_size;
	const struct css_declaration *font_family;
	size_t index;
	int is_root;
	int error;
	int side;

	/* Starts from the initial values, with the inherited ones from the parent. */
	css_initial_style(style);
	if (parent != NULL) {
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
		style->list_style = parent->list_style;
		style->underline = parent->underline;
		style->custom = parent->custom;
	}

	/* Gathers the declarations that apply, in cascade order, noting the pseudo-elements some rule gives the element. */
	wb_vector_init(&list, sizeof(struct cascade_match));
	wb_arena_init(&scratch, 0);
	engine->pseudo_wanted = pseudo;
	engine->pseudo_seen = 0;
	error = cascade_collect(engine, element, &list, &scratch);
	engine->pseudo_wanted = CSS_PSEUDO_ELEMENT_NONE;
	if (pseudo == CSS_PSEUDO_ELEMENT_NONE)
		style->pseudo_elements = engine->pseudo_seen;
	if (error != 0) {
		wb_vector_release(&list);
		wb_arena_release(&scratch);
		return error;
	}

	/* Sorts them into cascade order (an empty list has no storage to sort). */
	matches = list.items;
	if (list.count > 1)
		qsort(matches, list.count, sizeof(*matches), cascade_compare);

	/* The element's custom properties, then the declarations that wait for them. */
	error = cascade_customs(engine, style, matches, list.count);
	if (error == 0)
		error = cascade_resolve_pending(engine, style, &list, &scratch);
	if (error != 0) {
		wb_vector_release(&list);
		wb_arena_release(&scratch);
		return error;
	}

	/* The list may have been rebuilt. */
	matches = list.items;

	/*
	 * The font size and family come first: the other lengths in em depend on
	 * the size, and the monospace family rescales a size on the keyword scale.
	 */
	font_size = NULL;
	font_family = NULL;
	for (index = 0; index < list.count; index++) {
		if (matches[index].declaration->property == CSS_PROP_FONT_SIZE)
			font_size = matches[index].declaration;
		if (matches[index].declaration->property == CSS_PROP_FONT_FAMILY)
			font_family = matches[index].declaration;
	}

	/* Applies the winning font size. */
	if (font_size != NULL)
		cascade_apply(engine, style, parent, font_size);

	/* Applies the winning font family. */
	if (font_family != NULL)
		cascade_apply(engine, style, parent, font_family);

	/* Rescales a keyword-scale size for a change to or from the monospace family. */
	cascade_monospace_size(style, parent, font_size);

	/* Then every other declaration in order, the later winning. */
	for (index = 0; index < list.count; index++) {
		if (matches[index].declaration->property == CSS_PROP_FONT_SIZE)
			continue;
		if (matches[index].declaration->property == CSS_PROP_FONT_FAMILY)
			continue;
		cascade_apply(engine, style, parent, matches[index].declaration);
	}

	/* The lists are no longer needed. */
	wb_vector_release(&list);
	wb_arena_release(&scratch);

	/* currentcolor in the border colors becomes the color. */
	for (side = 0; side < 4; side++) {
		if (style->border_color[side] == CSS_CURRENT_COLOR)
			style->border_color[side] = style->color;
		if (style->border_style[side] == CSS_BORDER_NONE)
			style->border_width[side] = 0;
	}

	/* And in the background color. */
	if (style->background_color == CSS_CURRENT_COLOR)
		style->background_color = style->color;

	/* The root's font size is what rem measures (its pseudo-elements do not change it). */
	is_root = 0;
	if (pseudo == CSS_PSEUDO_ELEMENT_NONE && element->node.parent != NULL && element->node.parent->type == DOM_DOCUMENT)
		is_root = 1;
	if (is_root)
		engine->root_font_size = style->font_size;

	/* Succeeded: the style is computed. */
	return 0;
}

/*
 * Fills a style with the initial values of every property.
 */
void
css_initial_style(
	struct css_style *style)
{
	int side;

	/* Everything zero first, then the values that are not. */
	memset(style, 0, sizeof(*style));
	style->display = CSS_DISPLAY_INLINE;
	style->width.unit = CSS_UNIT_AUTO;
	style->height.unit = CSS_UNIT_AUTO;
	style->min_width.unit = CSS_UNIT_PX;
	style->max_width.unit = CSS_UNIT_NONE;
	style->min_height.unit = CSS_UNIT_PX;
	style->max_height.unit = CSS_UNIT_NONE;
	for (side = 0; side < 4; side++) {
		style->margin[side].unit = CSS_UNIT_PX;
		style->padding[side].unit = CSS_UNIT_PX;
		style->offset[side].unit = CSS_UNIT_AUTO;
		style->border_width[side] = 3;
		style->border_style[side] = CSS_BORDER_NONE;
		style->border_color[side] = CSS_CURRENT_COLOR;
	}

	/* The initial values that are not zero. */
	style->color = 0xff000000U;
	style->background_color = 0;
	style->background_image = NULL;
	style->background_repeat = CSS_REPEAT_BOTH;
	style->background_position[0].unit = CSS_UNIT_PERCENT;
	style->background_position[0].value = 0;
	style->background_position[1].unit = CSS_UNIT_PERCENT;
	style->background_position[1].value = 0;
	style->background_size_keyword = CSS_BACKGROUND_SIZE_LENGTHS;
	style->background_size[0].unit = CSS_UNIT_AUTO;
	style->background_size[1].unit = CSS_UNIT_AUTO;
	style->font_size = CASCADE_DEFAULT_FONT_SIZE;
	style->font_size_keyword = 1;
	style->font_weight = 400;
	style->generic_family = CSS_FAMILY_SERIF;
	style->line_height.unit = CSS_UNIT_NORMAL;
	style->text_align = CSS_TEXT_ALIGN_START;
	style->white_space = CSS_WHITE_SPACE_NORMAL;
	style->list_style = CSS_LIST_DISC;
	style->z_index_auto = 1;

	/* The flexible box's initial values that are not zero. */
	style->justify_content = CSS_ALIGN_START;
	style->align_items = CSS_ALIGN_STRETCH;
	style->align_content = CSS_ALIGN_STRETCH;
	style->align_self = CSS_ALIGN_AUTO;
	style->flex_shrink = 1;
	style->flex_basis.unit = CSS_UNIT_AUTO;
}

/* Gathers every declaration that applies to an element: the matching rules' and the style attribute's. */
static int
cascade_collect(
	struct css_engine *engine,
	struct dom_element *element,
	struct wb_vector *matches,
	struct wb_arena *scratch)
{
	const struct cascade_sheet *sheets;
	struct css_declaration *declarations;
	struct dom_attribute *attribute;
	struct vm_string *id;
	struct wb_units units;
	size_t declaration_count;
	size_t sheet;
	size_t index;
	int holds;
	int error;

	/* The keys the rule indexes are searched by: the element's id, classes and type. */
	error = cascade_element_keys(engine, element, &id);
	if (error != 0)
		return error;

	/* The rules of every sheet whose media hold and whose selectors match, in the order of the sheets. */
	sheets = engine->sheets.items;
	for (sheet = 0; sheet < engine->sheets.count; sheet++) {
		holds = 1;
		for (index = 0; index < sheets[sheet].media_count && holds; index++)
			holds = css_media_matches(sheets[sheet].media[index], engine->viewport_width, engine->viewport_height);
		if (!holds)
			continue;

		/* The sheet's matching rules. */
		error = cascade_collect_sheet(engine, element, sheets[sheet].sheet, (uint64_t)sheet, id, matches);
		if (error != 0)
			return error;
	}

	/* The style attribute, more specific than any selector (the element's own, not its pseudo-elements'). */
	if (engine->pseudo_wanted != CSS_PSEUDO_ELEMENT_NONE)
		return 0;
	attribute = dom_element_find_attribute(element, DOM_NS_NONE, engine->atom_style);
	if (attribute == NULL)
		return 0;
	wb_units_init(&units);
	error = 0;
	if ((attribute->value->flags & VM_STRING_WIDE) != 0) {
		error = wb_units_append(&units, vm_string_units(attribute->value), attribute->value->length);
	} else {
		for (index = 0; index < attribute->value->length && error == 0; index++)
			error = wb_units_append_code_point(&units, vm_string_latin1(attribute->value)[index]);
	}

	/* Parses the attribute's declarations. */
	if (error == 0)
		error = css_parse_declarations(engine->heap, scratch, units.data, units.length, &declarations, &declaration_count);
	wb_units_release(&units);
	if (error == ENOMEM)
		return error;
	if (error != 0)
		return 0;

	/* They come after every sheet's rules. */
	error = cascade_add_declarations(matches, declarations, declaration_count, CSS_ORIGIN_AUTHOR, 0xffffffffU, (uint64_t)engine->sheets.count << 44);
	if (error != 0)
		return error;

	/* Succeeded: every applying declaration is gathered. */
	return 0;
}

/*
 * Finds the atoms an element is looked up by in the rule indexes: its id
 * (NULL when it has none, or when no sheet names it), and its classes in
 * the engine's class_keys (the ones no sheet names are left out: no atom
 * exists for them, so no selector can name them).
 */
static int
cascade_element_keys(
	struct css_engine *engine,
	struct dom_element *element,
	struct vm_string **id)
{
	struct dom_attribute *attribute;
	struct vm_string *classes;
	struct vm_string *atom;
	size_t start;
	size_t end;
	size_t position;
	uint16_t unit;
	int error;

	/* The id's atom, found without making one. */
	*id = NULL;
	wb_vector_clear(&engine->class_keys);
	attribute = dom_element_find_attribute(element, DOM_NS_NONE, engine->atom_id);
	if (attribute != NULL) {
		wb_units_clear(&engine->word);
		for (position = 0; position < attribute->value->length; position++) {
			unit = vm_string_at(attribute->value, position);
			error = wb_units_append(&engine->word, &unit, 1);
			if (error != 0)
				return ENOMEM;
		}

		/* The atom, when a sheet made one. */
		*id = vm_atom_find_units(engine->heap, engine->word.data, engine->word.length);
	}

	/* An element without classes has no class keys. */
	attribute = dom_element_find_attribute(element, DOM_NS_NONE, engine->atom_class);
	if (attribute == NULL)
		return 0;
	classes = attribute->value;

	/* Each whitespace-separated word of the class attribute. */
	start = 0;
	while (start < classes->length) {
		/* Skips whitespace before the word. */
		unit = vm_string_at(classes, start);
		if (unit == ' ' || unit == '\t' || unit == '\n' || unit == '\f' || unit == '\r') {
			start++;
			continue;
		}

		/* Copies the word's characters up to the next whitespace. */
		wb_units_clear(&engine->word);
		end = start;
		while (end < classes->length) {
			unit = vm_string_at(classes, end);
			if (unit == ' ' || unit == '\t' || unit == '\n' || unit == '\f' || unit == '\r')
				break;
			error = wb_units_append(&engine->word, &unit, 1);
			if (error != 0)
				return ENOMEM;
			end++;
		}

		/* The word's atom, when some sheet named it, is a key. */
		atom = vm_atom_find_units(engine->heap, engine->word.data, engine->word.length);
		if (atom != NULL) {
			error = wb_vector_push(&engine->class_keys, &atom);
			if (error != 0)
				return ENOMEM;
		}

		/* On to the next word. */
		start = end;
	}

	/* Succeeded: the element's keys are found. */
	return 0;
}

/* Appends the index entries of a bucket (NULL: none) to the engine's candidates. */
static int
cascade_add_bucket(
	struct css_engine *engine,
	const struct css_rule_index *index,
	const struct css_index_bucket *bucket)
{
	uint32_t position;
	int error;

	/* A key no selector is filed under adds nothing. */
	if (bucket == NULL)
		return 0;

	/* Each entry of the run. */
	for (position = bucket->start; position < bucket->start + bucket->count; position++) {
		error = wb_vector_push(&engine->candidates, &index->entries[position]);
		if (error != 0)
			return ENOMEM;
	}

	/* Succeeded: the run is among the candidates. */
	return 0;
}

/*
 * Gathers the declarations of one sheet's rules that match an element:
 * the selectors its index files under the element's id, classes and type
 * and the universal ones, tried in rule order, each rule once with the
 * most specific of its selectors that match.
 */
static int
cascade_collect_sheet(
	struct css_engine *engine,
	struct dom_element *element,
	const struct css_sheet *sheet,
	uint64_t sheet_number,
	struct vm_string *id,
	struct wb_vector *matches)
{
	const struct css_rule_index *index;
	const struct css_index_bucket *bucket;
	const struct css_index_entry *candidates;
	const struct css_rule *rule;
	const struct css_selector *selector;
	struct css_index_bucket universal;
	struct vm_string **class_keys;
	uint32_t best;
	uint64_t order;
	size_t position;
	size_t group;
	size_t item;
	int matched;
	int holds;
	int selector_matched;
	int error;

	/* The candidates: the runs of the id, each class, the type, and the universal run. */
	index = &sheet->index;
	wb_vector_clear(&engine->candidates);
	bucket = css_index_find(&index->ids, id);
	error = cascade_add_bucket(engine, index, bucket);
	class_keys = engine->class_keys.items;
	for (position = 0; position < engine->class_keys.count && error == 0; position++) {
		bucket = css_index_find(&index->classes, class_keys[position]);
		error = cascade_add_bucket(engine, index, bucket);
	}

	/* The type's run. */
	if (error == 0) {
		bucket = css_index_find(&index->tags, element->local_name);
		error = cascade_add_bucket(engine, index, bucket);
	}

	/* The universal run. */
	if (error == 0) {
		universal.key = NULL;
		universal.start = index->universal_start;
		universal.count = index->universal_count;
		error = cascade_add_bucket(engine, index, &universal);
	}

	/* Memory ran out on the way. */
	if (error != 0)
		return error;

	/* Several runs are merged into rule order (each run is in rule order already). */
	candidates = engine->candidates.items;
	if (engine->candidates.count > 1)
		qsort(engine->candidates.items, engine->candidates.count, sizeof(struct css_index_entry), cascade_entry_compare);

	/* Tries the candidates rule by rule. */
	position = 0;
	while (position < engine->candidates.count) {
		rule = &sheet->rules[candidates[position].rule];
		matched = 0;
		best = 0;

		/* The end of the rule's candidates. */
		group = position;
		while (group < engine->candidates.count && candidates[group].rule == candidates[position].rule)
			group++;

		/* A rule inside @media applies only while its lists hold. */
		holds = 1;
		if (rule->media != NULL)
			holds = css_media_matches(rule->media, engine->viewport_width, engine->viewport_height);

		/* Every candidate selector of the rule. */
		for (item = position; item < group && holds; item++) {
			selector = &rule->selectors[candidates[item].selector];
			selector_matched = cascade_candidate_matches(engine, element, selector);
			if (selector_matched) {
				if (!matched || selector->specificity > best)
					best = selector->specificity;
				matched = 1;
			}
		}

		/* A rule whose selectors all failed does not apply. */
		if (matched) {
			order = (sheet_number << 44) | ((uint64_t)candidates[position].rule << 16);
			error = cascade_add_declarations(matches, rule->declarations, rule->declaration_count, sheet->origin, best, order);
			if (error != 0)
				return error;
		}

		/* On to the next rule. */
		position = group;
	}

	/* Succeeded: the sheet's applying declarations are gathered. */
	return 0;
}

/* Orders two index entries by rule, then selector. */
static int
cascade_entry_compare(
	const void *left,
	const void *right)
{
	const struct css_index_entry *a;
	const struct css_index_entry *b;

	/* Compares the rule, then the selector. */
	a = left;
	b = right;
	if (a->rule < b->rule)
		return -1;
	if (a->rule > b->rule)
		return 1;
	if (a->selector < b->selector)
		return -1;
	if (a->selector > b->selector)
		return 1;

	/* The same entry. */
	return 0;
}

/* Adds a rule's declarations with their origin, specificity and order. */
static int
cascade_add_declarations(
	struct wb_vector *matches,
	const struct css_declaration *declarations,
	size_t count,
	int origin,
	uint32_t specificity,
	uint64_t order)
{
	struct cascade_match match;
	size_t index;
	int error;

	/* Each declaration with its rank. */
	for (index = 0; index < count; index++) {
		match.declaration = &declarations[index];
		match.specificity = specificity;
		match.order = order + (uint64_t)index;
		if (origin == CSS_ORIGIN_USER_AGENT) {
			match.rank = RANK_USER_AGENT;
			if (declarations[index].important)
				match.rank = RANK_USER_AGENT_IMPORTANT;
		} else {
			match.rank = RANK_AUTHOR;
			if (declarations[index].important)
				match.rank = RANK_AUTHOR_IMPORTANT;
		}

		/* Adds it. */
		error = wb_vector_push(matches, &match);
		if (error != 0)
			return ENOMEM;
	}

	/* Succeeded: the declarations are gathered. */
	return 0;
}

/*
 * Gives the element its custom properties: its parent's list, with the
 * ones it declares in front (the last declaration of a name found first),
 * each with its var() references replaced; a property whose references
 * cannot be replaced is invalid.  The list lives in the engine's arena.
 */
static int
cascade_customs(
	struct css_engine *engine,
	struct css_style *style,
	const struct cascade_match *matches,
	size_t count)
{
	struct css_custom *node;
	struct css_custom **own;
	struct wb_vector owned;
	struct wb_vector tokens;
	size_t index;
	int error;

	/* The element's own properties go in front of the inherited list, in cascade order. */
	wb_vector_init(&owned, sizeof(struct css_custom *));
	for (index = 0; index < count; index++) {
		if (matches[index].declaration->property != CSS_PROP_CUSTOM)
			continue;

		/* A node for the declaration, its tokens as written for now. */
		node = wb_arena_zalloc(&engine->arena, sizeof(*node));
		if (node == NULL) {
			wb_vector_release(&owned);
			return ENOMEM;
		}

		/* The node is the list's first now. */
		node->name = matches[index].declaration->custom_name;
		node->tokens = matches[index].declaration->raw;
		node->count = matches[index].declaration->raw_count;
		node->next = style->custom;
		style->custom = node;
		error = wb_vector_push(&owned, &node);
		if (error != 0) {
			wb_vector_release(&owned);
			return ENOMEM;
		}
	}

	/* An element that declares none shares its parent's list. */
	if (owned.count == 0) {
		wb_vector_release(&owned);
		return 0;
	}

	/* Each own property's tokens, with their references replaced, copied into the engine's arena. */
	own = owned.items;
	wb_vector_init(&tokens, sizeof(struct css_token));
	for (index = 0; index < owned.count; index++) {
		node = own[index];
		wb_vector_clear(&tokens);
		error = cascade_substitute(style->custom, node->tokens, node->count, 0, &tokens);
		if (error == ENOMEM) {
			wb_vector_release(&tokens);
			wb_vector_release(&owned);
			return error;
		}

		/* A reference that cannot be replaced makes the property invalid. */
		if (error != 0) {
			node->invalid = 1;
			node->tokens = NULL;
			node->count = 0;
			continue;
		}

		/* The replaced tokens, kept as long as the engine. */
		error = cascade_keep_tokens(engine, tokens.items, tokens.count, &node->tokens);
		if (error != 0) {
			wb_vector_release(&tokens);
			wb_vector_release(&owned);
			return error;
		}

		/* The node holds the replaced tokens. */
		node->count = tokens.count;
	}

	/* The buffers are no longer needed. */
	wb_vector_release(&tokens);
	wb_vector_release(&owned);

	/* Succeeded: the element has its custom properties. */
	return 0;
}

/*
 * Appends tokens to a buffer with every var(--name[, fallback]) replaced
 * by the named property's tokens from a custom property list (its own
 * references replaced in turn, up to a depth that stops a cycle), or by
 * the fallback.  Returns EINVAL when a reference has neither.
 */
static int
cascade_substitute(
	const struct css_custom *list,
	const struct css_token *tokens,
	size_t count,
	int depth,
	struct wb_vector *out)
{
	const struct css_custom *node;
	size_t index;
	size_t end;
	size_t name_index;
	size_t fallback;
	int nesting;
	int is_var;
	int error;

	/* A reference too deep is a cycle. */
	if (depth > CASCADE_VAR_DEPTH)
		return EINVAL;

	/* Copies token by token, replacing each var(). */
	index = 0;
	while (index < count) {
		is_var = 0;
		if (tokens[index].type == CSS_TOKEN_FUNCTION)
			is_var = css_ident_equal(&tokens[index], "var");
		if (!is_var) {
			error = wb_vector_push(out, &tokens[index]);
			if (error != 0)
				return ENOMEM;
			index++;
			continue;
		}

		/* The reference's closing parenthesis. */
		end = index + 1U;
		nesting = 1;
		while (end < count) {
			if (tokens[end].type == CSS_TOKEN_FUNCTION || tokens[end].type == CSS_TOKEN_OPEN_PAREN)
				nesting++;
			if (tokens[end].type == CSS_TOKEN_CLOSE_PAREN)
				nesting--;
			if (nesting == 0)
				break;
			end++;
		}

		/* The name, and the fallback after a comma. */
		name_index = index + 1U;
		while (name_index < end && tokens[name_index].type == CSS_TOKEN_WHITESPACE)
			name_index++;
		if (name_index >= end || tokens[name_index].type != CSS_TOKEN_IDENT)
			return EINVAL;
		fallback = name_index + 1U;
		while (fallback < end && tokens[fallback].type == CSS_TOKEN_WHITESPACE)
			fallback++;
		if (fallback < end && tokens[fallback].type != CSS_TOKEN_COMMA)
			return EINVAL;

		/* The named property, when the list has a valid one. */
		node = cascade_custom_find(list, &tokens[name_index]);
		if (node != NULL) {
			error = cascade_substitute(list, node->tokens, node->count, depth + 1, out);
		} else if (fallback < end) {
			error = cascade_substitute(list, tokens + fallback + 1U, end - fallback - 1U, depth + 1, out);
		} else {
			error = EINVAL;
		}

		/* A reference that could not be replaced spoils the value. */
		if (error != 0)
			return error;

		/* Past the reference. */
		index = end + 1U;
	}

	/* Succeeded: the tokens are appended. */
	return 0;
}

/* Finds the valid custom property a name token names in a list (NULL when it has none). */
static const struct css_custom *
cascade_custom_find(
	const struct css_custom *list,
	const struct css_token *name)
{
	const struct css_custom *node;
	int same;

	/* The first node of the name is the one that wins. */
	for (node = list; node != NULL; node = node->next) {
		same = vm_string_equal_units(node->name, name->text, name->length);
		if (!same)
			continue;

		/* An invalid property is as good as none. */
		if (node->invalid)
			return NULL;
		return node;
	}

	/* No property of the name. */
	return NULL;
}

/* Copies tokens and their text into the engine's arena. */
static int
cascade_keep_tokens(
	struct css_engine *engine,
	const struct css_token *tokens,
	size_t count,
	const struct css_token **kept)
{
	struct css_token *copy;
	uint16_t *text;
	size_t index;

	/* An empty value keeps no tokens. */
	*kept = NULL;
	if (count == 0)
		return 0;

	/* The tokens. */
	copy = wb_arena_alloc(&engine->arena, count * sizeof(*copy));
	if (copy == NULL)
		return ENOMEM;
	memcpy(copy, tokens, count * sizeof(*copy));

	/* Each token's text, which may live in an arena that ends sooner. */
	for (index = 0; index < count; index++) {
		if (copy[index].text == NULL || copy[index].length == 0)
			continue;
		text = wb_arena_alloc(&engine->arena, copy[index].length * sizeof(uint16_t));
		if (text == NULL)
			return ENOMEM;
		memcpy(text, copy[index].text, copy[index].length * sizeof(uint16_t));
		copy[index].text = text;
	}

	/* Succeeded: the tokens live as long as the engine. */
	*kept = copy;
	return 0;
}

/*
 * Replaces each declaration that waits for var() in a sorted list of
 * matches by the declarations its value makes once the element's custom
 * properties are substituted (in the scratch arena), in the same place of
 * the cascade; a value that cannot be substituted or parsed makes the
 * property unset.
 */
static int
cascade_resolve_pending(
	struct css_engine *engine,
	const struct css_style *style,
	struct wb_vector *list,
	struct wb_arena *scratch)
{
	struct css_declaration expanded[CASCADE_EXPANSION_MAX];
	struct css_declaration *kept;
	struct cascade_match *matches;
	struct cascade_match match;
	struct css_parse parse;
	struct css_token unset;
	struct wb_vector tokens;
	struct wb_vector resolved;
	static const uint16_t unset_text[] = { 'u', 'n', 's', 'e', 't' };
	size_t index;
	size_t item;
	size_t made;
	int pending;
	int error;

	/* Nothing to do without a pending declaration. */
	matches = list->items;
	pending = 0;
	for (index = 0; index < list->count && !pending; index++) {
		if (matches[index].declaration->property == CSS_PROP_PENDING)
			pending = 1;
	}

	/* A list without one stays as it is. */
	if (!pending)
		return 0;

	/* The value parser's context: the heap's names, calculations in the scratch arena. */
	parse.heap = engine->heap;
	parse.arena = scratch;
	memset(&unset, 0, sizeof(unset));
	unset.type = CSS_TOKEN_IDENT;
	unset.text = unset_text;
	unset.length = 5;

	/* Rebuilds the list with each pending declaration expanded in its place. */
	wb_vector_init(&tokens, sizeof(struct css_token));
	wb_vector_init(&resolved, sizeof(struct cascade_match));
	error = 0;
	for (index = 0; index < list->count && error == 0; index++) {
		if (matches[index].declaration->property != CSS_PROP_PENDING) {
			error = wb_vector_push(&resolved, &matches[index]);
			continue;
		}

		/* The value with the element's custom properties, parsed as its property. */
		wb_vector_clear(&tokens);
		made = 0;
		error = cascade_substitute(style->custom, matches[index].declaration->raw, matches[index].declaration->raw_count, 0, &tokens);
		if (error == 0)
			error = css_parse_property(&parse, matches[index].declaration->pending_property, tokens.items, tokens.count, expanded, &made);
		if (error == ENOMEM)
			break;

		/* A value invalid at computed-value time is unset. */
		if (error != 0 || made == 0) {
			made = 0;
			error = css_parse_property(&parse, matches[index].declaration->pending_property, &unset, 1, expanded, &made);
			if (error != 0)
				break;
		}

		/* The declarations made, kept in the scratch arena, in the pending one's place. */
		kept = wb_arena_alloc(scratch, made * sizeof(*kept) + 1U);
		if (kept == NULL) {
			error = ENOMEM;
			break;
		}

		/* Each one takes the pending declaration's rank and order. */
		memcpy(kept, expanded, made * sizeof(*kept));
		for (item = 0; item < made && error == 0; item++) {
			match = matches[index];
			match.declaration = &kept[item];
			error = wb_vector_push(&resolved, &match);
		}
	}

	/* The substituted tokens are no longer needed. */
	wb_vector_release(&tokens);
	if (error != 0) {
		wb_vector_release(&resolved);
		return error;
	}

	/* The resolved list replaces the old one. */
	wb_vector_release(list);
	*list = resolved;

	/* Succeeded: no declaration waits any more. */
	return 0;
}

/*
 * Computes a calculated length: pixels, or a percentage with pixels added
 * (min(), max() and clamp() over percentages measure them against the
 * viewport's width, as the layout's containing block is not known here).
 */
static struct css_length
cascade_calc(
	struct css_engine *engine,
	const struct css_calc *calc,
	float font_size)
{
	struct css_length length;
	float values[CSS_CALC_ARGUMENTS];
	float percents[CSS_CALC_ARGUMENTS];
	float least;
	float vmin;
	float vmax;
	size_t index;
	int any_percent;

	/* Each argument's pixels and percentage (a calculation has one argument at least). */
	memset(values, 0, sizeof(values));
	memset(percents, 0, sizeof(percents));
	vmin = engine->viewport_width;
	if (engine->viewport_height < vmin)
		vmin = engine->viewport_height;
	vmax = engine->viewport_width;
	if (engine->viewport_height > vmax)
		vmax = engine->viewport_height;
	any_percent = 0;
	for (index = 0; index < calc->count; index++) {
		values[index] = calc->sums[index].px +
		    calc->sums[index].em * font_size +
		    calc->sums[index].ex * font_size * 0.5f +
		    calc->sums[index].rem * engine->root_font_size +
		    calc->sums[index].vw * engine->viewport_width / 100.0f +
		    calc->sums[index].vh * engine->viewport_height / 100.0f +
		    calc->sums[index].vmin * vmin / 100.0f +
		    calc->sums[index].vmax * vmax / 100.0f;
		percents[index] = calc->sums[index].percent;
		if (percents[index] != 0)
			any_percent = 1;
	}

	/* A sum keeps its percentage for the layout. */
	length.value = values[0];
	length.unit = CSS_UNIT_PX;
	length.offset = 0;
	if (calc->operation == CSS_CALC_SUM) {
		if (percents[0] != 0) {
			length.unit = CSS_UNIT_PERCENT;
			length.value = percents[0];
			length.offset = values[0];
		}

		/* The sum's length. */
		return length;
	}

	/* The others compare pixels: a percentage is measured against the viewport's width. */
	if (any_percent) {
		for (index = 0; index < calc->count; index++)
			values[index] += percents[index] * engine->viewport_width / 100.0f;
	}

	/* min() takes the least, max() the greatest. */
	least = values[0];
	for (index = 1; index < calc->count; index++) {
		if (calc->operation == CSS_CALC_MIN && values[index] < least)
			least = values[index];
		if (calc->operation != CSS_CALC_MIN && values[index] > least)
			least = values[index];
	}

	/* clamp(low, preferred, high) is the preferred one held between the two. */
	if (calc->operation == CSS_CALC_CLAMP) {
		least = values[1];
		if (least > values[2])
			least = values[2];
		if (least < values[0])
			least = values[0];
	}

	/* Reports the length in pixels. */
	length.value = least;
	return length;
}

/* Orders two applying declarations: rank, then specificity, then order. */
static int
cascade_compare(
	const void *left,
	const void *right)
{
	const struct cascade_match *a;
	const struct cascade_match *b;

	/* Compares field by field: a lower rank, specificity or order comes first. */
	a = left;
	b = right;
	if (a->rank < b->rank)
		return -1;
	if (a->rank > b->rank)
		return 1;
	if (a->specificity < b->specificity)
		return -1;
	if (a->specificity > b->specificity)
		return 1;
	if (a->order < b->order)
		return -1;
	if (a->order > b->order)
		return 1;

	/* The same declaration. */
	return 0;
}

/*
 * Tells whether a candidate selector of the index applies to what is being
 * styled: the element itself, or its ::before or ::after.  While the
 * element itself is styled, a selector of its ::before or ::after that
 * matches marks the element's pseudo_seen instead.
 */
static int
cascade_candidate_matches(
	struct css_engine *engine,
	struct dom_element *element,
	const struct css_selector *selector)
{
	int pseudo;
	int matches;

	/* A selector for what is being styled is matched as it is. */
	pseudo = selector->compounds[selector->count - 1U].pseudo_element;
	if (pseudo == engine->pseudo_wanted) {
		matches = cascade_selector_matches(engine, element, selector, selector->count - 1U, NULL);
		return matches;
	}

	/* Only the element's own styling looks at the others, to see which pseudo-elements it has. */
	if (engine->pseudo_wanted != CSS_PSEUDO_ELEMENT_NONE || pseudo == CSS_PSEUDO_ELEMENT_OTHER)
		return 0;

	/* A ::before or ::after selector that would match marks the element. */
	engine->pseudo_wanted = pseudo;
	matches = cascade_selector_matches(engine, element, selector, selector->count - 1U, NULL);
	engine->pseudo_wanted = CSS_PSEUDO_ELEMENT_NONE;
	if (matches)
		engine->pseudo_seen |= 1 << pseudo;

	/* Its declarations are not the element's. */
	return 0;
}

/*
 * Tells whether an element matches a selector's compounds up to index,
 * right to left.  With an anchor (the element a :has() is on), the first
 * compound must also stand in its combinator's relation to the anchor.
 */
static int
cascade_selector_matches(
	struct css_engine *engine,
	struct dom_element *element,
	const struct css_selector *selector,
	size_t index,
	struct dom_element *anchor)
{
	const struct css_compound *compound;
	struct dom_element *other;
	int matches;

	/* The compound at index must match the element itself. */
	compound = &selector->compounds[index];
	matches = cascade_compound_matches(engine, element, compound);
	if (!matches)
		return 0;

	/* The first compound ends the selector, or relates it to the anchor. */
	if (index == 0) {
		if (anchor == NULL)
			return 1;
		matches = cascade_anchored(element, compound->combinator, anchor);
		return matches;
	}

	/* Then the compound on its left, through the combinator. */
	switch (compound->combinator) {
	case CSS_COMBINATOR_CHILD:
		other = cascade_parent_element(element);
		if (other == NULL || other == anchor)
			return 0;
		return cascade_selector_matches(engine, other, selector, index - 1U, anchor);
	case CSS_COMBINATOR_DESCENDANT:
		for (other = cascade_parent_element(element); other != NULL && other != anchor; other = cascade_parent_element(other)) {
			matches = cascade_selector_matches(engine, other, selector, index - 1U, anchor);
			if (matches)
				return 1;
		}

		/* Otherwise no descendant relation holds. */
		return 0;
	case CSS_COMBINATOR_NEXT:
		other = cascade_previous_element(element);
		if (other == NULL || other == anchor)
			return 0;
		return cascade_selector_matches(engine, other, selector, index - 1U, anchor);
	case CSS_COMBINATOR_SUBSEQUENT:
		for (other = cascade_previous_element(element); other != NULL && other != anchor; other = cascade_previous_element(other)) {
			matches = cascade_selector_matches(engine, other, selector, index - 1U, anchor);
			if (matches)
				return 1;
		}

		/* Otherwise no earlier sibling matched. */
		return 0;
	default:
		return 0;
	}
}

/* Tells whether an element stands in a combinator's relation to an anchor (on the combinator's left). */
static int
cascade_anchored(
	struct dom_element *element,
	int combinator,
	struct dom_element *anchor)
{
	struct dom_element *other;

	/* The relation. */
	switch (combinator) {
	case CSS_COMBINATOR_CHILD:
		other = cascade_parent_element(element);
		return other == anchor;
	case CSS_COMBINATOR_NEXT:
		other = cascade_previous_element(element);
		return other == anchor;
	case CSS_COMBINATOR_SUBSEQUENT:
		for (other = cascade_previous_element(element); other != NULL; other = cascade_previous_element(other)) {
			if (other == anchor)
				return 1;
		}

		/* The anchor is not an earlier sibling. */
		return 0;
	default:
		for (other = cascade_parent_element(element); other != NULL; other = cascade_parent_element(other)) {
			if (other == anchor)
				return 1;
		}

		/* The anchor is not an ancestor. */
		return 0;
	}
}

/* Tells whether an element matches every simple selector of a compound. */
static int
cascade_compound_matches(
	struct css_engine *engine,
	struct dom_element *element,
	const struct css_compound *compound)
{
	size_t index;
	int matches;

	/* Every simple selector must match. */
	for (index = 0; index < compound->count; index++) {
		matches = cascade_simple_matches(engine, element, &compound->simples[index]);
		if (!matches)
			return 0;
	}

	/* The compound matches. */
	return 1;
}

/* Tells whether an element matches one simple selector. */
static int
cascade_simple_matches(
	struct css_engine *engine,
	struct dom_element *element,
	const struct css_simple *simple)
{
	struct dom_attribute *attribute;
	int matches;

	/* The kind of selector. */
	switch (simple->kind) {
	case CSS_SIMPLE_UNIVERSAL:
		return 1;
	case CSS_SIMPLE_TYPE:
		/* HTML element names match case-insensitively; theirs are lower case already. */
		return element->local_name == simple->name;
	case CSS_SIMPLE_ID:
		attribute = dom_element_find_attribute(element, DOM_NS_NONE, engine->atom_id);
		if (attribute == NULL)
			return 0;
		return vm_string_equal(attribute->value, simple->name);
	case CSS_SIMPLE_CLASS:
		attribute = dom_element_find_attribute(element, DOM_NS_NONE, engine->atom_class);
		if (attribute == NULL)
			return 0;
		return cascade_has_class(attribute->value, simple->name);
	case CSS_SIMPLE_ATTRIBUTE:
		attribute = dom_element_find_attribute(element, DOM_NS_NONE, simple->name);
		if (attribute == NULL)
			return 0;
		return cascade_attribute_matches(attribute->value, simple);
	case CSS_SIMPLE_PSEUDO_CLASS:
		matches = cascade_pseudo_class(engine, element, simple);
		return matches;
	case CSS_SIMPLE_PSEUDO_ELEMENT:
		/* A pseudo-element matches while its own style is computed. */
		return simple->pseudo == engine->pseudo_wanted;
	default:
		return 0;
	}
}

/* Tells whether an element matches a pseudo-class. */
static int
cascade_pseudo_class(
	struct css_engine *engine,
	struct dom_element *element,
	const struct css_simple *simple)
{
	struct dom_node *child;
	struct vm_string *attribute;
	struct wb_units value;
	int kind;
	int matches;
	int error;

	/* The pseudo-class. */
	switch (simple->pseudo) {
	case CSS_PSEUDO_ROOT:
		return element->node.parent != NULL && element->node.parent->type == DOM_DOCUMENT;
	case CSS_PSEUDO_FIRST_CHILD:
		return cascade_previous_element(element) == NULL && cascade_parent_element(element) != NULL;
	case CSS_PSEUDO_LAST_CHILD:
		return cascade_next_element(element) == NULL && cascade_parent_element(element) != NULL;
	case CSS_PSEUDO_ONLY_CHILD:
		return cascade_previous_element(element) == NULL && cascade_next_element(element) == NULL;
	case CSS_PSEUDO_EMPTY:
		for (child = element->node.first_child; child != NULL; child = child->next) {
			if (child->type == DOM_ELEMENT || child->type == DOM_TEXT)
				return 0;
		}

		/* The empty element. */
		return 1;
	case CSS_PSEUDO_LINK:
		if (element->ns != DOM_NS_HTML || (element->tag != DOM_TAG_A && element->tag != DOM_TAG_AREA))
			return 0;
		return dom_element_find_attribute(element, DOM_NS_NONE, engine->atom_href) != NULL;
	case CSS_PSEUDO_NOT:
		matches = cascade_any_matches(engine, element, simple);
		return !matches;
	case CSS_PSEUDO_IS:
		matches = cascade_any_matches(engine, element, simple);
		return matches;
	case CSS_PSEUDO_HAS:
		matches = cascade_has(engine, element, simple);
		return matches;
	case CSS_PSEUDO_NTH_CHILD:
	case CSS_PSEUDO_NTH_LAST_CHILD:
	case CSS_PSEUDO_NTH_OF_TYPE:
	case CSS_PSEUDO_NTH_LAST_OF_TYPE:
	case CSS_PSEUDO_FIRST_OF_TYPE:
	case CSS_PSEUDO_LAST_OF_TYPE:
		matches = cascade_nth(element, simple);
		return matches;
	case CSS_PSEUDO_ONLY_OF_TYPE:
		matches = cascade_nth(element, simple);
		return matches;
	case CSS_PSEUDO_DISABLED:
	case CSS_PSEUDO_ENABLED:
		/* A form element is disabled by its attribute. */
		matches = cascade_is_form_element(element);
		if (!matches)
			return 0;
		attribute = dom_attribute_ascii(element, "disabled");
		if (simple->pseudo == CSS_PSEUDO_DISABLED)
			return attribute != NULL;
		return attribute == NULL;
	case CSS_PSEUDO_CHECKED:
		/* A checked checkbox or radio button, or a selected option. */
		if (element->ns == DOM_NS_HTML && element->tag == DOM_TAG_OPTION) {
			attribute = dom_attribute_ascii(element, "selected");
			return attribute != NULL;
		}

		/* Otherwise only a checkbox or a radio button can be checked. */
		kind = dom_control_kind(element);
		if (kind != DOM_CONTROL_CHECKBOX && kind != DOM_CONTROL_RADIO)
			return 0;
		matches = dom_control_checked(element);
		return matches;
	case CSS_PSEUDO_PLACEHOLDER_SHOWN:
		/* A field with a placeholder and no value. */
		attribute = dom_attribute_ascii(element, "placeholder");
		if (attribute == NULL)
			return 0;
		wb_units_init(&value);
		error = dom_control_value(element, &value);
		matches = 0;
		if (error == 0 && value.length == 0)
			matches = 1;
		wb_units_release(&value);
		return matches;
	case CSS_PSEUDO_REQUIRED:
	case CSS_PSEUDO_OPTIONAL:
		/* A form element is required by its attribute. */
		matches = cascade_is_form_element(element);
		if (!matches)
			return 0;
		attribute = dom_attribute_ascii(element, "required");
		if (simple->pseudo == CSS_PSEUDO_REQUIRED)
			return attribute != NULL;
		return attribute == NULL;
	case CSS_PSEUDO_ALWAYS:
		return 1;
	default:
		return 0;
	}
}

/* Tells whether an element matches any selector of an :is() or :not() argument list. */
static int
cascade_any_matches(
	struct css_engine *engine,
	struct dom_element *element,
	const struct css_simple *simple)
{
	const struct css_selector *selector;
	size_t index;
	int matches;

	/* Each selector of the list, as for the element itself. */
	for (index = 0; index < simple->argument_count; index++) {
		selector = &simple->arguments[index];
		if (selector->count == 0)
			continue;
		matches = cascade_selector_matches(engine, element, selector, selector->count - 1U, NULL);
		if (matches)
			return 1;
	}

	/* None matched. */
	return 0;
}

/*
 * Tells whether an element has what a :has() argument asks for: an
 * element among its descendants, or its later siblings and theirs, that a
 * relative selector matches anchored at the element.
 */
static int
cascade_has(
	struct css_engine *engine,
	struct dom_element *element,
	const struct css_simple *simple)
{
	struct dom_node *sibling;
	size_t index;
	int siblings;
	int matches;

	/* The descendants. */
	matches = cascade_has_below(engine, &element->node, simple, element, 0);
	if (matches)
		return 1;

	/* The later siblings and their descendants, when a selector starts with + or ~. */
	siblings = 0;
	for (index = 0; index < simple->argument_count; index++) {
		if (simple->arguments[index].compounds[0].combinator == CSS_COMBINATOR_NEXT)
			siblings = 1;
		if (simple->arguments[index].compounds[0].combinator == CSS_COMBINATOR_SUBSEQUENT)
			siblings = 1;
	}

	/* Without one, the siblings are not looked at. */
	if (!siblings)
		return 0;

	/* Each later sibling, itself and below. */
	for (sibling = element->node.next; sibling != NULL; sibling = sibling->next) {
		if (sibling->type != DOM_ELEMENT)
			continue;
		matches = cascade_any_relative(engine, (struct dom_element *)sibling, simple, element);
		if (matches)
			return 1;
		matches = cascade_has_below(engine, sibling, simple, element, 0);
		if (matches)
			return 1;
	}

	/* Nothing matched. */
	return 0;
}

/* Tells whether an element below a node (at any depth) matches a relative selector of :has() anchored at anchor. */
static int
cascade_has_below(
	struct css_engine *engine,
	struct dom_node *node,
	const struct css_simple *simple,
	struct dom_element *anchor,
	int depth)
{
	struct dom_node *child;
	int matches;

	/* Stops at the depth the parser stops at. */
	if (depth > CASCADE_HAS_DEPTH)
		return 0;

	/* Each element child, itself then its own descendants. */
	for (child = node->first_child; child != NULL; child = child->next) {
		if (child->type != DOM_ELEMENT)
			continue;
		matches = cascade_any_relative(engine, (struct dom_element *)child, simple, anchor);
		if (matches)
			return 1;
		matches = cascade_has_below(engine, child, simple, anchor, depth + 1);
		if (matches)
			return 1;
	}

	/* Nothing below matched. */
	return 0;
}

/* Tells whether an element matches any relative selector of a :has() anchored at anchor. */
static int
cascade_any_relative(
	struct css_engine *engine,
	struct dom_element *element,
	const struct css_simple *simple,
	struct dom_element *anchor)
{
	const struct css_selector *selector;
	size_t index;
	int matches;

	/* Each relative selector. */
	for (index = 0; index < simple->argument_count; index++) {
		selector = &simple->arguments[index];
		matches = cascade_selector_matches(engine, element, selector, selector->count - 1U, anchor);
		if (matches)
			return 1;
	}

	/* None matched. */
	return 0;
}

/*
 * Tells whether an element's place among its siblings matches an
 * :nth-*() (an+b, counting from 1, of every element or of its type, from
 * the start or the end) or a :*-of-type pseudo-class.
 */
static int
cascade_nth(
	struct dom_element *element,
	const struct css_simple *simple)
{
	struct dom_node *node;
	struct dom_element *other;
	int of_type;
	int from_end;
	int place;
	int after;
	int step;

	/* What is counted, and from which end. */
	of_type = 0;
	from_end = 0;
	if (simple->pseudo == CSS_PSEUDO_NTH_OF_TYPE ||
	    simple->pseudo == CSS_PSEUDO_NTH_LAST_OF_TYPE ||
	    simple->pseudo == CSS_PSEUDO_FIRST_OF_TYPE ||
	    simple->pseudo == CSS_PSEUDO_LAST_OF_TYPE ||
	    simple->pseudo == CSS_PSEUDO_ONLY_OF_TYPE)
		of_type = 1;
	if (simple->pseudo == CSS_PSEUDO_NTH_LAST_CHILD ||
	    simple->pseudo == CSS_PSEUDO_NTH_LAST_OF_TYPE ||
	    simple->pseudo == CSS_PSEUDO_LAST_OF_TYPE)
		from_end = 1;

	/* A root, or an element without an element parent, is not counted among siblings. */
	other = cascade_parent_element(element);
	if (other == NULL)
		return 0;

	/* The places before and after the element (1 for the first). */
	place = 1;
	for (node = element->node.previous; node != NULL; node = node->previous) {
		if (node->type != DOM_ELEMENT)
			continue;
		other = (struct dom_element *)node;
		if (!of_type || other->local_name == element->local_name)
			place++;
	}

	/* The places after it, counted the same way. */
	after = 1;
	for (node = element->node.next; node != NULL; node = node->next) {
		if (node->type != DOM_ELEMENT)
			continue;
		other = (struct dom_element *)node;
		if (!of_type || other->local_name == element->local_name)
			after++;
	}

	/* The :*-of-type forms. */
	if (simple->pseudo == CSS_PSEUDO_FIRST_OF_TYPE)
		return place == 1;
	if (simple->pseudo == CSS_PSEUDO_LAST_OF_TYPE)
		return after == 1;
	if (simple->pseudo == CSS_PSEUDO_ONLY_OF_TYPE)
		return place == 1 && after == 1;

	/* an+b from the chosen end: some n >= 0 gives the place. */
	if (from_end)
		place = after;
	if (simple->nth_a == 0)
		return place == simple->nth_b;
	step = place - simple->nth_b;
	if (step % simple->nth_a != 0)
		return 0;

	/* n = step / a must not be negative. */
	return step / simple->nth_a >= 0;
}

/* Tells whether an element is an HTML form element that can be disabled or required. */
static int
cascade_is_form_element(
	const struct dom_element *element)
{
	/* The HTML form elements. */
	if (element->ns != DOM_NS_HTML)
		return 0;
	switch (element->tag) {
	case DOM_TAG_INPUT:
	case DOM_TAG_BUTTON:
	case DOM_TAG_SELECT:
	case DOM_TAG_TEXTAREA:
	case DOM_TAG_OPTION:
	case DOM_TAG_FIELDSET:
		return 1;
	default:
		return 0;
	}
}

/* Compares an attribute's value with an attribute selector's. */
static int
cascade_attribute_matches(
	const struct vm_string *value,
	const struct css_simple *simple)
{
	const struct vm_string *wanted;
	size_t start;
	size_t index;
	size_t length;
	uint16_t a;
	uint16_t b;

	/* Existence alone. */
	if (simple->match == CSS_MATCH_EXISTS)
		return 1;
	wanted = simple->value;

	/* A word of a space-separated list. */
	if (simple->match == CSS_MATCH_INCLUDES)
		return cascade_has_class(value, wanted);

	/* Compares at the place the kind of match asks for. */
	length = wanted->length;
	if (simple->match == CSS_MATCH_EQUAL && value->length != length)
		return 0;
	if (value->length < length)
		return 0;
	if (simple->match == CSS_MATCH_DASH && value->length > length) {
		a = vm_string_at(value, length);
		if (a != '-')
			return 0;
	}

	/* Tries each place the value may start at. */
	for (start = 0; start + length <= value->length; start++) {
		if (simple->match == CSS_MATCH_SUFFIX && start != value->length - length)
			continue;
		for (index = 0; index < length; index++) {
			a = vm_string_at(value, start + index);
			b = vm_string_at(wanted, index);
			if (simple->case_insensitive) {
				if (a >= 'A' && a <= 'Z')
					a = (uint16_t)(a + 0x20U);
				if (b >= 'A' && b <= 'Z')
					b = (uint16_t)(b + 0x20U);
			}

			/* The first difference ends the comparison at this place. */
			if (a != b)
				break;
		}

		/* The whole value matched here. */
		if (index == length)
			return 1;
		if (simple->match != CSS_MATCH_SUBSTRING && simple->match != CSS_MATCH_SUFFIX)
			return 0;
	}

	/* No place matched. */
	return 0;
}

/* Tells whether a whitespace-separated list holds a word. */
static int
cascade_has_class(
	const struct vm_string *classes,
	const struct vm_string *name)
{
	size_t start;
	size_t end;
	size_t index;
	uint16_t unit;
	uint16_t wanted;

	/* Walks the words. */
	start = 0;
	while (start < classes->length) {
		/* Skips whitespace, then finds the word's end. */
		unit = vm_string_at(classes, start);
		if (unit == ' ' || unit == '\t' || unit == '\n' || unit == '\f' || unit == '\r') {
			start++;
			continue;
		}

		/* Finds the end of the word. */
		end = start;
		while (end < classes->length) {
			unit = vm_string_at(classes, end);
			if (unit == ' ' || unit == '\t' || unit == '\n' || unit == '\f' || unit == '\r')
				break;
			end++;
		}

		/* A word of the same length and units is the class. */
		if (end - start == name->length) {
			for (index = 0; index < name->length; index++) {
				unit = vm_string_at(classes, start + index);
				wanted = vm_string_at(name, index);
				if (unit != wanted)
					break;
			}

			/* The whole word matched. */
			if (index == name->length)
				return 1;
		}

		/* On to the next word. */
		start = end;
	}

	/* The word is not in the list. */
	return 0;
}

/* Finds an element's parent element, or NULL. */
static struct dom_element *
cascade_parent_element(
	struct dom_element *element)
{
	/* Only an element parent counts. */
	if (element->node.parent == NULL || element->node.parent->type != DOM_ELEMENT)
		return NULL;

	/* Reports the parent. */
	return (struct dom_element *)element->node.parent;
}

/* Finds the element sibling before an element, or NULL. */
static struct dom_element *
cascade_previous_element(
	struct dom_element *element)
{
	struct dom_node *node;

	/* Walks back over the text and comments. */
	for (node = element->node.previous; node != NULL; node = node->previous) {
		if (node->type == DOM_ELEMENT)
			return (struct dom_element *)node;
	}

	/* No element before it. */
	return NULL;
}

/* Finds the element sibling after an element, or NULL. */
static struct dom_element *
cascade_next_element(
	struct dom_element *element)
{
	struct dom_node *node;

	/* Walks forward over the text and comments. */
	for (node = element->node.next; node != NULL; node = node->next) {
		if (node->type == DOM_ELEMENT)
			return (struct dom_element *)node;
	}

	/* No element after it. */
	return NULL;
}

/* Applies one declaration to the style being computed. */
static void
cascade_apply(
	struct css_engine *engine,
	struct css_style *style,
	const struct css_style *parent,
	const struct css_declaration *declaration)
{
	const struct css_value *value;
	struct css_style initial;
	int property;
	int inherited;
	int parent_keyword;
	float parent_size;

	/* The CSS-wide keywords. */
	value = &declaration->value;
	property = declaration->property;
	if (value->kind == CSS_VALUE_INHERIT || value->kind == CSS_VALUE_UNSET) {
		inherited = property == CSS_PROP_COLOR || property == CSS_PROP_FONT_SIZE || property == CSS_PROP_FONT_WEIGHT ||
		    property == CSS_PROP_FONT_STYLE || property == CSS_PROP_FONT_FAMILY || property == CSS_PROP_LINE_HEIGHT ||
		    property == CSS_PROP_TEXT_ALIGN || property == CSS_PROP_WHITE_SPACE || property == CSS_PROP_VISIBILITY ||
		    property == CSS_PROP_LIST_STYLE_TYPE;
		if (value->kind == CSS_VALUE_INHERIT || inherited) {
			if (parent != NULL)
				cascade_inherit(style, parent, property);
			return;
		}
	}

	/* The initial value. */
	if (value->kind == CSS_VALUE_INITIAL || value->kind == CSS_VALUE_UNSET) {
		css_initial_style(&initial);
		cascade_inherit(style, &initial, property);
		return;
	}

	/* The property's value. */
	switch (property) {
	case CSS_PROP_DISPLAY:
		style->display = value->keyword;
		break;
	case CSS_PROP_POSITION:
		style->position = value->keyword;
		break;
	case CSS_PROP_FLOAT:
		style->float_side = value->keyword;
		break;
	case CSS_PROP_CLEAR:
		style->clear = value->keyword;
		break;
	case CSS_PROP_OVERFLOW:
		style->overflow_x = value->keyword;
		style->overflow_y = value->keyword;
		break;
	case CSS_PROP_OVERFLOW_X:
		style->overflow_x = value->keyword;
		break;
	case CSS_PROP_OVERFLOW_Y:
		style->overflow_y = value->keyword;
		break;
	case CSS_PROP_VISIBILITY:
		style->visibility = value->keyword;
		break;
	case CSS_PROP_WIDTH:
		style->width = cascade_length(engine, value, style->font_size);
		break;
	case CSS_PROP_HEIGHT:
		style->height = cascade_length(engine, value, style->font_size);
		break;
	case CSS_PROP_MIN_WIDTH:
		style->min_width = cascade_length(engine, value, style->font_size);
		break;
	case CSS_PROP_MAX_WIDTH:
		style->max_width = cascade_length(engine, value, style->font_size);
		break;
	case CSS_PROP_MIN_HEIGHT:
		style->min_height = cascade_length(engine, value, style->font_size);
		break;
	case CSS_PROP_MAX_HEIGHT:
		style->max_height = cascade_length(engine, value, style->font_size);
		break;
	case CSS_PROP_MARGIN_TOP:
	case CSS_PROP_MARGIN_RIGHT:
	case CSS_PROP_MARGIN_BOTTOM:
	case CSS_PROP_MARGIN_LEFT:
		style->margin[property - CSS_PROP_MARGIN_TOP] = cascade_length(engine, value, style->font_size);
		break;
	case CSS_PROP_PADDING_TOP:
	case CSS_PROP_PADDING_RIGHT:
	case CSS_PROP_PADDING_BOTTOM:
	case CSS_PROP_PADDING_LEFT:
		style->padding[property - CSS_PROP_PADDING_TOP] = cascade_length(engine, value, style->font_size);
		break;
	case CSS_PROP_TOP:
	case CSS_PROP_RIGHT:
	case CSS_PROP_BOTTOM:
	case CSS_PROP_LEFT:
		style->offset[property - CSS_PROP_TOP] = cascade_length(engine, value, style->font_size);
		break;
	case CSS_PROP_Z_INDEX:
		style->z_index_auto = 1;
		style->z_index = 0;
		if (value->kind == CSS_VALUE_NUMBER)
			style->z_index_auto = 0;
		if (value->kind == CSS_VALUE_NUMBER)
			style->z_index = (int)value->number;
		break;
	case CSS_PROP_BORDER_TOP_WIDTH:
	case CSS_PROP_BORDER_RIGHT_WIDTH:
	case CSS_PROP_BORDER_BOTTOM_WIDTH:
	case CSS_PROP_BORDER_LEFT_WIDTH:
		style->border_width[property - CSS_PROP_BORDER_TOP_WIDTH] = cascade_length(engine, value, style->font_size).value;
		break;
	case CSS_PROP_BORDER_TOP_STYLE:
	case CSS_PROP_BORDER_RIGHT_STYLE:
	case CSS_PROP_BORDER_BOTTOM_STYLE:
	case CSS_PROP_BORDER_LEFT_STYLE:
		style->border_style[property - CSS_PROP_BORDER_TOP_STYLE] = value->keyword;
		break;
	case CSS_PROP_BORDER_TOP_COLOR:
	case CSS_PROP_BORDER_RIGHT_COLOR:
	case CSS_PROP_BORDER_BOTTOM_COLOR:
	case CSS_PROP_BORDER_LEFT_COLOR:
		style->border_color[property - CSS_PROP_BORDER_TOP_COLOR] = value->color;
		break;
	case CSS_PROP_COLOR:
		style->color = value->color;
		if (value->color == CSS_CURRENT_COLOR) {
			/* currentcolor in color itself is the parent's color. */
			style->color = 0xff000000U;
			if (parent != NULL)
				style->color = parent->color;
		}

		break;
	case CSS_PROP_BACKGROUND_COLOR:
		style->background_color = value->color;
		break;
	case CSS_PROP_BACKGROUND_IMAGE:
		style->background_image = NULL;
		if (value->kind == CSS_VALUE_URL)
			style->background_image = value->url;
		break;
	case CSS_PROP_BACKGROUND_REPEAT:
		style->background_repeat = value->keyword;
		break;
	case CSS_PROP_BACKGROUND_POSITION_X:
	case CSS_PROP_BACKGROUND_POSITION_Y:
		style->background_position[property - CSS_PROP_BACKGROUND_POSITION_X] = cascade_length(engine, value, style->font_size);
		break;
	case CSS_PROP_BACKGROUND_SIZE_WIDTH:
		/* contain and cover size both sides; otherwise the width. */
		style->background_size_keyword = CSS_BACKGROUND_SIZE_LENGTHS;
		if (value->kind == CSS_VALUE_KEYWORD && (value->keyword == CSS_BACKGROUND_SIZE_CONTAIN || value->keyword == CSS_BACKGROUND_SIZE_COVER)) {
			style->background_size_keyword = value->keyword;
			style->background_size[0].unit = CSS_UNIT_AUTO;
			break;
		}

		/* A width of lengths. */
		style->background_size[0] = cascade_length(engine, value, style->font_size);
		break;
	case CSS_PROP_BACKGROUND_SIZE_HEIGHT:
		style->background_size[1] = cascade_length(engine, value, style->font_size);
		break;
	case CSS_PROP_FONT_SIZE:
		/* The root measures against the default size, on the keyword scale. */
		parent_size = CASCADE_DEFAULT_FONT_SIZE;
		parent_keyword = 1;
		if (parent != NULL) {
			parent_size = parent->font_size;
			parent_keyword = parent->font_size_keyword;
		}

		/* The size, and whether it still follows the keyword scale. */
		style->font_size = cascade_font_size(engine, value, parent_size);
		style->font_size_keyword = cascade_font_size_keyword(value, parent_keyword);
		break;
	case CSS_PROP_FONT_WEIGHT:
		style->font_weight = value->keyword;
		break;
	case CSS_PROP_FONT_STYLE:
		style->font_italic = value->keyword;
		break;
	case CSS_PROP_FONT_FAMILY:
		cascade_families(style, value);
		break;
	case CSS_PROP_LINE_HEIGHT:
		if (value->kind == CSS_VALUE_NUMBER) {
			style->line_height.unit = CSS_UNIT_NUMBER;
			style->line_height.value = value->number;
		} else {
			style->line_height = cascade_length(engine, value, style->font_size);
			if (style->line_height.unit == CSS_UNIT_PERCENT) {
				style->line_height.unit = CSS_UNIT_PX;
				style->line_height.value = style->font_size * style->line_height.value / 100.0f + style->line_height.offset;
				style->line_height.offset = 0;
			}
		}

		break;
	case CSS_PROP_TEXT_ALIGN:
		style->text_align = value->keyword;
		break;
	case CSS_PROP_WHITE_SPACE:
		style->white_space = value->keyword;
		break;
	case CSS_PROP_TEXT_DECORATION_LINE:
		style->underline = value->keyword;
		break;
	case CSS_PROP_LIST_STYLE_TYPE:
		style->list_style = value->keyword;
		break;
	case CSS_PROP_BOX_SIZING:
		style->box_sizing = value->keyword;
		break;
	case CSS_PROP_FLEX_DIRECTION:
		style->flex_direction = value->keyword;
		break;
	case CSS_PROP_FLEX_WRAP:
		style->flex_wrap = value->keyword;
		break;
	case CSS_PROP_JUSTIFY_CONTENT:
		style->justify_content = value->keyword;
		break;
	case CSS_PROP_ALIGN_ITEMS:
		style->align_items = value->keyword;
		break;
	case CSS_PROP_ALIGN_CONTENT:
		style->align_content = value->keyword;
		break;
	case CSS_PROP_ALIGN_SELF:
		style->align_self = value->keyword;
		break;
	case CSS_PROP_ROW_GAP:
		style->row_gap = cascade_length(engine, value, style->font_size);
		break;
	case CSS_PROP_COLUMN_GAP:
		style->column_gap = cascade_length(engine, value, style->font_size);
		break;
	case CSS_PROP_FLEX_GROW:
		style->flex_grow = value->number;
		break;
	case CSS_PROP_FLEX_SHRINK:
		style->flex_shrink = value->number;
		break;
	case CSS_PROP_FLEX_BASIS:
		style->flex_basis = cascade_length(engine, value, style->font_size);
		break;
	case CSS_PROP_ORDER:
		style->order = (int)value->number;
		break;
	case CSS_PROP_CONTENT:
		/* A list of items, or none (normal and none). */
		style->content_kind = CSS_CONTENT_NONE;
		style->content = NULL;
		if (value->kind == CSS_VALUE_CONTENT) {
			style->content_kind = CSS_CONTENT_LIST;
			style->content = value->content;
		}

		/* The content is set. */
		break;
	default:
		break;
	}
}

/* Copies one property's computed value from another style. */
static void
cascade_inherit(
	struct css_style *style,
	const struct css_style *parent,
	int property)
{
	/* The property's field or fields. */
	switch (property) {
	case CSS_PROP_DISPLAY:
		style->display = parent->display;
		break;
	case CSS_PROP_POSITION:
		style->position = parent->position;
		break;
	case CSS_PROP_FLOAT:
		style->float_side = parent->float_side;
		break;
	case CSS_PROP_CLEAR:
		style->clear = parent->clear;
		break;
	case CSS_PROP_OVERFLOW:
		style->overflow_x = parent->overflow_x;
		style->overflow_y = parent->overflow_y;
		break;
	case CSS_PROP_OVERFLOW_X:
		style->overflow_x = parent->overflow_x;
		break;
	case CSS_PROP_OVERFLOW_Y:
		style->overflow_y = parent->overflow_y;
		break;
	case CSS_PROP_VISIBILITY:
		style->visibility = parent->visibility;
		break;
	case CSS_PROP_BOX_SIZING:
		style->box_sizing = parent->box_sizing;
		break;
	case CSS_PROP_CONTENT:
		style->content_kind = parent->content_kind;
		style->content = parent->content;
		break;
	case CSS_PROP_FLEX_DIRECTION:
		style->flex_direction = parent->flex_direction;
		break;
	case CSS_PROP_FLEX_WRAP:
		style->flex_wrap = parent->flex_wrap;
		break;
	case CSS_PROP_JUSTIFY_CONTENT:
		style->justify_content = parent->justify_content;
		break;
	case CSS_PROP_ALIGN_ITEMS:
		style->align_items = parent->align_items;
		break;
	case CSS_PROP_ALIGN_CONTENT:
		style->align_content = parent->align_content;
		break;
	case CSS_PROP_ALIGN_SELF:
		style->align_self = parent->align_self;
		break;
	case CSS_PROP_ROW_GAP:
		style->row_gap = parent->row_gap;
		break;
	case CSS_PROP_COLUMN_GAP:
		style->column_gap = parent->column_gap;
		break;
	case CSS_PROP_FLEX_GROW:
		style->flex_grow = parent->flex_grow;
		break;
	case CSS_PROP_FLEX_SHRINK:
		style->flex_shrink = parent->flex_shrink;
		break;
	case CSS_PROP_FLEX_BASIS:
		style->flex_basis = parent->flex_basis;
		break;
	case CSS_PROP_ORDER:
		style->order = parent->order;
		break;
	case CSS_PROP_WIDTH:
		style->width = parent->width;
		break;
	case CSS_PROP_HEIGHT:
		style->height = parent->height;
		break;
	case CSS_PROP_MIN_WIDTH:
		style->min_width = parent->min_width;
		break;
	case CSS_PROP_MAX_WIDTH:
		style->max_width = parent->max_width;
		break;
	case CSS_PROP_MIN_HEIGHT:
		style->min_height = parent->min_height;
		break;
	case CSS_PROP_MAX_HEIGHT:
		style->max_height = parent->max_height;
		break;
	case CSS_PROP_MARGIN_TOP:
	case CSS_PROP_MARGIN_RIGHT:
	case CSS_PROP_MARGIN_BOTTOM:
	case CSS_PROP_MARGIN_LEFT:
		style->margin[property - CSS_PROP_MARGIN_TOP] = parent->margin[property - CSS_PROP_MARGIN_TOP];
		break;
	case CSS_PROP_PADDING_TOP:
	case CSS_PROP_PADDING_RIGHT:
	case CSS_PROP_PADDING_BOTTOM:
	case CSS_PROP_PADDING_LEFT:
		style->padding[property - CSS_PROP_PADDING_TOP] = parent->padding[property - CSS_PROP_PADDING_TOP];
		break;
	case CSS_PROP_TOP:
	case CSS_PROP_RIGHT:
	case CSS_PROP_BOTTOM:
	case CSS_PROP_LEFT:
		style->offset[property - CSS_PROP_TOP] = parent->offset[property - CSS_PROP_TOP];
		break;
	case CSS_PROP_Z_INDEX:
		style->z_index = parent->z_index;
		style->z_index_auto = parent->z_index_auto;
		break;
	case CSS_PROP_BORDER_TOP_WIDTH:
	case CSS_PROP_BORDER_RIGHT_WIDTH:
	case CSS_PROP_BORDER_BOTTOM_WIDTH:
	case CSS_PROP_BORDER_LEFT_WIDTH:
		style->border_width[property - CSS_PROP_BORDER_TOP_WIDTH] = parent->border_width[property - CSS_PROP_BORDER_TOP_WIDTH];
		break;
	case CSS_PROP_BORDER_TOP_STYLE:
	case CSS_PROP_BORDER_RIGHT_STYLE:
	case CSS_PROP_BORDER_BOTTOM_STYLE:
	case CSS_PROP_BORDER_LEFT_STYLE:
		style->border_style[property - CSS_PROP_BORDER_TOP_STYLE] = parent->border_style[property - CSS_PROP_BORDER_TOP_STYLE];
		break;
	case CSS_PROP_BORDER_TOP_COLOR:
	case CSS_PROP_BORDER_RIGHT_COLOR:
	case CSS_PROP_BORDER_BOTTOM_COLOR:
	case CSS_PROP_BORDER_LEFT_COLOR:
		style->border_color[property - CSS_PROP_BORDER_TOP_COLOR] = parent->border_color[property - CSS_PROP_BORDER_TOP_COLOR];
		break;
	case CSS_PROP_COLOR:
		style->color = parent->color;
		break;
	case CSS_PROP_BACKGROUND_COLOR:
		style->background_color = parent->background_color;
		break;
	case CSS_PROP_BACKGROUND_IMAGE:
		style->background_image = parent->background_image;
		break;
	case CSS_PROP_BACKGROUND_REPEAT:
		style->background_repeat = parent->background_repeat;
		break;
	case CSS_PROP_BACKGROUND_POSITION_X:
	case CSS_PROP_BACKGROUND_POSITION_Y:
		style->background_position[property - CSS_PROP_BACKGROUND_POSITION_X] = parent->background_position[property - CSS_PROP_BACKGROUND_POSITION_X];
		break;
	case CSS_PROP_BACKGROUND_SIZE_WIDTH:
		style->background_size_keyword = parent->background_size_keyword;
		style->background_size[0] = parent->background_size[0];
		break;
	case CSS_PROP_BACKGROUND_SIZE_HEIGHT:
		style->background_size[1] = parent->background_size[1];
		break;
	case CSS_PROP_FONT_SIZE:
		style->font_size = parent->font_size;
		style->font_size_keyword = parent->font_size_keyword;
		break;
	case CSS_PROP_FONT_WEIGHT:
		style->font_weight = parent->font_weight;
		break;
	case CSS_PROP_FONT_STYLE:
		style->font_italic = parent->font_italic;
		break;
	case CSS_PROP_FONT_FAMILY:
		style->generic_family = parent->generic_family;
		memcpy(style->families, parent->families, sizeof(style->families));
		style->family_count = parent->family_count;
		break;
	case CSS_PROP_LINE_HEIGHT:
		style->line_height = parent->line_height;
		break;
	case CSS_PROP_TEXT_ALIGN:
		style->text_align = parent->text_align;
		break;
	case CSS_PROP_WHITE_SPACE:
		style->white_space = parent->white_space;
		break;
	case CSS_PROP_TEXT_DECORATION_LINE:
		style->underline = parent->underline;
		break;
	case CSS_PROP_LIST_STYLE_TYPE:
		style->list_style = parent->list_style;
		break;
	default:
		break;
	}
}

/* Converts a declared length to a computed one: pixels, a percentage or a keyword. */
static struct css_length
cascade_length(
	struct css_engine *engine,
	const struct css_value *value,
	float font_size)
{
	struct css_length length;

	/* Keywords keep their unit. */
	length.value = 0;
	length.unit = CSS_UNIT_PX;
	length.offset = 0;
	if (value->kind == CSS_VALUE_KEYWORD) {
		length.unit = value->keyword;
		return length;
	}

	/* A calculation. */
	if (value->unit == CSS_DUNIT_CALC && value->calc != NULL) {
		length = cascade_calc(engine, value->calc, font_size);
		return length;
	}

	/* Each unit's size in pixels. */
	switch (value->unit) {
	case CSS_DUNIT_PX:
		length.value = value->number;
		break;
	case CSS_DUNIT_EM:
		length.value = value->number * font_size;
		break;
	case CSS_DUNIT_REM:
		length.value = value->number * engine->root_font_size;
		break;
	case CSS_DUNIT_EX:
		length.value = value->number * font_size * 0.5f;
		break;
	case CSS_DUNIT_PERCENT:
		length.value = value->number;
		length.unit = CSS_UNIT_PERCENT;
		break;
	case CSS_DUNIT_PT:
		length.value = value->number * 96.0f / 72.0f;
		break;
	case CSS_DUNIT_PC:
		length.value = value->number * 16.0f;
		break;
	case CSS_DUNIT_IN:
		length.value = value->number * 96.0f;
		break;
	case CSS_DUNIT_CM:
		length.value = value->number * 96.0f / 2.54f;
		break;
	case CSS_DUNIT_MM:
		length.value = value->number * 96.0f / 25.4f;
		break;
	case CSS_DUNIT_VW:
		length.value = value->number * engine->viewport_width / 100.0f;
		break;
	case CSS_DUNIT_VH:
		length.value = value->number * engine->viewport_height / 100.0f;
		break;
	case CSS_DUNIT_VMIN:
		length.value = value->number * engine->viewport_width / 100.0f;
		if (engine->viewport_height < engine->viewport_width)
			length.value = value->number * engine->viewport_height / 100.0f;
		break;
	case CSS_DUNIT_VMAX:
		length.value = value->number * engine->viewport_width / 100.0f;
		if (engine->viewport_height > engine->viewport_width)
			length.value = value->number * engine->viewport_height / 100.0f;
		break;
	default:
		break;
	}

	/* Reports the computed length. */
	return length;
}

/* Computes a font size from its declared value and the parent's size. */
static float
cascade_font_size(
	struct css_engine *engine,
	const struct css_value *value,
	float parent_size)
{
	struct css_length length;

	/* em and percentages measure the parent's size. */
	if (value->kind == CSS_VALUE_LENGTH && value->unit == CSS_DUNIT_PERCENT)
		return parent_size * value->number / 100.0f;

	/* So do a calculation's. */
	if (value->kind == CSS_VALUE_LENGTH && value->unit == CSS_DUNIT_CALC && value->calc != NULL) {
		length = cascade_calc(engine, value->calc, parent_size);
		if (length.unit == CSS_UNIT_PERCENT)
			return parent_size * length.value / 100.0f + length.offset;
		return length.value;
	}

	/* A keyword is its size at the default size. */
	if (value->kind == CSS_VALUE_LENGTH && value->unit == CSS_DUNIT_FONT_KEYWORD)
		return value->number;

	length = cascade_length(engine, value, parent_size);
	if (length.unit != CSS_UNIT_PX)
		return parent_size;

	/* Reports the size in pixels. */
	return length.value;
}

/* Records a font-family list: the names, and the generic family the list ends in. */
static void
cascade_families(
	struct css_style *style,
	const struct css_value *value)
{
	int index;
	int monospace;
	int sans_serif;
	int serif;

	/* Keeps the names; the last generic name in the list picks the generic family. */
	style->family_count = value->family_count;
	for (index = 0; index < value->family_count; index++) {
		style->families[index] = value->families[index];
		monospace = vm_string_equal_ascii(value->families[index], "monospace");
		sans_serif = vm_string_equal_ascii(value->families[index], "sans-serif");
		serif = vm_string_equal_ascii(value->families[index], "serif");
		if (monospace) {
			style->generic_family = CSS_FAMILY_MONOSPACE;
		} else if (sans_serif) {
			style->generic_family = CSS_FAMILY_SANS_SERIF;
		} else if (serif) {
			style->generic_family = CSS_FAMILY_SERIF;
		}
	}
}

/* Tells whether a declared font size keeps a size on the keyword scale. */
static int
cascade_font_size_keyword(
	const struct css_value *value,
	int parent_keyword)
{
	/* A keyword is on the scale by definition. */
	if (value->kind == CSS_VALUE_LENGTH && value->unit == CSS_DUNIT_FONT_KEYWORD)
		return 1;

	/* An em or a percentage keeps the parent's scale. */
	if (value->kind == CSS_VALUE_LENGTH && value->unit == CSS_DUNIT_EM)
		return parent_keyword;
	if (value->kind == CSS_VALUE_LENGTH && value->unit == CSS_DUNIT_PERCENT)
		return parent_keyword;

	/* Any other length is absolute. */
	return 0;
}

/* Tells whether a style's font family is the monospace family and nothing else. */
static int
cascade_monospace_only(
	const struct css_style *style)
{
	int monospace;

	/* Only a list of one name can be monospace alone. */
	if (style->family_count != 1)
		return 0;

	/* That name must be the generic monospace. */
	monospace = vm_string_equal_ascii(style->families[0], "monospace");
	if (!monospace)
		return 0;

	/* The family is monospace alone. */
	return 1;
}

/*
 * Rescales a font size on the keyword scale when the family becomes, or
 * stops being, the monospace family alone.
 *
 * Chromium's default monospace size is 13 pixels against 16 for the other
 * families, and a size that follows the keywords follows that default: a
 * keyword declared here was sized for the other families, and an inherited
 * or relative size for the parent's family.
 */
static void
cascade_monospace_size(
	struct css_style *style,
	const struct css_style *parent,
	const struct css_declaration *font_size)
{
	int reference_monospace;
	int monospace;

	/* An absolute size is not rescaled. */
	if (!style->font_size_keyword)
		return;

	/* The family the size was measured for: the parent's, or the others' for a keyword declared here. */
	reference_monospace = 0;
	if (parent != NULL)
		reference_monospace = cascade_monospace_only(parent);
	if (font_size != NULL &&
	    font_size->value.kind == CSS_VALUE_LENGTH &&
	    font_size->value.unit == CSS_DUNIT_FONT_KEYWORD)
		reference_monospace = 0;

	/* Nothing changes while the family stays on the same side. */
	monospace = cascade_monospace_only(style);
	if (monospace == reference_monospace)
		return;

	/* Scales the size by the ratio of the two defaults. */
	if (monospace) {
		style->font_size = style->font_size * CASCADE_MONOSPACE_FONT_SIZE / CASCADE_DEFAULT_FONT_SIZE;
	} else {
		style->font_size = style->font_size * CASCADE_DEFAULT_FONT_SIZE / CASCADE_MONOSPACE_FONT_SIZE;
	}
}
