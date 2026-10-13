/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* q1083: a named import reads its export's current binding, even after GC. */

#include "js/js.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The direct export and local import names used by the fixture. */
static const uint16_t host_counter[] = { 'c', 'o', 'u', 'n', 't', 'e', 'r' };
static const uint16_t host_increment[] = { 'i', 'n', 'c', 'r', 'e', 'm', 'e', 'n', 't' };
static const uint16_t host_linked[] = { 'l', 'i', 'n', 'k', 'e', 'd' };
static const uint16_t host_bump[] = { 'b', 'u', 'm', 'p' };
static const uint16_t host_current[] = { 'c', 'u', 'r', 'r', 'e', 'n', 't' };
static const uint16_t host_step[] = { 's', 't', 'e', 'p' };
static const uint16_t host_missing[] = { 'm', 'i', 's', 's', 'i', 'n', 'g' };
static const uint16_t host_target_specifier[] = { '.', '/', 't', 'a', 'r', 'g', 'e', 't', '.', 'j', 's' };
static const uint16_t host_wrong_specifier[] = { '.', '/', 'w', 'r', 'o', 'n', 'g', '.', 'j', 's' };
static const uint16_t host_other_specifier[] = { '.', '/', 'o', 't', 'h', 'e', 'r', '.', 'j', 's' };

static int host_module_source(struct vm_realm *realm, const char *source, struct js_module **module);
static int host_call_number(struct vm_realm *realm, vm_value function, double wanted);

