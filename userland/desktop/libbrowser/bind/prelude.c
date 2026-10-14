/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The parts of the web platform written in the page's own language
 * (ws074-p230): AbortController, URL and URLSearchParams (over the browser's
 * own URL parser), and the smaller objects that scripts probe for (Intl,
 * Reflect, matchMedia, history, MessageChannel).  The source is
 * prelude-source.inc, made from plan/ws074/phase230/prelude.js.
 */

#include "bind/internal.h"
#include "js/builtin.h"
#include "net/net.h"

#include <errno.h>
#include <string.h>

#include "bind/prelude-source.inc"

/* The parts of a URL in the order the prelude reads them (enum net_url_part). */
#define PRELUDE_URL_PARTS	11

/* parseURL(input, base): the parts of the URL as an array of strings, or null when it does not parse. */
static int
prelude_parse_url(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct net_url base;
	struct net_url url;
	struct wb_buffer text;
	struct wb_buffer part;
	struct vm_string *string;
	vm_value parts[PRELUDE_URL_PARTS];
	int have_base;
	int index;
	int status;

	UNUSED_PARAMETER(this_value);
	*result = VM_VALUE_NULL;

	/* The input and the optional base as UTF-8. */
	wb_buffer_init(&text);
	wb_buffer_init(&part);
	status = vm_string_to_utf8((const struct vm_string *)vm_value_as_cell(js_argument(args, count, 0)), &text);
	if (status != 0) {
		wb_buffer_release(&text);
		return status;
	}
	have_base = 0;
	if (js_argument(args, count, 1) != VM_VALUE_UNDEFINED) {
		status = vm_string_to_utf8((const struct vm_string *)vm_value_as_cell(js_argument(args, count, 1)), &part);
		if (status == 0)
			status = net_url_parse(wb_buffer_string(&part), part.length, NULL, &base);
		if (status != 0) {
			wb_buffer_release(&text);
			wb_buffer_release(&part);
			return status == ENOMEM ? status : 0;
		}
		have_base = 1;
	}

	/* The parse. */
	status = net_url_parse(wb_buffer_string(&text), text.length, have_base ? &base : NULL, &url);
	if (have_base)
		net_url_release(&base);
	wb_buffer_release(&text);
	wb_buffer_release(&part);
	if (status != 0)
		return status == ENOMEM ? status : 0;

	/* Each part as a string. */
	for (index = 0; index < PRELUDE_URL_PARTS && status == 0; index++) {
		wb_buffer_init(&part);
		status = net_url_component(&url, index, &part);
		if (status == 0) {
			string = vm_string_from_utf8(realm->heap, part.data == NULL ? "" : wb_buffer_string(&part), part.length);
			if (string == NULL)
				status = ENOMEM;
			else
				parts[index] = vm_value_cell(string);
		}
		wb_buffer_release(&part);
	}
	net_url_release(&url);
	if (status != 0)
		return status;
	return js_builtin_array(realm, parts, PRELUDE_URL_PARTS, result);
}

/* Installs the prelude's objects on the window's global object. */
int
bind_install_prelude(
	struct bind_window *window)
{
	struct vm_realm *realm;
	struct vm_function *parser;
	struct vm_string *source;
	vm_value function;
	vm_value arguments[2];
	vm_value ignored;
	int status;

	realm = window->realm;
	status = js_builtin_function(realm, "parseURL", 2, prelude_parse_url, NULL, &parser);
	if (status != 0)
		return status;
	source = vm_string_from_utf8(realm->heap, prelude_source, sizeof(prelude_source) - 1U);
	if (source == NULL)
		return ENOMEM;
	status = js_builtin_evaluate(realm, source, 0, &function);
	if (status != 0) {
		realm->exception = VM_VALUE_UNDEFINED;
		return 0;
	}
	arguments[0] = vm_value_cell(parser);
	arguments[1] = vm_value_cell(realm->global);
	status = vm_call(realm, function, VM_VALUE_UNDEFINED, arguments, 2, &ignored);
	if (status != 0) {
		/* A failure of the prelude leaves the rest of the page working. */
		realm->exception = VM_VALUE_UNDEFINED;
		return status == ENOMEM ? status : 0;
	}
	return 0;
}
