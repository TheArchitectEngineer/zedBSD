/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws074-p013: the host test of positioned boxes: where the absolutely
 * positioned boxes of pages/position.html are placed, the relative shift,
 * the painting order by z-index, and which box a point hits where boxes
 * overlap.
 *
 *   host-position PAGES SANS MONO FALLBACK
 *
 * (PAGES: plan/ws074/tests/pages; the fonts: build/ws035-fonts/Inter.ttf,
 * JetBrainsMono-Regular.ttf, DroidSansFallbackFull.ttf).  The expected
 * places are Chromium's for the same page at 800 by 450.  Prints one line
 * per failed check and a summary.
 */

#include "page/page.h"

#include <stdio.h>
#include <string.h>

static int failures;
static int checks;

static void check(int condition, const char *what);
static const struct layout_box *find_class(const struct layout_box *box, const char *name);
static const char *class_of(const struct dom_node *node);
static void placed(const struct layout_box *root, const char *name, int x, int y, int width, int height);
static void hits(const struct page *page, int x, int y, const char *name);

int
main(
	int argc,
	char **argv)
{
	struct text_font_paths paths;
	struct wb_vector order;
	struct page *page;
	const struct layout_box *box;
	const char *name;
	char path[1024];
	char sequence[256];
	size_t flow_index;
	size_t index;
	int error;

	if (argc < 5) {
		fprintf(stderr, "usage: host-position PAGES SANS MONO FALLBACK\n");
		return 2;
	}

	/* position.html laid out at 800 by 450. */
	paths.sans = argv[2];
	paths.mono = argv[3];
	paths.fallback = argv[4];
	snprintf(path, sizeof(path), "%s/position.html", argv[1]);
	error = page_create(&page, __builtin_frame_address(0));
	check(error == 0, "page: made");
	if (error != 0)
		return 1;
	error = page_load_file(page, path);
	if (error == 0)
		error = page_open_fonts(page, &paths);
	if (error == 0)
		error = page_layout(page, 800, 450);
	check(error == 0, "page: position.html laid out");
	if (error != 0)
		return 1;

	/* The stage: 20 pixels in, 2 pixel borders, 300 high; its padding box starts at 22,22 and is 756 wide. */
	placed(page->layout.root, "stage", 20, 20, 760, 304);
	placed(page->layout.root, "corner top-left", 32, 32, 120, 60);
	placed(page->layout.root, "corner top-right", 648, 32, 120, 60);
	placed(page->layout.root, "corner bottom-left", 32, 252, 120, 60);
	placed(page->layout.root, "corner bottom-right", 648, 252, 120, 60);
	placed(page->layout.root, "over", 272, 172, 200, 100);
	placed(page->layout.root, "stretch", 582, 272, 176, 40);

	/* The label shrinks to its text (its width depends on the font; it is narrower than the room). */
	box = find_class(page->layout.root, "label");
	check(box != NULL && box->x == 182 * LAYOUT_UNIT && box->y == 142 * LAYOUT_UNIT, "label: at 182,142");
	check(box != NULL && box->width > 200 * LAYOUT_UNIT && box->width < 300 * LAYOUT_UNIT, "label: shrinks to its text");

	/* The relative paragraph is 30 right and 8 down of where the flow put it (20 + 30, under the stage). */
	box = find_class(page->layout.root, "shifted");
	check(box != NULL && box->x == 50 * LAYOUT_UNIT, "shifted: 30 pixels right of its margin");

	/* The painting order: z-index -1, then the flow, then the auto ones in tree order, then 1 and 2. */
	wb_vector_init(&order, sizeof(const struct layout_box *));
	error = layout_stacking_order(&page->layout, &order, &flow_index);
	check(error == 0, "order: listed");
	sequence[0] = '\0';
	for (index = 0; index < order.count; index++) {
		box = *(const struct layout_box **)wb_vector_at(&order, index);
		name = class_of(box->node);
		if (index == flow_index)
			strncat(sequence, "|flow|", sizeof(sequence) - strlen(sequence) - 1U);
		strncat(sequence, name, sizeof(sequence) - strlen(sequence) - 1U);
		strncat(sequence, ";", sizeof(sequence) - strlen(sequence) - 1U);
	}
	printf("host-position: order %s\n", sequence);
	check(strcmp(sequence, "below;|flow|stage;corner top-left;corner top-right;corner bottom-left;corner bottom-right;"
	    "label;stretch;shifted;middle;over;") == 0, "order: by z-index, then tree order");
	wb_vector_release(&order);

	/* Where z-index 2 overlaps z-index 1, the point hits the higher; beside it, the lower. */
	hits(page, 400, 220, "over");
	hits(page, 500, 220, "middle");
	hits(page, 90, 60, "corner top-left");
	hits(page, 300, 30, "stage");
	hits(page, 600, 300, "stretch");

	/* Frees the page. */
	page_destroy(page);
	printf("host-position: %d checks, %d failed\n", checks, failures);
	if (failures != 0)
		return 1;
	return 0;
}

