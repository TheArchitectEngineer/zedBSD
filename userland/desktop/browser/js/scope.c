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
 * let and const (ws074-p078) are bindings of the scope of the block, the
 * for statement's head or the switch's cases they are written in, or of
 * the function's scope at the top level of its body; at the top level of
 * the program they are listed for the realm's shared record of them.  A
 * function declaration is hoisted to its function like a var (the ES5
 * practice); inside a block that has a scope it is made when the block is
 * entered, so that it sees the block's bindings, and its var then takes
 * it (Annex B).  class is left for the code pass to refuse.
 */

#include "js/compile.h"

#include <stdio.h>
#include <string.h>

/* The length of the name arguments. */
#define SCOPE_ARGUMENTS_LENGTH	9U

/* The name arguments, as the tree's texts are written (a constant for the life of the program). */
static const uint16_t scope_arguments_name[SCOPE_ARGUMENTS_LENGTH] = {
	'a', 'r', 'g', 'u', 'm', 'e', 'n', 't', 's'
};

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
static void scope_arrow_this(struct js_compiler *compiler, struct js_scope *scope);
static struct js_scope *scope_lexical_block(struct js_compiler *compiler, struct js_scope *parent, struct js_node *list, int switch_cases);
static int scope_declare_lexicals(struct js_compiler *compiler, struct js_scope *scope, struct js_node *list);
static void scope_declare_lexical(struct js_compiler *compiler, struct js_scope *scope, struct js_node *target, int kind);
static void scope_global_lexical(struct js_compiler *compiler, struct js_function_info *info, struct js_node *target, int is_const);
static void scope_visit_block(struct js_compiler *compiler, struct js_scope *scope, struct js_node *node);
static void scope_visit_for(struct js_compiler *compiler, struct js_scope *scope, struct js_node *node);
static void scope_visit_switch(struct js_compiler *compiler, struct js_scope *scope, struct js_node *node);
static int scope_is_lexical(const struct js_node *node);
static void scope_redeclared(struct js_compiler *compiler, const struct js_node *target);
static void scope_check_vars(struct js_compiler *compiler, const struct js_scope *block, struct js_node *list, int direct);
static void scope_check_var_target(struct js_compiler *compiler, const struct js_scope *block, struct js_node *target);
static void scope_block_function(struct js_compiler *compiler, struct js_scope *block, struct js_node *node);

/*
 * The hidden binding of a function's this that its arrow functions read:
 * "#this", which no identifier can be.  A constant for the life of the
 * program.
 */
const uint16_t js_this_name[JS_THIS_NAME_LENGTH] = {
	'#', 't', 'h', 'i', 's'
};
static void scope_arguments_var(struct js_function_info *info);

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

	/* Outwards through the scopes, counting the environments passed. */
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

		/* Leaving a block with an environment of its own passes that environment. */
		if (scope->kind == JS_SCOPE_BLOCK && scope->has_env)
			(*hops)++;
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

	/* A declaration in a block that has a scope is made when the block is entered. */
	if (node->kind == JS_NODE_FUNCTION_DECLARATION && parent != NULL && parent->kind == JS_SCOPE_BLOCK) {
		node->flags |= JS_FLAG_BLOCK_FUNCTION;
		scope_block_function(compiler, parent, node);
	}

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

	/* The declarations of the body (the program's let and const are the realm's), then the names it uses. */
	if (program) {
		scope_declarations(compiler, info, node->first);
		scope_declare_lexicals(compiler, info->scope, node->first);
		scope_visit_list(compiler, info->scope, node->first);
	} else if ((node->flags & JS_FLAG_EXPRESSION_BODY) != 0U) {
		scope_arguments_var(info);
		scope_visit(compiler, info->scope, node->second);
	} else {
		scope_declarations(compiler, info, node->second);
		scope_declare_lexicals(compiler, info->scope, node->second);
		scope_arguments_var(info);
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
	case JS_NODE_THIS:
		scope_arrow_this(compiler, scope);
		return;
	case JS_NODE_BLOCK:
		scope_visit_block(compiler, scope, node);
		return;
	case JS_NODE_FOR:
	case JS_NODE_FOR_IN:
	case JS_NODE_FOR_OF:
		scope_visit_for(compiler, scope, node);
		return;
	case JS_NODE_SWITCH:
		scope_visit_switch(compiler, scope, node);
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

		/* The scope that declares it; a function's binding used by another is captured (in the block's environment for a block's). */
		if (binding != NULL) {
			if (search->function != scope->function) {
				binding->captured = 1;
				if (search->kind == JS_SCOPE_BLOCK) {
					search->has_env = 1;
				} else {
					search->function->has_env = 1;
				}
			}

			/* Resolved. */
			return;
		}
	}
}

