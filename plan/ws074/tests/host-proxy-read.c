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

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* One synchronous deletion observation keeps only integer addresses and callback counts, never managed edges. */
struct delete_observation {
	struct vm_heap *heap;
	uintptr_t addresses[5];
	unsigned get_collections;
	unsigned delete_collections;
	unsigned mode;
};

/* Integer addresses observe a single synchronous extensibility operation without retaining any managed cells. */
struct extent_observation {
	struct vm_heap *heap;
	uintptr_t addresses[4];
	unsigned collections;
	unsigned mode;
};

/* Only delete_pressure owns this context; zero means no deletion policy observation is active. */
static struct delete_observation delete_case;

/* Only extent_pressure owns this context; zero means no native policy observation is active. */
static struct extent_observation extent_case;

/* Inert storage reaches the unchanged production collection threshold during factory construction. */
static const struct vm_cell_type proxy_pressure_type = { "proxy-read-pressure", NULL, NULL };

/* These fixture observations refer only to the active independent native descriptor collection case. */
static struct vm_heap *sole_heap;

/* An integer observation never retains the managed realm that the property operation must own. */
static uintptr_t sole_realm_address;

/* Counts actual native descriptor collections that found their original managed realm alive. */
static unsigned sole_realm_alive;

/* Records the independent conversion callback's fresh primitive address without retaining its cell. */
static uintptr_t conversion_primitive_address;

/* Records collections at callback return so the independent case can prove a later parser collection occurred. */
static size_t conversion_primitive_collections;

/* The active typed GC case borrows the production immutable policy while its live object is temporarily wrapped. */
static const struct vm_native_operations *typed_saved_operations;

/* Supplies collection context only for the bounded native typed-storage observation. */
static struct vm_heap *typed_case_heap;

/* Observes the original managed owner without creating a collector edge to it. */
static uintptr_t typed_case_realm_address;

/* Counts forced typed validity collections that retained their original managed owner. */
static unsigned typed_case_collections;

static int delete_collect_get_own(struct vm_object *object, vm_value key, struct vm_property *property);
static int delete_collect_native(struct vm_heap *heap, struct vm_object *object, vm_value key, int *handled, int *deleted);

/* These hooks only collect and delegate actual descriptor/deletion policy to ordinary storage. */
static const struct vm_native_operations delete_collect_operations = {
	delete_collect_get_own, NULL, NULL, delete_collect_native, NULL
};

static int extent_collect_prevent(struct vm_object *object, int *allowed);

/* This pure policy supplies only collection and native acceptance/refusal/errno, never user script. */
static const struct vm_native_operations extent_collect_operations = {
	NULL, NULL, NULL, NULL, extent_collect_prevent
};

static int typed_collect_get_own(struct vm_object *object, vm_value key, struct vm_property *property);
static int typed_forward_define(struct vm_realm *realm, struct vm_object *object, vm_value key, const struct vm_descriptor *descriptor, int *handled, int *done);

/* This fixture table only collects and forwards actual native typed policy; it cannot implement script-side storage. */
static const struct vm_native_operations typed_collect_operations = {
	typed_collect_get_own, NULL, typed_forward_define, NULL, NULL
};

static int proxy_sole_get_own(struct vm_object *object, vm_value key, struct vm_property *property);

/* A target-specific fixture policy collects before returning its ordinary descriptor and never runs user script. */
static const struct vm_native_operations proxy_sole_operations = {
	proxy_sole_get_own, NULL, NULL, NULL, NULL
};