/* Counts a check and reports it when it failed. */
static void
check(
	int condition,
	const char *what)
{
	/* One more check. */
	checks++;
	if (condition)
		return;

	/* The failure. */
	failures++;
	printf("FAIL %s\n", what);
}

/* Finds the first box of an element whose class attribute is a name. */
static const struct layout_box *
find_class(
	const struct layout_box *box,
	const char *name)
{
	const struct layout_box *child;
	const struct layout_box *found;

	/* The box itself. */
	if (strcmp(class_of(box->node), name) == 0)
		return box;

	/* Its children. */
	for (child = box->first_child; child != NULL; child = child->next) {
		found = find_class(child, name);
		if (found != NULL)
			return found;
	}

	/* Not under this box. */
	return NULL;
}

/* Reports a node's class attribute as ASCII (empty for none; a static buffer). */
static const char *
class_of(
	const struct dom_node *node)
{
	static char text[128];
	const struct dom_element *element;
	size_t index;
	size_t length;
	int same;

	/* Only elements have classes. */
	text[0] = '\0';
	if (node == NULL || node->type != DOM_ELEMENT)
		return text;
	element = (const struct dom_element *)node;

	/* The class attribute's characters. */
	for (index = 0; index < element->attribute_count; index++) {
		same = vm_string_equal_ascii(element->attributes[index].name, "class");
		if (!same)
			continue;
		for (length = 0; length < element->attributes[index].value->length && length + 1U < sizeof(text); length++)
			text[length] = (char)vm_string_at(element->attributes[index].value, length);
		text[length] = '\0';
		break;
	}

	/* The class, or nothing. */
	return text;
}

/* Checks a box's border box, in whole pixels. */
static void
placed(
	const struct layout_box *root,
	const char *name,
	int x,
	int y,
	int width,
	int height)
{
	const struct layout_box *box;
	char what[256];
	layout_unit outer_width;
	layout_unit outer_height;

	/* The box and its border box. */
	box = find_class(root, name);
	snprintf(what, sizeof(what), "place: %s at %d,%d size %dx%d", name, x, y, width, height);
	if (box == NULL) {
		check(0, what);
		return;
	}
	outer_width = box->border[CSS_LEFT] + box->padding[CSS_LEFT] + box->width + box->padding[CSS_RIGHT] + box->border[CSS_RIGHT];
	outer_height = box->border[CSS_TOP] + box->padding[CSS_TOP] + box->height + box->padding[CSS_BOTTOM] + box->border[CSS_BOTTOM];
	check(box->x == x * LAYOUT_UNIT && box->y == y * LAYOUT_UNIT && outer_width == width * LAYOUT_UNIT &&
	    outer_height == height * LAYOUT_UNIT, what);
	if (box->x != x * LAYOUT_UNIT || box->y != y * LAYOUT_UNIT || outer_width != width * LAYOUT_UNIT)
		printf("  got %.2f,%.2f size %.2fx%.2f\n", (double)layout_to_px(box->x), (double)layout_to_px(box->y),
		    (double)layout_to_px(outer_width), (double)layout_to_px(outer_height));
}

/* Checks which element a point hits. */
static void
hits(
	const struct page *page,
	int x,
	int y,
	const char *name)
{
	struct dom_node *node;
	char what[256];

	/* The node, then its element. */
	node = layout_hit_node(&page->layout, (layout_unit)x * LAYOUT_UNIT, (layout_unit)y * LAYOUT_UNIT);
	while (node != NULL && node->type != DOM_ELEMENT)
		node = node->parent;
	snprintf(what, sizeof(what), "hit: %d,%d is on %s (got %s)", x, y, name, class_of(node));
	check(node != NULL && strcmp(class_of(node), name) == 0, what);
}