/*
 * Declares the let and const written directly in a list of statements in
 * a scope: the program's go to the realm's record instead.  Reports
 * whether there was any.
 */
static int
scope_declare_lexicals(
	struct js_compiler *compiler,
	struct js_scope *scope,
	struct js_node *list)
{
	struct js_node *node;
	struct js_node *declarator;
	int is_const;
	int kind;
	int found;
	int lexical;

	/* Each statement that is a let or const declaration. */
	found = 0;
	for (node = list; node != NULL; node = node->next) {
		lexical = scope_is_lexical(node);
		if (!lexical)
			continue;
		found = 1;

		/* The kind of its names. */
		is_const = 0;
		kind = JS_BINDING_LET;
		if (node->op == JS_P_CONST) {
			is_const = 1;
			kind = JS_BINDING_CONST;
		}

		/* Each declarator's names: the realm's for the program's top level, the scope's otherwise. */
		for (declarator = node->first; declarator != NULL; declarator = declarator->next) {
			if (scope->kind == JS_SCOPE_FUNCTION && scope->function->program) {
				scope_global_lexical(compiler, scope->function, declarator->first, is_const);
			} else {
				scope_declare_lexical(compiler, scope, declarator->first, kind);
			}
		}
	}

	/* Reports whether the list declared any. */
	return found;
}

/* Declares the names of a let or const target (a name, or the names inside a pattern) in a scope. */
static void
scope_declare_lexical(
	struct js_compiler *compiler,
	struct js_scope *scope,
	struct js_node *target,
	int kind)
{
	struct js_binding *declared;
	struct js_node *element;

	/* Nothing to declare. */
	if (target == NULL)
		return;

	/* A name, which the scope may not declare already (a parameter, a var, a function or another let or const). */
	if (target->kind == JS_NODE_IDENTIFIER) {
		declared = scope_find(scope, target->text, target->text_length);
		if (declared != NULL)
			scope_redeclared(compiler, target);
		scope_declare(compiler, scope, target->text, target->text_length, kind);
		return;
	}

	/* A pattern's elements (the code pass refuses patterns for now, but their names are still declared). */
	for (element = target->first; element != NULL; element = element->next) {
		if (element->kind == JS_NODE_PROPERTY) {
			scope_declare_lexical(compiler, scope, element->second, kind);
		} else {
			scope_declare_lexical(compiler, scope, element, kind);
		}
	}
}

/* Lists a let or const name of the program's top level for the realm's record (a pattern's names are refused by the code pass). */
static void
scope_global_lexical(
	struct js_compiler *compiler,
	struct js_function_info *info,
	struct js_node *target,
	int is_const)
{
	struct js_global_lexical *lexical;
	struct js_global_lexical *listed;
	struct js_global_name *global;
	int same;

	/* Only a name is listed. */
	if (target == NULL || target->kind != JS_NODE_IDENTIFIER)
		return;

	/* The script may not declare the name twice, nor as a var or a function too. */
	for (listed = info->global_lexicals; listed != NULL; listed = listed->next) {
		same = js_text_equal(listed->name, listed->length, target->text, target->text_length);
		if (same)
			scope_redeclared(compiler, target);
	}
	for (global = info->global_vars; global != NULL; global = global->next) {
		same = js_text_equal(global->name, global->length, target->text, target->text_length);
		if (same)
			scope_redeclared(compiler, target);
	}

	/* The entry, at the front of the list (the order does not matter). */
	lexical = wb_arena_zalloc(compiler->arena, sizeof(*lexical));
	if (lexical == NULL)
		js_compile_out_of_memory(compiler);
	lexical->name = target->text;
	lexical->length = target->text_length;
	lexical->is_const = is_const;
	lexical->next = info->global_lexicals;
	info->global_lexicals = lexical;
}

/*
 * Makes the scope of a block, a for statement's head or a switch's cases
 * when it declares a let or const (the parent scope otherwise); for a
 * switch the list is the cases, whose statements are searched.
 */
static struct js_scope *
scope_lexical_block(
	struct js_compiler *compiler,
	struct js_scope *parent,
	struct js_node *list,
	int switch_cases)
{
	struct js_scope *block;
	struct js_node *clause;
	int found;
	int any;

	/* A block scope, kept only when something is declared in it. */
	block = scope_new(compiler, parent, parent->function, JS_SCOPE_BLOCK);
	any = 0;
	if (switch_cases) {
		for (clause = list; clause != NULL; clause = clause->next) {
			found = scope_declare_lexicals(compiler, block, clause->second);
			if (found)
				any = 1;
		}
	} else {
		any = scope_declare_lexicals(compiler, block, list);
	}

	/* A scope that declares nothing is not needed (it stays on the function's list, empty). */
	if (!any)
		return parent;

	/* A var inside the block, or a function declared in it, may not have the name of one of its let or const. */
	if (switch_cases) {
		for (clause = list; clause != NULL; clause = clause->next)
			scope_check_vars(compiler, block, clause->second, 1);
	} else {
		scope_check_vars(compiler, block, list, 1);
	}

	/* Succeeded: the block's scope. */
	return block;
}

