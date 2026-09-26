/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The inside of zedBSD's GLSL compiler: the arena, the tokens, the types,
 * the syntax tree, the symbols and the constants that the preprocessor,
 * the parser, the checker and the SPIR-V emitter share.
 *
 * Everything a compile makes lives in the compile's arena and goes with
 * the shader.  An allocation that fails does not return: it jumps back to
 * the entry of the compile or the link, which reports "out of memory", so
 * the code that builds trees and lists is not interleaved with checks.
 */

#ifndef GLSL_INTERNAL_H
#define GLSL_INTERNAL_H

#include "glsl.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* The most errors a compile reports before it gives up. */
#define GLSL_MAX_ERRORS		16U

/* The versions the compiler takes. */
#define GLSL_VERSION_ES100	100U
#define GLSL_VERSION_110	110U
#define GLSL_VERSION_120	120U
#define GLSL_VERSION_130	130U

/* The kinds of tokens. */
#define GLSL_TOKEN_EOF		0U
#define GLSL_TOKEN_IDENTIFIER	1U
#define GLSL_TOKEN_INT		2U
#define GLSL_TOKEN_UINT		3U
#define GLSL_TOKEN_FLOAT	4U
#define GLSL_TOKEN_PUNCT	5U
#define GLSL_TOKEN_NEWLINE	6U
#define GLSL_TOKEN_OTHER	7U

/* The punctuators. */
enum glsl_punct {
	GLSL_P_NONE = 0,
	GLSL_P_LPAREN,
	GLSL_P_RPAREN,
	GLSL_P_LBRACKET,
	GLSL_P_RBRACKET,
	GLSL_P_LBRACE,
	GLSL_P_RBRACE,
	GLSL_P_DOT,
	GLSL_P_COMMA,
	GLSL_P_SEMICOLON,
	GLSL_P_COLON,
	GLSL_P_QUESTION,
	GLSL_P_PLUS,
	GLSL_P_MINUS,
	GLSL_P_STAR,
	GLSL_P_SLASH,
	GLSL_P_PERCENT,
	GLSL_P_LT,
	GLSL_P_GT,
	GLSL_P_LE,
	GLSL_P_GE,
	GLSL_P_EQ,
	GLSL_P_NE,
	GLSL_P_AND_AND,
	GLSL_P_OR_OR,
	GLSL_P_XOR_XOR,
	GLSL_P_BANG,
	GLSL_P_TILDE,
	GLSL_P_AMP,
	GLSL_P_BAR,
	GLSL_P_CARET,
	GLSL_P_SHL,
	GLSL_P_SHR,
	GLSL_P_ASSIGN,
	GLSL_P_ADD_ASSIGN,
	GLSL_P_SUB_ASSIGN,
	GLSL_P_MUL_ASSIGN,
	GLSL_P_DIV_ASSIGN,
	GLSL_P_MOD_ASSIGN,
	GLSL_P_SHL_ASSIGN,
	GLSL_P_SHR_ASSIGN,
	GLSL_P_AND_ASSIGN,
	GLSL_P_OR_ASSIGN,
	GLSL_P_XOR_ASSIGN,
	GLSL_P_INC,
	GLSL_P_DEC,
	GLSL_P_HASH,
	GLSL_P_HASH_HASH
};

/* The keywords (the built-in type names are GLSL_K_TYPE with the type). */
enum glsl_keyword {
	GLSL_K_NONE = 0,
	GLSL_K_TYPE,
	GLSL_K_RESERVED,
	GLSL_K_ATTRIBUTE,
	GLSL_K_CONST,
	GLSL_K_UNIFORM,
	GLSL_K_VARYING,
	GLSL_K_BREAK,
	GLSL_K_CONTINUE,
	GLSL_K_DO,
	GLSL_K_FOR,
	GLSL_K_WHILE,
	GLSL_K_IF,
	GLSL_K_ELSE,
	GLSL_K_IN,
	GLSL_K_OUT,
	GLSL_K_INOUT,
	GLSL_K_TRUE,
	GLSL_K_FALSE,
	GLSL_K_LOWP,
	GLSL_K_MEDIUMP,
	GLSL_K_HIGHP,
	GLSL_K_PRECISION,
	GLSL_K_INVARIANT,
	GLSL_K_DISCARD,
	GLSL_K_RETURN,
	GLSL_K_STRUCT,
	GLSL_K_SWITCH,
	GLSL_K_CASE,
	GLSL_K_DEFAULT,
	GLSL_K_FLAT,
	GLSL_K_SMOOTH,
	GLSL_K_NOPERSPECTIVE,
	GLSL_K_CENTROID
};

