/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Markup for scripts (ws074-p081): innerHTML and outerHTML, and
 * insertAdjacentHTML, insertAdjacentElement and insertAdjacentText.
 *
 * Reading serializes the nodes (dom/serialize.c) with scripting enabled,
 * as the page runs scripts.  Writing parses the text as an HTML fragment
 * in the context of an element (html_parser_create_fragment): the
 * element itself for innerHTML and for insertAdjacentHTML inside it, its
 * parent for outerHTML and for insertAdjacentHTML beside it, and a body
 * element in place of a fragment or of the html element.  The script
 * elements of a fragment do not run, as in other browsers.
 */

#include "bind/internal.h"
#include "html/html.h"

#include <errno.h>
#include <string.h>

/*
 * Where insertAdjacent* puts a node relative to the element.
 */
enum markup_position {
	MARKUP_BEFORE_BEGIN,
	MARKUP_AFTER_BEGIN,
	MARKUP_BEFORE_END,
	MARKUP_AFTER_END
};

static int markup_element_this(struct vm_realm *realm, vm_value this_value, struct dom_element **element);
static int markup_serialize(struct vm_realm *realm, const struct dom_node *node, int self, vm_value *result);
static int markup_parse(struct vm_realm *realm, struct dom_element *context, vm_value text, struct dom_node **fragment);
static int markup_context(struct bind_window *window, struct dom_node *node, struct dom_element **context);
static int markup_position(struct vm_realm *realm, vm_value value, int *position);
static int markup_insert(struct vm_realm *realm, struct dom_element *element, int position, struct dom_node *node, int *inserted);

/*
 * Reports the HTML of an element's children, or of a template's contents
 * (innerHTML).
 */
int
bind_inner_html_get(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct dom_element *element;
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The element. */
	status = markup_element_this(realm, this_value, &element);
	if (status != 0)
		return status;

	/* Succeeded: its children's HTML. */
	status = markup_serialize(realm, &element->node, 0, result);
	if (status != 0)
		return status;
	return 0;
}

/*
 * Replaces an element's children (or a template's contents) with the
 * nodes of an HTML fragment (innerHTML).
 */
int
bind_inner_html_set(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct dom_element *element;
	struct dom_node *target;
	struct dom_node *fragment;
	int status;

	/* The element, and the fragment parsed in its context. */
	*result = VM_VALUE_UNDEFINED;
	status = markup_element_this(realm, this_value, &element);
	if (status != 0)
		return status;
	status = markup_parse(realm, element, js_argument(args, count, 0), &fragment);
	if (status != 0)
		return status;

	/* A template's contents take the nodes, and any other element's children. */
	target = &element->node;
	if (element->content != NULL)
		target = element->content;

	/* The old children go. */
	while (target->first_child != NULL)
		dom_remove(target->first_child);

	/* Succeeded: the fragment's nodes are the children. */
	status = bind_insert(realm, target, fragment, NULL);
	if (status != 0)
		return status;
	return 0;
}

/*
 * Reports the HTML of an element itself with its descendants (outerHTML).
 */
int
bind_outer_html_get(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct dom_element *element;
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The element. */
	status = markup_element_this(realm, this_value, &element);
	if (status != 0)
		return status;

	/* Succeeded: its HTML. */
	status = markup_serialize(realm, &element->node, 1, result);
	if (status != 0)
		return status;
	return 0;
}

/*
 * Replaces an element with the nodes of an HTML fragment parsed in its
 * parent's context (outerHTML); an element without a parent is left, and
 * a child of the document cannot be replaced.
 */
int
bind_outer_html_set(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct bind_window *window;
	struct dom_element *element;
	struct dom_element *context;
	struct dom_node *parent;
	struct dom_node *fragment;
	int status;

	/* The element and its parent. */
	*result = VM_VALUE_UNDEFINED;
	window = bind_window_of(realm);
	status = markup_element_this(realm, this_value, &element);
	if (status != 0)
		return status;
	parent = element->node.parent;

	/* Without a parent nothing happens; the document's element cannot be replaced. */
	if (parent == NULL)
		return 0;
	if (parent->type == DOM_DOCUMENT) {
		status = bind_throw_dom(realm, "NoModificationAllowedError", "The element's parent is the document.");
		return status;
	}

	/* The fragment, parsed in the parent's context. */
	status = markup_context(window, parent, &context);
	if (status != 0)
		return status;
	status = markup_parse(realm, context, js_argument(args, count, 0), &fragment);
	if (status != 0)
		return status;

	/* Its nodes go where the element was. */
	status = bind_insert(realm, parent, fragment, &element->node);
	if (status != 0)
		return status;

	/* Succeeded: the element goes. */
	dom_remove(&element->node);
	return 0;
}

/*
 * Parses HTML and inserts its nodes before the element, as its first or
 * last children, or after it (insertAdjacentHTML).
 */
