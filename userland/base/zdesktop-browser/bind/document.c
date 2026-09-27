/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Document interface: the document's parts (its root element, head,
 * body and title), finding elements, and making nodes.
 */

#include "bind/internal.h"

#include <errno.h>
#include <string.h>

static int document_this(struct vm_realm *realm, vm_value this_value, struct dom_document **document);
static struct dom_element *document_html_child(const struct dom_document *document, int tag);
static struct dom_node *document_find_title(const struct dom_document *document);
static int document_element(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int document_head(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int document_body(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int document_doctype(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int document_title_get(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int document_title_set(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int document_ready_state(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int document_default_view(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int document_get_element_by_id(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int document_create_element(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int document_create_text_node(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int document_create_comment(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int document_create_fragment(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int document_create_character_data(struct vm_realm *realm, vm_value this_value, int type, const vm_value *args, unsigned count, vm_value *result);

/*
 * The attributes of Document, with ParentNode's.  The table is constant
 * for the life of the program.
 */
static const struct bind_attribute document_attributes[] = {
	{ "documentElement", document_element, NULL },
	{ "head", document_head, NULL },
	{ "body", document_body, NULL },
	{ "doctype", document_doctype, NULL },
	{ "title", document_title_get, document_title_set },
	{ "readyState", document_ready_state, NULL },
	{ "defaultView", document_default_view, NULL },
	{ "children", bind_children, NULL },
	{ "firstElementChild", bind_first_element_child_get, NULL },
	{ "lastElementChild", bind_last_element_child_get, NULL },
	{ "childElementCount", bind_child_element_count, NULL },
	{ NULL, NULL, NULL }
};

/*
 * The operations of Document, with ParentNode's.  The table is constant
 * for the life of the program.
 */
static const struct bind_operation document_operations[] = {
	{ "getElementById", 1, document_get_element_by_id },
	{ "getElementsByTagName", 1, bind_get_elements_by_tag_name },
	{ "getElementsByClassName", 1, bind_get_elements_by_class_name },
	{ "createElement", 1, document_create_element },
	{ "createTextNode", 1, document_create_text_node },
	{ "createComment", 1, document_create_comment },
	{ "createDocumentFragment", 0, document_create_fragment },
	{ "append", 0, bind_append },
	{ "prepend", 0, bind_prepend },
	{ NULL, 0, NULL }
};

/*
 * The Document interface.
 */
const struct bind_interface bind_document_interface = {
	"Document", BIND_NODE, 0, NULL, document_attributes, document_operations, NULL
};

/* Finds the document a method's this value stands for, throwing a TypeError otherwise. */
static int
document_this(
	struct vm_realm *realm,
	vm_value this_value,
	struct dom_document **document)
{
	struct dom_node *node;
	int status;

	/* The node, which must be a document. */
	node = bind_node_of(this_value);
	if (node == NULL || node->type != DOM_DOCUMENT) {
		status = bind_throw_illegal(realm);
		return status;
	}

	/* Succeeded: the document is found. */
	*document = (struct dom_document *)node;
	return 0;
}

/* Finds the first HTML child of a tag of the document's <html> element (its head or body). */
static struct dom_element *
document_html_child(
	const struct dom_document *document,
	int tag)
{
	struct dom_element *root;
	struct dom_node *child;
	int matches;

	/* The root must be an HTML <html>. */
	root = bind_first_element_child(&document->node);
	matches = 0;
	if (root != NULL)
		matches = dom_element_is(&root->node, DOM_NS_HTML, DOM_TAG_HTML);
	if (!matches)
		return NULL;

	/* Its first child of the tag. */
	for (child = root->node.first_child; child != NULL; child = child->next) {
		matches = dom_element_is(child, DOM_NS_HTML, tag);
		if (matches)
			return (struct dom_element *)child;
	}

	/* None. */
	return NULL;
}

/* Finds the document's first <title> element in tree order. */
static struct dom_node *
document_find_title(
	const struct dom_document *document)
{
	struct dom_node *walk;
	int is_title;

	/* The first <title> of the whole tree. */
	for (walk = bind_following(&document->node, &document->node);
	     walk != NULL;
	     walk = bind_following(walk, &document->node)) {
		is_title = dom_element_is(walk, DOM_NS_HTML, DOM_TAG_TITLE);
		if (is_title)
			return walk;
	}

	/* The document has no title. */
	return NULL;
}

/* Reports the document's root element (documentElement). */
static int
document_element(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct dom_document *document;
	struct dom_element *root;
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The document. */
	status = document_this(realm, this_value, &document);
	if (status != 0)
		return status;

	/* Its element child, or null. */
	root = bind_first_element_child(&document->node);
	status = bind_wrap_or_null(bind_window_of(realm), (struct dom_node *)root, result);
	if (status != 0)
		return status;

	/* Succeeded: the root is reported. */
	return 0;
}

/* Reports the document's <head> (head). */
static int
document_head(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct dom_document *document;
	struct dom_element *head;
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The document. */
	status = document_this(realm, this_value, &document);
	if (status != 0)
		return status;

	/* The <head> child of <html>, or null. */
	head = document_html_child(document, DOM_TAG_HEAD);
	status = bind_wrap_or_null(bind_window_of(realm), (struct dom_node *)head, result);
	if (status != 0)
		return status;

	/* Succeeded: the head is reported. */
	return 0;
}

/* Reports the document's <body> (body). */
static int
document_body(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct dom_document *document;
	struct dom_element *body;
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The document. */
	status = document_this(realm, this_value, &document);
	if (status != 0)
		return status;

	/* The <body> child of <html>, or its <frameset>, or null. */
	body = document_html_child(document, DOM_TAG_BODY);
	if (body == NULL)
		body = document_html_child(document, DOM_TAG_FRAMESET);
	status = bind_wrap_or_null(bind_window_of(realm), (struct dom_node *)body, result);
	if (status != 0)
		return status;

	/* Succeeded: the body is reported. */
	return 0;
}

/* Reports the document's DOCTYPE node (doctype). */
static int
document_doctype(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct dom_document *document;
	struct dom_node *child;
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The document. */
	status = document_this(realm, this_value, &document);
	if (status != 0)
		return status;

	/* Its DOCTYPE child. */
	for (child = document->node.first_child; child != NULL; child = child->next) {
		if (child->type == DOM_DOCUMENT_TYPE)
			break;
	}

	/* The DOCTYPE, or null. */
	status = bind_wrap_or_null(bind_window_of(realm), child, result);
	if (status != 0)
		return status;

	/* Succeeded: the DOCTYPE is reported. */
	return 0;
}

/* Reports the document's title: its <title>'s text with whitespace collapsed and trimmed (title). */
static int
document_title_get(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct dom_document *document;
	struct dom_node *title;
	struct wb_units text;
	struct wb_units collapsed;
	uint16_t unit;
	size_t index;
	int space;
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The document and its title's text. */
	status = document_this(realm, this_value, &document);
	if (status != 0)
		return status;
	title = document_find_title(document);
	wb_units_init(&text);
	wb_units_init(&collapsed);
	status = 0;
	if (title != NULL)
		status = bind_text_content(title, &text);

	/* Each run of whitespace becomes one space, and none at either end. */
	space = 0;
	for (index = 0; status == 0 && index < text.length; index++) {
		unit = text.data[index];
		if (unit == 0x20U || unit == 0x09U || unit == 0x0aU || unit == 0x0cU || unit == 0x0dU) {
			space = 1;
			continue;
		}

		/* The space before a character, when text precedes it. */
		if (space && collapsed.length != 0)
			status = wb_units_append_code_point(&collapsed, 0x20U);
		space = 0;
		if (status == 0)
			status = wb_units_append(&collapsed, &text.data[index], 1);
	}

	/* The string. */
	if (status == 0)
		status = bind_units(realm, collapsed.data, collapsed.length, result);
	wb_units_release(&text);
	wb_units_release(&collapsed);
	if (status != 0)
		return status;

	/* Succeeded: the title is reported. */
	return 0;
}

/* Sets the document's title: the text of its <title>, made in the head when there is none (title). */
static int
document_title_set(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct dom_document *document;
	struct dom_element *head;
	struct dom_element *created;
	struct dom_node *title;
	struct dom_node *text;
	struct vm_string *string;
	struct vm_string *name;
	struct wb_units units;
	int status;

	/* The document and the new title. */
	*result = VM_VALUE_UNDEFINED;
	status = document_this(realm, this_value, &document);
	if (status != 0)
		return status;
	status = bind_to_string(realm, js_argument(args, count, 0), &string);
	if (status != 0)
		return status;

	/* The <title>, or a new one at the end of the head (without a head, nothing changes). */
	title = document_find_title(document);
	if (title == NULL) {
		head = document_html_child(document, DOM_TAG_HEAD);
		if (head == NULL)
			return 0;
		name = vm_atom_from_ascii(realm->heap, "title");
		if (name == NULL)
			return ENOMEM;
		created = dom_element_create(document, DOM_NS_HTML, name, NULL);
		if (created == NULL)
			return ENOMEM;
		title = &created->node;
		dom_append_child(&head->node, title);
	}

	/* Its children go, and one text node holds the title. */
	while (title->first_child != NULL)
		dom_remove(title->first_child);
	wb_units_init(&units);
	status = vm_string_append_units(string, &units);
	text = NULL;
	if (status == 0 && units.length != 0) {
		text = dom_text_create(document, units.data, units.length);
		if (text == NULL)
			status = ENOMEM;
	}
	wb_units_release(&units);
	if (status != 0)
		return status;
	if (text != NULL)
		dom_append_child(title, text);

	/* Succeeded: the title is set. */
	return 0;
}

/* Reports how far the document is loaded: loading, interactive or complete (readyState). */
static int
document_ready_state(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct dom_document *document;
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The document. */
	status = document_this(realm, this_value, &document);
	if (status != 0)
		return status;

	/* The state the page gave the window. */
	status = bind_string(realm, bind_window_of(realm)->ready_state, result);
	if (status != 0)
		return status;

	/* Succeeded: the state is reported. */
	return 0;
}

/* Reports the document's window (defaultView). */
static int
document_default_view(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct dom_document *document;
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The document. */
	status = document_this(realm, this_value, &document);
	if (status != 0)
		return status;

	/* Succeeded: the window is the global object. */
	*result = vm_value_cell(realm->global);
	return 0;
}

/* Finds the first element in tree order whose id is a string (getElementById). */
static int
document_get_element_by_id(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct dom_document *document;
	struct dom_node *walk;
	struct dom_attribute *attribute;
	struct vm_string *id;
	struct vm_string *name;
	int same;
	int status;

	/* The document, the id and the attribute's name. */
	status = document_this(realm, this_value, &document);
	if (status != 0)
		return status;
	status = bind_to_string(realm, js_argument(args, count, 0), &id);
	if (status != 0)
		return status;
	name = vm_atom_from_ascii(realm->heap, "id");
	if (name == NULL)
		return ENOMEM;

	/* The first element whose id attribute has the value. */
	for (walk = bind_following(&document->node, &document->node);
	     walk != NULL;
	     walk = bind_following(walk, &document->node)) {
		if (walk->type != DOM_ELEMENT)
			continue;
		attribute = dom_element_find_attribute((struct dom_element *)walk, DOM_NS_NONE, name);
		if (attribute == NULL)
			continue;
		same = vm_string_equal(attribute->value, id);
		if (same)
			break;
	}

	/* The element, or null. */
	status = bind_wrap_or_null(bind_window_of(realm), walk, result);
	if (status != 0)
		return status;

	/* Succeeded: the element is reported. */
	return 0;
}

/* Makes an HTML element with a name, folded to lower case (createElement). */
static int
document_create_element(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct dom_document *document;
	struct dom_element *element;
	struct vm_string *name;
	int status;

	/* The document and the name. */
	status = document_this(realm, this_value, &document);
	if (status != 0)
		return status;
	status = bind_to_atom(realm, js_argument(args, count, 0), 1, &name);
	if (status != 0)
		return status;

	/* An empty name is not a name. */
	if (name->length == 0) {
		status = bind_throw_dom(realm, "InvalidCharacterError", "The tag name provided is not a valid name.");
		return status;
	}

	/* The element, in the HTML namespace. */
	element = dom_element_create(document, DOM_NS_HTML, name, NULL);
	if (element == NULL)
		return ENOMEM;

	/* Its object. */
	status = bind_wrap(bind_window_of(realm), &element->node, result);
	if (status != 0)
		return status;

	/* Succeeded: the element is made. */
	return 0;
}

/* Makes a text node (createTextNode). */
static int
document_create_text_node(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int status;

	/* A text node of the document. */
	status = document_create_character_data(realm, this_value, DOM_TEXT, args, count, result);
	if (status != 0)
		return status;

	/* Succeeded: the node is made. */
	return 0;
}

/* Makes a comment (createComment). */
static int
document_create_comment(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int status;

	/* A comment of the document. */
	status = document_create_character_data(realm, this_value, DOM_COMMENT, args, count, result);
	if (status != 0)
		return status;

	/* Succeeded: the node is made. */
	return 0;
}

/* Makes an empty document fragment (createDocumentFragment). */
static int
document_create_fragment(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct dom_document *document;
	struct dom_node *fragment;
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The document. */
	status = document_this(realm, this_value, &document);
	if (status != 0)
		return status;

	/* The fragment. */
	fragment = dom_fragment_create(document);
	if (fragment == NULL)
		return ENOMEM;

	/* Its object. */
	status = bind_wrap(bind_window_of(realm), fragment, result);
	if (status != 0)
		return status;

	/* Succeeded: the fragment is made. */
	return 0;
}

/* Makes a text node or a comment of the document with a string's characters. */
static int
document_create_character_data(
	struct vm_realm *realm,
	vm_value this_value,
	int type,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct dom_document *document;
	struct dom_node *node;
	struct vm_string *string;
	struct wb_units units;
	int status;

	/* The document and the characters. */
	status = document_this(realm, this_value, &document);
	if (status != 0)
		return status;
	status = bind_to_string(realm, js_argument(args, count, 0), &string);
	if (status != 0)
		return status;
	wb_units_init(&units);
	status = vm_string_append_units(string, &units);
	if (status != 0) {
		wb_units_release(&units);
		return status;
	}

	/* The node. */
	if (type == DOM_TEXT) {
		node = dom_text_create(document, units.data, units.length);
	} else {
		node = dom_comment_create(document, units.data, units.length);
	}
	wb_units_release(&units);
	if (node == NULL)
		return ENOMEM;

	/* Its object. */
	status = bind_wrap(bind_window_of(realm), node, result);
	if (status != 0)
		return status;

	/* Succeeded: the node is made. */
	return 0;
}
