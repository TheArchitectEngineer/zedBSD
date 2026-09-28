/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The CSS parser: style sheets into style rules (selectors and
 * declarations), and style attributes into declarations.
 *
 * The URLs of the @import rules before the style rules are kept for the
 * page to fetch (ws074-p068); other at-rules are skipped whole in this
 * pass.  A rule whose selector this pass cannot read is dropped, as the
 * standard drops a rule with an invalid selector.  The parsed sheet's
 * selectors are filed in its rule index (index.c).
 */

#include "css/internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* The most declarations one declaration may expand into (a shorthand's longhands). */
#define PARSER_EXPANSION_MAX	16U

/*
 * A run of tokens: where it starts and how many there are.
 */
struct token_range {
	const struct css_token *tokens;
	size_t count;
};

/*
 * A pseudo-class name and what it is.
 */
struct parser_pseudo_name {
	const char *name;
	int pseudo;
};

/* The pseudo-classes this pass evaluates. */
static const struct parser_pseudo_name parser_pseudo_names[] = {
	{ "root", CSS_PSEUDO_ROOT },
	{ "first-child", CSS_PSEUDO_FIRST_CHILD },
	{ "last-child", CSS_PSEUDO_LAST_CHILD },
	{ "only-child", CSS_PSEUDO_ONLY_CHILD },
	{ "empty", CSS_PSEUDO_EMPTY },
	{ "link", CSS_PSEUDO_LINK },
	{ "any-link", CSS_PSEUDO_LINK },
	{ NULL, 0 }
};

static size_t parser_skip_block(const struct css_token *tokens, size_t count, size_t index);
static size_t parser_skip_at_rule(const struct css_token *tokens, size_t count, size_t index);
static int parser_import(struct vm_heap *heap, const struct css_token *tokens, size_t count, struct wb_vector *imports);
static int parser_add_rule(struct vm_heap *heap, struct css_sheet *sheet, struct wb_vector *rules, struct token_range prelude, struct token_range block);
static int parser_selectors(struct vm_heap *heap, struct wb_arena *arena, struct token_range prelude, struct css_selector **selectors, size_t *count);
static int parser_selector(struct vm_heap *heap, struct wb_arena *arena, struct token_range tokens, struct css_selector *selector);
static int parser_compound(struct vm_heap *heap, struct wb_arena *arena, const struct css_token *tokens, size_t count, size_t *index, struct css_compound *compound, uint32_t *specificity);
static int parser_attribute(struct vm_heap *heap, const struct css_token *tokens, size_t count, struct css_simple *simple);
static int parser_pseudo(const struct css_token *token, struct css_simple *simple);
static int parser_declarations(struct vm_heap *heap, struct wb_arena *arena, struct token_range block, struct css_declaration **declarations, size_t *count);
static struct vm_string *parser_atom(struct vm_heap *heap, const struct css_token *token, int lower);
static struct token_range parser_trim(struct token_range range);

/*
 * Parses a style sheet into its rules.
 */
int
css_parse_sheet(
	struct vm_heap *heap,
	const uint16_t *units,
	size_t length,
	int origin,
	struct css_sheet *sheet)
{
	struct css_token *tokens;
	struct token_range prelude;
	struct token_range block;
	struct wb_vector rules;
	struct wb_vector imports;
	size_t count;
	size_t index;
	size_t start;
	size_t end;
	int is_import;
	int error;

	/* Tokenizes the text into the sheet's arena. */
	memset(sheet, 0, sizeof(*sheet));
	wb_arena_init(&sheet->arena, 0);
	sheet->origin = origin;
	error = css_tokenize(&sheet->arena, units, length, &tokens, &count);
	if (error != 0)
		return error;