static int prototype_pressure(struct page *page);
static int delete_collect_participants(void);
static int delete_pressure(struct page *page);
static int proxy_delete_property(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int extent_pressure(struct page *page);
static int array_identity_pressure(struct page *page);
static int typed_storage_pressure(struct page *page);
static int proxy_canonical_index(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int proxy_to_bigint(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int conversion_primitive(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int numeric_conversion_pressure(struct page *page);
static int proxy_set_receiver(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int proxy_define_descriptor(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int descriptor_roundtrip(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int descriptor_pressure(struct page *page);
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
	int done;

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

	/* A fixture bridge exercises the shared VM descriptor operations with arbitrary input. */
	status = js_builtin_method(page->realm, page->realm->global, "descriptorRoundtrip", 2, descriptor_roundtrip);
	if (status != 0)
		goto cleanup;

	/* A private Boolean bridge compares actual VM definition with genuine Chromium reflection. */
	status = js_builtin_method(page->realm, page->realm->global, "defineReadProperty", 3, proxy_define_descriptor);
	if (status != 0)
		goto cleanup;

	/* A private Boolean bridge compares receiver-preserving VM Set with genuine Chromium reflection. */
	status = js_builtin_method(page->realm, page->realm->global, "setReadReceiver", 4, proxy_set_receiver);
	if (status != 0)
		goto cleanup;

	/* Private bridges exercise shared numeric-index and BigInt abstract operations without fixture algorithms. */
	status = js_builtin_method(page->realm, page->realm->global, "canonicalReadIndex", 1, proxy_canonical_index);
	if (status != 0)
		goto cleanup;
	status = js_builtin_method(page->realm, page->realm->global, "toBigIntRead", 1, proxy_to_bigint);
	if (status != 0)
		goto cleanup;

	/* A private Boolean bridge exposes actual deletion without strict-mode result conversion. */
	status = js_builtin_method(page->realm, page->realm->global, "deleteReadProperty", 2, proxy_delete_property);
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

	/* Fixed-buffer TypedArray Set now accepts its actual numeric element operation. */
	status = proxy_script(page->realm, "new Uint8Array(1)", &answer);
	if (status != 0)
		goto cleanup;
	status = vm_set_receiver(page->realm, (struct vm_object *)vm_value_as_cell(answer), vm_value_int32(0), vm_value_int32(1), answer, &done);
	if (status != 0 || !done) {
		status = 1;
		goto cleanup;
	}

	/* Observe actual native graph retention after the shared protocol controls. */
	status = proxy_lifetime(page);
	if (status != 0)
		goto cleanup;

	/* Exercise managed realm and sole-result retention at real native collection points. */
	status = proxy_gc_edges(page);
	if (status != 0)
		goto cleanup;

	/* Direct native partial descriptors must survive real allocation pressure without caller roots. */
	status = descriptor_pressure(page);
	if (status != 0)
		goto cleanup;
	status = numeric_conversion_pressure(page);
	if (status != 0)
		goto cleanup;
	status = typed_storage_pressure(page);
	if (status != 0)
		goto cleanup;

	/* Revoked IsArray errors retain their otherwise unowned realm and native record during real allocation. */
	status = array_identity_pressure(page);
	if (status != 0)
		goto cleanup;

	/* Extensibility retains original participants after reentrant revocation and pure native policy collection. */
	status = extent_pressure(page);
	if (status != 0)
		goto cleanup;

	/* Deletion keeps original participants and a sole fresh Symbol across post-trap and native fallback collections. */
	status = delete_pressure(page);
	if (status != 0)
		goto cleanup;

	/* Prototype operations retain requested inputs and independently release original protocol graphs. */
	status = prototype_pressure(page);

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
	struct vm_cell *roots[6];
	struct vm_cell *pressure;
	struct vm_cell *found;
	struct vm_descriptor own;
	struct vm_descriptor change;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	vm_value answer;
	vm_value made;
	vm_value key;
	vm_value get_key;
	vm_value descriptor_key;
	vm_value payload;
	vm_value write_value;
	vm_value write_receiver;
	uintptr_t realm_address;
	uintptr_t descriptor_address;
	uintptr_t define_address;
	uintptr_t receiver_address;
	unsigned index;
	unsigned registered;
	int status;
	int same;
	int present;
	int done;

	/* Setup ownership is retired deliberately before each product operation takes responsibility. */
	memset(roots, 0, sizeof(roots));
	registered = 0;
	for (index = 0; index < 6U; index++) {
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

	/* A separate fresh descriptor result must cross the same native post-trap collector. */
	descriptor_key = vm_key_from_ascii(page->heap, "getOwnPropertyDescriptor");
	if (descriptor_key == VM_VALUE_EMPTY) {
		status = 2;
		goto cleanup;
	}

	/* The fixture trap returns a descriptor and object payload with no persistent script owner. */
	status = proxy_script(page->realm, "(function(){return {value:{sole:75},writable:true,enumerable:true,configurable:true};})", &answer);
	if (status != 0)
		goto cleanup;
	roots[4] = vm_value_as_cell(answer);
	status = vm_object_define(page->heap, handler, descriptor_key, answer, VM_PROPERTY_DEFAULT);
	if (status != 0)
		goto cleanup;
	roots[4] = NULL;

	/* Define's trap erases its argument value before returning, leaving only the original native descriptor owner. */
	descriptor_key = vm_key_from_ascii(page->heap, "defineProperty");
	if (descriptor_key == VM_VALUE_EMPTY) {
		status = 2;
		goto cleanup;
	}

	/* Script collection precedes the separate post-trap native descriptor collection. */
	status = proxy_script(page->realm, "(function(t,k,d){var v=d.value;delete d.value;collectReadProxy();return v!==77;})", &answer);
	if (status != 0)
		goto cleanup;
	roots[4] = vm_value_as_cell(answer);
	status = vm_object_define(page->heap, handler, descriptor_key, answer, VM_PROPERTY_DEFAULT);
	if (status != 0)
		goto cleanup;
	roots[4] = NULL;

	/* Set's fresh value has no trap or target owner after the call frame returns. */
	descriptor_key = vm_key_from_ascii(page->heap, "set");
	if (descriptor_key == VM_VALUE_EMPTY) {
		status = 2;
		goto cleanup;
	}

	/* The subsequent native descriptor collection must rely on the production write-frame tracer. */
	status = proxy_script(page->realm, "(function(t,k,v,r){collectReadProxy();return v!==78;})", &answer);
	if (status != 0)
		goto cleanup;
	roots[4] = vm_value_as_cell(answer);
	status = vm_object_define(page->heap, handler, descriptor_key, answer, VM_PROPERTY_DEFAULT);
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

	/* GetOwn retains its managed realm and otherwise unowned trap descriptor during target collection. */
	status = vm_get_own_property(realm, (struct vm_object *)vm_value_as_cell(made), key, &own, &present);
	if (status != 0)
		goto cleanup;
	if (!present || own.has != (VM_HAS_VALUE | VM_HAS_WRITABLE | VM_HAS_ENUMERABLE | VM_HAS_CONFIGURABLE)) {
		status = 1;
		goto cleanup;
	}

	/* The complete descriptor owns the original fresh object until the embedding takes it. */
	roots[0] = vm_value_as_cell(own.value);
	descriptor_address = (uintptr_t)roots[0];
	descriptor_key = vm_key_from_ascii(page->heap, "sole");
	if (descriptor_key == VM_VALUE_EMPTY) {
		status = 2;
		goto cleanup;
	}

	/* Read a real field from the payload that had no owner during the target policy collection. */
	status = vm_get(page->realm, own.value, descriptor_key, &payload);
	if (status != 0)
		goto cleanup;
	same = vm_same_value(payload, vm_value_int32(75));
	if (!same) {
		status = 1;
		goto cleanup;
	}

	/* Protect setup only while allocating a new object that no target or trap closure will own. */
	roots[1] = &realm->cell;
	found = (struct vm_cell *)vm_object_create(page->heap, NULL);
	if (found == NULL) {
		status = 2;
		goto cleanup;
	}

	/* The temporary setup root is removed immediately before production Define takes responsibility. */
	roots[5] = found;
	define_address = (uintptr_t)found;
	status = vm_object_define(page->heap, (struct vm_object *)found, descriptor_key, vm_value_int32(76), VM_PROPERTY_DEFAULT);
	if (status != 0)
		goto cleanup;
	memset(&change, 0, sizeof(change));
	change.has = VM_HAS_VALUE | VM_HAS_CONFIGURABLE;
	change.attributes = VM_PROPERTY_CONFIGURABLE;
	change.value = vm_value_cell(found);
	roots[1] = NULL;
	roots[5] = NULL;
	status = vm_define_own_property(realm, (struct vm_object *)vm_value_as_cell(made), key, &change, &done);
	if (status != 0)
		goto cleanup;
	if (!done) {
		status = 1;
		goto cleanup;
	}

	/* The sole input payload must survive even after the trap deleted its representation field and returned. */
	found = vm_heap_find_cell(page->heap, define_address);
	if (found == NULL) {
		status = 1;
		goto cleanup;
	}

	/* Let the embedding retain the completed result only after the native ownership observation. */
	roots[5] = found;
	status = vm_get(page->realm, vm_value_cell(found), descriptor_key, &payload);
	if (status != 0)
		goto cleanup;
	same = vm_same_value(payload, vm_value_int32(76));
	if (!same) {
		status = 1;
		goto cleanup;
	}

	/* A false trap result must not query the target's native descriptor hook. */
	change.value = vm_value_int32(77);
	status = vm_define_own_property(realm, (struct vm_object *)vm_value_as_cell(made), key, &change, &done);
	if (status != 0)
		goto cleanup;
	if (done || sole_realm_alive != 4U) {
		status = 1;
		goto cleanup;
	}

	/* A separate otherwise unowned receiver must survive the same post-trap native collector. */
	roots[1] = &realm->cell;
	found = (struct vm_cell *)vm_object_create(page->heap, NULL);
	if (found == NULL) {
		status = 2;
		goto cleanup;
	}

	/* Retire all receiver and realm setup ownership before the write frame becomes responsible. */
	roots[2] = found;
	receiver_address = (uintptr_t)found;
	write_receiver = vm_value_cell(found);
	roots[1] = NULL;
	roots[2] = NULL;

	/* Retire the embedding owner before Set must preserve this otherwise unowned original write value. */
	write_value = vm_value_cell(roots[5]);
	roots[5] = NULL;
	status = vm_set_receiver(realm, (struct vm_object *)vm_value_as_cell(made), key, write_value, write_receiver, &done);
	if (status != 0)
		goto cleanup;
	if (!done) {
		status = 1;
		goto cleanup;
	}

	/* The post-trap target hook collected after the script argument frame stopped owning this value. */
	found = vm_heap_find_cell(page->heap, define_address);
	if (found == NULL) {
		status = 1;
		goto cleanup;
	}

	/* Restore value ownership before checking the independent receiver address. */
	roots[5] = found;
	found = vm_heap_find_cell(page->heap, receiver_address);
	if (found == NULL) {
		status = 1;
		goto cleanup;
	}

	/* Restore embedding receiver ownership only after the actual native retention observation. */
	roots[2] = found;
	status = vm_set_receiver(realm, (struct vm_object *)vm_value_as_cell(made), key, vm_value_int32(78), VM_VALUE_NULL, &done);
	if (status != 0)
		goto cleanup;
	if (done || sole_realm_alive != 5U) {
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
	    sole_realm_alive != 5U ||
	    realm->property_depth != 0U) {
		status = 1;
		goto cleanup;
	}

	/* No surviving protocol participant references the temporary realm once the native Get frame ends. */
	roots[0] = NULL;
	roots[2] = NULL;
	roots[3] = NULL;
	roots[4] = NULL;
	roots[5] = NULL;
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, realm_address);
	if (found != NULL) {
		status = 1;
		goto cleanup;
	}

	/* The fresh descriptor payload must also be actually reclaimed after its last embedding root ends. */
	found = vm_heap_find_cell(page->heap, descriptor_address);
	if (found != NULL) {
		status = 1;
		goto cleanup;
	}

	/* The original Define input payload has no owner once the completed embedding root ends. */
	found = vm_heap_find_cell(page->heap, define_address);
	if (found != NULL) {
		status = 1;
		goto cleanup;
	}

	/* No trap, snapshot or write-frame root may retain the completed receiver. */
	found = vm_heap_find_cell(page->heap, receiver_address);
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

/* Runs genuine VM descriptor operations through a fixture-only entry point. */
static int
descriptor_roundtrip(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_descriptor descriptor;
	int complete;
	int status;

	UNUSED_PARAMETER(receiver);

	/* All observable field reads and validation belong to the production VM conversion. */
	status = vm_to_property_descriptor(realm, js_argument(args, count, 0), &descriptor);
	if (status != 0)
		return status;
	complete = vm_to_boolean(js_argument(args, count, 1));
	if (complete)
		vm_complete_property_descriptor(&descriptor);

	/* Completion performs no allocation before the representation operation roots every present payload. */
	status = vm_from_property_descriptor(realm, &descriptor, result);
	if (status != 0)
		return status;

	/* Succeeded: the real language operations returned the partial or completed descriptor representation. */
	return 0;
}

/* Verifies FromPropertyDescriptor owns managed realms and flagged values through actual allocation pressure. */
static int
descriptor_pressure(
	struct page *page)
{
	struct vm_realm *realm;
	struct vm_object *object;
	struct vm_cell *roots[5];
	struct vm_cell *unused;
	struct vm_cell *pressure;
	struct vm_cell *found;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	struct vm_descriptor descriptor;
	struct vm_descriptor output_field;
	struct wb_vector keys;
	vm_value payload;
	vm_value answer;
	vm_value kept;
	vm_value key;
	const char *field_name;
	uintptr_t realm_address;
	uintptr_t payload_address;
	uintptr_t made_address;
	uintptr_t unused_address[2];
	unsigned mode;
	unsigned index;
	unsigned registered;
	int status;
	int present;
	int equal;

	/* A missing descriptor is represented directly and allocates no result object. */
	status = vm_from_property_descriptor(page->realm, NULL, &answer);
	if (status != 0)
		return status;
	if (answer != VM_VALUE_UNDEFINED)
		return 1;

	/* Setup roots retire before each conversion, so production ownership must take over. */
	memset(roots, 0, sizeof(roots));
	registered = 0;
	for (index = 0; index < 5U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;
		registered++;
	}

	/* A data payload and an accessor payload exercise the two independently traced descriptor kinds. */
	for (mode = 0; mode < 2U; mode++) {
		status = vm_realm_create_managed(page->heap, &realm);
		if (status != 0)
			goto cleanup;
		roots[0] = &realm->cell;
		realm_address = (uintptr_t)realm;
		if (mode == 0U) {
			object = vm_object_create(page->heap, NULL);
			if (object == NULL) {
				status = 2;
				goto cleanup;
			}

			/* The fresh plain payload has no owner beyond this setup frame. */
			payload = vm_value_cell(object);
			field_name = "value";
		} else {
			status = proxy_script(page->realm, "(function(){return 73;})", &payload);
			if (status != 0)
				goto cleanup;
			field_name = "get";
		}

		/* Protect the selected payload only until the production conversion takes responsibility. */
		roots[1] = vm_value_as_cell(payload);
		payload_address = (uintptr_t)roots[1];
		key = vm_key_from_ascii(page->heap, field_name);
		if (key == VM_VALUE_EMPTY) {
			status = 2;
			goto cleanup;
		}

		/* Unflagged payload slots contain real cells whose larger size prevents reuse as the output object. */
		for (index = 0; index < 2U; index++) {
			unused = vm_heap_alloc(page->heap, &proxy_pressure_type, 4096U);
			if (unused == NULL) {
				status = 2;
				goto cleanup;
			}

			/* Setup temporarily owns each otherwise meaningless payload cell. */
			roots[index + 3U] = unused;
			unused_address[index] = (uintptr_t)unused;
		}

		/* Presence flags alone decide which C payload slots and Boolean attributes are meaningful. */
		memset(&descriptor, 0, sizeof(descriptor));
		descriptor.attributes = VM_PROPERTY_DEFAULT;
		if (mode == 0U) {
			descriptor.has = VM_HAS_VALUE;
			descriptor.value = payload;
			descriptor.getter = vm_value_cell(roots[3]);
			descriptor.setter = vm_value_cell(roots[4]);
		} else {
			descriptor.has = VM_HAS_GET;
			descriptor.getter = payload;
			descriptor.value = vm_value_cell(roots[3]);
			descriptor.setter = vm_value_cell(roots[4]);
		}

		/* The next production allocation must collect with only the conversion's own descriptor and realm roots. */
		vm_heap_collect(page->heap);
		pressure = vm_heap_alloc(page->heap, &proxy_pressure_type, 8U * 1024U * 1024U);
		if (pressure == NULL) {
			status = 2;
			goto cleanup;
		}

		/* Transfer all meaningful ownership to the conversion before threshold collection. */
		roots[0] = NULL;
		roots[1] = NULL;
		roots[3] = NULL;
		roots[4] = NULL;
		vm_heap_stats(page->heap, &before);
		status = vm_from_property_descriptor(realm, &descriptor, &answer);
		if (status != 0)
			goto cleanup;
		roots[2] = vm_value_as_cell(answer);
		made_address = (uintptr_t)roots[2];
		vm_heap_stats(page->heap, &after);
		if (after.collections <= before.collections) {
			status = 1;
			goto cleanup;
		}

		/* The managed owner must survive the collection triggered inside conversion. */
		found = vm_heap_find_cell(page->heap, realm_address);
		if (found == NULL) {
			status = 1;
			goto cleanup;
		}

		/* Absent slots must not pin arbitrary cells merely because bytes remain in their C storage. */
		for (index = 0; index < 2U; index++) {
			found = vm_heap_find_cell(page->heap, unused_address[index]);
			if (found != NULL) {
				status = 1;
				goto cleanup;
			}
		}

		/* The sole output field is an ordinary data property retaining the original flagged value. */
		status = vm_get(page->realm, answer, key, &kept);
		if (status != 0)
			goto cleanup;
		equal = vm_same_value(kept, payload);
		if (!equal) {
			status = 1;
			goto cleanup;
		}

		/* Verify ordinary data attributes independently of script observations. */
		present = vm_get_own_descriptor((struct vm_object *)roots[2], key, &output_field);
		if (present != 1 || output_field.attributes != VM_PROPERTY_DEFAULT) {
			status = 1;
			goto cleanup;
		}

		/* Inspect the real own-key set to reject materialized absent fields. */
		wb_vector_init(&keys, sizeof(vm_value));
		status = vm_object_own_keys(page->heap, (struct vm_object *)roots[2], &keys);
		if (status != 0) {
			wb_vector_release(&keys);
			goto cleanup;
		}

		/* Stale attribute bits must not create omitted Boolean fields. */
		if (keys.count != 1U) {
			wb_vector_release(&keys);
			status = 1;
			goto cleanup;
		}

		/* Discard the fixture key buffer before observing graph retirement. */
		wb_vector_release(&keys);

		/* Releasing the actual representation drops every remaining descriptor and managed-realm owner. */
		roots[2] = NULL;
		vm_heap_collect(page->heap);
		found = vm_heap_find_cell(page->heap, realm_address);
		if (found != NULL) {
			status = 1;
			goto cleanup;
		}

		/* The original flagged payload must retire with its last representation owner. */
		found = vm_heap_find_cell(page->heap, payload_address);
		if (found != NULL) {
			status = 1;
			goto cleanup;
		}

		/* The ordinary output itself must have no residual conversion root. */
		found = vm_heap_find_cell(page->heap, made_address);
		if (found != NULL) {
			status = 1;
			goto cleanup;
		}
	}

	/* Both descriptor kinds met the pressure and lifetime criteria. */
	status = 0;

cleanup:
	/* Only successfully acquired native registrations belong to this independent probe. */
	while (registered != 0) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve failed observations after retiring this native frame's registrations. */
	if (status != 0)
		return status;

	/* Succeeded: both descriptor kinds preserved only meaningful values and retired every owned graph. */
	return 0;
}

/* Exposes Reflect-style Boolean definition through the actual VM operation for independent fixture comparisons. */
static int
proxy_define_descriptor(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_descriptor descriptor;
	struct vm_cell *key_root;
	vm_value object;
	vm_value key;
	int is_object;
	int is_cell;
	int done;
	int status;

	UNUSED_PARAMETER(receiver);

	/* Match reflection's target validation before any key or descriptor coercion. */
	object = js_argument(args, count, 0);
	is_object = vm_value_is_object(object);
	if (!is_object) {
		status = vm_throw_type_error(realm, "Define target must be an object");
		return status;
	}

	/* A freshly returned symbol key must survive later descriptor getters. */
	status = vm_to_key(realm, js_argument(args, count, 1), &key);
	if (status != 0)
		return status;
	key_root = NULL;
	is_cell = vm_value_is_cell(key);
	if (is_cell)
		key_root = vm_value_as_cell(key);
	status = vm_heap_add_root(realm->heap, &key_root);
	if (status != 0)
		return status;
	status = vm_to_property_descriptor(realm, js_argument(args, count, 2), &descriptor);
	if (status != 0)
		goto cleanup;
	status = vm_define_own_property(realm, (struct vm_object *)vm_value_as_cell(object), key, &descriptor, &done);
	if (status != 0)
		goto cleanup;
	*result = vm_value_boolean(done);

cleanup:
	/* The fixture does not retain a converted key beyond the product operation. */
	vm_heap_remove_root(realm->heap, &key_root);
	if (status != 0)
		return status;

	/* Succeeded: the actual internal definition produced the Boolean result. */
	return 0;
}

/* Calls the real receiver-aware VM operation with Reflect-style Boolean outcomes for fixture comparisons. */
static int
proxy_set_receiver(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	vm_value object;
	vm_value key;
	vm_value actual_receiver;
	int is_object;
	int done;
	int status;

	UNUSED_PARAMETER(receiver);

	/* Reflection validates its target before coercing the property key. */
	object = js_argument(args, count, 0);
	is_object = vm_value_is_object(object);
	if (!is_object) {
		status = vm_throw_type_error(realm, "Set target must be an object");
		return status;
	}

	/* A missing receiver defaults to the original target while explicit undefined remains unchanged. */
	actual_receiver = object;
	if (count > 3U)
		actual_receiver = args[3];
	status = vm_to_key(realm, js_argument(args, count, 1), &key);
	if (status != 0)
		return status;
	status = vm_set_receiver(realm, (struct vm_object *)vm_value_as_cell(object), key, js_argument(args, count, 2), actual_receiver, &done);
	if (status != 0)
		return status;
	*result = vm_value_boolean(done);

	/* Succeeded: reflection exposes the actual internal operation's Boolean outcome. */
	return 0;
}

/* Exposes only the actual VM canonical-key operation after ordinary property-key conversion. */
static int
proxy_canonical_index(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	vm_value key;
	double number;
	int canonical;
	int status;

	UNUSED_PARAMETER(receiver);

	/* The VM classifier receives an already converted property key and cannot run script itself. */
	status = vm_to_key(realm, js_argument(args, count, 0), &key);
	if (status != 0)
		return status;
	status = vm_canonical_numeric_index(key, &number, &canonical);
	if (status != 0)
		return status;
	*result = VM_VALUE_UNDEFINED;
	if (canonical)
		*result = vm_value_number(number);

	/* Succeeded: undefined distinguishes an ordinary name from every numeric result including NaN. */
	return 0;
}

/* Calls shared ToBigInt directly so the fixture cannot substitute constructor Number semantics. */
static int
proxy_to_bigint(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int status;

	UNUSED_PARAMETER(receiver);

	/* The production abstract operation owns all primitive conversion and lifetime behavior. */
	status = vm_to_bigint(realm, js_argument(args, count, 0), result);
	if (status != 0)
		return status;

	/* Succeeded: the original or converted BigInt is returned without fixture arithmetic. */
	return 0;
}

/* Returns a fresh primitive followed by enough inert pressure to force collection during its subsequent BigInt conversion. */
static int
conversion_primitive(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_string *string;
	struct vm_cell *root;
	struct vm_cell *pressure;
	struct vm_heap_stats stats;
	int status;

	UNUSED_PARAMETER(receiver);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* Protect a noninterned numeric primitive only until its caller takes responsibility. */
	root = NULL;
	status = vm_heap_add_root(realm->heap, &root);
	if (status != 0)
		return status;
	string = vm_string_from_utf8(realm->heap, "9007199254740993", 16U);
	if (string == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* The saved address is an observation and never a collector root. */
	root = &string->cell;
	conversion_primitive_address = (uintptr_t)string;
	pressure = vm_heap_alloc(realm->heap, &proxy_pressure_type, 8U * 1024U * 1024U);
	if (pressure == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* A later collection must happen after this callback releases its own primitive root. */
	vm_heap_stats(realm->heap, &stats);
	conversion_primitive_collections = stats.collections;

	/* The primitive must next be owned by ToPrimitive and the production ToBigInt tracer. */
	*result = vm_value_cell(string);
	status = 0;

cleanup:
	/* No callback root may remain when the production parser triggers its own collection. */
	vm_heap_remove_root(realm->heap, &root);
	if (status != 0)
		return status;

	/* Succeeded: a sole fresh primitive and pending real allocation pressure reach the conversion. */
	return 0;
}

/* Checks actual managed realm, source, fresh primitive and output lifetimes across Boolean and object ToBigInt allocation. */
static int
numeric_conversion_pressure(
	struct page *page)
{
	struct vm_cell *roots[3];
	struct vm_cell *pressure;
	struct vm_cell *found;
	struct vm_realm *realm;
	struct vm_object *source;
	struct vm_bigint *big;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	struct wb_buffer text;
	vm_value input;
	vm_value converted;
	uintptr_t realm_address;
	uintptr_t source_address;
	uintptr_t output_address;
	unsigned registered;
	unsigned index;
	unsigned mode;
	int differs;
	int status;

	/* Caller setup ownership ends before each actual product conversion begins. */
	memset(roots, 0, sizeof(roots));
	wb_buffer_init(&text);
	registered = 0;
	for (index = 0; index < 3U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;
		registered++;
	}

	/* Boolean allocation and a fresh object-produced primitive exercise distinct source and result paths. */
	for (mode = 0; mode < 2U; mode++) {
		conversion_primitive_address = 0;
		conversion_primitive_collections = 0;
		status = vm_realm_create_managed(page->heap, &realm);
		if (status != 0)
			goto cleanup;
		roots[0] = &realm->cell;
		realm_address = (uintptr_t)realm;
		source_address = 0;
		input = VM_VALUE_TRUE;
		if (mode != 0U) {
			source = vm_object_create(page->heap, NULL);
			if (source == NULL) {
				status = ENOMEM;
				goto cleanup;
			}

			/* Native valueOf returns a fresh string before arranging the actual parser collection. */
			roots[1] = &source->cell;
			source_address = (uintptr_t)source;
			status = js_builtin_method(page->realm, source, "valueOf", 0, conversion_primitive);
			if (status != 0)
				goto cleanup;
			input = vm_value_cell(source);
		}

		/* Start from the unchanged heap threshold with no pressure left from the previous lifetime case. */
		vm_heap_collect(page->heap);
		if (mode == 0U) {
			pressure = vm_heap_alloc(page->heap, &proxy_pressure_type, 8U * 1024U * 1024U);
			if (pressure == NULL) {
				status = ENOMEM;
				goto cleanup;
			}
		}

		/* The conversion must now retain its managed realm and original input without embedding roots. */
		roots[0] = NULL;
		roots[1] = NULL;
		vm_heap_stats(page->heap, &before);
		status = vm_to_bigint(realm, input, &converted);
		if (status != 0)
			goto cleanup;
		roots[2] = vm_value_as_cell(converted);
		output_address = (uintptr_t)roots[2];
		vm_heap_stats(page->heap, &after);
		if (after.collections <= before.collections) {
			status = 1;
			goto cleanup;
		}

		/* Exclude a test that collected only while the native valueOf callback still owned its fresh primitive. */
		if (mode != 0U && after.collections <= conversion_primitive_collections) {
			status = 1;
			goto cleanup;
		}

		/* A real managed owner survived the allocation performed inside conversion. */
		found = vm_heap_find_cell(page->heap, realm_address);
		if (found == NULL) {
			status = 1;
			goto cleanup;
		}

		/* The object-produced primitive remains a string rather than a reused address of a newly allocated result. */
		if (mode != 0U) {
			found = vm_heap_find_cell(page->heap, source_address);
			if (found == NULL) {
				status = 1;
				goto cleanup;
			}

			/* The exact fresh primitive, with its actual cell type, survived the post-return parser collection. */
			found = vm_heap_find_cell(page->heap, conversion_primitive_address);
			if (found == NULL || found->type != &vm_string_type) {
				status = 1;
				goto cleanup;
			}
		}

		/* Verify exact result magnitude with noncollectible text storage. */
		big = vm_value_as_bigint(converted);
		if (big == NULL) {
			status = 1;
			goto cleanup;
		}

		/* The Boolean and fresh string have distinct independently known exact values. */
		status = vm_bigint_to_text(big, 10, &text);
		if (status != 0)
			goto cleanup;
		differs = strcmp(wb_buffer_string(&text), "1");
		if (mode != 0U)
			differs = strcmp(wb_buffer_string(&text), "9007199254740993");
		wb_buffer_release(&text);
		wb_buffer_init(&text);
		if (differs != 0) {
			status = 1;
			goto cleanup;
		}

		/* Keeping only the BigInt output must not keep its former realm or source alive. */
		vm_heap_collect(page->heap);
		found = vm_heap_find_cell(page->heap, realm_address);
		if (found != NULL) {
			status = 1;
			goto cleanup;
		}

		/* Conversion source and fresh primitive must also be actually reclaimed once their frame ends. */
		if (mode != 0U) {
			found = vm_heap_find_cell(page->heap, source_address);
			if (found != NULL) {
				status = 1;
				goto cleanup;
			}

			/* No output graph edge may retain the temporary numeric string. */
			found = vm_heap_find_cell(page->heap, conversion_primitive_address);
			if (found != NULL) {
				status = 1;
				goto cleanup;
			}
		}

		/* The embedding still owns the completed output until its own root is released. */
		found = vm_heap_find_cell(page->heap, output_address);
		if (found == NULL) {
			status = 1;
			goto cleanup;
		}

		/* The last output root must be sufficient to control actual BigInt reclamation. */
		roots[2] = NULL;
		vm_heap_collect(page->heap);
		found = vm_heap_find_cell(page->heap, output_address);
		if (found != NULL) {
			status = 1;
			goto cleanup;
		}
	}

	/* Both conversion kinds met the independent pressure and lifetime observations. */
	status = 0;

cleanup:
	/* Saved addresses are observations only and never survive the bounded conversion cases. */
	conversion_primitive_address = 0;
	conversion_primitive_collections = 0;
	wb_buffer_release(&text);
	while (registered != 0U) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve failed lifetime observations after retiring every embedding registration. */
	if (status != 0)
		return status;

	/* Succeeded: both conversion frames and their complete temporary graphs were actually retired. */
	return 0;
}

/* Forces real collection at a typed validity read, then preserves the original immutable native policy result. */
static int
typed_collect_get_own(
	struct vm_object *object,
	vm_value key,
	struct vm_property *property)
{
	struct vm_cell *found;
	int present;

	/* The active Set frame must retain both the bare owner and a sole converted BigInt. */
	vm_heap_collect(typed_case_heap);
	found = vm_heap_find_cell(typed_case_heap, typed_case_realm_address);
	if (found != NULL)
		typed_case_collections++;
	present = typed_saved_operations->get_own(object, key, property);

	/* Preserve the real typed storage policy's found/missing/negative errno convention. */
	if (present < 0)
		return present;

	/* Succeeded: the original native descriptor decides validity. */
	return present;
}

/* Verifies that realm conversion finished before forwarding the actual pure native definition policy. */
static int
typed_forward_define(
	struct vm_realm *realm,
	struct vm_object *object,
	vm_value key,
	const struct vm_descriptor *descriptor,
	int *handled,
	int *done)
{
	struct vm_bigint *big;
	int status;

	/* Native policy must receive no script realm and an already converted BigInt value. */
	if (realm != NULL)
		return EINVAL;
	if ((descriptor->has & VM_HAS_VALUE) != 0U) {
		big = vm_value_as_bigint(descriptor->value);
		if (big == NULL)
			return EINVAL;
	}

	/* The original callback owns validation and actual byte storage; the fixture does not emulate it. */
	status = typed_saved_operations->define(NULL, object, key, descriptor, handled, done);
	if (status != 0)
		return status;

	/* Succeeded: actual native storage completed without a script-capable callback. */
	return 0;
}

/* Checks the real typed object, view, buffer and converted input graph across native collection and actual retirement. */
static int
typed_storage_pressure(
	struct page *page)
{
	struct vm_cell *roots[4];
	struct vm_cell *found;
	struct vm_realm *realm;
	struct vm_object *object;
	struct vm_object *source;
	struct vm_object *buffer;
	struct vm_bigint *big;
	struct wb_buffer text;
	vm_value array;
	vm_value input;
	vm_value answer;
	vm_value key;
	uintptr_t object_address;
	uintptr_t source_address;
	uintptr_t view_address;
	uintptr_t buffer_address;
	uintptr_t storage_address;
	uintptr_t result_address;
	unsigned index;
	unsigned registered;
	int is_object;
	int done;
	int differs;
	int status;

	/* All saved addresses are observations; only the explicitly registered setup slots own cells. */
	memset(roots, 0, sizeof(roots));
	wb_buffer_init(&text);
	registered = 0;
	object_address = 0;
	typed_case_heap = page->heap;
	typed_case_realm_address = 0;
	typed_case_collections = 0;
	typed_saved_operations = NULL;
	for (index = 0; index < 4U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;
		registered++;
	}

	/* The public constructor must create the actual BigInt content metadata and backing graph. */
	status = proxy_script(page->realm, "new BigInt64Array(1)", &array);
	if (status != 0)
		goto cleanup;
	object = (struct vm_object *)vm_value_as_cell(array);
	roots[1] = &object->cell;
	object_address = (uintptr_t)object;
	view_address = (uintptr_t)vm_value_as_cell(object->internal);
	if ((object->flags & VM_OBJECT_TYPED_BIGINT) == 0U) {
		status = 1;
		goto cleanup;
	}

	/* Observe the real public backing object without inventing private typed-view layout in the fixture. */
	key = vm_key_from_ascii(page->heap, "buffer");
	if (key == VM_VALUE_EMPTY) {
		status = ENOMEM;
		goto cleanup;
	}

	/* The typed object's own graph is the only owner of its buffer after this read. */
	status = vm_get(page->realm, array, key, &answer);
	if (status != 0)
		goto cleanup;
	buffer = (struct vm_object *)vm_value_as_cell(answer);
	buffer_address = (uintptr_t)buffer;
	storage_address = (uintptr_t)vm_value_as_cell(buffer->internal);
	status = vm_realm_create_managed(page->heap, &realm);
	if (status != 0)
		goto cleanup;
	roots[0] = &realm->cell;
	typed_case_realm_address = (uintptr_t)realm;
	source = vm_object_create(page->heap, NULL);
	if (source == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* This otherwise unowned source creates a fresh primitive and real subsequent conversion pressure. */
	roots[2] = &source->cell;
	source_address = (uintptr_t)source;
	status = js_builtin_method(page->realm, source, "valueOf", 0, conversion_primitive);
	if (status != 0)
		goto cleanup;
	input = vm_value_cell(source);
	typed_saved_operations = object->native_operations;
	object->native_operations = &typed_collect_operations;
	vm_heap_collect(page->heap);
	roots[0] = NULL;
	roots[1] = NULL;
	roots[2] = NULL;
	status = vm_set_receiver(realm, object, vm_value_int32(0), input, array, &done);
	if (status != 0)
		goto cleanup;
	if (!done ||
	    typed_case_collections < 2U ||
	    realm->property_depth != 0U) {
		status = 1;
		goto cleanup;
	}

	/* Restore ordinary embedding ownership only after the native collectors observed the original managed owner. */
	roots[1] = &object->cell;
	object->native_operations = typed_saved_operations;
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, typed_case_realm_address);
	if (found != NULL) {
		status = 1;
		goto cleanup;
	}

	/* Neither the typed result graph nor completed conversion may retain its original source object. */
	found = vm_heap_find_cell(page->heap, source_address);
	if (found != NULL) {
		status = 1;
		goto cleanup;
	}

	/* The native valueOf primitive has no persistent owner after conversion and pure storage end. */
	found = vm_heap_find_cell(page->heap, conversion_primitive_address);
	if (found != NULL) {
		status = 1;
		goto cleanup;
	}

	/* Read the actual stored bytes through the production typed descriptor path. */
	status = vm_get(page->realm, array, vm_value_int32(0), &answer);
	if (status != 0)
		goto cleanup;
	big = vm_value_as_bigint(answer);
	if (big == NULL) {
		status = 1;
		goto cleanup;
	}

	/* The sole converted BigInt must survive native validity collection rather than being replaced by a loaded zero. */
	roots[3] = vm_value_as_cell(answer);
	result_address = (uintptr_t)roots[3];
	status = vm_bigint_to_text(big, 10, &text);
	if (status != 0)
		goto cleanup;
	differs = strcmp(wb_buffer_string(&text), "9007199254740993");
	if (differs != 0) {
		status = 1;
		goto cleanup;
	}

	/* The last typed-object root must own its actual view and backing storage rather than an abandoned setup frame. */
	roots[1] = NULL;
	roots[3] = NULL;
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, object_address);
	if (found != NULL) {
		status = 1;
		goto cleanup;
	}

	/* The view cell retires with its final typed wrapper. */
	found = vm_heap_find_cell(page->heap, view_address);
	if (found != NULL) {
		status = 1;
		goto cleanup;
	}

	/* The actual public ArrayBuffer object has no residual constructor or conversion owner. */
	found = vm_heap_find_cell(page->heap, buffer_address);
	if (found != NULL) {
		status = 1;
		goto cleanup;
	}

	/* Its native storage cell is also collectible after the complete view graph retires. */
	found = vm_heap_find_cell(page->heap, storage_address);
	if (found != NULL) {
		status = 1;
		goto cleanup;
	}

	/* The loaded output BigInt has no accidental native root after the embedding releases it. */
	found = vm_heap_find_cell(page->heap, result_address);
	if (found != NULL) {
		status = 1;
		goto cleanup;
	}

	/* Every observed production storage component met its independent ownership and retirement condition. */
	status = 0;

cleanup:
	/* Restore native policy only on a still-live object before retiring the fixture's saved immutable table. */
	if (object_address != 0U && typed_saved_operations != NULL) {
		found = vm_heap_find_cell(page->heap, object_address);
		if (found != NULL) {
			is_object = vm_value_is_object(vm_value_cell(found));
			if (is_object)
				((struct vm_object *)found)->native_operations = typed_saved_operations;
		}
	}

	/* The observational globals and callback have no lifecycle beyond this bounded native case. */
	typed_saved_operations = NULL;
	typed_case_heap = NULL;
	typed_case_realm_address = 0;
	typed_case_collections = 0;
	conversion_primitive_address = 0;
	conversion_primitive_collections = 0;
	wb_buffer_release(&text);
	while (registered != 0U) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve failed native observations after restoring policy and releasing every setup root. */
	if (status != 0)
		return status;

	/* Succeeded: actual pure native storage and its complete managed graph survived and retired correctly. */
	return 0;
}

/* Observes revoked IsArray error allocation with only the production frame retaining its inputs. */
static int
array_identity_pressure(
	struct page *page)
{
	struct vm_realm *realm;
	struct vm_object *target;
	struct vm_object *handler;
	struct vm_cell *roots[4];
	struct vm_cell *pressure;
	struct vm_cell *found;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	vm_value proxy;
	uintptr_t realm_address;
	uintptr_t proxy_address;
	unsigned registered;
	unsigned index;
	int is_array;
	int status;

	/* Setup ownership is temporary and is retired before the operation under observation. */
	memset(roots, 0, sizeof(roots));
	registered = 0;
	for (index = 0; index < 4U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;
		registered++;
	}

	/* A bare managed realm reaches the real bootstrap TypeError allocation path. */
	status = vm_realm_create_managed(page->heap, &realm);
	if (status != 0)
		goto cleanup;
	roots[0] = &realm->cell;
	realm_address = (uintptr_t)realm;
	target = vm_object_create(page->heap, NULL);
	if (target == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* The factory requires real target and handler objects before its genuine native record is revoked. */
	roots[1] = &target->cell;
	handler = vm_object_create(page->heap, NULL);
	if (handler == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* The revoked record must survive solely through the production IsArray frame during the throw. */
	roots[2] = &handler->cell;
	status = vm_proxy_create(realm, vm_value_cell(target), vm_value_cell(handler), &proxy);
	if (status != 0)
		goto cleanup;
	roots[3] = vm_value_as_cell(proxy);
	proxy_address = (uintptr_t)roots[3];
	status = vm_proxy_revoke(realm, proxy);
	if (status != 0)
		goto cleanup;

	/* Ordinary threshold collection is triggered by the next actual error string allocation. */
	vm_heap_collect(page->heap);
	pressure = vm_heap_alloc(page->heap, &proxy_pressure_type, 8U * 1024U * 1024U);
	if (pressure == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* No embedding root or conservative stack scan may supply the realm or Proxy edge. */
	for (index = 0; index < 4U; index++)
		roots[index] = NULL;
	vm_heap_stats(page->heap, &before);
	status = vm_is_array(realm, proxy, &is_array);
	vm_heap_stats(page->heap, &after);
	if (status != VM_THROWN ||
	    is_array != 0 ||
	    after.collections <= before.collections) {
		status = 1;
		goto cleanup;
	}

	/* Both otherwise unowned inputs must still exist after the collection inside the error path. */
	found = vm_heap_find_cell(page->heap, realm_address);
	if (found == NULL) {
		status = 1;
		goto cleanup;
	}

	/* A rooted owner alone would not keep the revoked record, so observe the input independently. */
	found = vm_heap_find_cell(page->heap, proxy_address);
	if (found == NULL) {
		status = 1;
		goto cleanup;
	}

	/* A completed abrupt operation leaves neither input retained by its former native frame. */
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, realm_address);
	if (found != NULL) {
		status = 1;
		goto cleanup;
	}

	/* The revoked record must be actually reclaimed as well as its managed owner. */
	found = vm_heap_find_cell(page->heap, proxy_address);
	if (found != NULL) {
		status = 1;
		goto cleanup;
	}

	/* Both the pressure collection and later release were independently observed. */
	status = 0;

cleanup:
	/* Retire every setup registration on normal and abrupt completion. */
	while (registered != 0U) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve failures after releasing all embedding registrations. */
	if (status != 0)
		return status;

	/* Succeeded: the actual error path retained and then released both inputs. */
	return 0;
}

/* Collects inside the existing pure native prevention contract and observes original operation participants. */
static int
extent_collect_prevent(
	struct vm_object *object,
	int *allowed)
{
	struct vm_cell *found;
	unsigned index;

	/* This callback never executes script, even though it invokes the real collector. */
	UNUSED_PARAMETER(object);
	vm_heap_collect(extent_case.heap);
	for (index = 0; index < 4U; index++) {
		found = vm_heap_find_cell(extent_case.heap, extent_case.addresses[index]);
		if (found == NULL)
			return EFAULT;
	}

	/* Count only collections that found every otherwise unowned participant alive. */
	extent_case.collections++;
	*allowed = 0;
	if (extent_case.mode == 2U)
		return EACCES;
	if (extent_case.mode == 0U)
		*allowed = 1;

	/* Succeeded: native refusal and acceptance use the same unchanged Boolean policy contract. */
	return 0;
}

/* Exercises reentrant revocation, fresh methods and native policy collection with all setup ownership retired. */
static int
extent_pressure(
	struct page *page)
{
	struct vm_realm *realm;
	struct vm_object *target;
	struct vm_object *handler;
	struct vm_cell *roots[5];
	struct vm_cell *found;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	vm_value proxy;
	vm_value getter;
	vm_value key;
	unsigned mode;
	unsigned index;
	unsigned registered;
	int done;
	int status;

	/* A synchronous observation context holds only integer addresses, never collector edges. */
	memset(&extent_case, 0, sizeof(extent_case));
	memset(roots, 0, sizeof(roots));
	registered = 0;
	for (index = 0; index < 5U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;
		registered++;
	}

	/* Independent native acceptance, refusal, errno and script-trap modes must all retire their input graphs. */
	for (mode = 0; mode < 4U; mode++) {
		status = vm_realm_create_managed(page->heap, &realm);
		if (status != 0)
			goto cleanup;
		roots[0] = &realm->cell;
		extent_case.heap = page->heap;
		extent_case.mode = mode;
		extent_case.collections = 0;
		extent_case.addresses[0] = (uintptr_t)realm;
		target = vm_object_create(page->heap, NULL);
		if (target == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* Pure native target policy observes the post-getter fallback collection. */
		roots[1] = &target->cell;
		extent_case.addresses[1] = (uintptr_t)target;
		target->native_operations = &extent_collect_operations;
		handler = vm_object_create(page->heap, NULL);
		if (handler == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* The handler is retained only by the helper snapshot once its lookup revokes the record. */
		roots[2] = &handler->cell;
		extent_case.addresses[2] = (uintptr_t)handler;
		if (mode == 3U) {
			status = proxy_script(page->realm, "(function(){revokeReadProxy(this.owner);collectReadProxy();return function(target){collectReadProxy();return Object.isExtensible(target);};})", &getter);
		} else {
			status = proxy_script(page->realm, "(function(){revokeReadProxy(this.owner);collectReadProxy();return undefined;})", &getter);
		}

		/* The getter must be rooted before defining the ordinary handler accessor. */
		if (status != 0)
			goto cleanup;
		roots[3] = vm_value_as_cell(getter);
		if (mode == 3U)
			key = vm_key_from_ascii(page->heap, "isExtensible");
		else
			key = vm_key_from_ascii(page->heap, "preventExtensions");
		if (key == VM_VALUE_EMPTY) {
			status = ENOMEM;
			goto cleanup;
		}

		/* Accessor definition uses production descriptor semantics without a scripted fixture object model. */
		status = vm_define_accessor(realm, vm_value_cell(handler), key, getter, 0);
		if (status != 0)
			goto cleanup;
		roots[3] = NULL;
		status = vm_proxy_create(realm, vm_value_cell(target), vm_value_cell(handler), &proxy);
		if (status != 0)
			goto cleanup;
		roots[4] = vm_value_as_cell(proxy);
		extent_case.addresses[3] = (uintptr_t)roots[4];
		key = vm_key_from_ascii(page->heap, "owner");
		if (key == VM_VALUE_EMPTY) {
			status = ENOMEM;
			goto cleanup;
		}

		/* The getter revokes its own Proxy, so target and handler need independent production snapshots. */
		status = vm_object_define(page->heap, handler, key, proxy, VM_PROPERTY_DEFAULT);
		if (status != 0)
			goto cleanup;
		vm_heap_stats(page->heap, &before);
		for (index = 0; index < 5U; index++)
			roots[index] = NULL;
		if (mode == 3U)
			status = vm_is_extensible(realm, (struct vm_object *)vm_value_as_cell(proxy), &done);
		else
			status = vm_prevent_extensions(realm, (struct vm_object *)vm_value_as_cell(proxy), &done);
		vm_heap_stats(page->heap, &after);
		if (mode == 2U) {
			if (status != EACCES) {
				status = 1;
				goto cleanup;
			}

			/* The original native errno is expected and must not become a language exception. */
			status = 0;
		}

		/* Every mode must collect after setup ownership ends and restore the native recursion depth. */
		if (status != 0)
			goto cleanup;
		if (after.collections <= before.collections || realm->property_depth != 0U) {
			status = 1;
			goto cleanup;
		}

		/* A refused native policy remains distinct from actual successful prevention. */
		if (mode == 0U || mode == 3U) {
			if (!done) {
				status = 1;
				goto cleanup;
			}
		} else if (done) {
			status = 1;
			goto cleanup;
		}

		/* Native policy modes require their own collection after the scripted getter has already returned. */
		if (mode != 3U && extent_case.collections != 1U) {
			status = 1;
			goto cleanup;
		}

		/* Observe every participant separately before collecting the completed frame's otherwise unreachable graph. */
		for (index = 0; index < 4U; index++) {
			found = vm_heap_find_cell(page->heap, extent_case.addresses[index]);
			if (found == NULL) {
				status = 1;
				goto cleanup;
			}
		}

		/* All four participants must actually disappear once their operation snapshots have retired. */
		vm_heap_collect(page->heap);
		for (index = 0; index < 4U; index++) {
			found = vm_heap_find_cell(page->heap, extent_case.addresses[index]);
			if (found != NULL) {
				status = 1;
				goto cleanup;
			}
		}
	}

	/* All independent operation outcomes preserved lifetime and native policy semantics. */
	status = 0;

cleanup:
	/* Observation state is synchronous and must not outlive this one fixture call. */
	memset(&extent_case, 0, sizeof(extent_case));
	while (registered != 0U) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve any failure after retiring every temporary registration. */
	if (status != 0)
		return status;

	/* Succeeded: both native and scripted extensibility operations retained and then released their full graph. */
	return 0;
}

/* Compares the real realm deletion operation with genuine reflection without replacing its semantics. */
static int
proxy_delete_property(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	vm_value object;
	vm_value key;
	int is_object;
	int deleted;
	int status;

	UNUSED_PARAMETER(receiver);

	/* Reflection rejects a primitive target before coercing its property key. */
	object = js_argument(args, count, 0);
	is_object = vm_value_is_object(object);
	if (!is_object) {
		status = vm_throw_type_error(realm, "Delete target must be an object");
		return status;
	}

	/* The production API owns the completed key before any subsequent allocation or trap. */
	status = vm_to_key(realm, js_argument(args, count, 1), &key);
	if (status != 0)
		return status;
	status = vm_delete_property(realm, (struct vm_object *)vm_value_as_cell(object), key, &deleted);
	if (status != 0)
		return status;

	/* Succeeded: report internal refusal directly, without the delete operator's strict policy. */
	*result = vm_value_boolean(deleted);
	return 0;
}

/* Collects while observing operation participants through integer addresses alone. */
static int
delete_collect_participants(
	void)
{
	struct vm_cell *found;
	unsigned index;

	/* Pure native policy may collect but never runs user script. */
	vm_heap_collect(delete_case.heap);
	for (index = 0; index < 5U; index++) {
		found = vm_heap_find_cell(delete_case.heap, delete_case.addresses[index]);
		if (found == NULL)
			return EFAULT;
	}

	/* Succeeded: owner, target, handler, original Proxy and fresh Symbol all survived this collection. */
	return 0;
}

/* Observes the real post-trap target descriptor read without supplying a replacement object model. */
static int
delete_collect_get_own(
	struct vm_object *object,
	vm_value key,
	struct vm_property *property)
{
	int status;
	int found;

	/* Negative errno remains the get-own callback's distinct failure convention. */
	status = delete_collect_participants();
	if (status != 0)
		return -status;
	delete_case.get_collections++;
	found = vm_object_get_own_ordinary(object, key, property);
	if (found < 0)
		return found;

	/* Succeeded: ordinary descriptor policy reports absence or presence unchanged. */
	return found;
}

/* Collects in pure native deletion policy and then delegates actual storage removal to ordinary fallback. */
static int
delete_collect_native(
	struct vm_heap *heap,
	struct vm_object *object,
	vm_value key,
	int *handled,
	int *deleted)
{
	int status;

	UNUSED_PARAMETER(heap);
	UNUSED_PARAMETER(object);
	UNUSED_PARAMETER(key);

	/* The caller must retain its whole operation frame through a native callback collection. */
	*handled = 0;
	*deleted = 0;
	status = delete_collect_participants();
	if (status != 0)
		return status;
	delete_case.delete_collections++;
	if (delete_case.mode == 3U)
		return EACCES;

	/* Succeeded: the unchanged ordinary deletion path now performs the actual mutation. */
	return 0;
}

/* Checks post-trap sole-key lifetime, native fallback collection, refusal and errno without conservative roots. */
static int
delete_pressure(
	struct page *page)
{
	struct vm_realm *realm;
	struct vm_object *target;
	struct vm_object *handler;
	struct vm_symbol *symbol;
	struct vm_cell *roots[6];
	struct vm_cell *found;
	struct vm_property property;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	vm_value proxy;
	vm_value method;
	vm_value key;
	vm_value method_key;
	vm_value owner_key;
	vm_value stored_key;
	unsigned registered;
	unsigned index;
	unsigned mode;
	int deleted;
	int present;
	int status;

	/* This synchronous observer borrows a heap and stores no managed pointer edges. */
	memset(&delete_case, 0, sizeof(delete_case));
	memset(roots, 0, sizeof(roots));
	registered = 0;
	for (index = 0; index < 6U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;
		registered++;
	}

	/* Actual trap deletion, false trap, native fallback and native errno need independent input graphs. */
	for (mode = 0; mode < 4U; mode++) {
		status = vm_realm_create_managed(page->heap, &realm);
		if (status != 0)
			goto cleanup;
		roots[0] = &realm->cell;
		delete_case.heap = page->heap;
		delete_case.mode = mode;
		delete_case.get_collections = 0;
		delete_case.delete_collections = 0;
		delete_case.addresses[0] = (uintptr_t)realm;
		target = vm_object_create(page->heap, NULL);
		if (target == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* The native table only observes collection and forwards real ordinary storage policy. */
		roots[1] = &target->cell;
		delete_case.addresses[1] = (uintptr_t)target;
		target->native_operations = &delete_collect_operations;
		handler = vm_object_create(page->heap, NULL);
		if (handler == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* A never-stored fresh Symbol has no registry or shape-cache owner outside the operation frame. */
		roots[2] = &handler->cell;
		delete_case.addresses[2] = (uintptr_t)handler;
		symbol = vm_symbol_create(page->heap, VM_VALUE_UNDEFINED);
		if (symbol == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* Temporary setup ownership ends before the tested production operation begins. */
		roots[5] = &symbol->cell;
		delete_case.addresses[4] = (uintptr_t)symbol;
		key = vm_value_cell(symbol);
		stored_key = vm_key_from_ascii(page->heap, "owned");
		if (stored_key == VM_VALUE_EMPTY) {
			status = ENOMEM;
			goto cleanup;
		}

		/* Actual deletion uses an independent ordinary key so the observed Symbol never enters a heap-owned shape. */
		status = vm_object_define(page->heap, target, stored_key, VM_VALUE_TRUE, VM_PROPERTY_DEFAULT);
		if (status != 0)
			goto cleanup;
		method_key = vm_key_from_ascii(page->heap, "deleteProperty");
		owner_key = vm_key_from_ascii(page->heap, "owner");
		if (method_key == VM_VALUE_EMPTY || owner_key == VM_VALUE_EMPTY) {
			status = ENOMEM;
			goto cleanup;
		}

		/* Reentrant revocation makes the helper snapshots responsible for the original target and handler. */
		if (mode == 0U) {
			status = proxy_script(page->realm, "(function(t,k){delete t.owned;revokeReadProxy(this.owner);collectReadProxy();return true;})", &method);
		} else if (mode == 1U) {
			status = proxy_script(page->realm, "(function(){revokeReadProxy(this.owner);collectReadProxy();return false;})", &method);
		} else {
			status = proxy_script(page->realm, "(function(){revokeReadProxy(this.owner);collectReadProxy();return undefined;})", &method);
		}

		/* Trap methods and accessor getters use the same real script evaluator and normal handler storage. */
		if (status != 0)
			goto cleanup;
		roots[3] = vm_value_as_cell(method);
		if (mode < 2U)
			status = vm_object_define(page->heap, handler, method_key, method, VM_PROPERTY_DEFAULT);
		else
			status = vm_define_accessor(realm, vm_value_cell(handler), method_key, method, 0);
		if (status != 0)
			goto cleanup;
		roots[3] = NULL;
		status = vm_proxy_create(realm, vm_value_cell(target), vm_value_cell(handler), &proxy);
		if (status != 0)
			goto cleanup;
		roots[4] = vm_value_as_cell(proxy);
		delete_case.addresses[3] = (uintptr_t)roots[4];
		status = vm_object_define(page->heap, handler, owner_key, proxy, VM_PROPERTY_DEFAULT);
		if (status != 0)
			goto cleanup;

		/* Neither embedding roots nor C stack scanning may retain any operation participant. */
		vm_heap_stats(page->heap, &before);
		for (index = 0; index < 6U; index++)
			roots[index] = NULL;
		status = vm_delete_property(realm, (struct vm_object *)vm_value_as_cell(proxy), key, &deleted);
		vm_heap_stats(page->heap, &after);
		if (mode == 3U) {
			if (status != EACCES) {
				status = 1;
				goto cleanup;
			}

			/* Preserve the native errno without inventing a language exception or successful deletion. */
			status = 0;
		}

		/* Every path must collect while active and restore its native recursion counter. */
		if (status != 0)
			goto cleanup;
		if (after.collections <= before.collections || realm->property_depth != 0U) {
			status = 1;
			goto cleanup;
		}

		/* Successful actual removal is distinct from false trap refusal and native errno. */
		if (mode == 0U || mode == 2U) {
			if (!deleted) {
				status = 1;
				goto cleanup;
			}
		} else if (deleted) {
			status = 1;
			goto cleanup;
		}

		/* Only the true trap reads a target descriptor after returning; the false trap skips it entirely. */
		if (mode == 0U) {
			if (delete_case.get_collections != 1U) {
				status = 1;
				goto cleanup;
			}
		} else if (delete_case.get_collections != 0U) {
			status = 1;
			goto cleanup;
		}

		/* Ordinary native storage policy runs once for actual trap deletion or fallback, never for a false trap. */
		if (mode == 1U) {
			if (delete_case.delete_collections != 0U) {
				status = 1;
				goto cleanup;
			}
		} else if (delete_case.delete_collections != 1U) {
			status = 1;
			goto cleanup;
		}

		/* Observe all five live participants before checking actual ordinary storage without calling policy hooks. */
		for (index = 0; index < 5U; index++) {
			found = vm_heap_find_cell(page->heap, delete_case.addresses[index]);
			if (found == NULL) {
				status = 1;
				goto cleanup;
			}
		}

		/* Actual trap mutation removes its independent property; native fallback on the missing Symbol preserves it. */
		present = vm_object_get_own_ordinary(target, stored_key, &property);
		if (mode == 0U) {
			if (present != 0) {
				status = 1;
				goto cleanup;
			}
		} else if (present != 1) {
			status = 1;
			goto cleanup;
		}

		/* Completed operation frames must actually release every participant, including the sole fresh Symbol. */
		vm_heap_collect(page->heap);
		for (index = 0; index < 5U; index++) {
			found = vm_heap_find_cell(page->heap, delete_case.addresses[index]);
			if (found != NULL) {
				status = 1;
				goto cleanup;
			}
		}
	}

	/* All independent paths preserved policy, post-trap ordering and complete ownership retirement. */
	status = 0;

cleanup:
	/* The observation context is borrowed only for this synchronous fixture call. */
	memset(&delete_case, 0, sizeof(delete_case));
	while (registered != 0U) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve failures after releasing every temporary setup registration. */
	if (status != 0)
		return status;

	/* Succeeded: post-trap sole-key and native fallback collections retained and then released all participants. */
	return 0;
}

/* Observes original prototype-operation participants and independently owned fresh prototype inputs or outputs. */
static int
prototype_pressure(
	struct page *page)
{
	struct vm_realm *realm;
	struct vm_object *target;
	struct vm_object *handler;
	struct vm_object *requested;
	struct vm_cell *roots[6];
	struct vm_cell *found;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	uintptr_t addresses[5];
	vm_value proxy;
	vm_value getter;
	vm_value key;
	vm_value owner_key;
	vm_value prototype;
	unsigned registered;
	unsigned index;
	unsigned mode;
	int is_object;
	int done;
	int status;

	/* Only temporary setup roots precede the production frame's ownership. */
	memset(roots, 0, sizeof(roots));
	registered = 0;
	for (index = 0; index < 6U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;
		registered++;
	}

	/* Fresh Get output, Set input, Set fallback and Get fallback exercise distinct ownership transfers. */
	for (mode = 0; mode < 4U; mode++) {
		status = vm_realm_create_managed(page->heap, &realm);
		if (status != 0)
			goto cleanup;
		roots[0] = &realm->cell;
		addresses[0] = (uintptr_t)realm;
		target = vm_object_create(page->heap, NULL);
		if (target == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* Original targets and handlers lose their Proxy owner during reentrant method lookup. */
		roots[1] = &target->cell;
		addresses[1] = (uintptr_t)target;
		handler = vm_object_create(page->heap, NULL);
		if (handler == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* Requested prototypes have no shape, registry or other external owner. */
		roots[2] = &handler->cell;
		addresses[2] = (uintptr_t)handler;
		requested = NULL;
		if (mode != 0U) {
			requested = vm_object_create(page->heap, NULL);
			if (requested == NULL) {
				status = ENOMEM;
				goto cleanup;
			}

			/* Set's requested object must remain alive during the later getter before it reaches a script argument. */
			roots[5] = &requested->cell;
		}

		/* Get fallback starts from a real target prototype configured through the production ordinary API. */
		if (mode == 3U) {
			status = vm_set_prototype(realm, target, requested, &done);
			if (status != 0)
				goto cleanup;
			if (!done) {
				status = 1;
				goto cleanup;
			}
		}

		/* The getter revokes its own original Proxy and returns either a fresh trap or actual fallback. */
		if (mode == 0U) {
			status = proxy_script(page->realm, "(function(){revokeReadProxy(this.owner);collectReadProxy();return function(){collectReadProxy();return {};};})", &getter);
		} else if (mode == 1U) {
			status = proxy_script(page->realm, "(function(){revokeReadProxy(this.owner);collectReadProxy();return function(target,proto){collectReadProxy();return proto!==null;};})", &getter);
		} else {
			status = proxy_script(page->realm, "(function(){revokeReadProxy(this.owner);collectReadProxy();return undefined;})", &getter);
		}

		/* Accessor storage must own the fresh getter before the setup frame retires it. */
		if (status != 0)
			goto cleanup;
		roots[3] = vm_value_as_cell(getter);
		if (mode == 0U || mode == 3U)
			key = vm_key_from_ascii(page->heap, "getPrototypeOf");
		else
			key = vm_key_from_ascii(page->heap, "setPrototypeOf");
		owner_key = vm_key_from_ascii(page->heap, "owner");
		if (key == VM_VALUE_EMPTY || owner_key == VM_VALUE_EMPTY) {
			status = ENOMEM;
			goto cleanup;
		}

		/* Both the accessor and original managed Proxy use the normal language and factory implementations. */
		status = vm_define_accessor(realm, vm_value_cell(handler), key, getter, 0);
		if (status != 0)
			goto cleanup;
		roots[3] = NULL;
		status = vm_proxy_create(realm, vm_value_cell(target), vm_value_cell(handler), &proxy);
		if (status != 0)
			goto cleanup;
		roots[4] = vm_value_as_cell(proxy);
		addresses[3] = (uintptr_t)roots[4];
		status = vm_object_define(page->heap, handler, owner_key, proxy, VM_PROPERTY_DEFAULT);
		if (status != 0)
			goto cleanup;

		/* No conservative scan or embedding root may supply the participants or requested prototype. */
		vm_heap_stats(page->heap, &before);
		for (index = 0; index < 6U; index++)
			roots[index] = NULL;
		if (mode == 0U || mode == 3U) {
			status = vm_get_prototype(realm, (struct vm_object *)vm_value_as_cell(proxy), &prototype);
		} else {
			status = vm_set_prototype(realm, (struct vm_object *)vm_value_as_cell(proxy), requested, &done);
			prototype = vm_value_cell(requested);
		}

		/* Production frames must preserve normal completion and retire their native recursion count. */
		if (status != 0)
			goto cleanup;
		vm_heap_stats(page->heap, &after);
		if (after.collections <= before.collections || realm->property_depth != 0U) {
			status = 1;
			goto cleanup;
		}

		/* This independent root now owns only the final output or original requested prototype. */
		is_object = vm_value_is_object(prototype);
		if (!is_object) {
			status = 1;
			goto cleanup;
		}

		/* Fresh Get results and requested Set inputs must be genuine plain objects, never reused freed cells. */
		roots[5] = vm_value_as_cell(prototype);
		addresses[4] = (uintptr_t)roots[5];
		if (roots[5]->type != &vm_object_type) {
			status = 1;
			goto cleanup;
		}

		/* All otherwise unowned operation participants survived the getter and trap collections. */
		for (index = 0; index < 4U; index++) {
			found = vm_heap_find_cell(page->heap, addresses[index]);
			if (found == NULL) {
				status = 1;
				goto cleanup;
			}
		}

		/* Set fallback really changes target storage; a true extensible trap may legally leave it unchanged. */
		if (mode == 1U || mode == 2U) {
			if (!done) {
				status = 1;
				goto cleanup;
			}

			/* The two Set paths are distinguished by actual target prototype storage. */
			if (mode == 2U) {
				if (target->prototype != requested) {
					status = 1;
					goto cleanup;
				}
			} else if (target->prototype != NULL) {
				status = 1;
				goto cleanup;
			}
		}

		/* Keeping only the returned/requested prototype must not keep its former owner or Proxy participants. */
		vm_heap_collect(page->heap);
		for (index = 0; index < 4U; index++) {
			found = vm_heap_find_cell(page->heap, addresses[index]);
			if (found != NULL) {
				status = 1;
				goto cleanup;
			}
		}

		/* The independent prototype must remain alive after its former protocol graph has actually disappeared. */
		found = vm_heap_find_cell(page->heap, addresses[4]);
		if (found == NULL) {
			status = 1;
			goto cleanup;
		}

		/* Dropping the last output/input root must actually reclaim that prototype too. */
		roots[5] = NULL;
		vm_heap_collect(page->heap);
		found = vm_heap_find_cell(page->heap, addresses[4]);
		if (found != NULL) {
			status = 1;
			goto cleanup;
		}
	}

	/* All independent Get and Set ownership transfers were observed through actual collections. */
	status = 0;

cleanup:
	/* Retire only the setup registrations acquired by this synchronous fixture frame. */
	while (registered != 0U) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve failures after releasing every setup or output root. */
	if (status != 0)
		return status;

	/* Succeeded: prototypes and original protocol graphs were independently retained and then reclaimed. */
	return 0;
}