int
bind_insert_adjacent_html(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct bind_window *window;
	struct dom_element *element;
	struct dom_element *context;
	struct dom_node *place;
	struct dom_node *fragment;
	int position;
	int inserted;
	int status;

	/* The element and the position. */
	*result = VM_VALUE_UNDEFINED;
	window = bind_window_of(realm);
	status = markup_element_this(realm, this_value, &element);
	if (status != 0)
		return status;
	status = markup_position(realm, js_argument(args, count, 0), &position);
	if (status != 0)
		return status;

	/* Beside the element the parent is the context, which must be an element or a fragment. */
	place = &element->node;
	if (position == MARKUP_BEFORE_BEGIN || position == MARKUP_AFTER_END) {
		place = element->node.parent;
		if (place == NULL || place->type == DOM_DOCUMENT) {
			status = bind_throw_dom(realm, "NoModificationAllowedError", "The element has no parent.");
			return status;
		}
	}

	/* The fragment, parsed in that context. */
	status = markup_context(window, place, &context);
	if (status != 0)
		return status;
	status = markup_parse(realm, context, js_argument(args, count, 1), &fragment);
	if (status != 0)
		return status;

	/* Succeeded: its nodes are inserted. */
	status = markup_insert(realm, element, position, fragment, &inserted);
	if (status != 0)
		return status;
	return 0;
}

/*
 * Inserts an element before, at the start of, at the end of or after the
 * element, and reports it, or null when there is no parent to insert it
 * beside the element in (insertAdjacentElement).
 */
int
bind_insert_adjacent_element(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct dom_element *element;
	struct dom_node *node;
	vm_value given;
	int position;
	int inserted;
	int status;

	/* The element, the position and the element to insert. */
	status = markup_element_this(realm, this_value, &element);
	if (status != 0)
		return status;
	status = markup_position(realm, js_argument(args, count, 0), &position);
	if (status != 0)
		return status;
	given = js_argument(args, count, 1);
	node = bind_node_of(given);
	if (node == NULL || node->type != DOM_ELEMENT) {
		status = vm_throw_type_error(realm, "Failed to execute 'insertAdjacentElement' on 'Element': parameter 2 is not of type 'Element'.");
		return status;
	}

	/* The insertion. */
	status = markup_insert(realm, element, position, node, &inserted);
	if (status != 0)
		return status;

	/* Nowhere to insert it reports null. */
	if (!inserted) {
		*result = VM_VALUE_NULL;
		return 0;
	}

	/* Succeeded: the element inserted. */
	*result = given;
	return 0;
}

/*
 * Inserts a text node before, at the start of, at the end of or after the
 * element (insertAdjacentText).
 */
int
bind_insert_adjacent_text(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct bind_window *window;
	struct dom_element *element;
	struct dom_node *text;
	struct vm_string *data;
	struct wb_units units;
	int position;
	int inserted;
	int status;

	/* The element, the position and the text. */
	*result = VM_VALUE_UNDEFINED;
	window = bind_window_of(realm);
	status = markup_element_this(realm, this_value, &element);
	if (status != 0)
		return status;
	status = markup_position(realm, js_argument(args, count, 0), &position);
	if (status != 0)
		return status;
	status = bind_to_string(realm, js_argument(args, count, 1), &data);
	if (status != 0)
		return status;

	/* The text node. */
	wb_units_init(&units);
	status = vm_string_append_units(data, &units);
	if (status != 0) {
		wb_units_release(&units);
		return status;
	}
	text = dom_text_create(window->document, units.data, units.length);
	wb_units_release(&units);
	if (text == NULL)
		return ENOMEM;

	/* Succeeded: it is inserted (nowhere is no failure). */
	status = markup_insert(realm, element, position, text, &inserted);
	if (status != 0)
		return status;
	return 0;
}

/* Finds the element a method's this value stands for, throwing a TypeError otherwise. */
static int
markup_element_this(
	struct vm_realm *realm,
	vm_value this_value,
	struct dom_element **element)
{
	struct dom_node *node;
	int status;

	/* The node, which must be an element. */
	node = bind_node_of(this_value);
	if (node == NULL || node->type != DOM_ELEMENT) {
		status = bind_throw_illegal(realm);
		return status;
	}

	/* Succeeded: the element. */
	*element = (struct dom_element *)node;
	return 0;
}

/* Reports the HTML of a node's children, or with self of the node itself, as a string. */
static int
markup_serialize(
	struct vm_realm *realm,
	const struct dom_node *node,
	int self,
	vm_value *result)
{
	struct wb_units units;
	int status;

	/* The HTML, with scripting enabled. */
	wb_units_init(&units);
	if (self) {
		status = dom_serialize_node(node, 1, &units);
	} else {
		status = dom_serialize_children(node, 1, &units);
	}
	if (status != 0) {
		wb_units_release(&units);
		return status;
	}

	/* Succeeded: as a string. */
	status = bind_units(realm, units.data, units.length, result);
	wb_units_release(&units);
	if (status != 0)
		return status;
	return 0;
}

/*
 * Parses a value's string as an HTML fragment in the context of an
 * element, into a new DocumentFragment of the context's document.
 */