	/* Reads the top-level rules. */
	wb_vector_init(&rules, sizeof(struct css_rule));
	wb_vector_init(&imports, sizeof(struct vm_string *));
	index = 0;
	while (index < count && tokens[index].type != CSS_TOKEN_EOF) {
		/* Whitespace and the HTML comment markers between rules are skipped. */
		if (tokens[index].type == CSS_TOKEN_WHITESPACE ||
		    tokens[index].type == CSS_TOKEN_CDO ||
		    tokens[index].type == CSS_TOKEN_CDC) {
			index++;
			continue;
		}

		/* An @import before the style rules names a sheet that comes before this one's rules. */
		if (tokens[index].type == CSS_TOKEN_AT_KEYWORD) {
			is_import = css_ident_equal(&tokens[index], "import");
			end = parser_skip_at_rule(tokens, count, index);
			if (is_import && rules.count == 0) {
				error = parser_import(heap, tokens + index + 1U, end - index - 1U, &imports);
				if (error == ENOMEM) {
					wb_vector_release(&rules);
					wb_vector_release(&imports);
					return error;
				}
			}

			/* Every other at-rule is skipped whole in this pass. */
			index = end;
			continue;
		}

		/* A qualified rule: the prelude up to the block, then the block. */
		start = index;
		while (index < count && tokens[index].type != CSS_TOKEN_OPEN_CURLY && tokens[index].type != CSS_TOKEN_EOF) {
			if (tokens[index].type == CSS_TOKEN_OPEN_PAREN || tokens[index].type == CSS_TOKEN_OPEN_SQUARE ||
			    tokens[index].type == CSS_TOKEN_FUNCTION) {
				index = parser_skip_block(tokens, count, index);
				continue;
			}

			/* Anything else is part of the prelude. */
			index++;
		}

		/* A prelude without a block ends the sheet. */
		if (index >= count || tokens[index].type == CSS_TOKEN_EOF)
			break;
		prelude.tokens = tokens + start;
		prelude.count = index - start;
		end = parser_skip_block(tokens, count, index);
		block.tokens = tokens + index + 1U;
		block.count = end - index - 1U;
		if (end > index + 1U && tokens[end - 1U].type == CSS_TOKEN_CLOSE_CURLY)
			block.count--;
		index = end;

		/* Adds the rule (a rule that does not parse is dropped). */
		error = parser_add_rule(heap, sheet, &rules, prelude, block);
		if (error == ENOMEM) {
			wb_vector_release(&rules);
			wb_vector_release(&imports);
			return error;
		}
	}

	/* Moves the imported URLs into the arena. */
	if (imports.count != 0) {
		sheet->imports = wb_arena_alloc(&sheet->arena, imports.count * sizeof(struct vm_string *));
		if (sheet->imports == NULL) {
			wb_vector_release(&rules);
			wb_vector_release(&imports);
			return ENOMEM;
		}

		/* Copies them. */
		memcpy(sheet->imports, imports.items, imports.count * sizeof(struct vm_string *));
		sheet->import_count = imports.count;
	}

	/* The list of imports is no longer needed. */
	wb_vector_release(&imports);

	/* Moves the rules into the arena. */
	if (rules.count != 0) {
		sheet->rules = wb_arena_alloc(&sheet->arena, rules.count * sizeof(struct css_rule));
		if (sheet->rules == NULL) {
			wb_vector_release(&rules);
			return ENOMEM;
		}

		/* Copies them. */
		memcpy(sheet->rules, rules.items, rules.count * sizeof(struct css_rule));
		sheet->rule_count = rules.count;
	}

	/* The list is no longer needed. */
	wb_vector_release(&rules);

	/* Files the selectors in the rule index. */
	error = css_index_build(sheet);
	if (error != 0)
		return error;

	/* Succeeded: the sheet holds its rules. */
	return 0;
}

/*
 * Parses the declarations of a style attribute (in an arena the caller
 * owns).
 */
int
css_parse_declarations(
	struct vm_heap *heap,
	struct wb_arena *arena,
	const uint16_t *units,
	size_t length,
	struct css_declaration **declarations,
	size_t *count)
{
	struct css_token *tokens;
	struct token_range block;
	size_t token_count;
	int error;

	/* Tokenizes the text. */
	error = css_tokenize(arena, units, length, &tokens, &token_count);
	if (error != 0)
		return error;

	/* Everything but the end-of-file token is one declaration block. */
	block.tokens = tokens;
	block.count = token_count - 1U;
	error = parser_declarations(heap, arena, block, declarations, count);
	if (error != 0)
		return error;

	/* Succeeded: the declarations are in the arena. */
	return 0;
}

/*
 * Frees a parsed style sheet.
 */
void
css_sheet_release(
	struct css_sheet *sheet)
{
	/* Everything the sheet holds is in its arena. */
	wb_arena_release(&sheet->arena);
	sheet->rules = NULL;
	sheet->rule_count = 0;
}

