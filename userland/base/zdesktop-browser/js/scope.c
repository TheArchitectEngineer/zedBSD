/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The compiler's scope pass: for the program and each function, the
 * bindings its scopes declare and which of them a nested function refers
 * to (captured).
 *
 * A function's declarations are found first (vars and function
 * declarations anywhere in its body, hoisted, but not inside nested
 * functions), then every name its code uses is resolved outwards through
 * the scopes: the first scope that declares it wins; a name that reaches a
 * function's own scope as "arguments" without a declaration gets that
 * function's arguments object; a name nothing declares is a global.  The
 * program's vars and function declarations are globals too (properties of
 * the global object), listed for the code pass to declare.
 *
 * Block-level function declarations are hoisted to their function like
 * vars (the ES5 practice); let, const and class, whose block scopes arrive
 * in ws074-p028, are left for the code pass to refuse.
 */

#include "js/compile.h"

#include <string.h>

static struct js_function_info *scope_function(struct js_compiler *compiler, struct js_scope *parent, struct js_node *node, int program);
static struct js_scope *scope_new(struct js_compiler *compiler, struct js_scope *parent, struct js_function_info *function, int kind);
static struct js_binding *scope_find(const struct js_scope *scope, const uint16_t *name, size_t length);
static struct js_binding *scope_declare(struct js_compiler *compiler, struct js_scope *scope, const uint16_t *name, size_t length, int kind);
static void scope_declare_var(struct js_compiler *compiler, struct js_function_info *info, const uint16_t *name, size_t length);
static void scope_declarations(struct js_compiler *compiler, struct js_function_info *info, struct js_node *list);
static void scope_declare_function(struct js_compiler *compiler, struct js_function_info *info, struct js_node *node);
static void scope_declare_target(struct js_compiler *compiler, struct js_function_info *info, struct js_node *target);
static void scope_visit_list(struct js_compiler *compiler, struct js_scope *scope, struct js_node *list);
static void scope_visit(struct js_compiler *compiler, struct js_scope *scope, struct js_node *node);
static void scope_visit_try(struct js_compiler *compiler, struct js_scope *scope, struct js_node *node);
static void scope_reference(struct js_compiler *compiler, struct js_scope *scope, const uint16_t *name, size_t length);
static int scope_has_own_arguments(const struct js_function_info *info);

/*
 * Runs the scope pass over a program: every function node, the program
 * and every catch clause gets its scope in node->scope; reports the
 * program's information.
 */
struct js_function_info *
js_scope_analyze(
	struct js_compiler *compiler,
	struct js_node *program)
{
	struct js_function_info *info;

	/* The program is the outermost function. */
	info = scope_function(compiler, NULL, program, 1);

	/* Succeeded: the program's information. */
	return info;
}

/*
 * Resolves a name from the scope the code pass is in: the binding that
 * declares it (NULL for a global) and, for a captured one, how many
 * environments outwards from the running function's its slot is.
 */
struct js_binding *
js_scope_resolve(
	const struct js_function_compiler *fc,
	const uint16_t *name,
	size_t length,
	uint32_t *hops)
{
	const struct js_scope *scope;
	const struct js_function_info *function;
	struct js_binding *binding;

	/* Outwards through the scopes, counting the environments of the functions passed. */
	*hops = 0;
	function = fc->info;
	for (scope = fc->scope; scope != NULL; scope = scope->parent) {
		/* Leaving a function for the one around it passes its environment, when it has one. */
		if (scope->function != function) {
			if (function->has_env)
				(*hops)++;
			function = scope->function;
		}

		/* The first scope that declares the name. */
		binding = scope_find(scope, name, length);
		if (binding != NULL)
			return binding;
	}

	/* No scope declares it: a global. */
	return NULL;
}