/* The kinds of types. */
#define GLSL_KIND_VOID		0U
#define GLSL_KIND_SCALAR	1U
#define GLSL_KIND_VECTOR	2U
#define GLSL_KIND_MATRIX	3U
#define GLSL_KIND_SAMPLER	4U
#define GLSL_KIND_STRUCT	5U
#define GLSL_KIND_ARRAY		6U
#define GLSL_KIND_ERROR		7U

/* The scalar bases (GLSL_BASE_NONE for void, structs, arrays and errors). */
#define GLSL_BASE_NONE		0U
#define GLSL_BASE_BOOL		1U
#define GLSL_BASE_INT		2U
#define GLSL_BASE_UINT		3U
#define GLSL_BASE_FLOAT		4U

/* The storage qualifiers of a declaration. */
#define GLSL_STORAGE_NONE	0U
#define GLSL_STORAGE_CONST	1U
#define GLSL_STORAGE_ATTRIBUTE	2U
#define GLSL_STORAGE_UNIFORM	3U
#define GLSL_STORAGE_VARYING	4U
#define GLSL_STORAGE_IN		5U
#define GLSL_STORAGE_OUT	6U
#define GLSL_STORAGE_INOUT	7U

/* The interpolation qualifiers. */
#define GLSL_INTERP_NONE	0U
#define GLSL_INTERP_SMOOTH	1U
#define GLSL_INTERP_FLAT	2U
#define GLSL_INTERP_NOPERSPECTIVE 3U

/* The precision qualifiers. */
#define GLSL_PRECISION_NONE	0U
#define GLSL_PRECISION_LOW	1U
#define GLSL_PRECISION_MEDIUM	2U
#define GLSL_PRECISION_HIGH	3U

/* What a variable symbol is to the emitter: where it lives. */
#define GLSL_VAR_LOCAL		0U
#define GLSL_VAR_GLOBAL		1U
#define GLSL_VAR_CONST		2U
#define GLSL_VAR_UNIFORM	3U
#define GLSL_VAR_INPUT		4U
#define GLSL_VAR_OUTPUT		5U
#define GLSL_VAR_PARAMETER	6U

/* The built-in variables (0: a variable the shader declared). */
#define GLSL_BUILTIN_NONE	0U
#define GLSL_BUILTIN_POSITION	1U
#define GLSL_BUILTIN_POINT_SIZE	2U
#define GLSL_BUILTIN_VERTEX_ID	3U
#define GLSL_BUILTIN_FRAG_COORD	4U
#define GLSL_BUILTIN_FRONT_FACING 5U
#define GLSL_BUILTIN_POINT_COORD 6U
#define GLSL_BUILTIN_FRAG_COLOR	7U
#define GLSL_BUILTIN_FRAG_DATA	8U
#define GLSL_BUILTIN_FRAG_DEPTH	9U

/* The kinds of symbols. */
#define GLSL_SYMBOL_VARIABLE	1U
#define GLSL_SYMBOL_FUNCTION	2U
#define GLSL_SYMBOL_TYPE	3U

/* The kinds of syntax tree nodes. */
enum glsl_node_kind {
	/* Expressions. */
	GLSL_N_INT = 1,
	GLSL_N_UINT,
	GLSL_N_FLOAT,
	GLSL_N_BOOL,
	GLSL_N_IDENTIFIER,
	GLSL_N_UNARY,
	GLSL_N_PREINC,
	GLSL_N_PREDEC,
	GLSL_N_POSTINC,
	GLSL_N_POSTDEC,
	GLSL_N_BINARY,
	GLSL_N_ASSIGN,
	GLSL_N_TERNARY,
	GLSL_N_COMMA,
	GLSL_N_CALL,
	GLSL_N_FIELD,
	GLSL_N_INDEX,
	GLSL_N_LENGTH,
	GLSL_N_CONVERT,