int
main(void)
{
	static const char target_source[] =
	    "export let counter=1; export function increment(){counter+=1;return counter;}";
	static const char early_source[] =
	    "import {counter as linked} from './target.js'; linked;";
	static const char importer_source[] =
	    "import {counter as linked, increment as bump} from './target.js';"
	    "export function current(){return linked;}"
	    "export function step(){bump();return linked;}";
	static const char assign_source[] =
	    "import {counter as linked} from './target.js'; linked=3;";
	static const char duplicate_source[] =
	    "import {counter as linked} from './target.js';"
	    "import {increment as linked} from './target.js';";
	static const char two_sources[] =
	    "import {counter as linked} from './target.js';"
	    "import {counter as other} from './other.js';";
	struct js_module *target;
	struct js_module *early;
	struct js_module *importer;
	struct js_module *assign;
	struct js_module *duplicate;
	struct js_module *metadata;
	struct vm_heap *heap;
	struct vm_realm *realm;
	struct js_module_import_info import_info;
	vm_value current;
	vm_value step;
	int status;
	int failed;

	/* The two modules share one realm and its GC. */
	target = NULL;
	early = NULL;
	importer = NULL;
	assign = NULL;
	duplicate = NULL;
	metadata = NULL;
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

	/* An unlinked importer cannot run; an early lexical read throws. */
	if (host_module_source(realm, target_source, &target) != 0 ||
	    host_module_source(realm, early_source, &early) != 0)
		failed = 1;
	if (!failed && js_module_evaluate(early) != EAGAIN)
		failed = 1;
	if (!failed) {
		status = js_module_link_import(early, host_target_specifier, 11, host_linked, 6,
		    target, host_counter, 7);
		if (status != 0 || js_module_evaluate(early) != VM_THROWN)
			failed = 1;
		realm->exception = VM_VALUE_UNDEFINED;
	}

	/* The good importer links both names, once each, before evaluation. */
	if (!failed && host_module_source(realm, importer_source, &importer) != 0)
		failed = 1;
	if (!failed) {
		if (js_module_import_count(importer) != 2)
			failed = 1;
		status = js_module_import_at(importer, 0, &import_info);
		if (status != 0 || import_info.specifier_length != 11 ||
		    memcmp(import_info.specifier, host_target_specifier, 11 * sizeof(uint16_t)) != 0 ||
		    import_info.imported_length != 7 ||
		    memcmp(import_info.imported, host_counter, 7 * sizeof(uint16_t)) != 0 ||
		    import_info.local_length != 6 ||
		    memcmp(import_info.local, host_linked, 6 * sizeof(uint16_t)) != 0)
			failed = 1;
		status = js_module_import_at(importer, 1, &import_info);
		if (status != 0 || import_info.specifier_length != 11 ||
		    memcmp(import_info.specifier, host_target_specifier, 11 * sizeof(uint16_t)) != 0 ||
		    import_info.imported_length != 9 ||
		    memcmp(import_info.imported, host_increment, 9 * sizeof(uint16_t)) != 0 ||
		    import_info.local_length != 4 ||
		    memcmp(import_info.local, host_bump, 4 * sizeof(uint16_t)) != 0)
			failed = 1;
		if (js_module_import_at(importer, 2, &import_info) != ENOENT)
			failed = 1;
		status = js_module_link_import(importer, host_wrong_specifier, 10, host_linked, 6,
		    target, host_counter, 7);
		if (status != EINVAL)
			failed = 1;
		status = js_module_link_import(importer, host_target_specifier, 11, host_linked, 6,
		    target, host_missing, 7);
		if (status != EINVAL)
			failed = 1;
		status = js_module_link_import(importer, host_target_specifier, 11, host_missing, 7,
		    target, host_counter, 7);
		if (status != ENOENT)
			failed = 1;
		status = js_module_link_import(importer, host_target_specifier, 11, host_linked, 6,
		    target, host_counter, 7);
		if (status != 0)
			failed = 1;
		status = js_module_link_import(importer, host_target_specifier, 11, host_linked, 6,
		    target, host_counter, 7);
		if (status != EALREADY)
			failed = 1;
		status = js_module_link_import(importer, host_target_specifier, 11, host_bump, 4,
		    target, host_increment, 9);
		if (status != 0)
			failed = 1;
	}
	if (!failed && (js_module_evaluate(target) != 0 || js_module_evaluate(importer) != 0))
		failed = 1;

	/* Imported assignment throws and duplicate local import names are rejected. */
	if (!failed && host_module_source(realm, assign_source, &assign) != 0)
		failed = 1;
	if (!failed) {
		status = js_module_link_import(assign, host_target_specifier, 11, host_linked, 6,
		    target, host_counter, 7);
		if (status != 0 || js_module_evaluate(assign) != VM_THROWN)
			failed = 1;
		realm->exception = VM_VALUE_UNDEFINED;
	}
	if (!failed) {
		status = host_module_source(realm, duplicate_source, &duplicate);
		if (status != EINVAL || duplicate != NULL)
			failed = 1;
	}
	if (!failed) {
		status = host_module_source(realm, two_sources, &metadata);
		if (status != 0 || js_module_import_count(metadata) != 2)
			failed = 1;
		if (!failed) {
			status = js_module_import_at(metadata, 1, &import_info);
			if (status != 0 || import_info.specifier_length != 10 ||
			    memcmp(import_info.specifier, host_other_specifier,
			    10 * sizeof(uint16_t)) != 0)
				failed = 1;
		}
	}

	/* Only the importer's root remains; its links keep the target alive. */
	js_module_release(target);
	target = NULL;
	js_module_release(early);
	early = NULL;
	js_module_release(assign);
	assign = NULL;
	if (!failed) {
		vm_heap_set_stack_base(heap, NULL);
		vm_heap_collect(heap);
		vm_heap_set_stack_base(heap, __builtin_frame_address(0));
		status = js_module_export(importer, host_current, 7, &current);
		if (status != 0 || host_call_number(realm, current, 1.0) != 0)
			failed = 1;
		status = js_module_export(importer, host_step, 4, &step);
		if (status != 0 || host_call_number(realm, step, 2.0) != 0)
			failed = 1;
		if (host_call_number(realm, current, 2.0) != 0)
			failed = 1;
	}

	/* Roots are removed before their realm and heap. */
	js_module_release(importer);
	js_module_release(duplicate);
	js_module_release(metadata);
	vm_realm_destroy(realm);
	vm_heap_destroy(heap);
	if (failed) {
		fprintf(stderr, "host-module-import: FAIL\n");
		return 1;
	}
	printf("host-module-import: PASS\n");
	return 0;
}

/* Makes a module from a short ASCII source. */
static int
host_module_source(
	struct vm_realm *realm,
	const char *source,
	struct js_module **module)
{
	struct js_syntax_error error;
	struct wb_units units;
	int status;

	*module = NULL;
	wb_units_init(&units);
	status = wb_utf8_to_units((const unsigned char *)source, strlen(source), &units);
	if (status == 0)
		status = js_module_create(realm, units.data, units.length, module, &error);
	wb_units_release(&units);
	return status;
}

/* Calls one exported function and checks its numeric result. */
static int
host_call_number(
	struct vm_realm *realm,
	vm_value function,
	double wanted)
{
	vm_value result;
	int status;

	status = vm_call(realm, function, VM_VALUE_UNDEFINED, NULL, 0, &result);
	if (status != 0)
		return status;
	if (!vm_value_is_number(result))
		return EINVAL;
	if (vm_value_as_number(result) != wanted)
		return EINVAL;
	return 0;
}
