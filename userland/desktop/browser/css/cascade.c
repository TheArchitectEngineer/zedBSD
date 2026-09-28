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

/* The precedence of a declaration by its origin and importance (higher wins). */
#define RANK_USER_AGENT		0
#define RANK_AUTHOR		1
#define RANK_AUTHOR_IMPORTANT	2
#define RANK_USER_AGENT_IMPORTANT	3

/*
 * The style sheets of a document and the atoms the matching looks up.
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
};

/*
 * One declaration that applies to the element being styled, with what
 * orders it among the others.
 */
struct cascade_match {
	const struct css_declaration *declaration;
	int rank;
	uint32_t specificity;
	uint32_t order;
};

static int cascade_collect(struct css_engine *engine, struct dom_element *element, struct wb_vector *matches, struct wb_arena *scratch);
static int cascade_add_declarations(struct wb_vector *matches, const struct css_declaration *declarations, size_t count, int origin, uint32_t specificity, uint32_t order);
static int cascade_compare(const void *left, const void *right);
static int cascade_selector_matches(struct css_engine *engine, struct dom_element *element, const struct css_selector *selector, size_t index);
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
	wb_vector_init(&created->sheets, sizeof(struct css_sheet *));
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
	struct css_sheet **sheets;
	size_t index;

	/* A NULL engine is nothing to destroy. */
	if (engine == NULL)
		return;

	/* Frees every sheet, then the engine. */
	sheets = engine->sheets.items;
	for (index = 0; index < engine->sheets.count; index++) {
		css_sheet_release(sheets[index]);
		free(sheets[index]);
	}

	/* Frees the list and the engine. */
	wb_vector_release(&engine->sheets);
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
	struct css_sheet *sheet;
	int error;

	/* Parses the sheet. */
	sheet = calloc(1, sizeof(*sheet));
	if (sheet == NULL)
		return ENOMEM;
	error = css_parse_sheet(engine->heap, units, length, origin, sheet);
	if (error != 0) {
		css_sheet_release(sheet);
		free(sheet);
		return error;
	}

	/* Appends it. */
	error = wb_vector_push(&engine->sheets, &sheet);
	if (error != 0) {
		css_sheet_release(sheet);
		free(sheet);
		return error;
	}

	/* Succeeded: the sheet is the engine's last. */
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
	}

	/* Gathers the declarations that apply, in cascade order. */
	wb_vector_init(&list, sizeof(struct cascade_match));
	wb_arena_init(&scratch, 0);
	error = cascade_collect(engine, element, &list, &scratch);
	if (error != 0) {
		wb_vector_release(&list);
		wb_arena_release(&scratch);
		return error;
	}

	/* Sorts them into cascade order (an empty list has no storage to sort). */
	matches = list.items;
	if (list.count > 1)
		qsort(matches, list.count, sizeof(*matches), cascade_compare);

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

	/* The root's font size is what rem measures. */
	is_root = 0;
	if (element->node.parent != NULL && element->node.parent->type == DOM_DOCUMENT)
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
}

/* Gathers every declaration that applies to an element: the matching rules' and the style attribute's. */
static int
cascade_collect(
	struct css_engine *engine,
	struct dom_element *element,
	struct wb_vector *matches,
	struct wb_arena *scratch)
{
	struct css_sheet **sheets;
	struct css_declaration *declarations;
	const struct css_rule *rule;
	struct dom_attribute *attribute;
	struct wb_units units;
	uint32_t best;
	size_t declaration_count;
	size_t sheet;
	size_t index;
	size_t selector;
	uint32_t order;
	int matched;
	int selector_matched;
	int error;

	/* The rules of every sheet whose selectors match, each once with its most specific selector. */
	sheets = engine->sheets.items;
	order = 0;
	for (sheet = 0; sheet < engine->sheets.count; sheet++) {
		for (index = 0; index < sheets[sheet]->rule_count; index++) {
			rule = &sheets[sheet]->rules[index];
			order++;
			matched = 0;
			best = 0;
			for (selector = 0; selector < rule->selector_count; selector++) {
				if (rule->selectors[selector].count == 0)
					continue;
				selector_matched = cascade_selector_matches(engine, element, &rule->selectors[selector], rule->selectors[selector].count - 1U);
				if (selector_matched) {
					if (!matched || rule->selectors[selector].specificity > best)
						best = rule->selectors[selector].specificity;
					matched = 1;
				}
			}

			/* A rule no selector matched does not apply. */
			if (!matched)
				continue;
			error = cascade_add_declarations(matches, rule->declarations, rule->declaration_count, sheets[sheet]->origin, best, order);
			if (error != 0)
				return error;
		}
	}

	/* The style attribute, more specific than any selector. */
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
	error = cascade_add_declarations(matches, declarations, declaration_count, CSS_ORIGIN_AUTHOR, 0xffffffffU, order + 1U);
	if (error != 0)
		return error;

	/* Succeeded: every applying declaration is gathered. */
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
	uint32_t order)
{
	struct cascade_match match;
	size_t index;
	int error;

	/* Each declaration with its rank. */
	for (index = 0; index < count; index++) {
		match.declaration = &declarations[index];
		match.specificity = specificity;
		match.order = order * 4096U + (uint32_t)index;
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

/* Tells whether an element matches a selector's compounds up to index, right to left. */
static int
cascade_selector_matches(
	struct css_engine *engine,
	struct dom_element *element,
	const struct css_selector *selector,
	size_t index)
{
	const struct css_compound *compound;
	struct dom_element *other;
	int matches;

	/* The compound at index must match the element itself. */
	compound = &selector->compounds[index];
	matches = cascade_compound_matches(engine, element, compound);
	if (!matches)
		return 0;
	if (index == 0)
		return 1;

	/* Then the compound on its left, through the combinator. */
	switch (compound->combinator) {
	case CSS_COMBINATOR_CHILD:
		other = cascade_parent_element(element);
		if (other == NULL)
			return 0;
		return cascade_selector_matches(engine, other, selector, index - 1U);
	case CSS_COMBINATOR_DESCENDANT:
		for (other = cascade_parent_element(element); other != NULL; other = cascade_parent_element(other)) {
			matches = cascade_selector_matches(engine, other, selector, index - 1U);
			if (matches)
				return 1;
		}

		/* Otherwise no descendant relation holds. */
		return 0;
	case CSS_COMBINATOR_NEXT:
		other = cascade_previous_element(element);
		if (other == NULL)
			return 0;
		return cascade_selector_matches(engine, other, selector, index - 1U);
	case CSS_COMBINATOR_SUBSEQUENT:
		for (other = cascade_previous_element(element); other != NULL; other = cascade_previous_element(other)) {
			matches = cascade_selector_matches(engine, other, selector, index - 1U);
			if (matches)
				return 1;
		}

		/* Otherwise no earlier sibling matched. */
		return 0;
	default:
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
	struct dom_node *child;

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
		default:
			return 0;
		}
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
				style->line_height.value = style->font_size * style->line_height.value / 100.0f;
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
	if (value->kind == CSS_VALUE_KEYWORD) {
		length.unit = value->keyword;
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