	/* Types and declarations. */
	GLSL_N_TYPE,
	GLSL_N_DECLARATION,
	GLSL_N_VARIABLE,
	GLSL_N_FUNCTION,
	GLSL_N_PARAMETER,
	GLSL_N_PRECISION,
	GLSL_N_INVARIANT,

	/* Statements. */
	GLSL_N_BLOCK,
	GLSL_N_EXPRESSION,
	GLSL_N_IF,
	GLSL_N_FOR,
	GLSL_N_WHILE,
	GLSL_N_DO,
	GLSL_N_SWITCH,
	GLSL_N_CASE,
	GLSL_N_DEFAULT,
	GLSL_N_RETURN,
	GLSL_N_BREAK,
	GLSL_N_CONTINUE,
	GLSL_N_DISCARD,
	GLSL_N_EMPTY
};

/* The flags of a node. */
#define GLSL_NODE_ARRAY		0x0001U
#define GLSL_NODE_UNSIZED	0x0002U
#define GLSL_NODE_SWIZZLE	0x0004U
#define GLSL_NODE_CONSTRUCTOR	0x0008U
#define GLSL_NODE_SCOPE		0x0010U
#define GLSL_NODE_CONST_PARAM	0x0020U
#define GLSL_NODE_VOID_PARAMS	0x0040U
#define GLSL_NODE_SIDE_EFFECTS	0x0080U
#define GLSL_NODE_STRUCT_DEFINITION 0x0100U

/*
 * A memory arena: the blocks one compile or one link allocates from, all
 * freed together.  An allocation that fails jumps to `failure`.
 */
struct glsl_arena_block;

struct glsl_arena {
	struct glsl_arena_block *blocks;
	jmp_buf *failure;
};

/*
 * A growing text: an info log or a name being built.
 */
struct glsl_text {
	char *data;
	size_t length;
	size_t capacity;
};

struct glsl_type;

/*
 * One token of the source, before or after preprocessing.
 *
 * The text points into the source or into the arena (for a token a
 * macro paste or __LINE__ made); it is not terminated.
 */
struct glsl_token {
	/* GLSL_TOKEN_*, the punctuator of a GLSL_TOKEN_PUNCT. */
	unsigned kind;
	unsigned punct;

	/* The spelling and its length. */
	const char *text;
	unsigned length;

	/* The line it came from, and whether white space came before it. */
	unsigned line;
	unsigned space;

	/* Nonzero when a macro of its name is being expanded around it, so it is never expanded again. */
	unsigned noexpand;

	/* The value of a number, and whether it carried a suffix (a float's f, an integer's u). */
	uint32_t integer;
	float number;
	unsigned suffix;

	/* The parser's classification of an identifier: whether it was made, the keyword, a built-in type's type. */
	unsigned classified;
	unsigned keyword;
	const struct glsl_type *keyword_type;
};

/*
 * One member of a struct type.
 */
struct glsl_field {
	const char *name;
	const struct glsl_type *type;
};

/*
 * A GLSL type.
 *
 * Scalars, vectors, matrices and samplers are static tables shared by
 * every compile (types.c); structs and arrays are made in a compile's
 * arena.  Two struct types are the same type only when they are the same
 * object (the same declaration); two array types are the same when their
 * elements and lengths are.
 */
struct glsl_type {
	/* GLSL_KIND_*, and GLSL_BASE_* of the scalars (a sampler's is the type its texels are read as). */
	unsigned kind;
	unsigned base;

	/* The components of a vector or of a matrix column, and a matrix's columns (1 otherwise). */
	unsigned components;
	unsigned columns;

	/* A sampler: GLSL_SAMPLER_*, and whether it is a shadow sampler. */
	unsigned sampler;
	unsigned shadow;

	/* The name GLSL spells the type with (NULL for an array). */
	const char *name;

	/* A struct's members. */
	struct glsl_field *fields;
	unsigned field_count;

	/* An array's element type and length. */
	const struct glsl_type *element;
	unsigned length;
};

/*
 * A scalar of a constant value.
 */