/* Resolves the names of a block, in a scope of its own when it declares a let or const. */
static void
scope_visit_block(
	struct js_compiler *compiler,
	struct js_scope *scope,
	struct js_node *node)
{
	struct js_scope *block;

	/* The block's scope, noted on the node when it has one. */
	block = scope_lexical_block(compiler, scope, node->first, 0);
	node->scope = NULL;
	if (block != scope)
		node->scope = block;

	/* The statements in it. */
	scope_visit_list(compiler, block, node->first);
}

/*
 * Resolves the names of a for, for-in or for-of statement: a let or const
 * in its head has a scope of its own around the head and the body (the
 * object of for-in and for-of is evaluated outside it).
 */
static void
scope_visit_for(
	struct js_compiler *compiler,
	struct js_scope *scope,
	struct js_node *node)
{
	struct js_scope *head;
	int lexical;

	/* The head's scope, noted on the node when it has one. */
	node->scope = NULL;
	head = scope;
	lexical = 0;
	if (node->first != NULL)
		lexical = scope_is_lexical(node->first);
	if (lexical) {
		head = scope_lexical_block(compiler, scope, node->first, 0);
		node->scope = head;
	}

	/* A plain for statement: every part in the head's scope. */
	if (node->kind == JS_NODE_FOR) {
		if (node->first != NULL)
			scope_visit(compiler, head, node->first);
		if (node->second != NULL)
			scope_visit(compiler, head, node->second);
		if (node->third != NULL)
			scope_visit(compiler, head, node->third);
		scope_visit(compiler, head, node->fourth);
		return;
	}

	/* for-in and for-of: the object outside, the left side and the body inside. */
	scope_visit(compiler, scope, node->second);
	scope_visit(compiler, head, node->first);
	scope_visit(compiler, head, node->fourth);
}

/* Resolves the names of a switch statement, whose cases share a scope when they declare a let or const. */
static void
scope_visit_switch(
	struct js_compiler *compiler,
	struct js_scope *scope,
	struct js_node *node)
{
	struct js_scope *cases;
	struct js_node *clause;

	/* The discriminant, outside. */
	scope_visit(compiler, scope, node->first);

	/* The cases' scope, noted on the node when it has one. */
	cases = scope_lexical_block(compiler, scope, node->second, 1);
	node->scope = NULL;
	if (cases != scope)
		node->scope = cases;

	/* Each case's test and statements. */
	for (clause = node->second; clause != NULL; clause = clause->next) {
		if (clause->first != NULL)
			scope_visit(compiler, cases, clause->first);
		scope_visit_list(compiler, cases, clause->second);
	}
}

/* Tells whether a statement is a let or const declaration. */
static int
scope_is_lexical(
	const struct js_node *node)
{
	/* Only a declaration of variables can be. */
	if (node->kind != JS_NODE_VARIABLES)
		return 0;

	/* var is not. */
	if (node->op == JS_P_VAR)
		return 0;

	/* let or const. */
	return 1;
}

/*
 * Fails the compilation when a var in a list of statements (down through
 * blocks, not into functions), or a function declared directly in it
 * (direct), has the name of a let or const of a block scope.
 */
static void
scope_check_vars(
	struct js_compiler *compiler,
	const struct js_scope *block,
	struct js_node *list,
	int direct)
{
	struct js_node *node;
	struct js_node *declarator;
	struct js_binding *declared;

	/* Each statement by its kind. */
	for (node = list; node != NULL; node = node->next) {
		switch (node->kind) {
		case JS_NODE_VARIABLES:
			/* A var's names (a let or const was checked when declared). */
			if (node->op != JS_P_VAR)
				break;
			for (declarator = node->first; declarator != NULL; declarator = declarator->next)
				scope_check_var_target(compiler, block, declarator->first);
			break;
		case JS_NODE_FUNCTION_DECLARATION:
			/* A function declared in the block itself. */
			if (!direct)
				break;
			declared = scope_find(block, node->text, node->text_length);
			if (declared != NULL)
				scope_redeclared(compiler, node);
			break;
		case JS_NODE_BLOCK:
			scope_check_vars(compiler, block, node->first, 0);
			break;
		case JS_NODE_IF:
			scope_check_vars(compiler, block, node->second, 0);
			scope_check_vars(compiler, block, node->third, 0);
			break;
		case JS_NODE_FOR:
		case JS_NODE_FOR_IN:
		case JS_NODE_FOR_OF:
			scope_check_vars(compiler, block, node->first, 0);
			scope_check_vars(compiler, block, node->fourth, 0);
			break;
		case JS_NODE_WHILE:
		case JS_NODE_DO_WHILE:
		case JS_NODE_WITH:
		case JS_NODE_LABELED:
			scope_check_vars(compiler, block, node->fourth, 0);
			break;
		case JS_NODE_TRY:
			scope_check_vars(compiler, block, node->first, 0);
			scope_check_vars(compiler, block, node->third, 0);
			scope_check_vars(compiler, block, node->fourth, 0);
			break;
		case JS_NODE_SWITCH:
			for (declarator = node->second; declarator != NULL; declarator = declarator->next)
				scope_check_vars(compiler, block, declarator->second, 0);
			break;
		default:
			break;
		}
	}
}