/*
 * Parses an author style sheet into a sheet of its own.
 */
int
css_sheet_create(
	struct css_sheet **sheet,
	struct vm_heap *heap,
	const uint16_t *units,
	size_t length)
{
	struct css_sheet *made;
	int error;

	/* Allocates the sheet. */
	made = calloc(1, sizeof(*made));
	if (made == NULL)
		return ENOMEM;

	/* Parses the text into it. */
	error = css_parse_sheet(heap, units, length, CSS_ORIGIN_AUTHOR, made);
	if (error != 0) {
		css_sheet_release(made);
		free(made);
		return error;
	}

	/* Succeeded: the caller owns the sheet. */
	*sheet = made;
	return 0;
}

/*
 * Frees a sheet made by css_sheet_create.
 */
void
css_sheet_destroy(
	struct css_sheet *sheet)
{
	/* A NULL sheet is nothing to free. */
	if (sheet == NULL)
		return;

	/* Its arena, then the sheet. */
	css_sheet_release(sheet);
	free(sheet);
}

/*
 * Tells how many sheets a sheet's @import rules name.
 */
size_t
css_sheet_import_count(
	const struct css_sheet *sheet)
{
	/* The number of imports. */
	return sheet->import_count;
}

/*
 * Gives the URL (an atom, as the sheet wrote it) of one of a sheet's
 * @import rules.
 */
struct vm_string *
css_sheet_import(
	const struct css_sheet *sheet,
	size_t index)
{
	/* The import's URL. */
	return sheet->imports[index];
}

/*
 * Tells how many style rules a sheet holds.
 */
size_t
css_sheet_rule_count(
	const struct css_sheet *sheet)
{
	/* The number of rules. */
	return sheet->rule_count;
}

/*
 * Resolves every URL the sheet's declarations name (and its imports)
 * with the caller's resolver, so that they no longer depend on where the
 * sheet came from.
 */
int
css_sheet_resolve_urls(
	struct css_sheet *sheet,
	css_url_resolver resolve,
	void *context)
{
	struct css_declaration *declaration;
	struct vm_string *resolved;
	size_t rule;
	size_t position;
	int error;

	/* The declarations of every rule. */
	for (rule = 0; rule < sheet->rule_count; rule++) {
		for (position = 0; position < sheet->rules[rule].declaration_count; position++) {
			declaration = &sheet->rules[rule].declarations[position];

			/* Only a URL value is resolved. */
			if (declaration->value.kind != CSS_VALUE_URL || declaration->value.url == NULL)
				continue;

			/* The resolver's URL replaces the written one; one it cannot resolve stays. */
			error = resolve(context, declaration->value.url, &resolved);
			if (error == ENOMEM)
				return error;
			if (error == 0)
				declaration->value.url = resolved;
		}
	}

	/* The imports. */
	for (position = 0; position < sheet->import_count; position++) {
		error = resolve(context, sheet->imports[position], &resolved);
		if (error == ENOMEM)
			return error;
		if (error == 0)
			sheet->imports[position] = resolved;
	}

	/* Succeeded: the URLs are resolved. */
	return 0;
}

/*
 * Tells whether a token's text is an ASCII word, ignoring ASCII case.
 */
int
css_ident_equal(
	const struct css_token *token,
	const char *ascii)
{
	int same;

	/* Compares the text. */
	same = css_units_equal_ascii(token->text, token->length, ascii);

	/* Reports the answer. */
	return same;
}

/*
 * Tells whether UTF-16 units are an ASCII word, ignoring ASCII case.
 */
int
css_units_equal_ascii(
	const uint16_t *units,
	size_t length,
	const char *ascii)
{
	uint16_t unit;
	size_t index;

	/* Compares unit by unit, folding ASCII upper case. */
	for (index = 0; index < length; index++) {
		if (ascii[index] == '\0')
			return 0;
		unit = units[index];
		if (unit >= 'A' && unit <= 'Z')
			unit = (uint16_t)(unit + 0x20U);
		if (unit != (unsigned char)ascii[index])
			return 0;
	}

	/* The word must end where the units do. */
	if (ascii[length] != '\0')
		return 0;

	/* The two are equal. */
	return 1;
}

