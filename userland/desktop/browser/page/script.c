/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A page's scripts and its event loop: the realm and window a page runs
 * its scripts in, the script elements run as the parser reaches them (an
 * inline script's text, or a src file next to the page), the load events,
 * and the timers driven by the page's clock (the user's input becomes DOM
 * events in input.c).
 *
 * The first pass runs classic scripts only, from the page's own files and
 * data: URLs (http arrives with the network, modules later), and runs them
 * while the parser waits, as a browser runs a parser-blocking script.
 */

#include "page/page.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The longest part of a script's src its errors are named by, in bytes. */
#define SCRIPT_NAME_MAX		200U

/* The most rounds of timers a settling page runs (a page whose timers never stop is cut short). */
#define SCRIPT_SETTLE_ROUNDS	100000

static void script_console(void *context, int level, const char *text, size_t length);
static int script_type_runs(const struct dom_element *script);
static int script_attribute(struct page *page, const struct dom_element *element, const char *name, struct dom_attribute **attribute);
static int script_run_file(struct page *page, const struct vm_string *src);
static int script_ascii_equal_folded(const struct vm_string *string, const char *ascii);

/*
 * The MIME types of a classic script's type attribute, in lower case (the
 * standard's JavaScript MIME type essence matches).  The table is constant
 * for the life of the program and ends with NULL.
 */
static const char *const script_types[] = {
	"application/ecmascript",
	"application/javascript",
	"application/x-ecmascript",
	"application/x-javascript",
	"text/ecmascript",
	"text/javascript",
	"text/javascript1.0",
	"text/javascript1.1",
	"text/javascript1.2",
	"text/javascript1.3",
	"text/javascript1.4",
	"text/javascript1.5",
	"text/jscript",
	"text/livescript",
	"text/x-ecmascript",
	"text/x-javascript",
	NULL
};

/*
 * Makes the page's realm with the built-ins and makes its global object
 * the document's window.
 */
int
page_start_scripts(
	struct page *page)
{
	struct bind_host host;
	int error;

	/* The realm and the language's built-in objects. */
	error = vm_realm_create(page->heap, &page->realm);
	if (error != 0)
		return error;
	error = js_install_builtins(page->realm);
	if (error != 0)
		return error;

	/* The window, whose console goes to the page's. */
	host.context = page;
	host.console = script_console;
	error = bind_window_create(page->realm, page->document, &host, &page->window);
	if (error != 0)
		return error;

	/* Succeeded: the page can run scripts. */
	return 0;
}

/*
 * Runs a script element the parser has reached (the parser's script
 * hook; context is the page): its src file, or its text.
 */
void
page_run_script_element(
	void *context,
	struct dom_element *script)
{
	struct page *page;
	struct dom_attribute *src;
	struct dom_node *child;
	const struct dom_character_data *data;
	struct wb_units text;
	const char *name;
	int runs;
	int error;

	/* A script of another type (a module, data) does not run. */
	page = context;
	runs = script_type_runs(script);
	if (!runs)
		return;

	/* A script with a src runs the file. */
	error = script_attribute(page, script, "src", &src);
	if (error != 0)
		return;
	if (src != NULL) {
		script_run_file(page, src->value);
		return;
	}

	/* Otherwise its text (its text children's) runs, named after the page's file. */
	wb_units_init(&text);
	error = 0;
	for (child = script->node.first_child; child != NULL && error == 0; child = child->next) {
		if (child->type != DOM_TEXT)
			continue;
		data = (const struct dom_character_data *)child;
		error = wb_units_append(&text, data->data.data, data->data.length);
	}

	/* The text runs, named after the page's file. */
	name = "(inline)";
	if (page->base != NULL)
		name = page->base;
	if (error == 0)
		bind_run_script(page->window, text.data, text.length, name);
	wb_units_release(&text);
}

/*
 * Tells the scripts the document is parsed: DOMContentLoaded at the
 * document, then load at the window.
 */
