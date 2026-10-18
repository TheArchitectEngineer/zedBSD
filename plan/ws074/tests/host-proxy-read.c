/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Tests the managed native Proxy read stage through private VM bridges before global constructor exposure. */

#include "bind/internal.h"
#include "js/builtin.h"
#include "page/page.h"
#include "vm/internal.h"

#include <stdio.h>
#include <string.h>

/* Inert storage reaches the unchanged production collection threshold during factory construction. */
static const struct vm_cell_type proxy_pressure_type = { "proxy-read-pressure", NULL, NULL };

/* These fixture observations refer only to the active independent native descriptor collection case. */
static struct vm_heap *sole_heap;

/* An integer observation never retains the managed realm that the property operation must own. */
static uintptr_t sole_realm_address;

/* Counts actual native descriptor collections that found their original managed realm alive. */
static unsigned sole_realm_alive;

static int proxy_sole_get_own(struct vm_object *object, vm_value key, struct vm_property *property);

/* A target-specific fixture policy collects before returning its ordinary descriptor and never runs user script. */
static const struct vm_native_operations proxy_sole_operations = {
	proxy_sole_get_own, NULL, NULL, NULL, NULL
};

static int proxy_make(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int proxy_revoke(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int proxy_get_receiver(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int proxy_collect(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int proxy_script(struct vm_realm *realm, const char *source, vm_value *answer);
static int proxy_lifetime(struct page *page);
static int proxy_gc_edges(struct page *page);

/*
 * Runs shared protocol controls and native lifetime observations without conservative stack roots.
 */
int
main(
	int argc,
	char **argv)
{
	struct page *page;
	struct wb_buffer source;
	struct wb_units units;
	struct js_syntax_error syntax;
	const char *source_path;
	static const unsigned char html[] = "<!doctype html><html><body></body></html>";
	vm_value answer;
	int status;
	int passed;
	int printed;

	/* Keep the shared control input reproducible for historical frozen fixture binaries. */
	source_path = "plan/ws074/tests/proxy-read.js";
	if (argc == 2)
		source_path = argv[1];
	if (argc > 2)
		return 2;

	/* Establish the production Page and builtins before installing fixture-only native bridges. */
	status = page_create(&page, __builtin_frame_address(0));
	if (status != 0)
		return 2;
	status = page_load_html(page, html, sizeof(html) - 1U);
	if (status != 0)
		goto cleanup;
	status = js_builtin_method(page->realm, page->realm->global, "makeReadProxy", 2, proxy_make);
	if (status != 0)
		goto cleanup;
	status = js_builtin_method(page->realm, page->realm->global, "revokeReadProxy", 1, proxy_revoke);
	if (status != 0)
		goto cleanup;
	status = js_builtin_method(page->realm, page->realm->global, "getReadReceiver", 3, proxy_get_receiver);
	if (status != 0)
		goto cleanup;
	status = js_builtin_method(page->realm, page->realm->global, "collectReadProxy", 0, proxy_collect);
	if (status != 0)
		goto cleanup;

	/* The same source reaches native Proxy or Chromium's genuine Proxy without API substitution inside the tests. */
	wb_buffer_init(&source);
	status = wb_file_read(source_path, &source);
	if (status != 0) {
		wb_buffer_release(&source);
		goto cleanup;
	}

	/* Convert the retained fixture bytes before executing the normal program. */
	wb_units_init(&units);
	status = wb_utf8_to_units(source.data, source.length, &units);
	wb_buffer_release(&source);
	if (status != 0) {
		wb_units_release(&units);
		goto cleanup;
	}

	/* Every script-selected collection now depends only on actual VM/native ownership. */
	vm_heap_set_stack_base(page->heap, NULL);
	status = js_run_script(page->realm, units.data, units.length, 0, &answer, &syntax);
	wb_units_release(&units);
	if (status != 0)
		goto cleanup;
	passed = 0;
	if (answer == VM_VALUE_TRUE)
		passed = 1;
	if (!passed) {
		status = 1;
		goto cleanup;
	}

	/* No partial product constructor may appear during the private foundation stage. */
	status = proxy_script(page->realm, "typeof Proxy==='undefined'&&"
					   "(function(){try{makeReadProxy(function(){},{});}catch(e){return e instanceof TypeError;}return false;})()&&"
					   "(function(){var p=makeReadProxy({},{});try{makeReadProxy(p,{});}catch(e){return e instanceof TypeError;}return false;})()",
			      &answer);
	if (status != 0)
		goto cleanup;
	if (answer != VM_VALUE_TRUE) {
		status = 1;
		goto cleanup;
	}

	/* Observe actual native graph retention after the shared protocol controls. */
	status = proxy_lifetime(page);
	if (status != 0)
		goto cleanup;

	/* Exercise managed realm and sole-result retention at real native collection points. */
	status = proxy_gc_edges(page);

cleanup:
	/* Restore ordinary scanning before retiring the complete Page graph. */
	vm_heap_set_stack_base(page->heap, __builtin_frame_address(0));
	page_destroy(page);
	if (status != 0)
		return status;
	printed = printf("native proxy read ownership: passed\n");
	if (printed < 0)
		return 2;

	/* Succeeded: shared controls and native graph lifetime both held. */
	return 0;
}

/* Calls the production private factory without supplying a script implementation of Proxy behavior. */
static int
proxy_make(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int status;

	UNUSED_PARAMETER(receiver);

	/* Product validation handles missing or primitive arguments. */
	status = vm_proxy_create(realm, js_argument(args, count, 0), js_argument(args, count, 1), result);
	if (status != 0)
		return status;

	/* Succeeded: a genuine managed VM Proxy reaches the fixture. */
	return 0;
}

/* Revokes the native record through its actual private VM operation. */
static int
proxy_revoke(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int status;

	UNUSED_PARAMETER(receiver);

	/* No script-visible reference supplied by the fixture substitutes for native revocation. */
	status = vm_proxy_revoke(realm, js_argument(args, count, 0));
	if (status != 0)
		return status;
	*result = VM_VALUE_UNDEFINED;

	/* Succeeded: future reads observe the native revoked slots. */
	return 0;
}

/* Exercises the explicit receiver primitive used by Proxy fallback. */
static int
proxy_get_receiver(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	vm_value key;
	int status;

	UNUSED_PARAMETER(receiver);

	/* Convert the public fixture argument with the real language property-key conversion. */
	status = vm_to_key(realm, js_argument(args, count, 1), &key);
	if (status != 0)
		return status;
	status = vm_get_receiver(realm, js_argument(args, count, 0), key, js_argument(args, count, 2), result);
	if (status != 0)
		return status;

	/* Succeeded: the supplied receiver survived the actual native property algorithm. */
	return 0;
}

/* Forces ordinary collection at the exact point chosen by a real getter or trap. */
static int
proxy_collect(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	UNUSED_PARAMETER(receiver);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* Conservative C stack retention is disabled by main during these operations. */
	vm_heap_collect(realm->heap);
	*result = VM_VALUE_UNDEFINED;

	/* Succeeded: real user code resumes after the actual collection. */
	return 0;
}

/* Executes a native lifetime probe through the normal parser, compiler and interpreter. */
static int
proxy_script(
	struct vm_realm *realm,
	const char *source,
	vm_value *answer)
{
	struct wb_units units;
	struct js_syntax_error syntax;
	int status;

	/* Fixture strings become ordinary UTF-16 source without product API emulation. */
	wb_units_init(&units);
	status = wb_utf8_to_units((const unsigned char *)source, strlen(source), &units);
	if (status != 0) {
		wb_units_release(&units);
		return status;
	}

	/* Execute ordinary source before releasing its temporary UTF-16 storage. */
	status = js_run_script(realm, units.data, units.length, 0, answer, &syntax);
	wb_units_release(&units);
	if (status != 0)
		return status;

	/* Succeeded: the VM returned the actual probe completion value. */
	return 0;
}

/* Observes managed target/handler retention and their real reclamation after revocation. */
static int
proxy_lifetime(
	struct page *page)
{
	struct vm_proxy *proxy;
	struct vm_cell *root;
	struct vm_cell *found;
	vm_value answer;
	uintptr_t target_address;
	uintptr_t handler_address;
	uintptr_t proxy_address;
	int status;
	int retained;
	int released;

	/* Only the Proxy should retain these otherwise anonymous protocol participants. */
	status = proxy_script(page->realm, "makeReadProxy({owned:1},{})", &answer);
	if (status != 0)
		return status;
	proxy = (struct vm_proxy *)vm_value_as_cell(answer);
	root = &proxy->object.cell;
	target_address = (uintptr_t)vm_value_as_cell(proxy->target);
	handler_address = (uintptr_t)vm_value_as_cell(proxy->handler);
	proxy_address = (uintptr_t)root;
	status = vm_heap_add_root(page->heap, &root);
	if (status != 0)
		return status;

	/* A retained Proxy must trace both participants without accidental C-stack support. */
	vm_heap_collect(page->heap);
	retained = 0;
	found = vm_heap_find_cell(page->heap, target_address);
	if (found != NULL) {
		found = vm_heap_find_cell(page->heap, handler_address);
		if (found != NULL)
			retained = 1;
	}

	/* Revocation releases both protocol fields while the Proxy itself remains rooted. */
	status = vm_proxy_revoke(page->realm, vm_value_cell(proxy));
	if (status != 0) {
		vm_heap_remove_root(page->heap, &root);
		return status;
	}

	/* Observe the retired participant graph while the Proxy itself remains rooted. */
	vm_heap_collect(page->heap);
	released = 0;
	found = vm_heap_find_cell(page->heap, target_address);
	if (found == NULL) {
		found = vm_heap_find_cell(page->heap, handler_address);
		if (found == NULL)
			released = 1;
	}

	/* Releasing the last Proxy root must also make its own storage collectible. */
	vm_heap_remove_root(page->heap, &root);
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, proxy_address);
	if (found != NULL)
		return 1;
	if (!retained || !released)
		return 1;
	if (page->realm->property_depth != 0U)
		return 1;

	/* Succeeded: the managed Proxy, participants and recursion ownership all retired correctly. */
	return 0;
}

/* Collects at a script-free descriptor read after the get trap returned its otherwise unowned result. */
static int
proxy_sole_get_own(
	struct vm_object *object,
	vm_value key,
	struct vm_property *property)
{
	struct vm_cell *found;
	int result;

	/* The managed realm and trap result must survive through real native property policy. */
	vm_heap_collect(sole_heap);
	found = vm_heap_find_cell(sole_heap, sole_realm_address);
	if (found != NULL)
		sole_realm_alive++;
	result = vm_object_get_own_ordinary(object, key, property);

	/* Preserve a native policy failure without changing its negative-errno convention. */
	if (result < 0)
		return result;

	/* Succeeded: report the ordinary descriptor's missing or found classification. */
	return result;
}

/* Exercises factory pressure, managed-realm ownership and the sole trap result at a native collection boundary. */
static int
proxy_gc_edges(
	struct page *page)
{
	struct vm_realm *realm;
	struct vm_object *target;
	struct vm_object *handler;
	struct vm_cell *roots[5];
	struct vm_cell *pressure;
	struct vm_cell *found;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	vm_value answer;
	vm_value made;
	vm_value key;
	vm_value get_key;
	uintptr_t realm_address;
	unsigned index;
	unsigned registered;
	int status;
	int same;
	int present;

	/* Setup ownership is retired deliberately before each product operation takes responsibility. */
	memset(roots, 0, sizeof(roots));
	registered = 0;
	for (index = 0; index < 5U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;
		registered++;
	}

	/* The temporary managed realm has no embedding Document or explicit lifetime. */
	status = vm_realm_create_managed(page->heap, &realm);
	if (status != 0)
		goto cleanup;
	roots[0] = &realm->cell;
	realm_address = (uintptr_t)realm;
	target = vm_object_create(page->heap, NULL);
	if (target == NULL) {
		status = 2;
		goto cleanup;
	}

	/* Retain the isolated target while allocating the remaining protocol participants. */
	roots[1] = &target->cell;
	handler = vm_object_create(page->heap, NULL);
	if (handler == NULL) {
		status = 2;
		goto cleanup;
	}

	/* Retain the isolated handler until the private factory takes ownership. */
	roots[2] = &handler->cell;

	/* Intern keys before obtaining a transient function returned only to this native frame. */
	key = vm_key_from_ascii(page->heap, "x");
	if (key == VM_VALUE_EMPTY) {
		status = 2;
		goto cleanup;
	}

	/* Intern the trap key before creating a transient method function. */
	get_key = vm_key_from_ascii(page->heap, "get");
	if (get_key == VM_VALUE_EMPTY) {
		status = 2;
		goto cleanup;
	}

	/* Create the ordinary configurable descriptor used by the post-trap invariant read. */
	status = vm_object_define(page->heap, target, key, VM_VALUE_UNDEFINED, VM_PROPERTY_DEFAULT);
	if (status != 0)
		goto cleanup;
	status = proxy_script(page->realm, "(function(){return {sole:74};})", &answer);
	if (status != 0)
		goto cleanup;
	roots[4] = vm_value_as_cell(answer);
	status = vm_object_define(page->heap, handler, get_key, answer, VM_PROPERTY_DEFAULT);
	if (status != 0)
		goto cleanup;
	roots[4] = NULL;

	/* Genuine allocation pressure makes the private factory retain all otherwise unrooted inputs. */
	vm_heap_collect(page->heap);
	pressure = vm_heap_alloc(page->heap, &proxy_pressure_type, 8U * 1024U * 1024U);
	if (pressure == NULL) {
		status = 2;
		goto cleanup;
	}

	/* Drop every setup owner before the factory must survive threshold collection. */
	roots[0] = NULL;
	roots[1] = NULL;
	roots[2] = NULL;
	vm_heap_stats(page->heap, &before);
	status = vm_proxy_create(realm, vm_value_cell(target), vm_value_cell(handler), &made);
	if (status != 0)
		goto cleanup;
	roots[3] = vm_value_as_cell(made);
	vm_heap_stats(page->heap, &after);
	if (after.collections <= before.collections) {
		status = 1;
		goto cleanup;
	}

	/* This native descriptor hook may collect but never executes script, preserving the production hook contract. */
	target->native_operations = &proxy_sole_operations;
	sole_heap = page->heap;
	sole_realm_address = realm_address;
	sole_realm_alive = 0;
	status = vm_get_receiver(realm, made, key, VM_VALUE_NULL, &answer);
	if (status != 0)
		goto cleanup;
	roots[4] = vm_value_as_cell(answer);

	/* Has fallback must independently retain the managed realm through the same native descriptor collector. */
	status = vm_has_property(realm, (struct vm_object *)vm_value_as_cell(made), key, &present);
	if (status != 0)
		goto cleanup;
	if (!present) {
		status = 1;
		goto cleanup;
	}

	/* The sole result from Get must also survive the subsequent Has collection. */
	key = vm_key_from_ascii(page->heap, "sole");
	if (key == VM_VALUE_EMPTY) {
		status = 2;
		goto cleanup;
	}

	/* Read the actual sole-result payload after the native policy collection. */
	status = vm_get(page->realm, answer, key, &answer);
	if (status != 0)
		goto cleanup;
	same = vm_same_value(answer, vm_value_int32(74));
	if (!same ||
	    sole_realm_alive != 2U ||
	    realm->property_depth != 0U) {
		status = 1;
		goto cleanup;
	}

	/* No surviving protocol participant references the temporary realm once the native Get frame ends. */
	roots[3] = NULL;
	roots[4] = NULL;
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, realm_address);
	if (found != NULL) {
		status = 1;
		goto cleanup;
	}

	/* Record the successful native ownership observations before releasing setup roots. */
	status = 0;

cleanup:
	/* Fixture observations are not collector roots and never survive the independent case. */
	sole_heap = NULL;
	sole_realm_address = 0;
	while (registered != 0) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve an abrupt outcome after releasing this frame's ownership. */
	if (status != 0)
		return status;

	/* Succeeded: allocation, native descriptor collection and later realm reclamation all used real ownership. */
	return 0;
}
