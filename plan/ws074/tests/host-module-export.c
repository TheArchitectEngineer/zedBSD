/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* q1082: module exports remain live after evaluation and collection. */

#include "js/js.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The source's two export names, kept as UTF-16 units for the internal API. */
static const uint16_t host_counter_name[] = { 'c', 'o', 'u', 'n', 't', 'e', 'r' };
static const uint16_t host_increment_name[] = { 'i', 'n', 'c', 'r', 'e', 'm', 'e', 'n', 't' };
static const uint16_t host_missing_name[] = { 'm', 'i', 's', 's', 'i', 'n', 'g' };
static const uint16_t host_run_tests_name[] = { 'r', 'u', 'n', 'T', 'e', 's', 't', 's' };

int
main(int argc, char **argv)
{
	static const char source[] =
	    "export let counter=1; export function increment(){counter+=1;return counter;}";
	static const char duplicate[] = "export var a=1; export var a=2;";
	struct js_syntax_error syntax;
	struct js_module *module;
	struct vm_heap *heap;
	struct vm_realm *realm;
	struct wb_buffer bytes;
	struct wb_units units;
	vm_value function;
	vm_value result;
	vm_value value;
	int status;
	int failed;

	/* A realm with the ordinary builtins and a known stack boundary. */
	failed = 0;
	status = vm_heap_create(&heap, 16U * 1024U * 1024U);
	if (status != 0)
		return 1;
	vm_heap_set_stack_base(heap, __builtin_frame_address(0));
	status = vm_realm_create(heap, &realm);
	if (status == 0)
		status = js_install_builtins(realm);
	if (status != 0) {
		vm_heap_destroy(heap);
		return 1;
	}

	/* The instance has a name before evaluation, but no value yet. */
	wb_units_init(&units);
	status = wb_utf8_to_units((const unsigned char *)source, strlen(source), &units);
	module = NULL;
	if (status == 0)
		status = js_module_create(realm, units.data, units.length, &module, &syntax);
	wb_units_release(&units);
	if (status != 0 || module == NULL)
		failed = 1;
	if (!failed) {
		status = js_module_export(module, host_counter_name, 7, &value);
		if (status != EAGAIN)
			failed = 1;
	}

	/* Evaluation creates the function and initializes the exported counter. */
	if (!failed && js_module_evaluate(module) != 0)
		failed = 1;
	if (!failed) {
		status = js_module_export(module, host_counter_name, 7, &value);
		if (status != 0 || !vm_value_is_number(value) || vm_value_as_number(value) != 1.0)
			failed = 1;
	}
	if (!failed) {
		status = js_module_export(module, host_increment_name, 9, &function);
		if (status != 0 || !vm_value_is_cell(function))
			failed = 1;
	}

	/* The module's roots retain its closure and environment across collection. */
	if (!failed) {
		vm_heap_set_stack_base(heap, NULL);
		vm_heap_collect(heap);
		vm_heap_set_stack_base(heap, __builtin_frame_address(0));
		status = vm_call(realm, function, VM_VALUE_UNDEFINED, NULL, 0, &result);
		if (status != 0 || !vm_value_is_number(result) || vm_value_as_number(result) != 2.0)
			failed = 1;
	}
	if (!failed) {
		status = js_module_export(module, host_counter_name, 7, &value);
		if (status != 0 || !vm_value_is_number(value) || vm_value_as_number(value) != 2.0)
			failed = 1;
	}
	if (!failed && js_module_evaluate(module) != 0)
		failed = 1;
	if (!failed) {
		status = js_module_export(module, host_missing_name, 7, &value);
		if (status != ENOENT)
			failed = 1;
	}
	js_module_release(module);

	/* Duplicate names are rejected during instance creation. */
	wb_units_init(&units);
	status = wb_utf8_to_units((const unsigned char *)duplicate, strlen(duplicate), &units);
	module = NULL;
	if (status == 0)
		status = js_module_create(realm, units.data, units.length, &module, &syntax);
	wb_units_release(&units);
	if (status != EINVAL || module != NULL || strcmp(syntax.message, "Duplicate module export") != 0)
		failed = 1;
	js_module_release(module);

	/* A supplied pinned WPT support module exports a callable runTests. */
	if (argc > 1) {
		wb_buffer_init(&bytes);
		wb_units_init(&units);
		status = wb_file_read(argv[1], &bytes);
		if (status == 0)
			status = wb_utf8_to_units(bytes.data, bytes.length, &units);
		module = NULL;
		if (status == 0)
			status = js_module_create(realm, units.data, units.length, &module, &syntax);
		if (status == 0)
			status = js_module_evaluate(module);
		if (status == 0)
			status = js_module_export(module, host_run_tests_name, 8, &value);
		if (status != 0 || !vm_value_is_cell(value))
			failed = 1;
		js_module_release(module);
		wb_units_release(&units);
		wb_buffer_release(&bytes);
	}

	/* The module's roots are gone before its realm and heap. */
	vm_realm_destroy(realm);
	vm_heap_destroy(heap);
	if (failed) {
		fprintf(stderr, "host-module-export: FAIL\n");
		return 1;
	}
	printf("host-module-export: PASS\n");
	return 0;
}