/* Makes the information of a function (or the program): its scopes, its declarations, and the resolution of its names. */
static struct js_function_info *
scope_function(
	struct js_compiler *compiler,
	struct js_scope *parent,
	struct js_node *node,
	int program)
{
	struct js_function_info *info;
	struct js_scope *outer;
	struct js_scope *callee;
	struct js_node *parameter;
	struct js_binding *binding;
	uint32_t index;
	int expression;

	/* The information. */
	info = wb_arena_zalloc(compiler->arena, sizeof(*info));
	if (info == NULL)
		js_compile_out_of_memory(compiler);
	info->node = node;
	info->program = program;
	if (parent != NULL)
		info->parent = parent->function;
	if ((node->flags & JS_FLAG_STRICT) != 0U)
		info->strict = 1;

	/* A named function expression sees its own name in a scope between it and the code around it. */
	outer = parent;
	expression = 0;
	if (node->kind == JS_NODE_FUNCTION && (node->flags & JS_FLAG_METHOD) == 0U && node->text != NULL)
		expression = 1;
	if (expression) {
		callee = scope_new(compiler, parent, info, JS_SCOPE_CALLEE);
		scope_declare(compiler, callee, node->text, node->text_length, JS_BINDING_CALLEE);
		outer = callee;
	}

	/* The function's own scope, noted on its node. */
	info->scope = scope_new(compiler, outer, info, JS_SCOPE_FUNCTION);
	node->scope = info->scope;

	/* The parameters, in order (a repeated name is the last one's); other forms are left for the code pass to refuse. */
	index = 0;
	parameter = NULL;
	if (!program)
		parameter = node->first;
	for (;
	     parameter != NULL;
	     parameter = parameter->next) {
		if (parameter->kind != JS_NODE_IDENTIFIER) {
			if (info->unsupported == NULL)
				info->unsupported = parameter;
			index++;
			continue;
		}

		/* The name, a binding of the function's scope. */
		binding = scope_declare(compiler, info->scope, parameter->text, parameter->text_length, JS_BINDING_PARAMETER);
		binding->parameter = index;
		index++;
	}

	/* The declarations of the body, then the names it uses. */
	if (program) {
		scope_declarations(compiler, info, node->first);
		scope_visit_list(compiler, info->scope, node->first);
	} else {
		scope_declarations(compiler, info, node->second);
		scope_visit_list(compiler, info->scope, node->second);
	}

	/* Succeeded: the function's information. */
	return info;
}

/* Makes a scope of a function inside a parent scope. */
static struct js_scope *
scope_new(
	struct js_compiler *compiler,
	struct js_scope *parent,
	struct js_function_info *function,
	int kind)
{
	struct js_scope *scope;

	/* The scope, in the arena. */
	scope = wb_arena_zalloc(compiler->arena, sizeof(*scope));
	if (scope == NULL)
		js_compile_out_of_memory(compiler);
	scope->parent = parent;
	scope->function = function;
	scope->kind = kind;

	/* The function's list of its scopes, for the code pass to give each binding its place. */
	scope->next_in_function = function->scopes;
	function->scopes = scope;

	/* Succeeded: the scope. */
	return scope;
}

/* Finds the binding a scope declares for a name. */
static struct js_binding *
scope_find(
	const struct js_scope *scope,
	const uint16_t *name,
	size_t length)
{
	struct js_binding *binding;
	int same;

	/* Each binding of the scope. */
	for (binding = scope->bindings; binding != NULL; binding = binding->next) {
		same = js_text_equal(binding->name, binding->length, name, length);
		if (same)
			return binding;
	}

	/* The scope does not declare it. */
	return NULL;
}

/* Declares a name in a scope (a name declared already keeps its binding). */
static struct js_binding *
scope_declare(
	struct js_compiler *compiler,
	struct js_scope *scope,
	const uint16_t *name,
	size_t length,
	int kind)
{
	struct js_binding *binding;

	/* A second declaration of the name is the same binding. */
	binding = scope_find(scope, name, length);
	if (binding != NULL)
		return binding;

	/* A new binding at the front of the scope's list. */
	binding = wb_arena_zalloc(compiler->arena, sizeof(*binding));
	if (binding == NULL)
		js_compile_out_of_memory(compiler);
	binding->name = name;
	binding->length = length;
	binding->kind = kind;
	binding->next = scope->bindings;
	scope->bindings = binding;

	/* Succeeded: the binding. */
	return binding;
}

/* Declares a var: a binding of a function's scope, or a global of the program. */
static void
scope_declare_var(
	struct js_compiler *compiler,
	struct js_function_info *info,
	const uint16_t *name,
	size_t length)
{
	struct js_global_name *global;
	struct js_global_name *listed;
	int same;

	/* A function's var is a binding. */
	if (!info->program) {
		scope_declare(compiler, info->scope, name, length, JS_BINDING_VAR);
		return;
	}

	/* The program lists each name once. */
	for (listed = info->global_vars; listed != NULL; listed = listed->next) {
		same = js_text_equal(listed->name, listed->length, name, length);
		if (same)
			return;
	}

	/* A new global name. */
	global = wb_arena_zalloc(compiler->arena, sizeof(*global));
	if (global == NULL)
		js_compile_out_of_memory(compiler);
	global->name = name;
	global->length = length;

	/* It joins the end of the list, so the globals are made in source order. */
	if (info->global_vars_last == NULL) {
		info->global_vars = global;
	} else {
		info->global_vars_last->next = global;
	}

	/* The name is the last now. */
	info->global_vars_last = global;
}