/* Skips a block that starts at index (a function, parenthesis, bracket or brace) to just past its end. */
static size_t
parser_skip_block(
	const struct css_token *tokens,
	size_t count,
	size_t index)
{
	int depth;
	int type;

	/* Counts the opening and closing tokens of every kind together. */
	depth = 0;
	while (index < count) {
		type = tokens[index].type;
		index++;
		if (type == CSS_TOKEN_EOF)
			return index - 1U;
		if (type == CSS_TOKEN_OPEN_CURLY || type == CSS_TOKEN_OPEN_PAREN ||
		    type == CSS_TOKEN_OPEN_SQUARE || type == CSS_TOKEN_FUNCTION)
			depth++;
		if (type == CSS_TOKEN_CLOSE_CURLY || type == CSS_TOKEN_CLOSE_PAREN || type == CSS_TOKEN_CLOSE_SQUARE)
			depth--;
		if (depth == 0)
			return index;
	}

	/* The block runs to the end. */
	return index;
}

/* Skips an at-rule: to its semicolon, or past its block. */
static size_t
parser_skip_at_rule(
	const struct css_token *tokens,
	size_t count,
	size_t index)
{
	/* Moves past the at-keyword, then to the end of the rule. */
	index++;
	while (index < count && tokens[index].type != CSS_TOKEN_EOF) {
		if (tokens[index].type == CSS_TOKEN_SEMICOLON)
			return index + 1U;
		if (tokens[index].type == CSS_TOKEN_OPEN_CURLY)
			return parser_skip_block(tokens, count, index);
		if (tokens[index].type == CSS_TOKEN_OPEN_PAREN || tokens[index].type == CSS_TOKEN_OPEN_SQUARE ||
		    tokens[index].type == CSS_TOKEN_FUNCTION) {
			index = parser_skip_block(tokens, count, index);
			continue;
		}

		/* Anything else belongs to the rule. */
		index++;
	}

	/* The rule ran to the end. */
	return index;
}

/*
 * Reads the URL of an @import rule (the tokens after its at-keyword): a
 * string or url() first; the media list after it comes with the media
 * queries.  An @import without a URL is ignored.
 */
static int
parser_import(
	struct vm_heap *heap,
	const struct css_token *tokens,
	size_t count,
	struct wb_vector *imports)
{
	struct vm_string *url;
	size_t index;
	int is_url;
	int error;

	/* Skips the whitespace before the URL. */
	index = 0;
	while (index < count && tokens[index].type == CSS_TOKEN_WHITESPACE)
		index++;
	if (index >= count)
		return EINVAL;

	/* A string, a url token, or url("...") with a string inside. */
	url = NULL;
	if (tokens[index].type == CSS_TOKEN_STRING || tokens[index].type == CSS_TOKEN_URL) {
		url = vm_atom_from_units(heap, tokens[index].text, tokens[index].length);
		if (url == NULL)
			return ENOMEM;
	} else if (tokens[index].type == CSS_TOKEN_FUNCTION) {
		is_url = css_ident_equal(&tokens[index], "url");
		index++;
		while (index < count && tokens[index].type == CSS_TOKEN_WHITESPACE)
			index++;
		if (is_url && index < count && tokens[index].type == CSS_TOKEN_STRING) {
			url = vm_atom_from_units(heap, tokens[index].text, tokens[index].length);
			if (url == NULL)
				return ENOMEM;
		}
	}

	/* Anything else names no sheet. */
	if (url == NULL)
		return EINVAL;

	/* Keeps the URL. */
	error = wb_vector_push(imports, &url);
	if (error != 0)
		return ENOMEM;

	/* Succeeded: the import is recorded. */
	return 0;
}

/* Parses one qualified rule and appends it to the list; returns EINVAL for a rule that is dropped. */
static int
parser_add_rule(
	struct vm_heap *heap,
	struct css_sheet *sheet,
	struct wb_vector *rules,
	struct token_range prelude,
	struct token_range block)
{
	struct css_rule rule;
	int error;

	/* Reads the selectors; a selector list that does not parse drops the rule. */
	memset(&rule, 0, sizeof(rule));
	error = parser_selectors(heap, &sheet->arena, prelude, &rule.selectors, &rule.selector_count);
	if (error != 0)
		return error;

	/* Reads the declarations. */
	error = parser_declarations(heap, &sheet->arena, block, &rule.declarations, &rule.declaration_count);
	if (error != 0)
		return error;