int
page_fire_load(
	struct page *page)
{
	int canceled;
	int error;

	/* The document is interactive, and its DOMContentLoaded bubbles to the window. */
	bind_window_set_ready_state(page->window, "interactive");
	error = bind_fire_event(page->window, &page->document->node, "DOMContentLoaded", BIND_EVENT_BUBBLES, &canceled);
	if (error != 0)
		return error;

	/* Then it is complete, and the window's load fires. */
	bind_window_set_ready_state(page->window, "complete");
	error = bind_fire_event(page->window, NULL, "load", BIND_EVENT_DOCUMENT, &canceled);
	if (error != 0)
		return error;

	/* Succeeded: the load events have run. */
	return 0;
}

/*
 * Moves the page's clock to now (milliseconds since the page began) and
 * runs the timers that are due.
 */
int
page_set_time(
	struct page *page,
	double now)
{
	int error;

	/* The clock only moves forward. */
	if (now > page->now)
		page->now = now;
	bind_window_set_time(page->window, page->now);

	/* The timers due by then. */
	error = bind_run_timers(page->window);
	if (error != 0)
		return error;

	/* Succeeded: no timer is overdue. */
	return 0;
}

/*
 * Reports when the page's next timer is due; zero when it has none.
 */
int
page_next_timer(
	const struct page *page,
	double *due)
{
	int found;

	/* The window keeps the timers. */
	found = bind_next_timer(page->window, due);

	/* Whether there is one. */
	return found;
}

/*
 * Runs a page's timers on a virtual clock until none is left or the next
 * is due after budget milliseconds (the headless modes, which show the
 * page as it stands then).
 */
int
page_settle(
	struct page *page,
	double budget)
{
	double due;
	int rounds;
	int found;
	int error;

	/* Jumps from timer to timer. */
	for (rounds = 0; rounds < SCRIPT_SETTLE_ROUNDS; rounds++) {
		found = page_next_timer(page, &due);
		if (!found || due > budget)
			break;

		/* The clock at the timer, which runs it. */
		error = page_set_time(page, due);
		if (error != 0)
			return error;
	}

	/* Succeeded: the page has settled. */
	return 0;
}

/*
 * Tells whether the document changed since the page was laid out.
 */
int
page_needs_layout(
	const struct page *page)
{
	/* A page never laid out needs it. */
	if (!page->laid_out)
		return 1;

	/* A change since the layout. */
	if (page->laid_out_generation != page->document->generation)
		return 1;

	/* Images that arrived since the layout. */
	if (page->laid_out_images != page->images_generation)
		return 1;

	/* Style sheets that arrived since the styling. */
	if (page->styled_sheets != page->sheets_generation)
		return 1;

	/* The layout is up to date. */
	return 0;
}

/* Writes a console line to the embedder's console, or to standard error. */
static void
script_console(
	void *context,
	int level,
	const char *text,
	size_t length)
{
	struct page *page;

	/* The embedder's console, when it has one. */
	page = context;
	if (page->console != NULL) {
		page->console(page->console_context, level, text, length);
		return;
	}

	/* Otherwise the line goes to standard error. */
	fprintf(stderr, "console: %.*s\n", (int)length, text);
}

/* Tells whether a script element's type attribute asks for a classic script. */
static int
script_type_runs(
	const struct dom_element *script)
{
	const struct dom_attribute *attribute;
	const struct vm_string *type;
	size_t index;
	int same;

	/* The type attribute (found by comparing names: the element's atoms are the parser's). */
	type = NULL;
	for (index = 0; index < script->attribute_count; index++) {
		attribute = &script->attributes[index];
		if (attribute->ns != DOM_NS_NONE)
			continue;
		same = vm_string_equal_ascii(attribute->name, "type");
		if (same) {
			type = attribute->value;
			break;
		}
	}

	/* No type, or an empty one, is JavaScript. */
	if (type == NULL || type->length == 0)
		return 1;

	/* Otherwise one of the JavaScript MIME types, in any case. */
	for (index = 0; script_types[index] != NULL; index++) {
		same = script_ascii_equal_folded(type, script_types[index]);
		if (same)
			return 1;
	}

	/* Another type (a module, a template, data) does not run in this pass. */
	return 0;
}

