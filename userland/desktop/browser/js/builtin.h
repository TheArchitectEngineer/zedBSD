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

#ifndef KEILAND_BROWSER_JS_BUILTIN_H
#define KEILAND_BROWSER_JS_BUILTIN_H

#include "js/js.h"
#include "vm/bytecode.h"

/* The groups (builtin_*.c). */
int js_builtin_install_error(struct vm_realm *realm);
int js_builtin_install_object(struct vm_realm *realm);
int js_builtin_install_function(struct vm_realm *realm);
int js_builtin_install_array(struct vm_realm *realm);
int js_builtin_install_string(struct vm_realm *realm);
int js_builtin_install_json(struct vm_realm *realm);
int js_builtin_install_boolean(struct vm_realm *realm);
int js_builtin_install_number(struct vm_realm *realm);
int js_builtin_install_math(struct vm_realm *realm);
int js_builtin_install_global(struct vm_realm *realm);
int js_builtin_install_regexp(struct vm_realm *realm);
int js_builtin_install_date(struct vm_realm *realm);

/* RegExp's algorithms that String.prototype's methods use (builtin_regexp.c). */
int js_regexp_is(vm_value value);
int js_regexp_create(struct vm_realm *realm, vm_value pattern, vm_value flags, vm_value *result);
int js_regexp_symbol_match(struct vm_realm *realm, vm_value regexp, struct vm_string *string, vm_value *result);
int js_regexp_symbol_replace(struct vm_realm *realm, vm_value regexp, struct vm_string *string, vm_value replace_value, vm_value *result);
int js_regexp_symbol_search(struct vm_realm *realm, vm_value regexp, struct vm_string *string, vm_value *result);
int js_regexp_symbol_split(struct vm_realm *realm, vm_value regexp, struct vm_string *string, vm_value limit, vm_value *result);
int js_regexp_substitution(struct vm_realm *realm, struct vm_string *matched, struct vm_string *string, size_t position, const vm_value *captures, uint32_t capture_count, vm_value named, struct vm_string *replacement, vm_value *result);

/* Evaluating source text for eval and the Function constructor (builtin_global.c). */
int js_builtin_evaluate(struct vm_realm *realm, const struct vm_string *source, int strict, vm_value *result);

#endif
