/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Checks that iframe descendants remain in the DOM but make no parent layout
 * or paint boxes, while the iframe element keeps its own dimensions.
 */

#include "page/page.h"

#include <stdio.h>
#include <string.h>

static struct dom_element *first_iframe(struct dom_node *root);

/* Runs the parent-page DOM, layout and paint checks for three iframe display modes. */
int
main(
	void)
{
	static const unsigned char html[] =
	    "<!doctype html><html><body><iframe style='display:block;width:0;height:0'>HIDDEN_ZERO<span>CHILD_ZERO</span></iframe>"
	    "<iframe style='display:block;width:80px;height:40px'>HIDDEN_NORMAL<span>CHILD_NORMAL</span></iframe>"
	    "<iframe style='display:contents'>HIDDEN_CONTENTS</iframe><p>VISIBLE</p></body></html>";
	struct text_font_paths fonts;
	struct wb_buffer dom;
	struct wb_buffer layout;
	struct wb_buffer paint;
	struct page *page;
	struct dom_element *iframe;
	struct dom_element *child;
	struct vm_string *name;
	int error;
	int passed;

	fonts.sans = "userland/desktop/fonts/Mahora-Regular.ttf";
	fonts.mono = "userland/desktop/fonts/JetBrainsMono-Regular.ttf";
	fonts.fallback = "userland/desktop/fonts/DroidSansFallbackFull.ttf";
	wb_buffer_init(&dom);
	wb_buffer_init(&layout);
	wb_buffer_init(&paint);
	error = page_create(&page, __builtin_frame_address(0));
	if (error != 0)
		return 2;
	page_set_fonts(page, &fonts);
	error = page_load_html(page, html, sizeof(html) - 1U);
	if (error == 0) {
		/* The parser preserves raw iframe text; a DOM-created element checks nested boxes too. */
		iframe = first_iframe(&page->document->node);
		name = vm_atom_from_ascii(page->heap, "span");
		child = NULL;
		if (iframe != NULL && name != NULL)
			child = dom_element_create(page->document, DOM_NS_HTML, name, NULL);
		if (child == NULL)
			error = 2;
		else
			dom_append_child(&iframe->node, &child->node);
	}
	if (error == 0)
		error = page_dump_dom(page, &dom);
	if (error == 0)
		error = page_open_fonts(page, &fonts);
	if (error == 0)
		error = page_layout(page, 800, 600);
	if (error == 0)
		error = layout_dump(&page->layout, &layout);
	if (error == 0)
		error = page_paint(page);
	if (error == 0)
		error = paint_dump(&page->paint, &paint);
	if (error != 0) {
		fprintf(stderr, "iframe fallback setup: %d\n", error);
		page_destroy(page);
		return 2;
	}

	passed = strstr(wb_buffer_string(&dom), "HIDDEN_ZERO") != NULL;
	passed = passed && strstr(wb_buffer_string(&dom), "CHILD_ZERO") != NULL;
	passed = passed && strstr(wb_buffer_string(&dom), "CHILD_NORMAL") != NULL;
	passed = passed && strstr(wb_buffer_string(&dom), "HIDDEN_CONTENTS") != NULL;
	passed = passed && strstr(wb_buffer_string(&dom), "<span>") != NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "80.00 40.00") != NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "<span>") == NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "HIDDEN_ZERO") == NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "CHILD_ZERO") == NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "HIDDEN_NORMAL") == NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "CHILD_NORMAL") == NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "HIDDEN_CONTENTS") == NULL;
	passed = passed && strstr(wb_buffer_string(&paint), "HIDDEN_ZERO") == NULL;
	passed = passed && strstr(wb_buffer_string(&paint), "CHILD_ZERO") == NULL;
	passed = passed && strstr(wb_buffer_string(&paint), "HIDDEN_NORMAL") == NULL;
	passed = passed && strstr(wb_buffer_string(&paint), "CHILD_NORMAL") == NULL;
	passed = passed && strstr(wb_buffer_string(&paint), "HIDDEN_CONTENTS") == NULL;
	passed = passed && strstr(wb_buffer_string(&paint), "VISIBLE") != NULL;
	if (!passed) {
		fprintf(stderr, "FAIL iframe fallback entered parent layout or paint\n");
		fprintf(stderr, "DOM:\n%s\nlayout:\n%s\npaint:\n%s\n", wb_buffer_string(&dom),
		    wb_buffer_string(&layout), wb_buffer_string(&paint));
	}
	page_destroy(page);
	wb_buffer_release(&dom);
	wb_buffer_release(&layout);
	wb_buffer_release(&paint);
	if (!passed)
		return 1;
	printf("iframe fallback: pass\n");
	return 0;
}

/* Finds the first iframe in the parsed document for a native child mutation. */
static struct dom_element *
first_iframe(
	struct dom_node *root)
{
	struct dom_node *node;
	struct dom_element *element;
	struct dom_element *found;

	for (node = root->first_child; node != NULL; node = node->next) {
		if (node->type == DOM_ELEMENT) {
			element = (struct dom_element *)node;
			if (element->ns == DOM_NS_HTML && element->tag == DOM_TAG_IFRAME)
				return element;
		}
		found = first_iframe(node);
		if (found != NULL)
			return found;
	}
	return NULL;
}
