/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* q1085: canonical URL caching, relative imports and dependency evaluation. */

#include "page/page.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The source set is a local, deterministic host in place of HTTP. */
struct host_source {
	const char *url;
	const char *text;
	int fetches;
};

/* Each module records its evaluation letter in the same realm. */
static struct host_source host_sources[] = {
	{ "https://example.test/root/main.js",
	    "import {value as left} from './left.js';"
	    "import {value as right} from './right.js';"
	    "globalThis.sequence+='M';globalThis.result=left+right;", 0 },
	{ "https://example.test/root/left.js",
	    "import {number} from '../shared.js';"
	    "globalThis.sequence+='L';export const value=number+1;", 0 },
	{ "https://example.test/root/right.js",
	    "import {number} from '../shared.js';"
	    "globalThis.sequence+='R';export const value=number+2;", 0 },
	{ "https://example.test/shared.js",
	    "globalThis.sequence+='S';export const number=2;", 0 },
	{ "https://example.test/root/bad.js",
	    "import {missing} from '../shared.js';globalThis.sequence+='X';", 0 },
	{ "https://example.test/root/cycle-a.js",
	    "import {b} from './cycle-b.js';globalThis.sequence+='A';export var a=1;", 0 },
	{ "https://example.test/root/cycle-b.js",
	    "import {a} from './cycle-a.js';globalThis.sequence+='B';export var b=2;", 0 }
};

static int host_resolve(void *context, const char *base, const uint16_t *specifier,
	size_t length, struct wb_buffer *url);
static int host_fetch(void *context, const char *url, struct wb_units *source);
static int host_script(struct vm_realm *realm, const char *source, vm_value *result);

int
main(void)
{
	struct js_module_graph *graph;
	struct js_syntax_error error;
	struct vm_heap *heap;
	struct vm_realm *realm;
	vm_value result;
	size_t index;
	int status;
	int failed;

	graph = NULL;
	failed = 0;
	status = vm_heap_create(&heap, 16U * 1024U * 1024U);
	if (status != 0)
		return 1;
	vm_heap_set_stack_base(heap, __builtin_frame_address(0));
	status = vm_realm_create(heap, &realm);
	if (status == 0)
		status = js_install_builtins(realm);
	if (status == 0)
		status = host_script(realm, "globalThis.sequence='';globalThis.result=0;", &result);
	if (status == 0)
		status = js_module_graph_create(realm, host_resolve, host_fetch, NULL, &graph);
	if (status != 0) {
		vm_realm_destroy(realm);
		vm_heap_destroy(heap);
		return 1;
	}

	/* The shared dependency runs first and is fetched/evaluated once. */
	status = js_module_graph_run(graph, host_sources[0].url, &error);
	if (status == 0)
		status = js_module_graph_run(graph, host_sources[0].url, &error);
	if (status == 0)
		status = host_script(realm,
		    "globalThis.sequence==='SLRM'&&globalThis.result===7;", &result);
	if (status != 0 || result != VM_VALUE_TRUE)
		failed = 1;
	for (index = 0; index < 4; index++) {
		if (host_sources[index].fetches != 1)
			failed = 1;
	}

	/* A missing exported name stops the bad module before its side effect. */
	if (!failed) {
		status = js_module_graph_run(graph, host_sources[4].url, &error);
		if (status != ENOENT)
			failed = 1;
		status = host_script(realm, "globalThis.sequence==='SLRM';", &result);
		if (status != 0 || result != VM_VALUE_TRUE)
			failed = 1;
	}

	/* Cyclic imports link both instances and terminate without duplicate runs. */
	if (!failed) {
		status = js_module_graph_run(graph, host_sources[5].url, &error);
		if (status == 0)
			status = js_module_graph_run(graph, host_sources[6].url, &error);
		if (status == 0)
			status = host_script(realm, "globalThis.sequence==='SLRMBA';", &result);
		if (status != 0 || result != VM_VALUE_TRUE || host_sources[5].fetches != 1 ||
		    host_sources[6].fetches != 1)
			failed = 1;
	}

	js_module_graph_release(graph);
	vm_realm_destroy(realm);
	vm_heap_destroy(heap);
	if (failed) {
		fprintf(stderr, "host-module-graph: FAIL\n");
		return 1;
	}
	printf("host-module-graph: PASS\n");
	return 0;
}

/* The production resolver supplies the same canonical URL operation. */
static int
host_resolve(
	void *context,
	const char *base,
	const uint16_t *specifier,
	size_t length,
	struct wb_buffer *url)
{
	struct wb_buffer href;
	int status;

	(void)context;
	wb_buffer_init(&href);
	status = wb_units_to_utf8(specifier, length, &href);
	if (status == 0)
		status = page_resolve_location(base, wb_buffer_string(&href), url);
	wb_buffer_release(&href);
	return status;
}

/* Counts fetches and returns source units for a canonical fixture URL. */
static int
host_fetch(
	void *context,
	const char *url,
	struct wb_units *source)
{
	size_t index;
	int status;

	(void)context;
	for (index = 0; index < sizeof(host_sources) / sizeof(host_sources[0]); index++) {
		if (strcmp(host_sources[index].url, url) != 0)
			continue;
		host_sources[index].fetches++;
		status = wb_utf8_to_units((const unsigned char *)host_sources[index].text,
		    strlen(host_sources[index].text), source);
		return status;
	}
	return ENOENT;
}

/* Runs a small classic assertion in the graph's realm. */
static int
host_script(
	struct vm_realm *realm,
	const char *source,
	vm_value *result)
{
	struct js_syntax_error error;
	struct wb_units units;
	int status;

	wb_units_init(&units);
	status = wb_utf8_to_units((const unsigned char *)source, strlen(source), &units);
	if (status == 0)
		status = js_run_script(realm, units.data, units.length, 0, result, &error);
	wb_units_release(&units);
	return status;
}
