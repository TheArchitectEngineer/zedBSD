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

/* Evaluating source text for eval and the Function constructor (builtin_global.c). */
int js_builtin_evaluate(struct vm_realm *realm, const struct vm_string *source, int strict, vm_value *result);

#endif
