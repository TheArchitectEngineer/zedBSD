/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws074-p005: the batch driver of the HTML tree builder for the html5lib
 * tree-construction tests (plan/ws074/tests/run-html5lib-tree.py).
 *
 *   host-tree < CASES > RESULTS
 *
 * Each input line is one case: SCRIPTING INPUT, tab separated, INPUT as
 * UTF-16 code units in four hex digits each ("-" for none).  For each case
 * the document is written in html5lib's tree format ("| " and two spaces a
 * level), followed by a line "#end".
 */

#include "html/html.h"
#include "dom/dom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One attribute's display name and value, for sorting. */
struct shown_attribute {
	char name[256];
	char value[4096];
};

static void dump(const struct dom_node *node, int depth);
static void print_units(const uint16_t *units, size_t length);
static void print_string(const struct vm_string *string);
static void string_to_ascii(const struct vm_string *string, char *out, size_t size);
static int compare_attributes(const void *left, const void *right);
static int parse_hex(const char *text, struct wb_units *units);
static struct dom_element *make_context(struct dom_document *document, const char *text);

int
main(void)
{
	static char line[1 << 20];
	struct vm_heap *heap;
	struct dom_document *document;
	struct html_parser *parser;
	struct wb_units input;
	struct dom_element *context;
	char *context_text;
	char *second;
	char *tab;
	int scripting;

	vm_heap_create(&heap, 0);
	vm_heap_set_stack_base(heap, __builtin_frame_address(0));
	wb_units_init(&input);
	while (fgets(line, sizeof(line), stdin) != NULL) {
		line[strcspn(line, "\n")] = '\0';
		tab = strchr(line, '\t');
		if (tab == NULL) {
			printf("#bad\n#end\n");
			continue;
		}
		*tab = '\0';
		scripting = atoi(line);

		/* ws074-p081: a fragment case has a third field before the input, its context (NS:NAME). */
		context_text = NULL;
		second = strchr(tab + 1, '\t');
		if (second != NULL) {
			*second = '\0';
			context_text = tab + 1;
			tab = second;
		}
		wb_units_clear(&input);
		parse_hex(tab + 1, &input);

		document = dom_document_create(heap);
		if (context_text == NULL) {
			html_parser_create(&parser, document, scripting);
		} else {
			context = make_context(document, context_text);
			html_parser_create_fragment(&parser, context, scripting);
		}
		html_parser_feed(parser, input.data, input.length);
		html_parser_finish(parser);
		if (context_text == NULL) {
			dump(&document->node, 0);
		} else {
			dump(&html_parser_fragment_root(parser)->node, 0);
		}
		html_parser_destroy(parser);
		printf("#end\n");
		fflush(stdout);
		document = NULL;
	}
	wb_units_release(&input);
	vm_heap_destroy(heap);
	return 0;
}

static int
parse_hex(
	const char *text,
	struct wb_units *units)
{
	unsigned value;
	uint16_t unit;

	if (strcmp(text, "-") == 0)
		return 0;
	while (text[0] != '\0') {
		if (sscanf(text, "%4x", &value) != 1)
			return -1;
		unit = (uint16_t)value;
		wb_units_append(units, &unit, 1);
		text += 4;
	}
	return 0;
}

static void
indent(
	int depth)
{
	int index;

	printf("| ");
	for (index = 0; index < depth; index++)
		printf("  ");
}