	/* Appends the rule. */
	error = wb_vector_push(rules, &rule);
	if (error != 0)
		return ENOMEM;

	/* Succeeded: the rule is the sheet's. */
	return 0;
}

/* Parses a selector list (comma separated). */
static int
parser_selectors(
	struct vm_heap *heap,
	struct wb_arena *arena,
	struct token_range prelude,
	struct css_selector **selectors,
	size_t *count)
{
	struct token_range one;
	struct css_selector *list;
	size_t start;
	size_t index;
	size_t commas;
	size_t made;
	int error;

	/* Counts the selectors to size the list. */
	commas = 0;
	for (index = 0; index < prelude.count; index++) {
		if (prelude.tokens[index].type == CSS_TOKEN_COMMA)
			commas++;
	}

	/* Makes room for them. */
	list = wb_arena_zalloc(arena, (commas + 1U) * sizeof(*list));
	if (list == NULL)
		return ENOMEM;

	/* Parses each selector between the commas. */
	made = 0;
	start = 0;
	for (index = 0; index <= prelude.count; index++) {
		if (index < prelude.count && prelude.tokens[index].type != CSS_TOKEN_COMMA)
			continue;
		one.tokens = prelude.tokens + start;
		one.count = index - start;
		one = parser_trim(one);
		error = parser_selector(heap, arena, one, &list[made]);
		if (error != 0)
			return error;
		made++;
		start = index + 1U;
	}

	/* Succeeded: the list holds every selector. */
	*selectors = list;
	*count = made;
	return 0;
}

/* Parses one complex selector: compounds joined by combinators. */
static int
parser_selector(
	struct vm_heap *heap,
	struct wb_arena *arena,
	struct token_range tokens,
	struct css_selector *selector)
{
	struct wb_vector compounds;
	struct css_compound compound;
	size_t index;
	int combinator;
	int error;

	/* An empty selector is invalid. */
	if (tokens.count == 0)
		return EINVAL;

	/* Reads compounds and the combinators between them. */
	wb_vector_init(&compounds, sizeof(struct css_compound));
	selector->specificity = 0;
	index = 0;
	combinator = CSS_COMBINATOR_NONE;
	while (index < tokens.count) {
		/* One compound. */
		error = parser_compound(heap, arena, tokens.tokens, tokens.count, &index, &compound, &selector->specificity);
		if (error != 0) {
			wb_vector_release(&compounds);
			return error;
		}

		/* The combinator joins it to the compound before. */
		compound.combinator = combinator;
		error = wb_vector_push(&compounds, &compound);
		if (error != 0) {
			wb_vector_release(&compounds);
			return ENOMEM;
		}

		/* The combinator to the next compound: whitespace, or >, + or ~ with optional whitespace. */
		if (index >= tokens.count)
			break;
		combinator = CSS_COMBINATOR_DESCENDANT;
		while (index < tokens.count && tokens.tokens[index].type == CSS_TOKEN_WHITESPACE)
			index++;
		if (index < tokens.count && tokens.tokens[index].type == CSS_TOKEN_DELIM) {
			if (tokens.tokens[index].delim == '>') {
				combinator = CSS_COMBINATOR_CHILD;
			} else if (tokens.tokens[index].delim == '+') {
				combinator = CSS_COMBINATOR_NEXT;
			} else if (tokens.tokens[index].delim == '~') {
				combinator = CSS_COMBINATOR_SUBSEQUENT;
			}

			/* An explicit combinator may be followed by whitespace. */
			if (combinator != CSS_COMBINATOR_DESCENDANT) {
				index++;
				while (index < tokens.count && tokens.tokens[index].type == CSS_TOKEN_WHITESPACE)
					index++;
			}
		}

		/* A combinator needs a compound after it. */
		if (index >= tokens.count) {
			wb_vector_release(&compounds);
			return EINVAL;
		}
	}

	/* Moves the compounds into the arena. */
	selector->compounds = wb_arena_alloc(arena, compounds.count * sizeof(struct css_compound));
	if (selector->compounds == NULL) {
		wb_vector_release(&compounds);
		return ENOMEM;
	}

	/* Copies them. */
	memcpy(selector->compounds, compounds.items, compounds.count * sizeof(struct css_compound));
	selector->count = compounds.count;
	wb_vector_release(&compounds);

	/* Succeeded: the selector is complete. */
	return 0;
}