/* Finds an element's attribute of no namespace by its ASCII name (*attribute is NULL when it has none). */
static int
script_attribute(
	struct page *page,
	const struct dom_element *element,
	const char *name,
	struct dom_attribute **attribute)
{
	struct vm_string *atom;

	/* The name's atom. */
	*attribute = NULL;
	atom = vm_atom_from_ascii(page->heap, name);
	if (atom == NULL)
		return ENOMEM;

	/* The attribute. */
	*attribute = dom_element_find_attribute(element, DOM_NS_NONE, atom);

	/* Succeeded: the attribute (or none) is found. */
	return 0;
}

/* Runs the script file a src names, resolved against the page's file; a failure goes to the console. */
static int
script_run_file(
	struct page *page,
	const struct vm_string *src)
{
	struct wb_buffer href;
	struct wb_buffer path;
	struct wb_buffer bytes;
	struct wb_buffer line;
	struct wb_units units;
	const unsigned char *data;
	size_t length;
	size_t name_length;
	int error;

	/* The src as UTF-8, and the bytes of the file or data: URL it names. */
	wb_buffer_init(&href);
	wb_buffer_init(&path);
	wb_buffer_init(&bytes);
	wb_buffer_init(&line);
	wb_units_init(&units);
	error = vm_string_to_utf8(src, &href);
	if (error == 0 && page->base == NULL)
		error = EINVAL;
	if (error == 0)
		error = page_fetch(page->base, wb_buffer_string(&href), &bytes, NULL);
	name_length = href.length;
	if (name_length > SCRIPT_NAME_MAX)
		name_length = SCRIPT_NAME_MAX;
	if (error == 0)
		error = wb_buffer_append(&path, href.data, name_length);

	/* The bytes, as UTF-8 text without a byte order mark. */
	data = bytes.data;
	length = bytes.length;
	if (error == 0 && length >= 3 && data[0] == 0xefU && data[1] == 0xbbU && data[2] == 0xbfU) {
		data += 3;
		length -= 3;
	}

	/* The text as UTF-16. */
	if (error == 0)
		error = wb_utf8_to_units(data, length, &units);

	/* The script runs, or the console says why it could not load. */
	if (error == 0) {
		error = bind_run_script(page->window, units.data, units.length, wb_buffer_string(&path));
	} else if (error != ENOMEM) {
		wb_buffer_printf(&line, "Failed to load the script %s: %s", wb_buffer_string(&href), strerror(error));
		bind_console(page->window, BIND_CONSOLE_ERROR, wb_buffer_string(&line));
		error = 0;
	}

	/* Frees the buffers. */
	wb_buffer_release(&href);
	wb_buffer_release(&path);
	wb_buffer_release(&bytes);
	wb_buffer_release(&line);
	wb_units_release(&units);
	if (error != 0)
		return error;

	/* Succeeded: the script ran, or its failure was reported. */
	return 0;
}

/* Tells whether a string equals lower-case ASCII text, ignoring the case of its ASCII letters. */
static int
script_ascii_equal_folded(
	const struct vm_string *string,
	const char *ascii)
{
	uint16_t unit;
	size_t length;
	size_t index;

	/* The lengths must match. */
	length = strlen(ascii);
	if (string->length != length)
		return 0;

	/* Then every unit, folded. */
	for (index = 0; index < string->length; index++) {
		unit = vm_string_at(string, index);
		if (unit >= 'A' && unit <= 'Z')
			unit = (uint16_t)(unit + 0x20U);
		if (unit != (unsigned char)ascii[index])
			return 0;
	}

	/* The same text. */
	return 1;
}
