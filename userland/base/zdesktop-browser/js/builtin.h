/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The inside of the built-in objects (plan/ws074/design.md §12.3), shared
 * by js/builtin*.c: the helpers that make functions, constructors and
 * properties of a realm, and the installers of each group.
 *
 * A native function reads its arguments with js_argument (a missing one
 * is undefined) and finds which function it is with js_builtin_callee
 * (the realm's callee, set by the caller just before it runs, and read
 * before anything that could call another function).
 */

#ifndef ZDESKTOP_BROWSER_JS_BUILTIN_H
#define ZDESKTOP_BROWSER_JS_BUILTIN_H

#include "js/js.h"
#include "vm/bytecode.h"

/* The attributes of a built-in method: writable and configurable, not enumerable. */
#define JS_BUILTIN_METHOD	(VM_PROPERTY_WRITABLE | VM_PROPERTY_CONFIGURABLE)

/* Reports an argument of a native call, undefined when the call has fewer. */
static __inline vm_value
js_argument(
	const vm_value *args,
	unsigned count,
	unsigned index)
{
	/* A missing argument. */
	if (index >= count)
		return VM_VALUE_UNDEFINED;

	/* The argument. */
	return args[index];
}

/* Making built-ins (builtin.c). */
int js_builtin_function(struct vm_realm *realm, const char *name, unsigned length, vm_native native, vm_native construct, struct vm_function **function);
int js_builtin_method(struct vm_realm *realm, struct vm_object *object, const char *name, unsigned length, vm_native native);
int js_builtin_value(struct vm_realm *realm, struct vm_object *object, const char *name, vm_value value, uint32_t attributes);
int js_builtin_accessor(struct vm_realm *realm, struct vm_object *object, const char *name, vm_native getter, vm_native setter);
int js_builtin_constructor(struct vm_realm *realm, const char *name, unsigned length, vm_native native, vm_native construct, struct vm_object *prototype, struct vm_function **function);
struct vm_function *js_builtin_callee(const struct vm_realm *realm);
int js_builtin_string(struct vm_realm *realm, const char *text, vm_value *value);
int js_builtin_array(struct vm_realm *realm, const vm_value *values, uint32_t count, vm_value *array);
int js_builtin_integer(struct vm_realm *realm, vm_value value, double *integer);
int js_builtin_length(struct vm_realm *realm, vm_value object, uint32_t *length);

/* The groups (builtin_*.c). */
int js_builtin_install_error(struct vm_realm *realm);
int js_builtin_install_object(struct vm_realm *realm);
int js_builtin_install_function(struct vm_realm *realm);
int js_builtin_install_boolean(struct vm_realm *realm);
int js_builtin_install_number(struct vm_realm *realm);
int js_builtin_install_math(struct vm_realm *realm);
int js_builtin_install_global(struct vm_realm *realm);

/* Evaluating source text for eval and the Function constructor (builtin_global.c). */
int js_builtin_evaluate(struct vm_realm *realm, const struct vm_string *source, int strict, vm_value *result);

#endif