union glsl_scalar {
	float f;
	int32_t i;
	uint32_t u;
};

/*
 * A constant value: its type and its scalars, flattened (vectors by
 * component, matrices column after column, arrays and structs element by
 * element and member by member; a bool is 0 or 1 in u).
 */
struct glsl_constant {
	const struct glsl_type *type;
	unsigned count;
	union glsl_scalar *values;
};

struct glsl_symbol;
struct glsl_function;
struct glsl_builtin;

/*
 * A node of the syntax tree: an expression, a statement, a declaration or
 * a type specifier.
 *
 * The parser fills the kind, the line, the operator, the children, the
 * name and the qualifiers; the checker fills the type, the symbol or
 * function a node refers to, a constant value, and the swizzle or member
 * a selection picks.
 */
struct glsl_node {
	/* enum glsl_node_kind, the line, and the punctuator of an operator. */
	unsigned kind;
	unsigned line;
	unsigned op;

	/* The children (their meaning depends on the kind), and the next node of a list. */
	struct glsl_node *child[4];
	struct glsl_node *next;

	/* A name: of an identifier, a field, a function, a declared variable or a struct. */
	const char *name;

	/* The value of a literal. */
	uint32_t integer;
	float number;

	/* GLSL_NODE_* flags. */
	unsigned flags;

	/* The qualifiers of a type specifier or a parameter. */
	unsigned storage;
	unsigned interpolation;
	unsigned precision;
	unsigned centroid;
	unsigned invariant;

	/* The type: a type specifier's built-in type as parsed, an expression's once checked. */
	const struct glsl_type *type;

	/* What the checker resolved: a variable, a user function, a built-in function. */
	struct glsl_symbol *symbol;
	struct glsl_function *function;
	const struct glsl_builtin *builtin;

	/* The value of an expression that is constant. */
	struct glsl_constant *constant;

	/* A selection: the member of a struct, or the components of a swizzle. */
	unsigned field;
	unsigned swizzle[4];
	unsigned swizzle_count;
};

/*
 * A declared name: a variable, a function (with its overloads) or a
 * struct type.
 *
 * A variable symbol also carries what the emitter needs: the SPIR-V id of
 * the variable holding it while the code that declares it is emitted.
 */
struct glsl_symbol {
	/* The name, GLSL_SYMBOL_*, and the line it was declared on. */
	const char *name;
	unsigned kind;
	unsigned line;

	/* A variable's or a struct's type. */
	const struct glsl_type *type;

	/* A variable's qualifiers, GLSL_VAR_* and GLSL_BUILTIN_*. */
	unsigned storage;
	unsigned interpolation;
	unsigned precision;
	unsigned centroid;
	unsigned invariant;
	unsigned where;
	unsigned builtin;

	/* A const variable's value, and a global's constant initializer. */
	struct glsl_constant *constant;
	struct glsl_constant *initial;

	/* A function's overloads. */
	struct glsl_function *functions;

	/* Whether the code main reaches reads or writes it, and whether it is written. */
	unsigned used;
	unsigned written;

	/* The next global of the shader, in declaration order. */
	struct glsl_symbol *next_global;

	/* While emitting: the SPIR-V id of the variable, and of the pointer type to it. */
	uint32_t id;
	uint32_t pointer_type;
};

/*
 * A user function: one overload of a name.
 */
struct glsl_function {
	/* The name, the return type, the line of the first declaration. */
	const char *name;
	const struct glsl_type *return_type;
	unsigned line;

	/* The parameters (variable symbols with GLSL_VAR_PARAMETER). */
	struct glsl_symbol **parameters;
	unsigned parameter_count;

	/* The definition's body (NULL while only declared). */
	struct glsl_node *body;

	/* The next overload of the name. */
	struct glsl_function *next;

	/* Whether it returns other than as its body's last statement (the emitter needs a loop to leave it). */
	unsigned early_return;

	/* Set while main's call graph walks through it (static recursion), and once it has been walked. */
	unsigned visiting;
	unsigned visited;
};

/*
 * A built-in function: one signature of the table in builtins.c.
 *
 * The signature is a string of type codes (types.c glsl_builtin_match):
 * the return type first, then the parameters.
 */
