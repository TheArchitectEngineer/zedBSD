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

/* One invocation observation stores only weak addresses and actual callback counters. */
struct invoke_observation {
	struct vm_heap *heap;
	uintptr_t addresses[9];
	unsigned mode;
	unsigned gets;
	unsigned traps;
};

/* One for-in probe observes only integer addresses and actual callback counts during a direct API call. */
struct for_in_observation {
	struct vm_heap *heap;
	uintptr_t addresses[3];
	unsigned calls;
};

/* One direct copying probe observes live cells through integer addresses without creating collector edges. */
struct spread_copy_observation {
	struct vm_heap *heap;
	uintptr_t addresses[4];
	unsigned mode;
	unsigned calls;
};

/* A boxed-source lifetime observation contains only an integer address and actual collection counters. */
struct flat_source_observation {
	uintptr_t box_address;
	unsigned length_calls;
	unsigned depth_collections;
};

/* Weak addresses observe one synchronous nested descriptor snapshot without retaining any managed participant. */
struct nested_snapshot_observation {
	uintptr_t target_address;
	uintptr_t payload_address;
	uintptr_t inner_address;
	unsigned getter_collections;
};

/* One public Object-consumer observation contains only weak addresses and collector/callback counters. */
struct object_consumer_observation {
	struct vm_heap *heap;
	uintptr_t caller_address;
	uintptr_t input_address;
	uintptr_t key_address;
	size_t callback_collections;
	unsigned calls;
};

/* One own-key observation borrows a heap and stores only weak integer addresses plus native callback counts. */
struct own_key_observation {
	struct vm_heap *heap;
	uintptr_t addresses[6];
	unsigned mode;
	unsigned calls;
};

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

/* Only invoke_pressure activates this synchronous observer; it contributes no collector edges. */
static struct invoke_observation invoke_case;

/* Only for_in_pressure activates this synchronous observer, which supplies no collector ownership. */
static struct for_in_observation for_in_case;

static int for_in_descriptor(struct vm_object *object, vm_value key, struct vm_property *property);

/* This pure native descriptor policy collects and returns negative errno without running script. */
static const struct vm_native_operations for_in_operations = {
	for_in_descriptor, NULL, NULL, NULL, NULL
};

/* Only spread_copy_pressure activates this synchronous observation, which never retains managed input cells. */
static struct spread_copy_observation spread_copy_case;

static int spread_copy_define(struct vm_realm *realm, struct vm_object *object, vm_value key, const struct vm_descriptor *descriptor, int *handled, int *done);

/* The target policy collects and returns native refusal or errno; it never runs user script. */
static const struct vm_native_operations spread_copy_operations = {
	NULL, NULL, spread_copy_define, NULL, NULL
};

/* Only flat_source_pressure owns this synchronous weak observer, which never marks a managed value. */
static struct flat_source_observation flat_source_case;

/* Only nested_snapshot_pressure activates this weak observer; zero means no native snapshot case is active. */
static struct nested_snapshot_observation nested_snapshot_case;

/* Only object_consumers_pressure owns this synchronous observer; zero means no active consumer case. */
static struct object_consumer_observation object_consumer_case;

/* Only own_keys_pressure owns this synchronous context; zero means no observation is active. */
static struct own_key_observation own_case;

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

static int consumer_error_keys(struct vm_heap *heap, struct vm_object *object, struct wb_vector *keys);
static int consumer_error_descriptor(struct vm_object *object, vm_value key, struct vm_property *property);

/* This pure native key policy collects and returns errno without running script. */
static const struct vm_native_operations consumer_error_keys_operations = {
	NULL, consumer_error_keys, NULL, NULL, NULL
};

/* This pure descriptor policy preserves negative errno while observing actual call ownership. */
static const struct vm_native_operations consumer_error_descriptor_operations = {
	consumer_error_descriptor, NULL, NULL, NULL, NULL
};

static int assignment_refuse_define(struct vm_realm *realm, struct vm_object *object, vm_value key, const struct vm_descriptor *descriptor, int *handled, int *done);

/* This pure assignment policy collects and refuses without invoking user code or writing a property. */
static const struct vm_native_operations assignment_refuse_operations = {
	NULL, NULL, assignment_refuse_define, NULL, NULL
};

static int array_length_delete(struct vm_heap *heap, struct vm_object *object, vm_value key, int *handled, int *deleted);

/* Native deletion observes actual descending array shrink and delegates descriptor refusal to ordinary storage. */
static const struct vm_native_operations array_length_operations = {
	NULL, NULL, NULL, array_length_delete, NULL
};

static int own_collect_keys(struct vm_heap *heap, struct vm_object *object, struct wb_vector *keys);

/* The pure native own-key prefix only collects or reports a bounded partial-error result. */
static const struct vm_native_operations own_collect_operations = {
	NULL, own_collect_keys, NULL, NULL, NULL
};

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