static int
markup_parse(
	struct vm_realm *realm,
	struct dom_element *context,
	vm_value text,
	struct dom_node **fragment)
{
	struct html_parser *parser;
	struct dom_element *root;
	struct dom_node *made;
	struct vm_string *string;
	struct wb_units units;
	int status;

	/* The text's characters. */
	status = bind_to_string(realm, text, &string);
	if (status != 0)
		return status;
	wb_units_init(&units);
	status = vm_string_append_units(string, &units);
	if (status != 0) {
		wb_units_release(&units);
		return status;
	}

	/* The fragment parser, which is fed the text and finished. */
	status = html_parser_create_fragment(&parser, context, 1);
	if (status != 0) {
		wb_units_release(&units);
		return status;
	}
	status = html_parser_feed(parser, units.data, units.length);
	if (status == 0)
		status = html_parser_finish(parser);
	wb_units_release(&units);
	if (status != 0) {
		html_parser_destroy(parser);
		return status;
	}

	/* The DocumentFragment the root's children move to. */
	made = dom_fragment_create(context->node.document);
	if (made == NULL) {
		html_parser_destroy(parser);
		return ENOMEM;
	}
	root = html_parser_fragment_root(parser);
	while (root->node.first_child != NULL)
		dom_insert_before(made, root->node.first_child, NULL);
	html_parser_destroy(parser);

	/* Succeeded: the fragment. */
	*fragment = made;
	return 0;
}

/*
 * Chooses the context element for a fragment inserted in a node: the node
 * itself when it is an element other than html, and a new body element
 * otherwise (a DocumentFragment, or the html element).
 */
static int
markup_context(
	struct bind_window *window,
	struct dom_node *node,
	struct dom_element **context)
{
	struct vm_string *body;
	int html;

	/* An element other than html is the context. */
	html = 0;
	if (node->type == DOM_ELEMENT)
		html = dom_element_is(node, DOM_NS_HTML, DOM_TAG_HTML);
	if (node->type == DOM_ELEMENT && !html) {
		*context = (struct dom_element *)node;
		return 0;
	}

	/* Otherwise a body element of the document. */
	body = vm_atom_from_ascii(window->realm->heap, "body");
	if (body == NULL)
		return ENOMEM;
	*context = dom_element_create(window->document, DOM_NS_HTML, body, NULL);
	if (*context == NULL)
		return ENOMEM;

	/* Succeeded: the body. */
	return 0;
}

/*
 * Reads an insertAdjacent* position, in any ASCII case; any other word
 * throws a SyntaxError.
 */
static int
markup_position(
	struct vm_realm *realm,
	vm_value value,
	int *position)
{
	static const char *const names[] = { "beforebegin", "afterbegin", "beforeend", "afterend", NULL };
	struct vm_string *word;
	char folded[16];
	size_t index;
	uint16_t unit;
	int same;
	int status;

	/* The word. */
	status = bind_to_string(realm, value, &word);
	if (status != 0)
		return status;

	/* In lower case, when it is short enough to be one of them. */
	folded[0] = '\0';
	if (word->length < sizeof(folded)) {
		for (index = 0; index < word->length; index++) {
			unit = vm_string_at(word, index);
			if (unit >= 'A' && unit <= 'Z')
				unit = (uint16_t)(unit + 0x20U);
			if (unit > 0x7fU)
				unit = '?';
			folded[index] = (char)unit;
		}
		folded[word->length] = '\0';
	}

	/* Compares it with each position's name. */
	for (index = 0; names[index] != NULL; index++) {
		same = strcmp(folded, names[index]);
		if (same == 0) {
			*position = (int)index;
			return 0;
		}
	}

	/* No position. */
	status = bind_throw_dom(realm, "SyntaxError", "The value provided is not one of 'beforeBegin', 'afterBegin', 'beforeEnd', or 'afterEnd'.");
	return status;
}

/*
 * Inserts a node (a DocumentFragment's children, or a node) at a position
 * relative to an element; inserted is 0 when there is no parent to insert
 * beside the element in.
 */
static int
markup_insert(
	struct vm_realm *realm,
	struct dom_element *element,
	int position,
	struct dom_node *node,
	int *inserted)
{
	struct dom_node *parent;
	int status;

	/* Chooses where the node goes. */
	*inserted = 0;
	parent = element->node.parent;
	switch (position) {
	case MARKUP_BEFORE_BEGIN:
		if (parent == NULL)
			return 0;
		status = bind_insert(realm, parent, node, &element->node);
		break;
	case MARKUP_AFTER_BEGIN:
		status = bind_insert(realm, &element->node, node, element->node.first_child);
		break;
	case MARKUP_BEFORE_END:
		status = bind_insert(realm, &element->node, node, NULL);
		break;
	default:
		if (parent == NULL)
			return 0;
		status = bind_insert(realm, parent, node, element->node.next);
		break;
	}

	/* A failed insertion. */
	if (status != 0)
		return status;

	/* Succeeded: the node is inserted. */
	*inserted = 1;
	return 0;
}