struct glsl_builtin {
	/* The name, and the signature: return type code, then parameter codes. */
	const char *name;
	const char *signature;

	/* GLSL_BI_* saying how it is emitted, and the GLSL.std.450 or SPIR-V number. */
	unsigned operation;
	unsigned number;

	/* The stages it exists in (bit 0 vertex, bit 1 fragment), and the versions. */
	unsigned stages;
	unsigned versions;
};

/* The versions a built-in exists in. */
#define GLSL_IN_ES100		0x01U
#define GLSL_IN_110		0x02U
#define GLSL_IN_120		0x04U
#define GLSL_IN_130		0x08U
#define GLSL_IN_ALL		0x0fU
#define GLSL_IN_DESKTOP		0x0eU
#define GLSL_IN_120_UP		0x0cU
#define GLSL_IN_130_UP		0x08U
#define GLSL_IN_OLD		0x07U
#define GLSL_IN_DERIVATIVES	0x10U

/* How a built-in function is emitted. */
#define GLSL_BI_EXT		1U
#define GLSL_BI_OP		2U
#define GLSL_BI_SPECIAL		3U

/* The special built-ins (glsl_builtin.number when GLSL_BI_SPECIAL). */
#define GLSL_SPECIAL_TEXTURE	1U
#define GLSL_SPECIAL_TEXTURE_BIAS 2U
#define GLSL_SPECIAL_TEXTURE_PROJ 3U
#define GLSL_SPECIAL_TEXTURE_PROJ_BIAS 4U
#define GLSL_SPECIAL_TEXTURE_LOD 5U
#define GLSL_SPECIAL_TEXTURE_PROJ_LOD 6U
#define GLSL_SPECIAL_SHADOW	7U
#define GLSL_SPECIAL_SHADOW_PROJ 8U
#define GLSL_SPECIAL_TEXTURE_SIZE 9U
#define GLSL_SPECIAL_TEXEL_FETCH 10U
#define GLSL_SPECIAL_MOD	11U
#define GLSL_SPECIAL_DOT	12U
#define GLSL_SPECIAL_ANY	13U
#define GLSL_SPECIAL_ALL	14U
#define GLSL_SPECIAL_NOT	15U
#define GLSL_SPECIAL_COMPARE	16U
#define GLSL_SPECIAL_MATRIX_COMP_MULT 17U
#define GLSL_SPECIAL_NOISE	18U
#define GLSL_SPECIAL_FACEFORWARD 19U
#define GLSL_SPECIAL_MIX_BOOL	20U
#define GLSL_SPECIAL_MODF	21U
#define GLSL_SPECIAL_ATAN	22U
#define GLSL_SPECIAL_OUTER_PRODUCT 23U

/*
 * The compile of one shader, and what stays of it for the link: the
 * checked tree, the globals and main.
 */
struct glsl_shader {
	/* The arena everything below lives in, and where a failed allocation or a fatal error goes. */
	struct glsl_arena arena;
	jmp_buf failure;

	/* The stage, the version, and whether it is OpenGL ES's language. */
	unsigned stage;
	unsigned version;
	unsigned es;

	/* The info log and the errors reported so far. */
	struct glsl_text log;
	unsigned errors;

	/* The extensions the shader enabled. */
	unsigned derivatives;

	/* The preprocessed tokens, and the parser's position in them. */
	struct glsl_token *tokens;
	unsigned token_count;
	unsigned position;

	/* The parser's scopes of struct type names. */
	struct glsl_parse_scope *type_scope;

	/* The translation unit: the external declarations in order. */
	struct glsl_node *unit;

	/* The checker's innermost scope, the function being checked, and loop and switch nesting. */
	struct glsl_scope *scope;
	struct glsl_function *current;
	unsigned loop_depth;
	unsigned switch_depth;

	/* The default precision of float in the current scope chain (ES fragment shaders have none until declared). */
	unsigned float_precision;

	/* The globals in declaration order, and main. */
	struct glsl_symbol *globals;
	struct glsl_symbol *last_global;
	struct glsl_function *main;
};

/*
 * A scope of the checker: the names declared in one block, function or
 * the global scope.
 */
