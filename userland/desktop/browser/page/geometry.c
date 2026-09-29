/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What a page's scripts ask the page about its layout and its selectors
 * (ws074-p031): the window's host callbacks behind querySelector,
 * getBoundingClientRect, clientWidth, scrollY and the like.
 *
 * A question about the geometry lays the page out first when the document
 * changed since the last layout, at the view's size and with its fonts,
 * as other browsers lay out when a script asks; a page whose view has not
 * given it fonts yet (or whose fonts do not open) has no layout, and its
 * nodes no boxes.  The selectors are matched with a style engine of their
 * own, which lives as long as the page: the styling's engine is made anew
 * at each change of the document.
 */

#include "page/page.h"

#include <errno.h>
#include <string.h>

static int geometry_layout(struct page *page);

/*
 * Finds the style engine a script's selectors are matched with (the
 * bind_host's selector_engine), made when first asked for; NULL when it
 * cannot be made.
 */
struct css_engine *
page_selector_engine(
	void *context)
{
	struct page *page;
	int error;

	/* The engine made before. */
	page = context;
	if (page->query_css != NULL)
		return page->query_css;

	/* A new one; without memory, none. */
	error = css_engine_create(&page->query_css, page->heap);
	if (error != 0) {
		page->query_css = NULL;
		return NULL;
	}

	/* Succeeded: the engine. */
	return page->query_css;
}

/*
 * Finds where a node is on the page as it is laid out now (the
 * bind_host's node_box), laying it out first when it changed; reports
 * whether the node has a box.
 */
int
page_node_box(
	void *context,
	struct dom_node *node,
	struct bind_box *box)
{
	const struct layout_box *first;
	struct layout_rect rect;
	struct page *page;
	int laid_out;
	int found;
	int block;

	/* A page that cannot be laid out has no boxes. */
	page = context;
	memset(box, 0, sizeof(*box));
	laid_out = geometry_layout(page);
	if (!laid_out)
		return 0;

	/* The union of the node's boxes. */
	found = layout_node_bounds(&page->layout, node, &rect);
	if (!found)
		return 0;
	box->x = layout_to_px(rect.x);
	box->y = layout_to_px(rect.y);
	box->width = layout_to_px(rect.width);
	box->height = layout_to_px(rect.height);

	/* The first box: a block (or a replaced or atomic box) has a client area and borders, an inline box not. */
	first = layout_box_of(&page->layout, node);
	block = 0;
	if (first != NULL) {
		if (first->kind == LAYOUT_BLOCK) {
			block = 1;
		} else if (first->kind == LAYOUT_REPLACED) {
			block = 1;
		} else if (first->atomic) {
			block = 1;
		}
	}

	/* Its borders, in the order top, right, bottom, left. */
	box->block = block;
	if (block) {
		box->border_top = layout_to_px(first->border[0]);
		box->border_right = layout_to_px(first->border[1]);
		box->border_bottom = layout_to_px(first->border[2]);
		box->border_left = layout_to_px(first->border[3]);
	}

	/* Succeeded: the node has a box. */
	return 1;
}

/*
 * Finds the laid out document's width and height (the bind_host's
 * document_size), laying the page out first when it changed; 0 when it
 * cannot be laid out.
 */
void
page_document_size(
	void *context,
	double *width,
	double *height)
{
	struct page *page;
	int laid_out;

	/* Nothing until the page is laid out. */
	page = context;
	*width = 0.0;
	*height = 0.0;
	laid_out = geometry_layout(page);
	if (!laid_out)
		return;

	/* The viewport's width, and the document's height. */
	*width = layout_to_px(page->layout.viewport_width);
	*height = layout_to_px(page->layout.document_height);
}

/*
 * Finds how far the page's view is scrolled, in CSS pixels (the
 * bind_host's scroll).
 */
void
page_scroll(
	void *context,
	double *x,
	double *y)
{
	struct page *page;

	/* What the view last said. */
	page = context;
	*x = page->scroll_x;
	*y = page->scroll_y;
}

/*
 * Lays the page out at its view's size when it changed since the last
 * layout; reports whether the page is laid out (it cannot be without the
 * view's fonts and size, or when the fonts do not open).
 */
static int
geometry_layout(
	struct page *page)
{
	int changed;
	int error;

	/* A page laid out as it is now. */
	changed = page_needs_layout(page);
	if (!changed)
		return 1;

	/* A page whose view has given it no fonts or no size yet cannot be laid out. */
	if (page->font_paths == NULL)
		return 0;
	if (page->viewport_width <= 0 || page->viewport_height <= 0)
		return 0;

	/* The fonts, opened once. */
	error = page_open_fonts(page, page->font_paths);
	if (error != 0)
		return 0;

	/* The layout; one that fails leaves the page without boxes. */
	error = page_layout(page, page->viewport_width, page->viewport_height);
	if (error != 0)
		return 0;

	/* Succeeded: the page is laid out. */
	return 1;
}