static int flat_source_length(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int flat_source_depth(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int flat_source_pressure(struct page *page);
static int nested_snapshot_extent(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int nested_snapshot_descriptor(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int nested_snapshot_value(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int nested_snapshot_pressure(struct page *page);
static int consumer_key_primitive(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int object_consumers_pressure(struct page *page);
static int ordinary_instance_pressure(struct page *page);
static int assignment_pressure(struct page *page);
static int array_length_pressure(struct page *page);
static void proxy_key_values_trace(struct vm_heap *heap, void *context);
static int proxy_own_keys(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int proxy_note_own_key(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int own_keys_pressure(struct page *page);
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
static int proxy_construct(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int proxy_foreign_constructor(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int proxy_foreign_construct(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int invoke_get_method(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int invoke_trap(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int invoke_pressure(struct page *page);
static int proxy_make(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int proxy_revoke(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int proxy_get_receiver(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int proxy_collect(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int spread_copy_value(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int for_in_pressure(struct page *page);
static int spread_copy_pressure(struct page *page);
static int proxy_parse_pressure(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
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

	/* Private construction and foreign-realm bridges leave the product globals unpublished. */
	status = js_builtin_method(page->realm, page->realm->global, "constructReadProxy", 2, proxy_construct);
	if (status != 0)
		goto cleanup;
	status = js_builtin_method(page->realm, page->realm->global, "makeForeignReadConstructor", 0, proxy_foreign_constructor);
	if (status != 0)
		goto cleanup;

	/* The parser probe reaches the unchanged production allocation threshold without editing heap policy. */
	status = js_builtin_method(page->realm, page->realm->global, "pressureReadProxy", 0, proxy_parse_pressure);
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

	/* Private own-key and weak observation bridges leave product enumeration entry points unchanged. */
	status = js_builtin_method(page->realm, page->realm->global, "ownReadKeys", 1, proxy_own_keys);
	if (status != 0)
		goto cleanup;
	status = js_builtin_method(page->realm, page->realm->global, "noteOwnKey", 1, proxy_note_own_key);
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
					   "(function(){var p=makeReadProxy(function(){return 7;},{});return typeof p==='function'&&p()===7;})()&&"
					   "(function(){var p=makeReadProxy({x:1},{});return makeReadProxy(p,{}).x===1;})()",
			      &answer);
	if (status != 0)
		goto cleanup;
	if (answer != VM_VALUE_TRUE) {
		status = 1;
		goto cleanup;
	}

	/* Fresh coercion text must survive a later parser allocation after the toString call frame has retired. */
	status = proxy_script(page->realm,
			      "(function(){var before,r=JSON.parse({toString:function(){var text=['{','\"a\":[1,{\"b\":2}]}'].join('');before=pressureReadProxy();return text;}});return r.a[1].b===2&&pressureReadProxy(true)>before;})()",
			      &answer);
	if (status != 0)
		goto cleanup;
	if (answer != VM_VALUE_TRUE) {
		status = 1;
		goto cleanup;
	}

	/* Publish only the actual threshold-counter and parsed-value gate after both have passed. */
	puts("native JSON parser threshold collection: passed");

	/* Fixed-buffer TypedArray Set now accepts its actual numeric element operation. */
	status = proxy_script(page->realm, "new Uint8Array(1)", &answer);
	if (status != 0)
		goto cleanup;
	status = vm_set_receiver(page->realm, (struct vm_object *)vm_value_as_cell(answer), vm_value_int32(0), vm_value_int32(1), answer, &done);
	if (status != 0 ||
	    !done) {
		status = 1;
		goto cleanup;
	}

	/* A direct CopyDataProperties call must own its bare native inputs and a fresh getter result through target policy. */
	status = spread_copy_pressure(page);
	if (status != 0)
		goto cleanup;
	puts("native object copy collection and reclamation: passed");

	/* Direct start and next calls must own their managed caller, private state and source without embedding roots. */
	status = for_in_pressure(page);
	if (status != 0)
		goto cleanup;
	puts("native for-in collection and reclamation: passed");

	/* Observe actual native graph retention after the shared protocol controls. */
	status = proxy_lifetime(page);
	if (status != 0)
		goto cleanup;

	/* Direct native invocation must own revoked snapshots, fresh methods and argument Arrays during actual GC. */
	status = invoke_pressure(page);
	if (status != 0)
		goto cleanup;
	puts("native proxy invocation snapshots and reclamation: passed");

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
	if (status != 0)
		goto cleanup;

	/* Own-key lists and preserved prefixes survive later getters, native policy collection and error rollback. */
	status = own_keys_pressure(page);
	if (status != 0)
		goto cleanup;

	/* Public Object consumers retain fresh keys through boxing and preserve native errors through actual calls. */
	status = object_consumers_pressure(page);
	if (status != 0)
		goto cleanup;

	/* The direct ordinary-instance API must own its managed caller and inputs before any collecting descriptor policy. */
	status = ordinary_instance_pressure(page);
	if (status != 0)
		goto cleanup;

	/* Object assignments must keep their direct native participants through callbacks and strict error allocation. */
	status = assignment_pressure(page);
	if (status != 0)
		goto cleanup;

	/* Native array shrink must publish its shorter length, collect safely and stop at the first fixed descriptor. */
	status = array_length_pressure(page);
	if (status != 0)
		goto cleanup;

	/* A nested target may revoke its last payload owner before the outer descriptor conversion collects. */
	status = nested_snapshot_pressure(page);
	if (status != 0)
		goto cleanup;

	/* A primitive receiver's temporary source box must survive later depth conversion and then become reclaimable. */
	status = flat_source_pressure(page);
	if (status != 0)
		goto cleanup;

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

/* Accumulate inert cells for the next real parser allocation, or report the actual collection counter. */
static int
proxy_parse_pressure(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_heap_stats stats;
	struct vm_cell *pressure;

	UNUSED_PARAMETER(receiver);
	UNUSED_PARAMETER(args);

	/* Queries allocate nothing and report actual collections rather than a simulated callback count. */
	if (count > 0U) {
		vm_heap_stats(realm->heap, &stats);
		*result = vm_value_number((double)stats.collections);
		return 0;
	}

	/* Drop old pressure before filling the unchanged minimum threshold with one inert, unrooted cell. */
	vm_heap_collect(realm->heap);
	pressure = vm_heap_alloc(realm->heap, &proxy_pressure_type, 8U * 1024U * 1024U);
	if (pressure == NULL)
		return ENOMEM;

	/* The next production allocation performs collection after this native call returns. */
	vm_heap_stats(realm->heap, &stats);
	*result = vm_value_number((double)stats.collections);
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

/* Marks only live values in a fixture vector, without retaining any completed operation owner. */
static void
proxy_key_values_trace(
	struct vm_heap *heap,
	void *context)
{
	struct wb_vector *keys;
	size_t index;

	/* Stale slots beyond count must not retain rolled-back native output. */
	keys = context;
	for (index = 0; index < keys->count; index++)
		vm_heap_mark_value(heap, ((vm_value *)keys->items)[index]);
}

/* Compares actual realm own-key enumeration with genuine reflection through an ordinary result array. */
static int
proxy_own_keys(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct wb_vector keys;
	struct vm_cell *realm_root;
	struct vm_string *string;
	vm_value object;
	vm_value key;
	size_t index;
	int traced;
	int is_object;
	int is_index;
	int status;

	UNUSED_PARAMETER(receiver);

	/* The bridge owns its managed realm even while producing an invalid-target exception or result array. */
	realm_root = NULL;
	if (realm->managed)
		realm_root = &realm->cell;
	status = vm_heap_add_root(realm->heap, &realm_root);
	if (status != 0)
		return status;
	wb_vector_init(&keys, sizeof(vm_value));
	traced = 0;
	status = vm_heap_add_tracer(realm->heap, proxy_key_values_trace, &keys);
	if (status != 0)
		goto cleanup;
	traced = 1;

	/* Reflective enumeration rejects primitive input rather than boxing it. */
	object = js_argument(args, count, 0);
	is_object = vm_value_is_object(object);
	if (!is_object) {
		status = vm_throw_type_error(realm, "Own key target must be an object");
		goto cleanup;
	}

	/* Only the production VM operation supplies keys and validates traps or invariants. */
	status = vm_own_property_keys(realm, (struct vm_object *)vm_value_as_cell(object), &keys);
	if (status != 0)
		goto cleanup;

	/* Convert internal indices only after the complete validated operation has returned. */
	for (index = 0; index < keys.count; index++) {
		key = ((vm_value *)keys.items)[index];
		is_index = vm_value_is_int32(key);
		if (!is_index)
			continue;

		/* Genuine reflection exposes numeric internal keys as primitive strings, preserving vector order. */
		status = vm_to_string(realm, key, &string);
		if (status != 0)
			goto cleanup;
		((vm_value *)keys.items)[index] = vm_value_cell(string);
	}

	/* The private result uses the same ordinary array size bound as CreateArrayFromList. */
	if (keys.count > 4294967295U) {
		status = vm_throw_range_error(realm, "Own key result exceeds the array length limit");
		goto cleanup;
	}

	/* The vector remains rooted while the real ordinary array is allocated and filled. */
	status = js_builtin_array(realm, keys.items, (unsigned)keys.count, result);

cleanup:
	/* Temporary vector storage and bridge ownership never escape into the produced array. */
	if (traced)
		vm_heap_remove_tracer(realm->heap, proxy_key_values_trace, &keys);
	wb_vector_release(&keys);
	vm_heap_remove_root(realm->heap, &realm_root);
	if (status != 0)
		return status;

	/* Succeeded: the ordinary array represents actual VM own keys. */
	return 0;
}

/* Records a fresh Symbol's address without adding any collector edge to it. */
static int
proxy_note_own_key(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	vm_value value;
	int is_cell;

	UNUSED_PARAMETER(receiver);

	/* This observation is active only during the independent synchronous own-key lifetime case. */
	value = js_argument(args, count, 0);
	is_cell = vm_value_is_cell(value);
	if (is_cell && own_case.heap == realm->heap)
		own_case.addresses[4] = (uintptr_t)vm_value_as_cell(value);
	*result = value;

	/* Succeeded: the value's only real owners remain its script or production VM frames. */
	return 0;
}

/* Collects in pure native own-key policy and verifies otherwise unowned operation participants. */
static int
own_collect_keys(
	struct vm_heap *heap,
	struct vm_object *object,
	struct wb_vector *keys)
{
	struct vm_symbol *symbol;
	struct vm_cell *found;
	vm_value key;
	unsigned index;
	int status;

	UNUSED_PARAMETER(object);

	/* This native callback runs no user script; all input and accumulated-list ownership belongs to the VM. */
	vm_heap_collect(heap);
	for (index = 0; index < 6U; index++) {
		if (own_case.addresses[index] == 0U)
			continue;
		found = vm_heap_find_cell(heap, own_case.addresses[index]);
		if (found == NULL)
			return EFAULT;
	}

	/* Count only post-trap or fallback collections that retained every expected participant. */
	own_case.calls++;
	if (own_case.mode == 3U) {
		/* A native partial append followed by errno must be rolled back without retaining its stale capacity slot. */
		symbol = vm_symbol_create(heap, VM_VALUE_UNDEFINED);
		if (symbol == NULL)
			return ENOMEM;
		key = vm_value_cell(symbol);
		status = wb_vector_push(keys, &key);
		if (status != 0)
			return status;
		own_case.addresses[4] = (uintptr_t)symbol;
		return EACCES;
	}

	/* Succeeded: ordinary storage supplies its actual keys after this collecting prefix policy. */
	return 0;
}

/* Tests accumulated-list and prefix lifetime, original participants, fallback and native partial-error rollback. */
static int
own_keys_pressure(
	struct page *page)
{
	struct vm_realm *realm;
	struct vm_object *target;
	struct vm_object *handler;
	struct vm_symbol *prefix;
	struct vm_cell *roots[6];
	struct vm_cell *found;
	struct wb_vector keys;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	vm_value proxy;
	vm_value getter;
	vm_value key;
	vm_value owner_key;
	vm_value prefix_value;
	vm_value expected_key;
	unsigned registered;
	unsigned index;
	unsigned mode;
	int traced;
	int is_string;
	int matches;
	int status;

	/* Setup roots retire before the production operation; a caller tracer is acquired only after its return. */
	memset(&own_case, 0, sizeof(own_case));
	memset(roots, 0, sizeof(roots));
	wb_vector_init(&keys, sizeof(vm_value));
	registered = 0;
	traced = 0;
	for (index = 0; index < 6U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;
		registered++;
	}

	/* Complete trap list, duplicate error, native fallback and partial native errno each need independent graphs. */
	for (mode = 0; mode < 4U; mode++) {
		status = vm_realm_create_managed(page->heap, &realm);
		if (status != 0)
			goto cleanup;
		roots[0] = &realm->cell;
		memset(&own_case, 0, sizeof(own_case));
		own_case.heap = page->heap;
		own_case.mode = mode;
		own_case.addresses[0] = (uintptr_t)realm;
		target = vm_object_create(page->heap, NULL);
		if (target == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* The original target policy only collects and delegates actual ordinary key enumeration. */
		roots[1] = &target->cell;
		own_case.addresses[1] = (uintptr_t)target;
		target->native_operations = &own_collect_operations;
		handler = vm_object_create(page->heap, NULL);
		if (handler == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* A caller prefix is a fresh never-stored Symbol, so only the API can retain it during the operation. */
		roots[2] = &handler->cell;
		own_case.addresses[2] = (uintptr_t)handler;
		prefix = vm_symbol_create(page->heap, VM_VALUE_UNDEFINED);
		if (prefix == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* No registry or shape cache owns this prefix value. */
		roots[5] = &prefix->cell;
		own_case.addresses[5] = (uintptr_t)prefix;
		prefix_value = vm_value_cell(prefix);
		status = wb_vector_push(&keys, &prefix_value);
		if (status != 0)
			goto cleanup;
		if (mode >= 2U) {
			key = vm_key_from_ascii(page->heap, "native");
			if (key == VM_VALUE_EMPTY) {
				status = ENOMEM;
				goto cleanup;
			}

			/* Native fallback must enumerate a real stored ordinary name after its collecting prefix hook. */
			status = vm_object_define(page->heap, target, key, VM_VALUE_TRUE, VM_PROPERTY_DEFAULT);
			if (status != 0)
				goto cleanup;
		}

		/* Later getters can collect the fresh Symbol returned by an earlier getter unless the accumulated list owns it. */
		if (mode == 0U) {
			status = proxy_script(page->realm, "(function(){revokeReadProxy(this.owner);collectReadProxy();return function(){return {get length(){return {valueOf:function(){collectReadProxy();return 2;}};},get 0(){return noteOwnKey(Symbol('fresh'));},get 1(){collectReadProxy();return 'v';}};};})", &getter);
		} else if (mode == 1U) {
			status = proxy_script(page->realm, "(function(){revokeReadProxy(this.owner);collectReadProxy();return function(){return ['x','x'];};})", &getter);
		} else {
			status = proxy_script(page->realm, "(function(){revokeReadProxy(this.owner);collectReadProxy();return null;})", &getter);
		}

		/* The handler accessor is ordinary production storage around a real script function. */
		if (status != 0)
			goto cleanup;
		roots[3] = vm_value_as_cell(getter);
		key = vm_key_from_ascii(page->heap, "ownKeys");
		owner_key = vm_key_from_ascii(page->heap, "owner");
		if (key == VM_VALUE_EMPTY || owner_key == VM_VALUE_EMPTY) {
			status = ENOMEM;
			goto cleanup;
		}

		/* After lookup revokes the Proxy, only its original operation snapshots retain target and handler. */
		status = vm_define_accessor(realm, vm_value_cell(handler), key, getter, 0);
		if (status != 0)
			goto cleanup;
		roots[3] = NULL;
		status = vm_proxy_create(realm, vm_value_cell(target), vm_value_cell(handler), &proxy);
		if (status != 0)
			goto cleanup;
		roots[4] = vm_value_as_cell(proxy);
		own_case.addresses[3] = (uintptr_t)roots[4];
		status = vm_object_define(page->heap, handler, owner_key, proxy, VM_PROPERTY_DEFAULT);
		if (status != 0)
			goto cleanup;

		/* Neither output vectors nor inputs have an embedding tracer during the actual VM operation. */
		vm_heap_stats(page->heap, &before);
		for (index = 0; index < 6U; index++)
			roots[index] = NULL;
		status = vm_own_property_keys(realm, (struct vm_object *)vm_value_as_cell(proxy), &keys);
		vm_heap_stats(page->heap, &after);
		if (mode == 1U) {
			if (status != VM_THROWN) {
				status = 1;
				goto cleanup;
			}

			/* A duplicate list is a language error before target native policy is ever consulted. */
			status = 0;
		} else if (mode == 3U) {
			if (status != EACCES) {
				status = 1;
				goto cleanup;
			}

			/* A partial native append preserves errno rather than becoming a completed key list. */
			status = 0;
		}

		/* Every outcome restores depth and has survived real getter or target-policy collection. */
		if (status != 0)
			goto cleanup;
		if (after.collections <= before.collections || realm->property_depth != 0U) {
			status = 1;
			goto cleanup;
		}

		/* Only now may an embedding tracer take ownership of the returned or preserved key sequence. */
		status = vm_heap_add_tracer(page->heap, proxy_key_values_trace, &keys);
		if (status != 0)
			goto cleanup;
		traced = 1;
		if (mode == 1U) {
			if (own_case.calls != 0U) {
				status = 1;
				goto cleanup;
			}
		} else if (own_case.calls != 1U) {
			status = 1;
			goto cleanup;
		}

		/* Original prefixes remain bit-identical on success and every rollback path. */
		if (keys.count == 0U || ((vm_value *)keys.items)[0] != prefix_value) {
			status = 1;
			goto cleanup;
		}

		/* Successful appends and abrupt rollback each have an exact independent vector-size expectation. */
		if (mode == 0U) {
			if (keys.count != 3U || own_case.addresses[4] == 0U) {
				status = 1;
				goto cleanup;
			}

			/* The first trap member is the actual fresh Symbol observed before its getter frame retired. */
			key = ((vm_value *)keys.items)[1];
			expected_key = vm_value_cell((void *)own_case.addresses[4]);
			if (key != expected_key) {
				status = 1;
				goto cleanup;
			}
		} else if (mode == 2U) {
			if (keys.count != 2U) {
				status = 1;
				goto cleanup;
			}

			/* This name comes from actual ordinary target storage after the collecting native prefix. */
			key = ((vm_value *)keys.items)[1];
			is_string = vm_value_is_string(key);
			if (!is_string) {
				status = 1;
				goto cleanup;
			}

			/* Verify the independently stored target name without allocating or consulting fixture policy. */
			matches = vm_string_equal_ascii((struct vm_string *)vm_value_as_cell(key), "native");
			if (!matches) {
				status = 1;
				goto cleanup;
			}
		} else if (keys.count != 1U) {
			status = 1;
			goto cleanup;
		}

		/* The output-only tracer cannot retain any original realm, target, handler or Proxy. */
		vm_heap_collect(page->heap);
		for (index = 0; index < 4U; index++) {
			found = vm_heap_find_cell(page->heap, own_case.addresses[index]);
			if (found != NULL) {
				status = 1;
				goto cleanup;
			}
		}

		/* A rolled-back partial Symbol must be reclaimed despite its stale bits remaining in vector capacity. */
		if (own_case.addresses[4] != 0U) {
			found = vm_heap_find_cell(page->heap, own_case.addresses[4]);
			if (mode == 3U) {
				if (found != NULL) {
					status = 1;
					goto cleanup;
				}
			} else if (found == NULL) {
				status = 1;
				goto cleanup;
			}
		}

		/* The preserved prefix remains alive until the caller explicitly releases its independent output sequence. */
		found = vm_heap_find_cell(page->heap, own_case.addresses[5]);
		if (found == NULL) {
			status = 1;
			goto cleanup;
		}

		/* Releasing only the live count and caller tracer must actually reclaim both never-stored Symbols. */
		keys.count = 0;
		vm_heap_remove_tracer(page->heap, proxy_key_values_trace, &keys);
		traced = 0;
		vm_heap_collect(page->heap);
		for (index = 4U; index < 6U; index++) {
			if (own_case.addresses[index] == 0U)
				continue;
			found = vm_heap_find_cell(page->heap, own_case.addresses[index]);
			if (found != NULL) {
				status = 1;
				goto cleanup;
			}
		}
	}

	/* All complete and abrupt operations preserved key-list ownership and exact append semantics. */
	status = 0;

cleanup:
	/* The observer and caller tracer are synchronous and never become permanent heap owners. */
	memset(&own_case, 0, sizeof(own_case));
	if (traced)
		vm_heap_remove_tracer(page->heap, proxy_key_values_trace, &keys);
	wb_vector_release(&keys);

	/* Retire all temporary input root registrations acquired before independent cases began. */
	while (registered != 0U) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve a failure after releasing temporary setup and vector ownership. */
	if (status != 0)
		return status;

	/* Succeeded: complete lists, rollback, native errno and real participant/Symbol reclamation were all observed. */
	return 0;
}

/* Returns a fresh unowned Symbol and leaves real allocation pressure for the caller's receiver boxing. */
static int
consumer_key_primitive(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_symbol *symbol;
	struct vm_cell *root;
	struct vm_cell *pressure;
	struct vm_heap_stats stats;
	int status;

	UNUSED_PARAMETER(receiver);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* Only this callback temporarily owns the fresh Symbol before ToPropertyKey hands it to its caller. */
	root = NULL;
	status = vm_heap_add_root(realm->heap, &root);
	if (status != 0)
		return status;
	symbol = vm_symbol_create(realm->heap, VM_VALUE_UNDEFINED);
	if (symbol == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* Its integer address is an observation, never a registry or shape-cache edge. */
	root = &symbol->cell;
	object_consumer_case.key_address = (uintptr_t)symbol;
	vm_heap_collect(realm->heap);
	pressure = vm_heap_alloc(realm->heap, &proxy_pressure_type, 8U * 1024U * 1024U);
	if (pressure == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* A strictly later collection must occur after this callback's local Symbol root retires. */
	vm_heap_stats(realm->heap, &stats);
	object_consumer_case.callback_collections = stats.collections;
	object_consumer_case.calls++;
	*result = vm_value_cell(symbol);
	status = 0;

cleanup:
	/* The returned value has no callback-owned root while the prototype method boxes its receiver. */
	vm_heap_remove_root(realm->heap, &root);
	if (status != 0)
		return status;

	/* Succeeded: the caller must now retain the fresh Symbol across its next real allocation. */
	return 0;
}

/* Collects in a native key policy before returning a real errno through the public integrity consumer. */
static int
consumer_error_keys(
	struct vm_heap *heap,
	struct vm_object *object,
	struct wb_vector *keys)
{
	struct vm_cell *found;

	UNUSED_PARAMETER(object);
	UNUSED_PARAMETER(keys);

	/* Native policy never invokes user script; VM call ownership must retain its managed caller and argument. */
	vm_heap_collect(heap);
	found = vm_heap_find_cell(heap, object_consumer_case.caller_address);
	if (found == NULL)
		return EFAULT;
	found = vm_heap_find_cell(heap, object_consumer_case.input_address);
	if (found == NULL)
		return EFAULT;
	object_consumer_case.calls++;

	/* The explicit native errno is expected, without a Boolean result or synthetic language exception. */
	return EACCES;
}

/* Preserves get-own's negative errno convention while independently collecting its public caller. */
static int
consumer_error_descriptor(
	struct vm_object *object,
	vm_value key,
	struct vm_property *property)
{
	struct vm_cell *found;

	UNUSED_PARAMETER(object);
	UNUSED_PARAMETER(key);
	UNUSED_PARAMETER(property);

	/* Native descriptors may collect but cannot run script or retire their current execution owner. */
	vm_heap_collect(object_consumer_case.heap);
	found = vm_heap_find_cell(object_consumer_case.heap, object_consumer_case.caller_address);
	if (found == NULL)
		return -EFAULT;
	found = vm_heap_find_cell(object_consumer_case.heap, object_consumer_case.input_address);
	if (found == NULL)
		return -EFAULT;
	object_consumer_case.calls++;

	/* The realm descriptor API must preserve this errno instead of negating a language throw status. */
	return -EACCES;
}

/* Tests real public Object consumers through VM call ownership, fresh-key boxing and native errno propagation. */
static int
object_consumers_pressure(
	struct page *page)
{
	struct vm_realm *caller;
	struct vm_object *input;
	struct vm_cell *roots[2];
	struct vm_cell *found;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	vm_value owner;
	vm_value function;
	vm_value key;
	vm_value argument;
	vm_value receiver;
	vm_value answer;
	const char *name;
	unsigned registered;
	unsigned index;
	unsigned mode;
	int done;
	int status;

	/* No static observation records a managed pointer or supplies a collector edge. */
	memset(&object_consumer_case, 0, sizeof(object_consumer_case));
	memset(roots, 0, sizeof(roots));
	registered = 0;
	for (index = 0; index < 2U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;
		registered++;
	}

	/* Prototype key boxing, key-list errno and descriptor errno each use actual installed native methods. */
	for (mode = 0; mode < 6U; mode++) {
		memset(&object_consumer_case, 0, sizeof(object_consumer_case));
		object_consumer_case.heap = page->heap;
		status = vm_realm_create_managed(page->heap, &caller);
		if (status != 0)
			goto cleanup;
		roots[0] = &caller->cell;
		object_consumer_case.caller_address = (uintptr_t)caller;
		input = vm_object_create(page->heap, page->realm->object_prototype);
		if (input == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* The argument is rooted only until the production VM call accepts ownership. */
		roots[1] = &input->cell;
		object_consumer_case.input_address = (uintptr_t)input;
		if (mode < 2U) {
			status = js_builtin_method(page->realm, input, "toString", 0, consumer_key_primitive);
			if (status != 0)
				goto cleanup;
			owner = vm_value_cell(page->realm->object_prototype);
			name = "hasOwnProperty";
			if (mode == 1U)
				name = "propertyIsEnumerable";
			receiver = vm_value_int32(1);
			argument = vm_value_cell(input);
		} else {
			key = vm_key_from_ascii(page->heap, "x");
			if (key == VM_VALUE_EMPTY) {
				status = ENOMEM;
				goto cleanup;
			}

			/* A real stored property is present before the target becomes nonextensible. */
			status = vm_object_define(page->heap, input, key, VM_VALUE_TRUE, VM_PROPERTY_DEFAULT);
			if (status != 0)
				goto cleanup;
			if (mode == 2U)
				input->native_operations = &consumer_error_keys_operations;
			else
				input->native_operations = &consumer_error_descriptor_operations;
			status = vm_prevent_extensions(page->realm, input, &done);
			if (status != 0)
				goto cleanup;
			if (!done) {
				status = 1;
				goto cleanup;
			}

			/* Integrity functions are fetched from the real installed Object constructor. */
			key = vm_key_from_ascii(page->heap, "Object");
			if (key == VM_VALUE_EMPTY) {
				status = ENOMEM;
				goto cleanup;
			}

			/* The intrinsic constructor already owns these methods; lookup adds no fixture implementation. */
			status = vm_get(page->realm, vm_value_cell(page->realm->global), key, &owner);
			if (status != 0)
				goto cleanup;
			name = "isFrozen";
			if (mode == 3U)
				name = "isSealed";
			receiver = owner;
			argument = vm_value_cell(input);
		}

		/* The legacy accessor consumers must also propagate pure native descriptor errors through real GetOwn. */
		if (mode >= 4U) {
			owner = vm_value_cell(page->realm->object_prototype);
			name = "__lookupGetter__";
			if (mode == 5U)
				name = "__lookupSetter__";
			receiver = vm_value_cell(input);
			argument = vm_key_from_ascii(page->heap, "x");
			if (argument == VM_VALUE_EMPTY) {
				status = ENOMEM;
				goto cleanup;
			}
		}

		/* Direct native invocation still uses vm_call, whose contract retains caller realm, callee and arguments. */
		key = vm_key_from_ascii(page->heap, name);
		if (key == VM_VALUE_EMPTY) {
			status = ENOMEM;
			goto cleanup;
		}

		/* The same production function is invoked after every temporary setup root is dropped. */
		status = vm_get(page->realm, owner, key, &function);
		if (status != 0)
			goto cleanup;
		roots[0] = NULL;
		roots[1] = NULL;
		vm_heap_stats(page->heap, &before);
		status = vm_call(caller, function, receiver, &argument, 1, &answer);
		vm_heap_stats(page->heap, &after);
		if (mode >= 2U) {
			if (status != EACCES) {
				status = 1;
				goto cleanup;
			}

			/* Native errors remain status values and never become successful integrity Booleans. */
			status = 0;
		}

		/* Every mode must reach exactly its one genuine native callback and collect during the call. */
		if (status != 0)
			goto cleanup;
		if (object_consumer_case.calls != 1U || after.collections <= before.collections) {
			status = 1;
			goto cleanup;
		}

		/* The two prototype methods report missing Symbol keys after boxing the primitive receiver. */
		if (mode < 2U) {
			if (answer != VM_VALUE_FALSE || after.collections <= object_consumer_case.callback_collections) {
				status = 1;
				goto cleanup;
			}

			/* This Symbol survived a strictly later boxing collection after the callback-local root had retired. */
			found = vm_heap_find_cell(page->heap, object_consumer_case.key_address);
			if (found == NULL) {
				status = 1;
				goto cleanup;
			}
		}

		/* Managed caller and source argument remain present after their collection-bearing native call. */
		found = vm_heap_find_cell(page->heap, object_consumer_case.caller_address);
		if (found == NULL) {
			status = 1;
			goto cleanup;
		}

		/* No returned Boolean or errno may retain the former input object. */
		found = vm_heap_find_cell(page->heap, object_consumer_case.input_address);
		if (found == NULL) {
			status = 1;
			goto cleanup;
		}

		/* Completion retires both VM-call ownership and each consumer's native temporary roots. */
		vm_heap_collect(page->heap);
		found = vm_heap_find_cell(page->heap, object_consumer_case.caller_address);
		if (found != NULL) {
			status = 1;
			goto cleanup;
		}

		/* The argument is actually reclaimed while only intrinsic methods remain globally owned. */
		found = vm_heap_find_cell(page->heap, object_consumer_case.input_address);
		if (found != NULL) {
			status = 1;
			goto cleanup;
		}

		/* Never-stored fresh Symbol keys also become actually reclaimable after the public operation ends. */
		if (mode < 2U) {
			found = vm_heap_find_cell(page->heap, object_consumer_case.key_address);
			if (found != NULL) {
				status = 1;
				goto cleanup;
			}
		}
	}

	/* All native and language statuses preserved their proper outcome and ownership conventions. */
	status = 0;

cleanup:
	/* Observation addresses never survive this synchronous native consumer case. */
	memset(&object_consumer_case, 0, sizeof(object_consumer_case));
	while (registered != 0U) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve a failure only after retiring every setup registration. */
	if (status != 0)
		return status;

	/* Succeeded: fresh-key boxing, native errors and all completed input/key reclamations were observed. */
	return 0;
}

/* Verifies direct OrdinaryHasInstance ownership without an enclosing VM call or embedding roots. */
static int
ordinary_instance_pressure(
	struct page *page)
{
	struct vm_realm *caller;
	struct vm_function *constructor;
	struct vm_object *value;
	struct vm_cell *roots[3];
	struct vm_cell *found;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	vm_value answer;
	uintptr_t value_address;
	unsigned registered;
	unsigned index;
	int status;

	/* Setup roots own only construction; the tested API must acquire all lifetime responsibility at entry. */
	memset(roots, 0, sizeof(roots));
	memset(&object_consumer_case, 0, sizeof(object_consumer_case));
	registered = 0;
	for (index = 0; index < 3U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;
		registered++;
	}

	/* A bare managed realm has no intrinsic or global registry edge that could hide missing API ownership. */
	status = vm_realm_create_managed(page->heap, &caller);
	if (status != 0)
		goto cleanup;
	roots[0] = &caller->cell;
	object_consumer_case.heap = page->heap;
	object_consumer_case.caller_address = (uintptr_t)caller;
	constructor = vm_function_create_native(page->realm, "instance-pressure", 0, proxy_collect);
	if (constructor == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* The callable constructor's native descriptor policy collects and returns a genuine negative errno. */
	roots[1] = &constructor->object.cell;
	object_consumer_case.input_address = (uintptr_t)constructor;
	constructor->object.native_operations = &consumer_error_descriptor_operations;
	value = vm_object_create(page->heap, NULL);
	if (value == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* No fixture-owned managed edge remains when the direct native operation begins. */
	roots[2] = &value->cell;
	value_address = (uintptr_t)value;
	roots[0] = NULL;
	roots[1] = NULL;
	roots[2] = NULL;
	vm_heap_stats(page->heap, &before);
	status = vm_ordinary_has_instance(caller, vm_value_cell(constructor), vm_value_cell(value), &answer);
	vm_heap_stats(page->heap, &after);
	if (status != EACCES) {
		status = EFAULT;
		goto cleanup;
	}

	/* Collection must actually run inside the one constructor-side descriptor observation. */
	if (object_consumer_case.calls != 1U || after.collections <= before.collections) {
		status = EFAULT;
		goto cleanup;
	}

	/* The value was not an argument of any enclosing VM call, yet survived constructor-side collection. */
	found = vm_heap_find_cell(page->heap, value_address);
	if (found == NULL) {
		status = EFAULT;
		goto cleanup;
	}

	/* Completion releases every direct-API owner, allowing all three original participants to be reclaimed. */
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, value_address);
	if (found != NULL) {
		status = EFAULT;
		goto cleanup;
	}

	/* The managed caller has no hidden intrinsic or test registry ownership. */
	found = vm_heap_find_cell(page->heap, object_consumer_case.caller_address);
	if (found != NULL) {
		status = EFAULT;
		goto cleanup;
	}

	/* The discarded callable constructor is independently reclaimed after native error propagation. */
	found = vm_heap_find_cell(page->heap, object_consumer_case.input_address);
	if (found != NULL) {
		status = EFAULT;
		goto cleanup;
	}

	/* All direct-call ownership and errno observations held under actual collection. */
	status = 0;

cleanup:
	/* Retire partial setup registrations and weak observations on every result path. */
	memset(&object_consumer_case, 0, sizeof(object_consumer_case));
	while (registered != 0U) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve a failed lifetime or native-error observation. */
	if (status != 0)
		return status;

	/* Succeeded: the direct API retained and then released its managed participants. */
	return 0;
}

/* Refuses a real receiver definition after collecting its native caller and leaving later error-allocation pressure. */
static int
assignment_refuse_define(
	struct vm_realm *realm,
	struct vm_object *object,
	vm_value key,
	const struct vm_descriptor *descriptor,
	int *handled,
	int *done)
{
	struct vm_cell *found;
	struct vm_cell *pressure;
	struct vm_heap_stats stats;

	UNUSED_PARAMETER(object);
	UNUSED_PARAMETER(key);
	UNUSED_PARAMETER(descriptor);

	/* This pure native policy observes weak addresses without creating an owner or running script. */
	vm_heap_collect(realm->heap);
	found = vm_heap_find_cell(realm->heap, object_consumer_case.caller_address);
	if (found == NULL)
		return EFAULT;
	found = vm_heap_find_cell(realm->heap, object_consumer_case.input_address);
	if (found == NULL)
		return EFAULT;
	pressure = vm_heap_alloc(realm->heap, &proxy_pressure_type, 8U * 1024U * 1024U);
	if (pressure == NULL)
		return ENOMEM;

	/* The outer strict-refusal allocation must cause a strictly later collection after inner Set ownership retires. */
	vm_heap_stats(realm->heap, &stats);
	object_consumer_case.callback_collections = stats.collections;
	object_consumer_case.calls++;
	*handled = 1;
	*done = 0;

	/* Succeeded: actual DefineOwnProperty refusal remains Boolean until the assignment wrapper handles strictness. */
	return 0;
}

/* Verifies direct object assignment ownership on pure native errno and later strict-refusal collection paths. */
static int
assignment_pressure(
	struct page *page)
{
	struct vm_realm *caller;
	struct vm_object *input;
	struct vm_object *value;
	struct vm_symbol *symbol;
	struct vm_cell *roots[4];
	struct vm_cell *found;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	uintptr_t addresses[4];
	vm_value key;
	unsigned registered;
	unsigned index;
	unsigned mode;
	int status;

	/* Setup owns construction only; the direct production API receives no outer VM-call protection. */
	memset(roots, 0, sizeof(roots));
	memset(&object_consumer_case, 0, sizeof(object_consumer_case));
	registered = 0;
	for (index = 0; index < 4U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;
		registered++;
	}

	/* Native GetOwn errno and native Define refusal exercise distinct stages of the real assignment wrapper. */
	for (mode = 0; mode < 2U; mode++) {
		memset(&object_consumer_case, 0, sizeof(object_consumer_case));
		object_consumer_case.heap = page->heap;
		status = vm_realm_create_managed(page->heap, &caller);
		if (status != 0)
			goto cleanup;
		roots[0] = &caller->cell;
		input = vm_object_create(page->heap, NULL);
		if (input == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* The ordinary receiver has no prototype or registry retaining any test participant. */
		roots[1] = &input->cell;
		input->native_operations = &consumer_error_descriptor_operations;
		if (mode == 1U)
			input->native_operations = &assignment_refuse_operations;
		value = vm_object_create(page->heap, NULL);
		if (value == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* Both a fresh object value and a never-stored Symbol key must survive the production operation. */
		roots[2] = &value->cell;
		symbol = vm_symbol_create(page->heap, VM_VALUE_UNDEFINED);
		if (symbol == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* Integer observations are weak and do not provide collector ownership. */
		roots[3] = &symbol->cell;
		key = vm_value_cell(symbol);
		object_consumer_case.caller_address = (uintptr_t)caller;
		object_consumer_case.input_address = (uintptr_t)input;
		for (index = 0; index < 4U; index++) {
			addresses[index] = (uintptr_t)roots[index];
			roots[index] = NULL;
		}

		/* The direct assignment alone now owns its caller, receiver, write value and Symbol key. */
		vm_heap_stats(page->heap, &before);
		status = vm_set(caller, vm_value_cell(input), key, vm_value_cell(value), 1);
		vm_heap_stats(page->heap, &after);
		if (mode == 0U) {
			if (status != EACCES) {
				status = EFAULT;
				goto cleanup;
			}
		} else {
			if (status != VM_THROWN || after.collections <= object_consumer_case.callback_collections) {
				status = EFAULT;
				goto cleanup;
			}
		}

		/* Both callbacks must actually run and collect, including the post-callback strict-error collection. */
		if (object_consumer_case.calls != 1U || after.collections <= before.collections) {
			status = EFAULT;
			goto cleanup;
		}

		/* All four participants survived the complete direct operation, including strict-refusal error construction. */
		for (index = 0; index < 4U; index++) {
			found = vm_heap_find_cell(page->heap, addresses[index]);
			if (found == NULL) {
				status = EFAULT;
				goto cleanup;
			}
		}

		/* Completed errno and thrown-error outcomes leave no permanent native owner for their original inputs. */
		vm_heap_collect(page->heap);
		for (index = 0; index < 4U; index++) {
			found = vm_heap_find_cell(page->heap, addresses[index]);
			if (found != NULL) {
				status = EFAULT;
				goto cleanup;
			}
		}
	}

	/* Both native-error and strict-refusal paths retained and then released the full original input graph. */
	status = 0;

cleanup:
	/* Retire setup registrations and weak observation context on every result path. */
	memset(&object_consumer_case, 0, sizeof(object_consumer_case));
	while (registered != 0U) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve an observed error only after every native registration has been retired. */
	if (status != 0)
		return status;

	/* Succeeded: direct assignment owns all participants through native callbacks and later refusal errors. */
	return 0;
}

/* Observes real array deletion order and collection while leaving actual configurability policy unchanged. */
static int
array_length_delete(
	struct vm_heap *heap,
	struct vm_object *object,
	vm_value key,
	int *handled,
	int *deleted)
{
	struct vm_cell *found;
	int32_t index;
	int is_index;

	UNUSED_PARAMETER(deleted);

	/* The actual snapshot is traversed descending after publishing the requested shorter metadata. */
	is_index = vm_value_is_int32(key);
	if (!is_index)
		return EFAULT;
	index = vm_value_as_int32(key);
	if (index != 3 - (int32_t)object_consumer_case.calls || object->length != 1U)
		return EFAULT;
	vm_heap_collect(heap);
	found = vm_heap_find_cell(heap, object_consumer_case.input_address);
	if (found == NULL)
		return EFAULT;
	found = vm_heap_find_cell(heap, object_consumer_case.key_address);
	if (found == NULL)
		return EFAULT;
	object_consumer_case.calls++;
	*handled = 0;

	/* Succeeded: ordinary storage still decides whether each real descriptor can be deleted. */
	return 0;
}

/* Exercises the heap-level array length contract through real partial deletion, native GC and final reclamation. */
static int
array_length_pressure(
	struct page *page)
{
	struct vm_object *array;
	struct vm_object *payload;
	struct vm_cell *roots[2];
	struct vm_cell *found;
	struct vm_property property;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	vm_value expected;
	unsigned registered;
	unsigned index;
	int present;
	int status;

	/* Setup roots retire before the low-level operation; the collector cannot rely on conservative stack scanning. */
	memset(roots, 0, sizeof(roots));
	memset(&object_consumer_case, 0, sizeof(object_consumer_case));
	registered = 0;
	for (index = 0; index < 2U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;
		registered++;
	}

	/* A standalone array owns one fresh lower element and both the fixed and removable higher indices. */
	array = vm_array_create(page->heap, NULL);
	if (array == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* The lower object payload must remain live even while temporary length metadata is smaller than its index. */
	roots[0] = &array->cell;
	payload = vm_object_create(page->heap, NULL);
	if (payload == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* Actual storage descriptors decide refusal; the callback supplies no mock deletion result. */
	roots[1] = &payload->cell;
	status = vm_object_define(page->heap, array, vm_value_int32(1), vm_value_cell(payload), VM_PROPERTY_DEFAULT);
	if (status != 0)
		goto cleanup;
	status = vm_object_define(page->heap, array, vm_value_int32(2), VM_VALUE_TRUE, VM_PROPERTY_WRITABLE);
	if (status != 0)
		goto cleanup;
	status = vm_object_define(page->heap, array, vm_value_int32(3), VM_VALUE_TRUE, VM_PROPERTY_DEFAULT);
	if (status != 0)
		goto cleanup;
	array->native_operations = &array_length_operations;
	object_consumer_case.input_address = (uintptr_t)array;
	object_consumer_case.key_address = (uintptr_t)payload;
	roots[0] = NULL;
	roots[1] = NULL;

	/* The production heap API alone owns the array while both descending deletions actually collect. */
	vm_heap_stats(page->heap, &before);
	status = vm_array_set_length(page->heap, array, 1U);
	vm_heap_stats(page->heap, &after);
	if (status != EPERM ||
	    object_consumer_case.calls != 2U ||
	    after.collections < before.collections + 2U ||
	    array->length != 3U) {
		status = EFAULT;
		goto cleanup;
	}

	/* The higher configurable element was deleted before the fixed element stopped further shrinking. */
	present = vm_object_get_own_ordinary(array, vm_value_int32(3), &property);
	if (present != 0) {
		status = EFAULT;
		goto cleanup;
	}

	/* The lower payload was never deleted or collected despite the temporarily published shorter length. */
	expected = vm_value_cell(payload);
	present = vm_object_get_own_ordinary(array, vm_value_int32(1), &property);
	if (present != 1 || *property.value != expected) {
		status = EFAULT;
		goto cleanup;
	}

	/* No key snapshot or native registration retains either original object after the operation completes. */
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, object_consumer_case.input_address);
	if (found != NULL) {
		status = EFAULT;
		goto cleanup;
	}

	/* The lower payload is independently reclaimed after its former array owner is gone. */
	found = vm_heap_find_cell(page->heap, object_consumer_case.key_address);
	if (found != NULL) {
		status = EFAULT;
		goto cleanup;
	}

	/* Real deletion ordering, partial metadata restoration and complete native ownership all held. */
	status = 0;

cleanup:
	/* Weak observation addresses and partial setup registrations retire on every exit. */
	memset(&object_consumer_case, 0, sizeof(object_consumer_case));
	while (registered != 0U) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve any violated native-array or lifetime observation after cleanup. */
	if (status != 0)
		return status;

	/* Succeeded: descending shrink preserves its failed boundary and releases both managed participants. */
	return 0;
}

/* Revokes the inner Proxy during its actual IsExtensible trap while preserving the original target answer. */
static int
nested_snapshot_extent(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_cell *inner;
	vm_value target;
	int extensible;
	int status;

	UNUSED_PARAMETER(receiver);

	/* The integer observer supplies no root; the outer operation must still own the inner target Proxy. */
	inner = vm_heap_find_cell(realm->heap, nested_snapshot_case.inner_address);
	if (inner == NULL)
		return EFAULT;
	status = vm_proxy_revoke(realm, vm_value_cell(inner));
	if (status != 0)
		return status;
	target = js_argument(args, count, 0);
	status = vm_is_extensible(realm, (struct vm_object *)vm_value_as_cell(target), &extensible);
	if (status != 0)
		return status;
	*result = vm_value_boolean(extensible);

	/* Succeeded: this trap returns while retiring the inner Proxy's original target and handler edges. */
	return 0;
}

/* Returns an actual descriptor representation whose value accessor will run only after target extent observation. */
static int
nested_snapshot_descriptor(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	vm_value key;
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The descriptor representation belongs only to the original outer handler graph. */
	key = vm_key_from_ascii(realm->heap, "resultDescriptor");
	if (key == VM_VALUE_EMPTY)
		return ENOMEM;
	status = vm_get(realm, receiver, key, result);
	if (status != 0)
		return status;

	/* Succeeded: descriptor conversion must wait until the actual nested target checks finish. */
	return 0;
}

/* Collects after the original target frame has retired, then recovers only the still-owned snapshot payload. */
static int
nested_snapshot_value(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_cell *found;
	struct vm_heap_stats before;
	struct vm_heap_stats after;

	UNUSED_PARAMETER(receiver);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* No conservative stack or original target edge may hide missing native descriptor ownership. */
	vm_heap_stats(realm->heap, &before);
	vm_heap_collect(realm->heap);
	vm_heap_stats(realm->heap, &after);
	if (after.collections <= before.collections)
		return EFAULT;
	found = vm_heap_find_cell(realm->heap, nested_snapshot_case.target_address);
	if (found != NULL)
		return EFAULT;
	found = vm_heap_find_cell(realm->heap, nested_snapshot_case.payload_address);
	if (found == NULL)
		return EFAULT;
	nested_snapshot_case.getter_collections++;
	*result = vm_value_cell(found);

	/* Succeeded: only the native current-descriptor snapshot retained the fixed data payload across collection. */
	return 0;
}

/* Tests a sole fixed descriptor payload across nested-target revocation, later conversion GC and independent reclamation. */
static int
nested_snapshot_pressure(
	struct page *page)
{
	struct vm_object *target;
	struct vm_object *payload;
	struct vm_object *inner_handler;
	struct vm_object *outer_handler;
	struct vm_object *representation;
	struct vm_cell *roots[8];
	struct vm_cell *found;
	struct vm_descriptor descriptor;
	uintptr_t addresses[7];
	vm_value key;
	vm_value inner;
	vm_value outer;
	unsigned registered;
	unsigned index;
	int present;
	int status;

	/* Temporary setup roots retire before the real operation, leaving only its production ownership. */
	memset(roots, 0, sizeof(roots));
	memset(&nested_snapshot_case, 0, sizeof(nested_snapshot_case));
	registered = 0;
	for (index = 0; index < 8U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;
		registered++;
	}

	/* The fixed target property initially supplies the only graph edge to a fresh ordinary payload. */
	target = vm_object_create(page->heap, NULL);
	if (target == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* A fresh payload has no global, atom, shape transition or native registry owner. */
	roots[0] = &target->cell;
	payload = vm_object_create(page->heap, NULL);
	if (payload == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* Nonconfigurable nonwritable data forces later SameValue comparison against the retained snapshot. */
	roots[1] = &payload->cell;
	key = vm_key_from_ascii(page->heap, "x");
	if (key == VM_VALUE_EMPTY) {
		status = ENOMEM;
		goto cleanup;
	}

	/* Actual ordinary storage, rather than a synthetic native descriptor, owns the original fixed value. */
	status = vm_object_define(page->heap, target, key, vm_value_cell(payload), 0U);
	if (status != 0)
		goto cleanup;
	inner_handler = vm_object_create(page->heap, NULL);
	if (inner_handler == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* Its native IsExtensible trap runs through vm_call and revokes the inner record without capturing the target. */
	roots[2] = &inner_handler->cell;
	status = js_builtin_method(page->realm, inner_handler, "isExtensible", 1, nested_snapshot_extent);
	if (status != 0)
		goto cleanup;
	status = vm_proxy_create(page->realm, vm_value_cell(target), vm_value_cell(inner_handler), &inner);
	if (status != 0)
		goto cleanup;
	roots[3] = vm_value_as_cell(inner);
	representation = vm_object_create(page->heap, NULL);
	if (representation == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* This representation owns no payload until its later value accessor retrieves the live native snapshot. */
	roots[4] = &representation->cell;
	status = js_builtin_accessor(page->realm, representation, "value", nested_snapshot_value, NULL);
	if (status != 0)
		goto cleanup;
	status = js_builtin_value(page->realm, representation, "configurable", VM_VALUE_FALSE, VM_PROPERTY_DEFAULT);
	if (status != 0)
		goto cleanup;
	status = js_builtin_value(page->realm, representation, "writable", VM_VALUE_FALSE, VM_PROPERTY_DEFAULT);
	if (status != 0)
		goto cleanup;
	outer_handler = vm_object_create(page->heap, NULL);
	if (outer_handler == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* The outer trap returns its real representation while the target remains the actual inner Proxy. */
	roots[5] = &outer_handler->cell;
	status = js_builtin_value(page->realm, outer_handler, "resultDescriptor", vm_value_cell(representation), VM_PROPERTY_DEFAULT);
	if (status != 0)
		goto cleanup;
	status = js_builtin_method(page->realm, outer_handler, "getOwnPropertyDescriptor", 2, nested_snapshot_descriptor);
	if (status != 0)
		goto cleanup;
	status = vm_proxy_create(page->realm, inner, vm_value_cell(outer_handler), &outer);
	if (status != 0)
		goto cleanup;
	roots[6] = vm_value_as_cell(outer);

	/* Weak integer observations never keep the old target, payload or either Proxy alive. */
	nested_snapshot_case.target_address = (uintptr_t)target;
	nested_snapshot_case.payload_address = (uintptr_t)payload;
	nested_snapshot_case.inner_address = (uintptr_t)vm_value_as_cell(inner);
	for (index = 0; index < 7U; index++) {
		addresses[index] = (uintptr_t)roots[index];
		roots[index] = NULL;
	}

	/* Actual target GetOwn precedes inner revoke, then descriptor value conversion collects the discarded target. */
	status = vm_get_own_property(page->realm, (struct vm_object *)vm_value_as_cell(outer), key, &descriptor, &present);
	if (status != 0)
		goto cleanup;
	if (!present || nested_snapshot_case.getter_collections != 1U) {
		status = EFAULT;
		goto cleanup;
	}

	/* The resulting descriptor contains exactly the live fixed payload after the original target was reclaimed. */
	roots[7] = vm_value_as_cell(descriptor.value);
	if ((uintptr_t)roots[7] != nested_snapshot_case.payload_address) {
		status = EFAULT;
		goto cleanup;
	}

	/* Output-only ownership now allows the complete original target, handlers, representation and Proxy graph to go. */
	vm_heap_collect(page->heap);
	for (index = 0; index < 7U; index++) {
		if (index == 1U)
			continue;
		found = vm_heap_find_cell(page->heap, addresses[index]);
		if (found != NULL) {
			status = EFAULT;
			goto cleanup;
		}
	}

	/* The payload remains independently rooted after every participant that produced its descriptor has gone. */
	found = vm_heap_find_cell(page->heap, nested_snapshot_case.payload_address);
	if (found == NULL) {
		status = EFAULT;
		goto cleanup;
	}

	/* Dropping the final output root makes the actual fresh payload reclaimable as well. */
	roots[7] = NULL;
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, nested_snapshot_case.payload_address);
	if (found != NULL) {
		status = EFAULT;
		goto cleanup;
	}

	/* Native snapshot ownership survived later collection and left no permanently retained graph. */
	status = 0;

cleanup:
	/* Partial construction and every abrupt native operation retire all setup roots and weak observations. */
	memset(&nested_snapshot_case, 0, sizeof(nested_snapshot_case));
	while (registered != 0U) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve any lifetime, order or reclamation failure after releasing native registrations. */
	if (status != 0)
		return status;

	/* Succeeded: the sole fixed snapshot payload outlived its original target and was finally reclaimed. */
	return 0;
}

/* Records the actual Number wrapper used by flat's source length retrieval without retaining that temporary box. */
static int
flat_source_length(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_object *box;
	int is_object;

	UNUSED_PARAMETER(realm);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The native length getter must receive the actual converted Number object, not the original primitive. */
	is_object = vm_value_is_object(receiver);
	if (!is_object)
		return EFAULT;
	box = (struct vm_object *)vm_value_as_cell(receiver);
	if (box->kind != VM_KIND_NUMBER)
		return EFAULT;
	flat_source_case.box_address = (uintptr_t)box;
	flat_source_case.length_calls++;
	*result = vm_value_int32(1);

	/* Succeeded: only an integer observation survives the length getter's VM-call frame. */
	return 0;
}

/* Collects after array_this has retired its source root and requires the outer flat consumer to own the box. */
static int
flat_source_depth(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_cell *found;
	struct vm_heap_stats before;
	struct vm_heap_stats after;

	UNUSED_PARAMETER(receiver);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* Depth conversion occurs after the earlier length getter and temporary array_this frame have returned. */
	vm_heap_stats(realm->heap, &before);
	vm_heap_collect(realm->heap);
	vm_heap_stats(realm->heap, &after);
	if (after.collections <= before.collections)
		return EFAULT;
	found = vm_heap_find_cell(realm->heap, flat_source_case.box_address);
	if (found == NULL)
		return EFAULT;
	flat_source_case.depth_collections++;
	*result = vm_value_int32(1);

	/* Succeeded: the outer consumer retained its sole boxed source through an actual later collection. */
	return 0;
}

/* Tests actual flat source boxing, later conversion GC, ordinary result creation and independent reclamation. */
static int
flat_source_pressure(
	struct page *page)
{
	struct vm_object *depth;
	struct vm_cell *roots[2];
	struct vm_cell *found;
	struct vm_descriptor prior;
	vm_value key;
	vm_value function;
	vm_value argument;
	vm_value answer;
	uintptr_t depth_address;
	uintptr_t output_address;
	unsigned registered;
	unsigned index;
	int present;
	int deleted;
	int installed;
	int status;
	int restore;

	/* Only construction and the eventual output use embedding roots; the source box is never one of them. */
	memset(roots, 0, sizeof(roots));
	memset(&flat_source_case, 0, sizeof(flat_source_case));
	registered = 0;
	installed = 0;
	for (index = 0; index < 2U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;
		registered++;
	}

	/* The isolated page's Number prototype must have no existing length property for this temporary getter. */
	key = vm_key_from_ascii(page->heap, "length");
	if (key == VM_VALUE_EMPTY) {
		status = ENOMEM;
		goto cleanup;
	}

	/* No preexisting descriptor is replaced or silently lost by this lifetime fixture. */
	status = vm_get_own_property(page->realm, page->realm->intrinsics[VM_INTRINSIC_NUMBER_PROTOTYPE], key, &prior, &present);
	if (status != 0)
		goto cleanup;
	if (present) {
		status = EFAULT;
		goto cleanup;
	}

	/* A real inherited getter observes the fresh box created only inside the production flat consumer. */
	status = js_builtin_accessor(page->realm, page->realm->intrinsics[VM_INTRINSIC_NUMBER_PROTOTYPE], "length", flat_source_length, NULL);
	if (status != 0)
		goto cleanup;
	installed = 1;
	depth = vm_object_create(page->heap, NULL);
	if (depth == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* Actual depth coercion uses the ordinary native valueOf method and cannot access the source through a closure. */
	roots[0] = &depth->cell;
	depth_address = (uintptr_t)depth;
	status = js_builtin_method(page->realm, depth, "valueOf", 0, flat_source_depth);
	if (status != 0)
		goto cleanup;
	key = vm_key_from_ascii(page->heap, "flat");
	if (key == VM_VALUE_EMPTY) {
		status = ENOMEM;
		goto cleanup;
	}

	/* Invoke the actual installed Array.prototype.flat with a primitive Number receiver and the coercible depth. */
	status = vm_get(page->realm, vm_value_cell(page->realm->array_prototype), key, &function);
	if (status != 0)
		goto cleanup;
	argument = vm_value_cell(depth);
	roots[0] = NULL;
	status = vm_call(page->realm, function, vm_value_int32(1), &argument, 1, &answer);
	if (status != 0)
		goto cleanup;
	roots[1] = vm_value_as_cell(answer);
	output_address = (uintptr_t)roots[1];
	if (flat_source_case.length_calls != 1U || flat_source_case.depth_collections != 1U) {
		status = EFAULT;
		goto cleanup;
	}

	/* Output-only ownership permits the earlier source box and depth argument to be actually reclaimed. */
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, flat_source_case.box_address);
	if (found != NULL) {
		status = EFAULT;
		goto cleanup;
	}

	/* The depth object's former call ownership also ends with the real consumer invocation. */
	found = vm_heap_find_cell(page->heap, depth_address);
	if (found != NULL) {
		status = EFAULT;
		goto cleanup;
	}

	/* Dropping the independent result root leaves the ordinary output array reclaimable as well. */
	roots[1] = NULL;
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, output_address);
	if (found != NULL) {
		status = EFAULT;
		goto cleanup;
	}

	/* The actual flat wrapper retained its sole box across later script and released both input and output ownership. */
	status = 0;

cleanup:
	/* Restore the isolated intrinsic before another fixture or page teardown can observe the temporary length getter. */
	if (installed) {
		key = vm_key_from_ascii(page->heap, "length");
		if (key == VM_VALUE_EMPTY) {
			if (status == 0)
				status = ENOMEM;
		} else {
			deleted = 0;
			restore = vm_object_delete(page->heap, page->realm->intrinsics[VM_INTRINSIC_NUMBER_PROTOTYPE], key, &deleted);
			if (restore != 0 && status == 0)
				status = restore;
			if (!deleted && status == 0)
				status = EFAULT;
		}
	}

	/* Weak observations and partial setup roots never outlive the synchronous lifetime case. */
	memset(&flat_source_case, 0, sizeof(flat_source_case));
	while (registered != 0U) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve lifetime, restoration or native-call errors after all cleanup. */
	if (status != 0)
		return status;

	/* Succeeded: the actual temporary source, depth argument and final result were independently reclaimed. */
	return 0;
}

/* Return an object owned only by the actual getter call result, without caching it in the source. */
static int
spread_copy_value(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_object *value;

	UNUSED_PARAMETER(receiver);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* A foreign page-realm native function supplies no edge to the independent managed caller realm. */
	value = vm_object_create(realm->heap, NULL);
	if (value == NULL)
		return ENOMEM;
	value->internal = vm_value_int32(713);
	spread_copy_case.addresses[3] = (uintptr_t)value;
	*result = vm_value_cell(value);

	/* Succeeded: only the pending Get result owns this fresh payload. */
	return 0;
}

/* Collect while the direct target definition owns bare inputs, then preserve errno or refuse the write. */
static int
spread_copy_define(
	struct vm_realm *realm,
	struct vm_object *object,
	vm_value key,
	const struct vm_descriptor *descriptor,
	int *handled,
	int *done)
{
	struct vm_cell *cell;
	struct vm_cell *pressure;
	unsigned index;
	int is_cell;

	UNUSED_PARAMETER(object);
	UNUSED_PARAMETER(key);

	/* Heap-only calls are not the realm-aware CopyDataProperties path under test. */
	*handled = 1;
	*done = 0;
	if (realm == NULL)
		return EINVAL;
	if ((uintptr_t)realm != spread_copy_case.addresses[0])
		return EINVAL;
	if ((descriptor->has & VM_HAS_VALUE) == 0U)
		return EINVAL;
	is_cell = vm_value_is_cell(descriptor->value);
	if (!is_cell)
		return EINVAL;
	cell = vm_value_as_cell(descriptor->value);
	if ((uintptr_t)cell != spread_copy_case.addresses[3])
		return EINVAL;

	/* The caller realm, source, target and fresh getter result must all have production owners now. */
	vm_heap_collect(spread_copy_case.heap);
	for (index = 0; index < 4U; index++) {
		cell = vm_heap_find_cell(spread_copy_case.heap, spread_copy_case.addresses[index]);
		if (cell == NULL)
			return EFAULT;
	}

	/* Count only policies that actually observed every participant during collection. */
	spread_copy_case.calls++;
	if (spread_copy_case.mode == 0U)
		return EACCES;

	/* Refusal leaves pressure for a strictly later collection in the outer TypeError allocation. */
	pressure = vm_heap_alloc(spread_copy_case.heap, &proxy_pressure_type, 8U * 1024U * 1024U);
	if (pressure == NULL)
		return ENOMEM;

	/* Succeeded: the policy refused the property and the caller must report that refusal as an exception. */
	return 0;
}

/* Check both native error paths with no embedding setup roots or conservative C stack retention. */
static int
spread_copy_pressure(
	struct page *page)
{
	struct vm_realm *caller;
	struct vm_object *target;
	struct vm_object *source;
	struct vm_function *getter;
	struct vm_cell *roots[4];
	struct vm_cell *cell;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	vm_value marker;
	vm_value key;
	unsigned index;
	unsigned mode;
	unsigned registered;
	int outcome;
	int status;

	/* The marker distinguishes the original payload from any object reusing its weak address. */
	marker = vm_value_int32(713);

	/* Setup roots are registered empty and cleared before the direct production call. */
	memset(roots, 0, sizeof(roots));
	registered = 0;
	for (index = 0; index < 4U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;

		/* Cleanup owns this registry slot only after acquisition succeeds. */
		registered++;
	}

	/* Separate cases distinguish native errno from the later allocation after a Boolean refusal. */
	for (mode = 0; mode < 2U; mode++) {
		memset(&spread_copy_case, 0, sizeof(spread_copy_case));
		spread_copy_case.heap = page->heap;
		spread_copy_case.mode = mode;
		status = vm_realm_create_managed(page->heap, &caller);
		if (status != 0)
			goto cleanup;
		roots[0] = &caller->cell;
		target = vm_object_create(page->heap, NULL);
		if (target == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* Neither ordinary object points back to the managed caller realm. */
		roots[1] = &target->cell;
		source = vm_object_create(page->heap, NULL);
		if (source == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* Keep the source alive until its accessor and production target policy are installed. */
		roots[2] = &source->cell;

		/* The getter belongs to the page realm, not the managed direct caller being observed. */
		getter = vm_function_create_native(page->realm, "copy-fresh-value", 0, spread_copy_value);
		if (getter == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* Key creation may allocate before source installation takes over getter ownership. */
		roots[3] = &getter->object.cell;
		key = vm_key_from_ascii(page->heap, "x");
		if (key == VM_VALUE_EMPTY) {
			status = ENOMEM;
			goto cleanup;
		}

		/* Installing the accessor transfers getter ownership to the source, never to the weak observer. */
		status = vm_define_accessor(page->realm, vm_value_cell(source), key, vm_value_cell(getter), 0);
		if (status != 0)
			goto cleanup;
		target->native_operations = &spread_copy_operations;
		spread_copy_case.addresses[0] = (uintptr_t)caller;
		spread_copy_case.addresses[1] = (uintptr_t)target;
		spread_copy_case.addresses[2] = (uintptr_t)source;

		/* Only production CopyDataProperties retains the managed caller, source and target after setup. */
		for (index = 0; index < 4U; index++)
			roots[index] = NULL;
		vm_heap_stats(page->heap, &before);
		outcome = vm_copy_data_properties(caller, vm_value_cell(target), vm_value_cell(source));
		vm_heap_stats(page->heap, &after);
		if (spread_copy_case.calls != 1U) {
			status = 1;
			goto cleanup;
		}

		/* The policy must perform an actual collection rather than merely refuse a write. */
		if (after.collections <= before.collections) {
			status = 1;
			goto cleanup;
		}

		/* Distinguish a native errno from the exception created after Boolean refusal. */
		if (mode == 0U) {
			if (outcome != EACCES) {
				status = 1;
				goto cleanup;
			}
		} else {
			if (outcome != VM_THROWN) {
				status = 1;
				goto cleanup;
			}

			/* The outer error allocation must collect strictly after the inner target policy retires. */
			if (after.collections < before.collections + 2U) {
				status = 1;
				goto cleanup;
			}
		}

		/* Even the collection after the inner Define frame retires must preserve the outer saved payload. */
		for (index = 0; index < 4U; index++) {
			cell = vm_heap_find_cell(page->heap, spread_copy_case.addresses[index]);
			if (cell == NULL) {
				status = 1;
				goto cleanup;
			}
		}

		/* A live cell at the weak address must still be the original fresh ordinary payload. */
		cell = vm_heap_find_cell(page->heap, spread_copy_case.addresses[3]);
		if (cell->type != &vm_object_type) {
			status = 1;
			goto cleanup;
		}

		/* An object of the right type must retain the fresh payload sentinel as well. */
		if (((struct vm_object *)cell)->internal != marker) {
			status = 1;
			goto cleanup;
		}

		/* No source shape contains a Symbol; all four weakly observed participants must actually retire. */
		vm_heap_collect(page->heap);
		for (index = 0; index < 4U; index++) {
			cell = vm_heap_find_cell(page->heap, spread_copy_case.addresses[index]);
			if (cell != NULL) {
				status = 1;
				goto cleanup;
			}
		}
	}

	/* Both cases completed real collection and subsequent participant reclamation. */
	status = 0;

cleanup:
	memset(&spread_copy_case, 0, sizeof(spread_copy_case));
	while (registered != 0U) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve abrupt property or root-registration errors after releasing native ownership. */
	if (status != 0)
		return status;

	/* Succeeded: errno and refusal retained all participants only as long as the API needed them. */
	return 0;
}

/* Collect while the private for-in state and managed direct caller are the only production owners. */
static int
for_in_descriptor(
	struct vm_object *object,
	vm_value key,
	struct vm_property *property)
{
	struct vm_cell *cell;
	unsigned index;

	UNUSED_PARAMETER(object);
	UNUSED_PARAMETER(key);
	UNUSED_PARAMETER(property);

	/* No allocation after collection can reuse a dead participant address before these liveness checks. */
	vm_heap_collect(for_in_case.heap);
	for (index = 0; index < 3U; index++) {
		cell = vm_heap_find_cell(for_in_case.heap, for_in_case.addresses[index]);
		if (cell == NULL)
			return -EFAULT;
	}

	/* The callback count records a policy that observed all three actual live participants. */
	for_in_case.calls++;

	/* Deliberately preserve a native descriptor errno after observing successful ownership. */
	return -EACCES;
}

/* Verify allocation-time start ownership and callback-time next ownership with all setup roots cleared. */
static int
for_in_pressure(
	struct page *page)
{
	struct vm_realm *caller;
	struct vm_object *source;
	struct vm_cell *roots[3];
	struct vm_cell *pressure;
	struct vm_cell *cell;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	vm_value iterator;
	vm_value key;
	vm_value value;
	unsigned index;
	unsigned registered;
	int done;
	int outcome;
	int status;

	/* Register empty setup slots before creating an independent managed caller and a source without realm backedges. */
	memset(roots, 0, sizeof(roots));
	memset(&for_in_case, 0, sizeof(for_in_case));
	for_in_case.heap = page->heap;
	registered = 0;
	for (index = 0; index < 3U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;

		/* Cleanup owns only successfully acquired registry slots. */
		registered++;
	}

	/* The independent realm has no source-object or embedding owner after setup finishes. */
	status = vm_realm_create_managed(page->heap, &caller);
	if (status != 0)
		goto cleanup;
	roots[0] = &caller->cell;
	source = vm_object_create(page->heap, NULL);
	if (source == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* A plain stored own key is visible to OwnPropertyKeys without invoking the later descriptor policy. */
	roots[1] = &source->cell;
	key = vm_key_from_ascii(page->heap, "x");
	if (key == VM_VALUE_EMPTY) {
		status = ENOMEM;
		goto cleanup;
	}

	/* The initial ordinary property creates an own-key snapshot before the pure error policy is installed. */
	value = vm_value_int32(1);
	status = vm_object_define(page->heap, source, key, value, VM_PROPERTY_DEFAULT);
	if (status != 0)
		goto cleanup;
	source->native_operations = &for_in_operations;
	for_in_case.addresses[0] = (uintptr_t)caller;
	for_in_case.addresses[1] = (uintptr_t)source;

	/* The next iterator-cell allocation must collect after the direct start operation accepts ownership. */
	vm_heap_collect(page->heap);
	pressure = vm_heap_alloc(page->heap, &proxy_pressure_type, 8U * 1024U * 1024U);
	if (pressure == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* No embedding slot or conservative C stack can retain the bare caller and source now. */
	for (index = 0; index < 3U; index++)
		roots[index] = NULL;
	vm_heap_stats(page->heap, &before);
	status = vm_for_in_start(caller, vm_value_cell(source), &iterator);
	vm_heap_stats(page->heap, &after);
	if (status != 0)
		goto cleanup;
	/* Start must cross the real allocation threshold with its direct inputs retained. */
	if (after.collections <= before.collections) {
		status = 1;
		goto cleanup;
	}

	/* The newly returned private state has no embedding root before next accepts it. */
	for_in_case.addresses[2] = (uintptr_t)vm_value_as_cell(iterator);
	vm_heap_stats(page->heap, &before);
	outcome = vm_for_in_next(caller, iterator, &key, &done);
	vm_heap_stats(page->heap, &after);
	if (outcome != EACCES) {
		status = 1;
		goto cleanup;
	}

	/* The descriptor error must originate from exactly one actual native policy invocation. */
	if (for_in_case.calls != 1U) {
		status = 1;
		goto cleanup;
	}

	/* Next must observe another collection while its private state is its only source owner. */
	if (after.collections <= before.collections) {
		status = 1;
		goto cleanup;
	}

	/* Native descriptor failure must leave the managed caller outside any property-recursion frame. */
	if (caller->property_depth != 0U) {
		status = 1;
		goto cleanup;
	}

	/* The error path leaves all three actual cells live until this post-call collection. */
	for (index = 0; index < 3U; index++) {
		cell = vm_heap_find_cell(page->heap, for_in_case.addresses[index]);
		if (cell == NULL) {
			status = 1;
			goto cleanup;
		}
	}

	/* The iterator finalizer releases both vectors and no root may keep any participant alive afterward. */
	vm_heap_collect(page->heap);
	for (index = 0; index < 3U; index++) {
		cell = vm_heap_find_cell(page->heap, for_in_case.addresses[index]);
		if (cell != NULL) {
			status = 1;
			goto cleanup;
		}
	}

	/* Both ownership intervals and final graph reclamation were actually observed. */
	status = 0;

cleanup:
	memset(&for_in_case, 0, sizeof(for_in_case));
	while (registered != 0U) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve failures after retiring all temporary registry slots. */
	if (status != 0)
		return status;

	/* Succeeded: start and next retained their direct inputs only as long as required. */
	return 0;
}

/* Forward construction to the production VM after validating both constructor operands. */
static int
proxy_construct(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	vm_value target;
	vm_value new_target;
	const vm_value *list;
	unsigned length;
	int constructor;
	int status;

	UNUSED_PARAMETER(receiver);

	/* Validate new.target before a Proxy construct getter can execute. */
	target = js_argument(args, count, 0);
	new_target = target;
	if (count >= 2U)
		new_target = args[1];
	constructor = vm_value_is_constructor(target);
	if (!constructor) {
		status = vm_throw_type_error(realm, "Target is not a constructor");
		return status;
	}

	/* The second constructor operand has its own independent language check. */
	constructor = vm_value_is_constructor(new_target);
	if (!constructor) {
		status = vm_throw_type_error(realm, "new.target is not a constructor");
		return status;
	}

	/* Argument storage remains owned by the normal native call frame. */
	list = NULL;
	length = 0;
	if (count > 2U) {
		list = args + 2;
		length = count - 2U;
	}

	/* Invoke genuine VM construction without a fixture implementation of its algorithms. */
	status = vm_construct(realm, target, list, length, new_target, result);
	if (status != 0)
		return status;

	/* Succeeded: return the actual constructed object. */
	return 0;
}

/* Allocate the receiver using the actual active new.target and intrinsic fallback operation. */
static int
proxy_foreign_construct(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int status;

	UNUSED_PARAMETER(receiver);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* Native construction uses the function realm selected by vm_construct. */
	status = vm_construct_this(realm, realm->new_target, result);
	if (status != 0)
		return status;

	/* Succeeded: return an object allocated by the production constructor operation. */
	return 0;
}

/* Create a real managed realm with its own intrinsic objects and error constructors. */
static int
proxy_foreign_constructor(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_realm *foreign;
	struct vm_function *function;
	struct vm_cell *roots[2];
	vm_value key;
	vm_value error;
	unsigned registered;
	unsigned index;
	int status;

	UNUSED_PARAMETER(receiver);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* Register empty placeholders before any collectible realm or function allocation. */
	memset(roots, 0, sizeof(roots));
	registered = 0;
	for (index = 0; index < 2U; index++) {
		status = vm_heap_add_root(realm->heap, &roots[index]);
		if (status != 0)
			goto cleanup;
		registered++;
	}

	/* Builtins are installed in the independent real realm, including its TypeError constructor. */
	status = vm_realm_create_managed(realm->heap, &foreign);
	if (status != 0)
		goto cleanup;
	roots[0] = &foreign->cell;
	status = js_install_builtins(foreign);
	if (status != 0)
		goto cleanup;
	function = vm_function_create_native(foreign, "Foreign", 0, proxy_foreign_construct);
	if (function == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* The function owns its managed realm after the temporary setup root retires. */
	roots[1] = &function->object.cell;
	function->construct = proxy_foreign_construct;
	key = vm_key_from_ascii(realm->heap, "prototype");
	if (key == VM_VALUE_EMPTY) {
		status = ENOMEM;
		goto cleanup;
	}

	/* A writable prototype allows a genuine Proxy to expose a primitive without violating fixed-property invariants. */
	status = vm_object_define(realm->heap, &function->object, key, vm_value_cell(foreign->object_prototype), VM_PROPERTY_WRITABLE);
	if (status != 0)
		goto cleanup;
	key = vm_key_from_ascii(realm->heap, "TypeError");
	if (key == VM_VALUE_EMPTY) {
		status = ENOMEM;
		goto cleanup;
	}

	/* The actual realm error constructor is exposed solely for cross-realm identity checks. */
	status = vm_get(foreign, vm_value_cell(foreign->global), key, &error);
	if (status != 0)
		goto cleanup;
	status = vm_object_define(realm->heap, &function->object, key, error, VM_PROPERTY_DEFAULT);
	if (status != 0)
		goto cleanup;
	*result = vm_value_cell(function);

cleanup:
	/* Release only successfully acquired temporary roots. */
	while (registered != 0U) {
		registered--;
		vm_heap_remove_root(realm->heap, &roots[registered]);
	}

	/* Preserve an installation or allocation failure. */
	if (status != 0)
		return status;

	/* Succeeded: the constructor retains its real foreign intrinsics. */
	return 0;
}

/* Revoke during GetMethod and return a fresh trap before argument-Array allocation collects. */
static int
invoke_get_method(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_cell *cell;
	struct vm_function *method;
	struct vm_cell *pressure;
	unsigned index;
	int status;

	UNUSED_PARAMETER(receiver);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* Revocation removes the only record edges to the original target and handler. */
	cell = vm_heap_find_cell(invoke_case.heap, invoke_case.addresses[1]);
	if (cell == NULL)
		return EFAULT;
	status = vm_proxy_revoke(realm, vm_value_cell(cell));
	if (status != 0)
		return status;
	vm_heap_collect(invoke_case.heap);

	/* No allocation can reuse a dead participant address before checking the snapshots. */
	for (index = 0; index < 6U; index++) {
		cell = vm_heap_find_cell(invoke_case.heap, invoke_case.addresses[index]);
		if (cell == NULL)
			return EFAULT;
	}

	/* A fresh method has no handler or script reference after this getter frame returns. */
	invoke_case.gets++;
	method = vm_function_create_native(realm, "fresh-invoke-trap", 3, invoke_trap);
	if (method == NULL)
		return ENOMEM;
	*result = vm_value_cell(method);
	invoke_case.addresses[6] = (uintptr_t)method;
	pressure = vm_heap_alloc(realm->heap, &proxy_pressure_type, 8U * 1024U * 1024U);
	if (pressure == NULL)
		return ENOMEM;

	/* Succeeded: the following production argument-Array allocation must retain the returned method. */
	return 0;
}

/* Observe original participants and actual argument Array through a collecting native trap. */
static int
invoke_trap(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_cell *cell;
	struct vm_object *output;
	vm_value list;
	vm_value value;
	unsigned index;
	int status;

	/* Check the specified trap argument positions against weak participant identities. */
	if (count != 3U)
		return EINVAL;
	cell = vm_value_as_cell(receiver);
	if ((uintptr_t)cell != invoke_case.addresses[3])
		return EFAULT;
	cell = vm_value_as_cell(args[0]);
	if ((uintptr_t)cell != invoke_case.addresses[2])
		return EFAULT;
	if (invoke_case.mode == 0U) {
		list = args[2];
		value = args[1];
	} else {
		list = args[1];
		value = args[2];
	}

	/* Call receiver and Construct new.target both use the independent identity function. */
	cell = vm_value_as_cell(value);
	if ((uintptr_t)cell != invoke_case.addresses[5])
		return EFAULT;
	invoke_case.addresses[7] = (uintptr_t)vm_value_as_cell(list);
	vm_heap_collect(invoke_case.heap);
	for (index = 0; index < 8U; index++) {
		cell = vm_heap_find_cell(invoke_case.heap, invoke_case.addresses[index]);
		if (cell == NULL)
			return EFAULT;
	}

	/* Read the production Array element after collection rather than retaining the original payload in the observer. */
	status = vm_get(realm, list, vm_value_int32(0), &value);
	if (status != 0)
		return status;
	cell = vm_value_as_cell(value);
	if ((uintptr_t)cell != invoke_case.addresses[4])
		return EFAULT;
	invoke_case.traps++;
	output = vm_object_create(realm->heap, NULL);
	if (output == NULL)
		return ENOMEM;
	invoke_case.addresses[8] = (uintptr_t)output;
	*result = vm_value_cell(output);

	/* Succeeded: the output has no prototype edge to the otherwise unowned direct caller. */
	return 0;
}

/* Verify direct Call and Construct ownership across revocation, threshold collection and trap collection. */
static int
invoke_pressure(
	struct page *page)
{
	struct vm_realm *caller;
	struct vm_function *target;
	struct vm_function *identity;
	struct vm_function *getter;
	struct vm_object *handler;
	struct vm_object *payload;
	struct vm_cell *roots[8];
	struct vm_cell *cell;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	const char *method_name;
	vm_value proxy;
	vm_value key;
	vm_value input;
	vm_value output;
	unsigned registered;
	unsigned index;
	unsigned mode;
	int status;

	/* Only setup placeholders and the final result slot are registered by the embedding fixture. */
	memset(roots, 0, sizeof(roots));
	registered = 0;
	for (index = 0; index < 8U; index++) {
		status = vm_heap_add_root(page->heap, &roots[index]);
		if (status != 0)
			goto cleanup;
		registered++;
	}

	/* Each invocation gets independent cells so the second mode cannot inherit ownership from the first. */
	for (mode = 0; mode < 2U; mode++) {
		memset(&invoke_case, 0, sizeof(invoke_case));
		invoke_case.heap = page->heap;
		invoke_case.mode = mode;
		status = vm_realm_create_managed(page->heap, &caller);
		if (status != 0)
			goto cleanup;
		roots[0] = &caller->cell;
		target = vm_function_create_native(page->realm, "unreached-target", 0, proxy_collect);
		if (target == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* The target is constructable but belongs to the permanently retained Page realm. */
		roots[1] = &target->object.cell;
		target->construct = proxy_collect;
		handler = vm_object_create(page->heap, NULL);
		if (handler == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* Handler and payload have no caller-realm prototype backedges. */
		roots[2] = &handler->cell;
		payload = vm_object_create(page->heap, NULL);
		if (payload == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* A distinct identity function serves as Call thisArg or Construct new.target. */
		roots[3] = &payload->cell;
		identity = vm_function_create_native(page->realm, "identity", 0, proxy_collect);
		if (identity == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* Neither identity nor getter owns the managed caller. */
		roots[4] = &identity->object.cell;
		identity->construct = proxy_collect;
		getter = vm_function_create_native(page->realm, "invoke-method-getter", 0, invoke_get_method);
		if (getter == NULL) {
			status = ENOMEM;
			goto cleanup;
		}

		/* Install an ordinary accessor so GetMethod exercises normal script-capable VM calls. */
		roots[5] = &getter->object.cell;
		method_name = "apply";
		if (mode != 0U)
			method_name = "construct";
		key = vm_key_from_ascii(page->heap, method_name);
		if (key == VM_VALUE_EMPTY) {
			status = ENOMEM;
			goto cleanup;
		}

		/* The handler owns its getter before any setup root can retire. */
		status = vm_define_accessor(page->realm, vm_value_cell(handler), key, vm_value_cell(getter), 0);
		if (status != 0)
			goto cleanup;
		status = vm_proxy_create(page->realm, vm_value_cell(target), vm_value_cell(handler), &proxy);
		if (status != 0)
			goto cleanup;

		/* The original record itself has no caller-realm prototype backedge. */
		roots[6] = vm_value_as_cell(proxy);
		((struct vm_object *)roots[6])->prototype = NULL;
		invoke_case.addresses[0] = (uintptr_t)caller;
		invoke_case.addresses[1] = (uintptr_t)roots[6];
		invoke_case.addresses[2] = (uintptr_t)target;
		invoke_case.addresses[3] = (uintptr_t)handler;
		invoke_case.addresses[4] = (uintptr_t)payload;
		invoke_case.addresses[5] = (uintptr_t)identity;
		input = vm_value_cell(payload);
		vm_heap_stats(page->heap, &before);
		memset(roots, 0, sizeof(roots));
		if (mode == 0U) {
			status = vm_call(caller, proxy, vm_value_cell(identity), &input, 1, &output);
		} else {
			status = vm_construct(caller, proxy, &input, 1, vm_value_cell(identity), &output);
		}

		/* Abrupt invocation cannot satisfy the lifetime gate. */
		if (status != 0)
			goto cleanup;

		/* The method getter and actual trap must each have collected, with an intervening allocation-threshold collection. */
		roots[7] = vm_value_as_cell(output);
		vm_heap_stats(page->heap, &after);
		if (invoke_case.gets != 1U ||
		    invoke_case.traps != 1U ||
		    after.collections < before.collections + 3U) {
			status = 1;
			goto cleanup;
		}

		/* Keeping only the fresh output must not retain any input or managed caller graph. */
		vm_heap_collect(page->heap);
		for (index = 0; index < 8U; index++) {
			cell = vm_heap_find_cell(page->heap, invoke_case.addresses[index]);
			if (cell != NULL) {
				status = 1;
				goto cleanup;
			}
		}

		/* The sole rooted result must remain alive until its own final embedding edge retires. */
		cell = vm_heap_find_cell(page->heap, invoke_case.addresses[8]);
		if (cell == NULL) {
			status = 1;
			goto cleanup;
		}

		/* Retire the final output owner before checking actual result reclamation. */
		roots[7] = NULL;
		vm_heap_collect(page->heap);
		cell = vm_heap_find_cell(page->heap, invoke_case.addresses[8]);
		if (cell != NULL) {
			status = 1;
			goto cleanup;
		}
	}

	/* Both operation modes satisfied all independent ownership and reclamation gates. */
	status = 0;

cleanup:
	/* The weak observer and successful root registrations retire on every outcome. */
	memset(&invoke_case, 0, sizeof(invoke_case));
	while (registered != 0U) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Preserve protocol errors and failed lifetime observations. */
	if (status != 0)
		return status;

	/* Succeeded: Call and Construct retain their graph only for the duration of the direct operation. */
	return 0;
}