struct glsl_scope_entry {
	struct glsl_symbol *symbol;
	struct glsl_scope_entry *next;
};

struct glsl_scope {
	struct glsl_scope_entry *entries;
	struct glsl_scope *parent;
	unsigned float_precision;
};

/* arena.c: memory, text and the info log. */
void *glsl_alloc(struct glsl_arena *arena, size_t size);
void glsl_arena_free(struct glsl_arena *arena);
char *glsl_strndup(struct glsl_arena *arena, const char *text, size_t length);
void glsl_text_append(struct glsl_text *text, const char *data, size_t length, jmp_buf *failure);
void glsl_text_printf(struct glsl_text *text, jmp_buf *failure, const char *format, ...);
void glsl_text_vprintf(struct glsl_text *text, jmp_buf *failure, const char *format, va_list arguments);
void glsl_error(struct glsl_shader *shader, unsigned line, const char *format, ...);
void glsl_warning(struct glsl_shader *shader, unsigned line, const char *format, ...);
void glsl_fatal(struct glsl_shader *shader, unsigned line, const char *format, ...);

/* lex.c: the tokens of a source. */
unsigned glsl_lex(struct glsl_shader *shader, const char *source, size_t length, unsigned first_line, struct glsl_token **tokens);
int glsl_token_is(const struct glsl_token *token, const char *text);

/* preprocess.c: the preprocessed tokens of a source. */
void glsl_preprocess(struct glsl_shader *shader, const char *source, unsigned default_version);

/* parse.c: the syntax tree of the tokens. */
void glsl_parse(struct glsl_shader *shader);

/* types.c: types. */
const struct glsl_type *glsl_type_void(void);
const struct glsl_type *glsl_type_error(void);
const struct glsl_type *glsl_type_scalar(unsigned base);
const struct glsl_type *glsl_type_vector(unsigned base, unsigned components);
const struct glsl_type *glsl_type_matrix(unsigned columns, unsigned rows);
const struct glsl_type *glsl_type_sampler(unsigned base, unsigned sampler, unsigned shadow);
const struct glsl_type *glsl_type_named(const char *name, unsigned length, unsigned *versions);
const struct glsl_type *glsl_type_array(struct glsl_arena *arena, const struct glsl_type *element, unsigned length);
const struct glsl_type *glsl_type_column(const struct glsl_type *type);
const struct glsl_type *glsl_type_with_base(const struct glsl_type *type, unsigned base);
int glsl_type_equal(const struct glsl_type *left, const struct glsl_type *right);
int glsl_type_numeric(const struct glsl_type *type);
int glsl_type_contains_sampler(const struct glsl_type *type);
unsigned glsl_type_scalars(const struct glsl_type *type);
unsigned glsl_type_locations(const struct glsl_type *type);
void glsl_type_name(const struct glsl_type *type, char *out, size_t size);
unsigned glsl_std140_alignment(const struct glsl_type *type);
unsigned glsl_std140_size(const struct glsl_type *type);
unsigned glsl_std140_stride(const struct glsl_type *type);

/* builtins.c: built-in functions, variables and constants. */
const struct glsl_builtin *glsl_builtin_first(const char *name);
const struct glsl_builtin *glsl_builtin_next(const struct glsl_builtin *builtin);
int glsl_builtin_match(const struct glsl_shader *shader, const struct glsl_builtin *builtin, const struct glsl_type **arguments, unsigned count, int convert, const struct glsl_type **parameters, const struct glsl_type **result);
void glsl_builtin_variables(struct glsl_shader *shader);

/* check.c: the checker. */
void glsl_check(struct glsl_shader *shader);
struct glsl_symbol *glsl_declare(struct glsl_shader *shader, const char *name, unsigned kind, unsigned line);
void glsl_add_global(struct glsl_shader *shader, struct glsl_symbol *symbol);

/* fold.c: constant expressions. */
struct glsl_constant *glsl_fold(struct glsl_shader *shader, struct glsl_node *node);
struct glsl_constant *glsl_constant_new(struct glsl_arena *arena, const struct glsl_type *type);
struct glsl_constant *glsl_constant_convert(struct glsl_arena *arena, const struct glsl_constant *value, const struct glsl_type *type);

#endif