static void
dump(
	const struct dom_node *node,
	int depth)
{
	const struct dom_node *child;
	const struct dom_element *element;
	const struct dom_doctype *doctype;
	const struct dom_character_data *text;
	struct shown_attribute *shown;
	size_t index;
	const char *prefix;

	for (child = node->first_child; child != NULL; child = child->next) {
		switch (child->type) {
		case DOM_DOCUMENT_TYPE:
			doctype = (const struct dom_doctype *)child;
			indent(depth);
			printf("<!DOCTYPE ");
			print_string(doctype->name);
			if (doctype->public_id->length != 0 || doctype->system_id->length != 0) {
				printf(" \"");
				print_string(doctype->public_id);
				printf("\" \"");
				print_string(doctype->system_id);
				printf("\"");
			}
			printf(">\n");
			break;
		case DOM_COMMENT:
			text = (const struct dom_character_data *)child;
			indent(depth);
			printf("<!-- ");
			print_units(text->data.data, text->data.length);
			printf(" -->\n");
			break;
		case DOM_TEXT:
			text = (const struct dom_character_data *)child;
			indent(depth);
			printf("\"");
			print_units(text->data.data, text->data.length);
			printf("\"\n");
			break;
		case DOM_ELEMENT:
			element = (const struct dom_element *)child;
			indent(depth);
			printf("<");
			if (element->ns == DOM_NS_SVG)
				printf("svg ");
			if (element->ns == DOM_NS_MATHML)
				printf("math ");
			print_string(element->local_name);
			printf(">\n");
			shown = calloc(element->attribute_count + 1U, sizeof(*shown));
			for (index = 0; index < element->attribute_count; index++) {
				prefix = "";
				if (element->attributes[index].ns == DOM_NS_XLINK)
					prefix = "xlink ";
				if (element->attributes[index].ns == DOM_NS_XML)
					prefix = "xml ";
				if (element->attributes[index].ns == DOM_NS_XMLNS)
					prefix = "xmlns ";
				snprintf(shown[index].name, sizeof(shown[index].name), "%s", prefix);
				string_to_ascii(element->attributes[index].name, shown[index].name + strlen(prefix),
				    sizeof(shown[index].name) - strlen(prefix));
				string_to_ascii(element->attributes[index].value, shown[index].value, sizeof(shown[index].value));
			}
			qsort(shown, element->attribute_count, sizeof(*shown), compare_attributes);
			for (index = 0; index < element->attribute_count; index++) {
				indent(depth + 1);
				printf("%s=\"%s\"\n", shown[index].name, shown[index].value);
			}
			free(shown);
			if (element->content != NULL) {
				indent(depth + 1);
				printf("content\n");
				dump(element->content, depth + 2);
			}
			dump(child, depth + 1);
			break;
		default:
			break;
		}
	}
}

static int
compare_attributes(
	const void *left,
	const void *right)
{
	return strcmp(((const struct shown_attribute *)left)->name, ((const struct shown_attribute *)right)->name);
}

/* Prints UTF-16 as UTF-8 (lone surrogates as U+FFFD). */
static void
print_units(
	const uint16_t *units,
	size_t length)
{
	struct wb_buffer buffer;

	wb_buffer_init(&buffer);
	wb_units_to_utf8(units, length, &buffer);
	if (buffer.length != 0)
		fwrite(buffer.data, 1, buffer.length, stdout);
	wb_buffer_release(&buffer);
}

static void
print_string(
	const struct vm_string *string)
{
	struct wb_buffer buffer;

	wb_buffer_init(&buffer);
	vm_string_to_utf8(string, &buffer);
	if (buffer.length != 0)
		fwrite(buffer.data, 1, buffer.length, stdout);
	wb_buffer_release(&buffer);
}

static void
string_to_ascii(
	const struct vm_string *string,
	char *out,
	size_t size)
{
	struct wb_buffer buffer;

	wb_buffer_init(&buffer);
	vm_string_to_utf8(string, &buffer);
	snprintf(out, size, "%s", wb_buffer_string(&buffer));
	wb_buffer_release(&buffer);
}

/* Makes a fragment case's context element from "html:NAME", "svg:NAME" or "math:NAME". */
static struct dom_element *
make_context(
	struct dom_document *document,
	const char *text)
{
	const char *colon;
	int ns;

	colon = strchr(text, ':');
	ns = DOM_NS_HTML;
	if (strncmp(text, "svg:", 4) == 0)
		ns = DOM_NS_SVG;
	if (strncmp(text, "math:", 5) == 0)
		ns = DOM_NS_MATHML;
	return dom_element_create(document, ns, vm_atom_from_ascii(document->heap, colon + 1), NULL);
}