/* Parses one compound selector starting at *index, adding to the specificity. */
static int
parser_compound(
	struct vm_heap *heap,
	struct wb_arena *arena,
	const struct css_token *tokens,
	size_t count,
	size_t *index,
	struct css_compound *compound,
	uint32_t *specificity)
{
	struct css_simple simples[32];
	struct css_simple *simple;
	const struct css_token *token;
	size_t made;
	size_t end;
	int error;

	/* Reads simple selectors until whitespace, a combinator or the end. */
	made = 0;
	while (*index < count && made < 32U) {
		token = &tokens[*index];
		simple = &simples[made];
		memset(simple, 0, sizeof(*simple));

		/* A type selector (lower case: HTML element names are), or the universal selector. */
		if (token->type == CSS_TOKEN_IDENT && made == 0) {
			simple->kind = CSS_SIMPLE_TYPE;
			simple->name = parser_atom(heap, token, 1);
			if (simple->name == NULL)
				return ENOMEM;
			*specificity += 1U;
		} else if (token->type == CSS_TOKEN_DELIM && token->delim == '*' && made == 0) {
			simple->kind = CSS_SIMPLE_UNIVERSAL;
		} else if (token->type == CSS_TOKEN_HASH && token->hash_is_id) {
			/* An id selector. */
			simple->kind = CSS_SIMPLE_ID;
			simple->name = parser_atom(heap, token, 0);
			if (simple->name == NULL)
				return ENOMEM;
			*specificity += 1U << 16;
		} else if (token->type == CSS_TOKEN_DELIM && token->delim == '.' &&
		    *index + 1U < count && tokens[*index + 1U].type == CSS_TOKEN_IDENT) {
			/* A class selector. */
			(*index)++;
			simple->kind = CSS_SIMPLE_CLASS;
			simple->name = parser_atom(heap, &tokens[*index], 0);
			if (simple->name == NULL)
				return ENOMEM;
			*specificity += 1U << 8;
		} else if (token->type == CSS_TOKEN_OPEN_SQUARE) {
			/* An attribute selector up to its closing bracket. */
			end = parser_skip_block(tokens, count, *index);
			error = parser_attribute(heap, tokens + *index + 1U, end - *index - 2U, simple);
			if (error != 0)
				return error;
			*index = end - 1U;
			*specificity += 1U << 8;
		} else if (token->type == CSS_TOKEN_COLON) {
			/* A pseudo-class (or, with a second colon, a pseudo-element, which never matches an element). */
			(*index)++;
			if (*index < count && tokens[*index].type == CSS_TOKEN_COLON) {
				simple->kind = CSS_SIMPLE_NEVER;
				(*index)++;
			} else if (*index < count) {
				error = parser_pseudo(&tokens[*index], simple);
				if (error != 0)
					return error;
				*specificity += 1U << 8;
			} else {
				return EINVAL;
			}

			/* A functional pseudo-class's arguments are skipped. */
			if (*index < count && tokens[*index].type == CSS_TOKEN_FUNCTION)
				*index = parser_skip_block(tokens, count, *index) - 1U;
		} else {
			break;
		}

		/* The simple selector is complete. */
		made++;
		(*index)++;
	}

	/* A compound needs at least one simple selector. */
	if (made == 0)
		return EINVAL;

	/* Moves the simple selectors into the arena. */
	compound->simples = wb_arena_alloc(arena, made * sizeof(struct css_simple));
	if (compound->simples == NULL)
		return ENOMEM;
	memcpy(compound->simples, simples, made * sizeof(struct css_simple));
	compound->count = made;
	compound->combinator = CSS_COMBINATOR_NONE;

	/* Succeeded: the compound is complete. */
	return 0;
}

/* Parses the inside of an attribute selector: a name, and optionally a comparison and a value. */
static int
parser_attribute(
	struct vm_heap *heap,
	const struct css_token *tokens,
	size_t count,
	struct css_simple *simple)
{
	struct token_range range;
	const struct css_token *operator_token;
	size_t index;

