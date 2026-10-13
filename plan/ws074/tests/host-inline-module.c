/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* q1086: classic parser order, deferred modules and DOMContentLoaded. */

#include "page/page.h"

#include <stdio.h>
#include <string.h>

/* Captures the syntax error from one failed deferred module. */
struct host_console_count {
	int syntax_errors;
};

static void host_console(void *context, int level, const char *text, size_t length);

int
main(void)
{
	static const char html[] =
	    "<!doctype html><script>"
	    "globalThis.sequence='C';"
	    "document.addEventListener('DOMContentLoaded',function(){sequence+='D';});"
	    "</script>"
	    "<script type=module>sequence+='M';</script>"
	    "<script>sequence+='c';</script>"
	    "<script type=module>let = ;</script>"
	    "<script type=module>sequence+='N';</script>";
	static const char assertion[] = "globalThis.sequence==='CcMND';";
	struct js_syntax_error syntax;
	struct wb_units units;
	struct page *page;
	struct host_console_count console;
	vm_value result;
	int status;

	page = NULL;
	console.syntax_errors = 0;
	status = page_create(&page, __builtin_frame_address(0));
	if (status != 0)
		return 1;
	page_set_console(page, host_console, &console);
	status = page_load_bytes(page, (const unsigned char *)html, strlen(html),
	    "https://example.test/index.html", "text/html", 9);
	wb_units_init(&units);
	if (status == 0)
		status = wb_utf8_to_units((const unsigned char *)assertion,
		    strlen(assertion), &units);
	if (status == 0)
		status = js_run_script(page->realm, units.data, units.length, 0,
		    &result, &syntax);
	if (status == 0 && result != VM_VALUE_TRUE)
		status = 1;
	if (status == 0 && console.syntax_errors != 1)
		status = 1;
	wb_units_release(&units);
	vm_heap_collect(page->heap);
	page_destroy(page);
	if (status != 0) {
		fprintf(stderr, "host-inline-module: FAIL (%d)\n", status);
		return 1;
	}
	printf("host-inline-module: PASS\n");
	return 0;
}

/* Counts only a genuine module syntax diagnostic. */
static void
host_console(
	void *context,
	int level,
	const char *text,
	size_t length)
{
	struct host_console_count *count;

	(void)level;
	(void)length;
	count = context;
	if (strstr(text, "SyntaxError") != NULL)
		count->syntax_errors++;
}
