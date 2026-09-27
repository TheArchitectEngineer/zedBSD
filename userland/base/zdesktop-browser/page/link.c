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
 * Until the URL parser (ws074-p015) and the network arrive, a target is a
 * local file: an absolute path, a file: URL, or a path relative to the
 * page's file.  The query and the fragment are dropped, and %XX escapes
 * are decoded.
 */

#include "page/page.h"

#include <errno.h>
#include <string.h>

/* The scheme of a file URL. */
#define LINK_FILE_SCHEME	"file://"

/* The deepest element nesting searched for a link (the parser caps nesting too). */
#define LINK_DEPTH		512

static int link_scheme_length(const char *text, size_t length);
static int link_is_letter(char character);
static int link_is_scheme_mark(char character);
static int link_hex(char digit);
static int link_normalize(const char *path, size_t length, struct wb_buffer *out);

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
 * Resolves a link's target against the absolute path of the page's file
 * and writes the target's absolute path.
 *
 * Returns EPROTONOSUPPORT for a URL of another scheme than file.
 */
int
page_resolve_file(
	const char *base,
	const char *href,
	struct wb_buffer *out)
{
	struct wb_buffer joined;
	const char *slash;
	size_t length;
	size_t scheme;
	size_t index;
	int high;
	int low;
	int differs;
	int error;

	/* The target without its query and fragment. */
	length = strcspn(href, "?#");

	/* No target at all is the page itself. */
	if (length == 0) {
		error = wb_buffer_append_string(out, base);
		if (error != 0)
			return error;
		return 0;
	}

	/* A file URL names its path after the scheme (and a host, which is dropped). */
	differs = strncmp(href, LINK_FILE_SCHEME, strlen(LINK_FILE_SCHEME));
	if (differs == 0) {
		href += strlen(LINK_FILE_SCHEME);
		length -= strlen(LINK_FILE_SCHEME);
		slash = memchr(href, '/', length);
		if (slash == NULL)
			return EINVAL;
		length -= (size_t)(slash - href);
		href = slash;
	}

	/* Any other scheme is not a file. */
	scheme = (size_t)link_scheme_length(href, length);
	if (scheme != 0)
		return EPROTONOSUPPORT;

	/* A relative target starts from the base's directory. */
	wb_buffer_init(&joined);
	error = 0;
	if (href[0] != '/') {
		slash = strrchr(base, '/');
		if (slash != NULL)
			error = wb_buffer_append(&joined, base, (size_t)(slash - base) + 1U);
	}

	/* The target's characters, with each %XX escape decoded. */
	for (index = 0; error == 0 && index < length; index++) {
		/* An escape of two hexadecimal digits is the byte they name. */
		if (href[index] == '%' && index + 2U < length) {
			high = link_hex(href[index + 1U]);
			low = -1;
			if (high >= 0)
				low = link_hex(href[index + 2U]);
			if (low >= 0) {
				error = wb_buffer_append_byte(&joined, (unsigned char)(high * 16 + low));
				index += 2U;
				continue;
			}
		}

		/* Any other character stays. */
		error = wb_buffer_append_byte(&joined, (unsigned char)href[index]);
	}

	/* The path with its . and .. parts taken out. */
	if (error == 0)
		error = link_normalize(wb_buffer_string(&joined), joined.length, out);
	wb_buffer_release(&joined);
	if (error != 0)
		return error;

	/* Succeeded: the target's path is written. */
	return 0;
}

/* Reports the length of a URL scheme and its colon at the start of a text (0 when there is none). */
static int
link_scheme_length(
	const char *text,
	size_t length)
{
	size_t index;
	int letter;
	int mark;

	/* A scheme starts with a letter. */
	if (length == 0)
		return 0;
	letter = link_is_letter(text[0]);
	if (!letter)
		return 0;

	/* Then letters, digits, +, - and . up to the colon. */
	for (index = 1; index < length; index++) {
		/* The colon ends the scheme. */
		if (text[index] == ':')
			return (int)index + 1;

		/* A letter may follow. */
		letter = link_is_letter(text[index]);
		if (letter)
			continue;

		/* So may a digit, +, - or . */
		mark = link_is_scheme_mark(text[index]);
		if (mark)
			continue;

		/* Anything else means the text is a path. */
		return 0;
	}

	/* No colon: a path. */
	return 0;
}

/* Tells whether a character is an ASCII letter. */
static int
link_is_letter(
	char character)
{
	/* A lower-case letter. */
	if (character >= 'a' && character <= 'z')
		return 1;

	/* An upper-case letter. */
	if (character >= 'A' && character <= 'Z')
		return 1;

	/* Anything else. */
	return 0;
}

/* Tells whether a character may follow the first letter of a scheme without being a letter: a digit, +, - or . */
static int
link_is_scheme_mark(
	char character)
{
	/* A digit. */
	if (character >= '0' && character <= '9')
		return 1;

	/* The three marks. */
	if (character == '+')
		return 1;
	if (character == '-')
		return 1;
	if (character == '.')
		return 1;

	/* Anything else. */
	return 0;
}

/* Reports a hexadecimal digit's value, or -1 for another character. */
static int
link_hex(
	char digit)
{
	/* The decimal digits. */
	if (digit >= '0' && digit <= '9')
		return digit - '0';

	/* The letters, in either case. */
	if (digit >= 'a' && digit <= 'f')
		return digit - 'a' + 10;
	if (digit >= 'A' && digit <= 'F')
		return digit - 'A' + 10;

	/* Not a digit. */
	return -1;
}

/*
 * Writes a path with its empty and . parts removed and each .. removing
 * the part before it (never above the root).
 */
static int
link_normalize(
	const char *path,
	size_t length,
	struct wb_buffer *out)
{
	size_t starts[256];
	size_t lengths[256];
	size_t count;
	size_t start;
	size_t end;
	size_t index;
	int absolute;
	int error;

	/* Whether the path starts at the root. */
	absolute = 0;
	if (length != 0 && path[0] == '/')
		absolute = 1;

	/* Walks the parts between the slashes. */
	count = 0;
	start = 0;
	while (start <= length) {
		end = start;
		while (end < length && path[end] != '/')
			end++;

		/* An empty part changes nothing. */
		if (end == start) {
			start = end + 1U;
			continue;
		}

		/* Nor does a . part. */
		if (end - start == 1U && path[start] == '.') {
			start = end + 1U;
			continue;
		}

		/* A .. part takes the last part away. */
		if (end - start == 2U &&
		    path[start] == '.' &&
		    path[start + 1U] == '.') {
			if (count != 0)
				count--;
			start = end + 1U;
			continue;
		}

		/* A path of too many parts is refused. */
		if (count == sizeof(starts) / sizeof(starts[0]))
			return ENAMETOOLONG;

		/* Any other part is kept. */
		starts[count] = start;
		lengths[count] = end - start;
		count++;
		start = end + 1U;
	}

	/* Writes the parts back with slashes between them. */
	error = 0;
	if (absolute)
		error = wb_buffer_append_byte(out, '/');
	for (index = 0; error == 0 && index < count; index++) {
		if (index != 0)
			error = wb_buffer_append_byte(out, '/');
		if (error == 0)
			error = wb_buffer_append(out, path + starts[index], lengths[index]);
	}

	/* Reports a buffer that could not grow. */
	if (error != 0)
		return error;

	/* Succeeded: the path is written. */
	return 0;
}