	/* The name comes first. */
	range.tokens = tokens;
	range.count = count;
	range = parser_trim(range);
	if (range.count == 0 || range.tokens[0].type != CSS_TOKEN_IDENT)
		return EINVAL;
	simple->kind = CSS_SIMPLE_ATTRIBUTE;
	simple->match = CSS_MATCH_EXISTS;
	simple->name = parser_atom(heap, &range.tokens[0], 1);
	if (simple->name == NULL)
		return ENOMEM;
	index = 1;
	while (index < range.count && range.tokens[index].type == CSS_TOKEN_WHITESPACE)
		index++;
	if (index >= range.count)
		return 0;

	/* The comparison: = alone, or one of ~ | ^ $ * before =. */
	operator_token = &range.tokens[index];
	if (operator_token->type != CSS_TOKEN_DELIM)
		return EINVAL;
	switch (operator_token->delim) {
	case '=':
		simple->match = CSS_MATCH_EQUAL;
		break;
	case '~':
		simple->match = CSS_MATCH_INCLUDES;
		break;
	case '|':
		simple->match = CSS_MATCH_DASH;
		break;
	case '^':
		simple->match = CSS_MATCH_PREFIX;
		break;
	case '$':
		simple->match = CSS_MATCH_SUFFIX;
		break;
	case '*':
		simple->match = CSS_MATCH_SUBSTRING;
		break;
	default:
		return EINVAL;
	}

	/* Skips whitespace after the value. */
	index++;
	if (simple->match != CSS_MATCH_EQUAL) {
		if (index >= range.count || range.tokens[index].type != CSS_TOKEN_DELIM || range.tokens[index].delim != '=')
			return EINVAL;
		index++;
	}

	/* The value: an identifier or a string, then optionally the i or s flag. */
	while (index < range.count && range.tokens[index].type == CSS_TOKEN_WHITESPACE)
		index++;
	if (index >= range.count)
		return EINVAL;
	if (range.tokens[index].type != CSS_TOKEN_IDENT && range.tokens[index].type != CSS_TOKEN_STRING)
		return EINVAL;
	simple->value = parser_atom(heap, &range.tokens[index], 0);
	if (simple->value == NULL)
		return ENOMEM;
	index++;
	while (index < range.count && range.tokens[index].type == CSS_TOKEN_WHITESPACE)
		index++;
	if (index < range.count && range.tokens[index].type == CSS_TOKEN_IDENT)
		simple->case_insensitive = css_ident_equal(&range.tokens[index], "i");

	/* Succeeded: the attribute selector is complete. */
	return 0;
}

/* Reads a pseudo-class name; one this pass does not know never matches. */
static int
parser_pseudo(
	const struct css_token *token,
	struct css_simple *simple)
{
	size_t index;
	int same;

	/* Only identifiers and functions name pseudo-classes. */
	if (token->type != CSS_TOKEN_IDENT && token->type != CSS_TOKEN_FUNCTION)
		return EINVAL;

	/* The pseudo-classes this pass evaluates; a function never matches yet. */
	simple->kind = CSS_SIMPLE_PSEUDO_CLASS;
	simple->pseudo = CSS_PSEUDO_NEVER;
	if (token->type != CSS_TOKEN_IDENT)
		return 0;

	/* Looks the name up. */
	for (index = 0; parser_pseudo_names[index].name != NULL; index++) {
		same = css_ident_equal(token, parser_pseudo_names[index].name);
		if (same) {
			simple->pseudo = parser_pseudo_names[index].pseudo;
			break;
		}
	}

	/* Succeeded: the pseudo-class is known or never matches. */
	return 0;
}

