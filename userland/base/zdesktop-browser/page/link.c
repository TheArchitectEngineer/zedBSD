/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Links: the <a href> under a point of the laid out page, and a link's
 * target resolved against the file the page came from.
 *
 * A target is resolved as a URL against the page's file: URL (the WHATWG
 * URL parser, net/url.c); until the network arrives only file: targets
 * are opened, and their path, percent-decoded, is the file.
 */

#include "page/page.h"
#include "net/net.h"

#include <errno.h>
#include <string.h>

/* The deepest element nesting searched for a link (the parser caps nesting too). */
#define LINK_DEPTH		512

/*
 * Finds the link under a point of the page (pixels from the top left of
 * the document) and writes its href as UTF-8; *found says whether there
 * was one.
 */
int
page_link_at(
	struct page *page,
	int x,
	int y,
	struct wb_buffer *href,
	int *found)
{
	const struct layout_box *box;
	const struct dom_node *node;
	const struct dom_attribute *attribute;
	struct vm_string *name;
	int is_link;
	int depth;
	int error;

	/* Nothing is found until an <a href> is. */
	*found = 0;
	if (!page->laid_out)
		return 0;

	/* The text under the point. */
	box = layout_hit(&page->layout, (layout_unit)x * LAYOUT_UNIT, (layout_unit)y * LAYOUT_UNIT);
	if (box == NULL || box->node == NULL)
		return 0;

	/* The attribute's name, as the atom the elements keep. */
	name = vm_atom_from_ascii(page->heap, "href");
	if (name == NULL)
		return ENOMEM;

	/* The nearest <a> with an href among the text's ancestors. */
	node = box->node;
	for (depth = 0; node != NULL && depth < LINK_DEPTH; depth++) {
		/* An HTML <a> element with an href is the link. */
		is_link = dom_element_is(node, DOM_NS_HTML, DOM_TAG_A);
		if (is_link) {
			attribute = dom_element_find_attribute((const struct dom_element *)node, DOM_NS_NONE, name);
			if (attribute != NULL)
				break;
		}

		/* Otherwise its parent. */
		node = node->parent;
	}

	/* No link around the text. */
	if (node == NULL || depth == LINK_DEPTH)
		return 0;

	/* The href's value. */
	error = vm_string_to_utf8(attribute->value, href);
	if (error != 0)
		return error;

	/* Succeeded: the link is found. */
	*found = 1;
	return 0;
}

/*
 * Resolves a link's target (a URL, usually relative) against the absolute
 * path of the page's file and writes the absolute path of the file it
 * names.
 *
 * Returns EINVAL for a target that is not a URL, and EPROTONOSUPPORT for a
 * URL of another scheme than file.
 */
int
page_resolve_file(
	const char *base,
	const char *href,
	struct wb_buffer *out)
{
	struct wb_buffer base_text;
	struct net_url base_url;
	struct net_url target;
	int error;

	/* The page's file as a file: URL. */
	wb_buffer_init(&base_text);
	error = net_url_from_file_path(base, &base_text);
	if (error == 0)
		error = net_url_parse(wb_buffer_string(&base_text), base_text.length, NULL, &base_url);
	wb_buffer_release(&base_text);
	if (error != 0)
		return error;

	/* The target against it. */
	error = net_url_parse(href, strlen(href), &base_url, &target);
	net_url_release(&base_url);
	if (error != 0)
		return error;

	/* The file a file: URL names. */
	error = net_url_file_path(&target, out);
	net_url_release(&target);
	if (error != 0)
		return error;

	/* Succeeded: the target's path is written. */
	return 0;
}

/*
 * Reads what a URL (resolved against the page's file) names, as a script
 * or an image would: a data: URL's body, or a file: URL's file.  Returns
 * EPROTONOSUPPORT for a URL of another scheme (the network comes later).
 */
int
page_fetch(
	const char *base,
	const char *href,
	struct wb_buffer *bytes)
{
	struct wb_buffer base_text;
	struct wb_buffer path;
	struct net_url base_url;
	struct net_url target;
	struct net_data data;
	int is_data;
	int differs;
	int error;

	/* The page's file as a file: URL, and the target against it. */
	wb_buffer_init(&base_text);
	error = net_url_from_file_path(base, &base_text);
	if (error == 0)
		error = net_url_parse(wb_buffer_string(&base_text), base_text.length, NULL, &base_url);
	wb_buffer_release(&base_text);
	if (error != 0)
		return error;
	error = net_url_parse(href, strlen(href), &base_url, &target);
	net_url_release(&base_url);
	if (error != 0)
		return error;

	/* A data: URL carries its bytes. */
	is_data = 0;
	differs = strcmp(target.scheme, "data");
	if (differs == 0)
		is_data = 1;
	if (is_data) {
		error = net_data_parse(&target, &data);
		net_url_release(&target);
		if (error != 0)
			return error;
		error = wb_buffer_append(bytes, data.body.data, data.body.length);
		net_data_release(&data);
		return error;
	}

	/* A file: URL names a file. */
	wb_buffer_init(&path);
	error = net_url_file_path(&target, &path);
	net_url_release(&target);
	if (error == 0)
		error = wb_file_read(wb_buffer_string(&path), bytes);
	wb_buffer_release(&path);
	if (error != 0)
		return error;

	/* Succeeded: the bytes are read. */
	return 0;
}