/* Finds the declarations of a list of statements, down through blocks but not into functions. */
static void
scope_declarations(
	struct js_compiler *compiler,
	struct js_function_info *info,
	struct js_node *list)
{
	struct js_node *node;
	struct js_node *declarator;

	/* Each statement by its kind. */
	for (node = list; node != NULL; node = node->next) {
		switch (node->kind) {
		case JS_NODE_VARIABLES:
			/* Only var hoists (let and const are refused by the code pass). */
			if (node->op != JS_P_VAR)
				break;
			for (declarator = node->first; declarator != NULL; declarator = declarator->next)
				scope_declare_target(compiler, info, declarator->first);
			break;
		case JS_NODE_FUNCTION_DECLARATION:
			scope_declare_function(compiler, info, node);
			break;
		case JS_NODE_BLOCK:
			scope_declarations(compiler, info, node->first);
			break;
		case JS_NODE_IF:
			scope_declarations(compiler, info, node->second);
			scope_declarations(compiler, info, node->third);
			break;
		case JS_NODE_FOR:
		case JS_NODE_FOR_IN:
		case JS_NODE_FOR_OF:
			/* The head's declaration, then the body. */
			scope_declarations(compiler, info, node->first);
			scope_declarations(compiler, info, node->fourth);
			break;
		case JS_NODE_WHILE:
		case JS_NODE_DO_WHILE:
		case JS_NODE_WITH:
		case JS_NODE_LABELED:
			scope_declarations(compiler, info, node->fourth);
			break;
		case JS_NODE_TRY:
			scope_declarations(compiler, info, node->first);
			scope_declarations(compiler, info, node->third);
			scope_declarations(compiler, info, node->fourth);
			break;
		case JS_NODE_SWITCH:
			/* Each case's statements. */
			for (declarator = node->second; declarator != NULL; declarator = declarator->next)
				scope_declarations(compiler, info, declarator->second);
			break;
		default:
			break;
		}
	}
}

/* Declares a function declaration: its name as a binding (or global) and its node to hoist. */
static void
scope_declare_function(
	struct js_compiler *compiler,
	struct js_function_info *info,
	struct js_node *node)
{
	struct js_hoisted *hoisted;

	/* The name, like a var (a function's binding records that it is a function). */
	if (info->program) {
		scope_declare_var(compiler, info, node->text, node->text_length);
	} else {
		scope_declare(compiler, info->scope, node->text, node->text_length, JS_BINDING_FUNCTION);
	}

	/* The node joins the list of declarations to hoist, in source order. */
	hoisted = wb_arena_zalloc(compiler->arena, sizeof(*hoisted));
	if (hoisted == NULL)
		js_compile_out_of_memory(compiler);
	hoisted->node = node;
	if (info->hoisted_last == NULL) {
		info->hoisted = hoisted;
	} else {
		info->hoisted_last->next = hoisted;
	}

	/* The node is the last now. */
	info->hoisted_last = hoisted;
}

/* Declares the names of a var's target: an identifier, or the identifiers inside a pattern. */
static void
scope_declare_target(
	struct js_compiler *compiler,
	struct js_function_info *info,
	struct js_node *target)
{
	struct js_node *element;

	/* Nothing to declare. */
	if (target == NULL)
		return;

	/* A name. */
	if (target->kind == JS_NODE_IDENTIFIER) {
		scope_declare_var(compiler, info, target->text, target->text_length);
		return;
	}

	/* A pattern's elements (the code pass refuses patterns for now, but their names are still declared). */
	for (element = target->first; element != NULL; element = element->next) {
		if (element->kind == JS_NODE_PROPERTY) {
			scope_declare_target(compiler, info, element->second);
		} else {
			scope_declare_target(compiler, info, element);
		}
	}

	/* A default's target, a rest's target. */
	if (target->kind == JS_NODE_ASSIGNMENT_PATTERN)
		scope_declare_target(compiler, info, target->first);
}

/* Resolves the names of each node of a list. */
static void
scope_visit_list(
	struct js_compiler *compiler,
	struct js_scope *scope,
	struct js_node *list)
{
	struct js_node *node;