/* Parses a declaration block into declarations (shorthands expanded). */
static int
parser_declarations(
	struct vm_heap *heap,
	struct wb_arena *arena,
	struct token_range block,
	struct css_declaration **declarations,
	size_t *count)
{
	struct css_declaration expanded[PARSER_EXPANSION_MAX];
	struct wb_vector list;
	struct token_range value;
	const struct css_token *name;
	size_t index;
	size_t start;
	size_t end;
	size_t made;
	size_t item;
	int important;
	int last_important;
	int error;

	/* Reads each declaration up to its semicolon. */
	wb_vector_init(&list, sizeof(struct css_declaration));
	index = 0;
	while (index < block.count) {
		/* Skips whitespace and stray semicolons. */
		if (block.tokens[index].type == CSS_TOKEN_WHITESPACE || block.tokens[index].type == CSS_TOKEN_SEMICOLON) {
			index++;
			continue;
		}

		/* The declaration runs to the next semicolon outside blocks. */
		start = index;
		while (index < block.count && block.tokens[index].type != CSS_TOKEN_SEMICOLON) {
			if (block.tokens[index].type == CSS_TOKEN_OPEN_PAREN || block.tokens[index].type == CSS_TOKEN_OPEN_SQUARE ||
			    block.tokens[index].type == CSS_TOKEN_FUNCTION || block.tokens[index].type == CSS_TOKEN_OPEN_CURLY) {
				index = parser_skip_block(block.tokens, block.count, index);
				continue;
			}

			/* Anything else belongs to the declaration. */
			index++;
		}

		/* The declaration ends here. */
		end = index;

		/* A name, a colon and a value. */
		if (block.tokens[start].type != CSS_TOKEN_IDENT)
			continue;
		name = &block.tokens[start];
		start++;
		while (start < end && block.tokens[start].type == CSS_TOKEN_WHITESPACE)
			start++;
		if (start >= end || block.tokens[start].type != CSS_TOKEN_COLON)
			continue;
		value.tokens = block.tokens + start + 1U;
		value.count = end - start - 1U;
		value = parser_trim(value);

		/* !important at the end. */
		important = 0;
		last_important = 0;
		if (value.count >= 2U && value.tokens[value.count - 1U].type == CSS_TOKEN_IDENT)
			last_important = css_ident_equal(&value.tokens[value.count - 1U], "important");
		if (last_important) {
			item = value.count - 2U;
			while (item > 0 && value.tokens[item].type == CSS_TOKEN_WHITESPACE)
				item--;
			if (value.tokens[item].type == CSS_TOKEN_DELIM && value.tokens[item].delim == '!') {
				important = 1;
				value.count = item;
				value = parser_trim(value);
			}
		}

		/* Parses the value into one or more declarations (an invalid one is dropped). */
		made = 0;
		error = css_parse_value(heap, value.tokens, value.count, name, expanded, &made, PARSER_EXPANSION_MAX);
		if (error == ENOMEM) {
			wb_vector_release(&list);
			return error;
		}

		/* Keeps each longhand with the importance. */
		for (item = 0; item < made; item++) {
			expanded[item].important = important;
			error = wb_vector_push(&list, &expanded[item]);
			if (error != 0) {
				wb_vector_release(&list);
				return ENOMEM;
			}
		}
	}

	/* Moves the declarations into the arena. */
	*declarations = NULL;
	*count = list.count;
	if (list.count != 0) {
		*declarations = wb_arena_alloc(arena, list.count * sizeof(struct css_declaration));
		if (*declarations == NULL) {
			wb_vector_release(&list);
			return ENOMEM;
		}

		/* Copies them. */
		memcpy(*declarations, list.items, list.count * sizeof(struct css_declaration));
	}

	/* The list is no longer needed. */
	wb_vector_release(&list);

	/* Succeeded: the declarations are in the arena. */
	return 0;
}

/* Interns a token's text as an atom, folding ASCII upper case when asked. */
static struct vm_string *
parser_atom(
	struct vm_heap *heap,
	const struct css_token *token,
	int lower)
{
	struct vm_string *atom;
	uint16_t folded[256];
	size_t index;

	/* Long names and names kept as they are are interned directly. */
	if (!lower || token->length > 256U) {
		atom = vm_atom_from_units(heap, token->text, token->length);
		return atom;
	}

	/* Folds ASCII upper case and interns the folded name. */
	for (index = 0; index < token->length; index++) {
		folded[index] = token->text[index];
		if (folded[index] >= 'A' && folded[index] <= 'Z')
			folded[index] = (uint16_t)(folded[index] + 0x20U);
	}

	/* Interns the folded name. */
	atom = vm_atom_from_units(heap, folded, token->length);

	/* Reports the atom (NULL when memory ran out). */
	return atom;
}

/* Drops the whitespace tokens at both ends of a range. */
static struct token_range
parser_trim(
	struct token_range range)
{
	/* Leading whitespace. */
	while (range.count > 0 && range.tokens[0].type == CSS_TOKEN_WHITESPACE) {
		range.tokens++;
		range.count--;
	}

	/* Trailing whitespace. */
	while (range.count > 0 && range.tokens[range.count - 1U].type == CSS_TOKEN_WHITESPACE)
		range.count--;

	/* Reports the trimmed range. */
	return range;
}