/* Fails the compilation when a var's target (a name, or the names of a pattern) names a let or const of a block scope. */
static void
scope_check_var_target(
	struct js_compiler *compiler,
	const struct js_scope *block,
	struct js_node *target)
{
	struct js_binding *declared;
	struct js_node *element;

	/* Nothing to check. */
	if (target == NULL)
		return;

	/* A name. */
	if (target->kind == JS_NODE_IDENTIFIER) {
		declared = scope_find(block, target->text, target->text_length);
		if (declared != NULL)
			scope_redeclared(compiler, target);
		return;
	}

	/* A pattern's elements. */
	for (element = target->first; element != NULL; element = element->next) {
		if (element->kind == JS_NODE_PROPERTY) {
			scope_check_var_target(compiler, block, element->second);
		} else {
			scope_check_var_target(compiler, block, element);
		}
	}
}

/* Fails the compilation with the SyntaxError of a name a scope declares twice. */
static void
scope_redeclared(
	struct js_compiler *compiler,
	const struct js_node *target)
{
	char message[200];
	char name[128];
	size_t index;

	/* The name in ASCII (other characters as ?), cut short when long. */
	for (index = 0; index < target->text_length && index + 1U < sizeof(name); index++) {
		name[index] = '?';
		if (target->text[index] < 0x80U)
			name[index] = (char)target->text[index];
	}
	name[index] = '\0';

	/* The early error, as Chromium words it. */
	snprintf(message, sizeof(message), "Identifier '%s' has already been declared", name);
	js_compile_fail(compiler, target, message);
}

/* Lists a function declaration a block makes when it is entered. */
static void
scope_block_function(
	struct js_compiler *compiler,
	struct js_scope *block,
	struct js_node *node)
{
	struct js_hoisted *entry;

	/* The entry, at the end of the block's list (source order). */
	entry = wb_arena_zalloc(compiler->arena, sizeof(*entry));
	if (entry == NULL)
		js_compile_out_of_memory(compiler);
	entry->node = node;
	if (block->functions_last == NULL) {
		block->functions = entry;
	} else {
		block->functions_last->next = entry;
	}

	/* The declaration is the last now. */
	block->functions_last = entry;
}

/*
 * Gives an arrow function the this of the function around it: that
 * function keeps its this in a hidden binding, which the arrow captures
 * (an ordinary function's own this needs nothing).
 */
static void
scope_arrow_this(
	struct js_compiler *compiler,
	struct js_scope *scope)
{
	struct js_function_info *owner;

	/* An ordinary function (or the program) reads its own this. */
	owner = scope->function;
	if (owner->program || (owner->node->flags & JS_FLAG_ARROW) == 0U)
		return;

	/* The nearest function around it that is not an arrow function (the program at the latest). */
	while (!owner->program && (owner->node->flags & JS_FLAG_ARROW) != 0U)
		owner = owner->parent;

	/* Its hidden binding, which the arrow's use captures. */
	scope_declare(compiler, owner->scope, js_this_name, JS_THIS_NAME_LENGTH, JS_BINDING_THIS);
	scope_reference(compiler, scope, js_this_name, JS_THIS_NAME_LENGTH);
}

/*
 * Makes a var named arguments the function's arguments object: the var
 * does not replace it (a parameter or a function declaration of that name
 * does, and keeps its binding).
 */
static void
scope_arguments_var(
	struct js_function_info *info)
{
	struct js_binding *binding;
	int own_arguments;

	/* A var of that name, in a function that has an arguments object. */
	binding = scope_find(info->scope, scope_arguments_name, SCOPE_ARGUMENTS_LENGTH);
	if (binding == NULL || binding->kind != JS_BINDING_VAR)
		return;
	own_arguments = scope_has_own_arguments(info);
	if (!own_arguments)
		return;

	/* The var is the arguments object's binding. */
	binding->kind = JS_BINDING_ARGUMENTS;
	info->arguments = binding;
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