	/* Each member in order. */
	for (node = list; node != NULL; node = node->next)
		scope_visit(compiler, scope, node);
}

/* Resolves the names a node uses, and makes the scopes of the functions and catch clauses under it. */
static void
scope_visit(
	struct js_compiler *compiler,
	struct js_scope *scope,
	struct js_node *node)
{
	/* The kinds whose children are not all names in use. */
	switch (node->kind) {
	case JS_NODE_IDENTIFIER:
		scope_reference(compiler, scope, node->text, node->text_length);
		return;
	case JS_NODE_FUNCTION:
	case JS_NODE_FUNCTION_DECLARATION:
		scope_function(compiler, scope, node, 0);
		return;
	case JS_NODE_TRY:
		scope_visit_try(compiler, scope, node);
		return;
	case JS_NODE_MEMBER:
		/* The object; the property only when computed. */
		scope_visit(compiler, scope, node->first);
		if ((node->flags & JS_FLAG_COMPUTED) != 0U)
			scope_visit(compiler, scope, node->second);
		return;
	case JS_NODE_PROPERTY:
	case JS_NODE_METHOD:
	case JS_NODE_FIELD:
		/* The key only when computed; the value. */
		if (node->first != NULL && (node->flags & JS_FLAG_COMPUTED) != 0U)
			scope_visit(compiler, scope, node->first);
		if (node->second != NULL)
			scope_visit(compiler, scope, node->second);
		return;
	case JS_NODE_PRIVATE_NAME:
	case JS_NODE_META_PROPERTY:
	case JS_NODE_IMPORT_SPECIFIER:
	case JS_NODE_EXPORT_SPECIFIER:
		return;
	default:
		break;
	}

	/* Every other node: its children in order. */
	scope_visit_list(compiler, scope, node->first);
	scope_visit_list(compiler, scope, node->second);
	scope_visit_list(compiler, scope, node->third);
	scope_visit_list(compiler, scope, node->fourth);
}

/* Resolves the names of a try statement, whose catch clause opens a scope with its parameter. */
static void
scope_visit_try(
	struct js_compiler *compiler,
	struct js_scope *scope,
	struct js_node *node)
{
	struct js_scope *clause;

	/* The protected block. */
	scope_visit_list(compiler, scope, node->first);

	/* The catch clause's scope and its parameter (a pattern is left for the code pass to refuse). */
	if (node->third != NULL) {
		clause = scope_new(compiler, scope, scope->function, JS_SCOPE_CATCH);
		node->scope = clause;
		if (node->second != NULL && node->second->kind == JS_NODE_IDENTIFIER)
			scope_declare(compiler, clause, node->second->text, node->second->text_length, JS_BINDING_CATCH);
		if (node->second != NULL && node->second->kind != JS_NODE_IDENTIFIER)
			scope_visit(compiler, clause, node->second);
		scope_visit_list(compiler, clause, node->third);
	}

	/* The finally block. */
	scope_visit_list(compiler, scope, node->fourth);
}

/* Resolves a name used in a scope, marking a binding of an outer function captured. */
static void
scope_reference(
	struct js_compiler *compiler,
	struct js_scope *scope,
	const uint16_t *name,
	size_t length)
{
	struct js_scope *search;
	struct js_binding *binding;
	int is_arguments;
	int own_arguments;

	/* Outwards through the scopes. */
	is_arguments = js_text_is(name, length, "arguments");
	for (search = scope; search != NULL; search = search->parent) {
		binding = scope_find(search, name, length);

		/* A function's own scope gives arguments to a function that has an arguments object. */
		if (binding == NULL && is_arguments && search->kind == JS_SCOPE_FUNCTION) {
			own_arguments = scope_has_own_arguments(search->function);
			if (own_arguments) {
				binding = scope_declare(compiler, search, name, length, JS_BINDING_ARGUMENTS);
				search->function->arguments = binding;
			}
		}

		/* The scope that declares it; a function's binding used by another is captured. */
		if (binding != NULL) {
			if (search->function != scope->function) {
				binding->captured = 1;
				search->function->has_env = 1;
			}

			/* Resolved. */
			return;
		}
	}
}

/* Tells whether a function has an arguments object of its own (the program and arrow functions do not). */
static int
scope_has_own_arguments(
	const struct js_function_info *info)
{
	/* The program. */
	if (info->program)
		return 0;

	/* An arrow function sees the one around it. */
	if ((info->node->flags & JS_FLAG_ARROW) != 0U)
		return 0;

	/* A function of its own. */
	return 1;
}
