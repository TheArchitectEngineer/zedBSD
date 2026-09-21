/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Implements the zedBSD userland shell parser component.
 *
 * The grammar is the one POSIX describes, read by recursive descent.  What
 * it produces is a tree of the shape of the commands, with the words left
 * where they were written: a loop body is written once and run many times,
 * so the words have to be expanded each time round rather than once here.
 *
 * A word is a reserved word only where a command may begin.  That is why
 * `echo done' prints a word and `done' ends a loop, and why the check is
 * made against the position rather than against the text alone.
 */

#include "userland/base/sh/parser.h"

#include <stdlib.h>
#include <string.h>

struct parser {
	const struct sh_token_list *list;
	size_t index;
	const char *error;
	int incomplete;
};

static struct sh_node *parse_list(struct parser *parser, int stop_on_reserved);
static struct sh_node *parse_and_or(struct parser *parser);
static struct sh_node *parse_pipeline_node(struct parser *parser);
static struct sh_node *parse_command(struct parser *parser);
static struct sh_node *parse_compound(struct parser *parser);
static struct sh_node *parse_if(struct parser *parser);
static struct sh_node *parse_loop(struct parser *parser, enum sh_node_kind kind);
static struct sh_node *parse_for(struct parser *parser);
static struct sh_node *parse_case(struct parser *parser);
static struct sh_node *parse_group(struct parser *parser, enum sh_node_kind kind);
static struct sh_node *parse_simple(struct parser *parser);
static struct sh_node *node_new(enum sh_node_kind kind);

/* Supports the token type operation. */
static enum sh_token_type
token_type(
	const struct parser *parser,
	size_t index)
{
	/* Returns the computed result. */
	return index < parser->list->count ? parser->list->tokens[index].type :
	       SH_TOKEN_END;
}

/*
 * Supports the word is operation.
 *
 * Reports whether a token is exactly the given word, written plainly.  A
 * word that was quoted is not a reserved word however it is spelled, which
 * is how `"if"' is made to name a program rather than open a branch.
 */
static int
word_is(
	const struct parser *parser,
	size_t index,
	const char *text)
{
	const struct sh_token *token;
	size_t position;

	/* Handles a token that is not a plain word. */
	if (token_type(parser, index) != SH_TOKEN_WORD)
		return 0;
	token = &parser->list->tokens[index];
	if (token->text == NULL || strcmp(token->text, text) != 0)
		return 0;

	/* Process each remaining element. */
	for (position = 0; position < token->length; position++)
		if (token->quote != NULL &&
		    token->quote[position] != SH_QUOTE_UNQUOTED)
			return 0;

	/* Reports successful completion. */
	return 1;
}

/*
 * Supports the reserved closing operation.
 *
 * Reports whether the token begins a word that closes the construct it is
 * inside, so that a command list knows to stop rather than read it as
 * another command.
 */
static int
reserved_closing(
	const struct parser *parser,
	size_t index)
{
	static const char *const words[] = {
		"then", "elif", "else", "fi", "do", "done", "esac", "}", NULL
	};
	unsigned position;

	/* Process each remaining element. */
	for (position = 0; words[position] != NULL; position++)
		if (word_is(parser, index, words[position]))
			return 1;

	/* Reports that no result is available. */
	return 0;
}

/* Supports the skip newlines operation. */
static void
skip_newlines(
	struct parser *parser)
{
	/* Continue while the operation condition remains true. */
	while (token_type(parser, parser->index) == SH_TOKEN_NEWLINE)
		parser->index++;
}

/*
 * Supports the expect word operation.
 *
 * Steps over a reserved word that must be there.  When the input has simply
 * run out the caller is told it is incomplete, so that a command written
 * across several lines is read on rather than refused.
 */
static int
expect_word(
	struct parser *parser,
	const char *text)
{
	skip_newlines(parser);

	/* Handles the end of the input, which may only be unfinished. */
	if (token_type(parser, parser->index) == SH_TOKEN_END) {
		parser->incomplete = 1;
		parser->error = "unexpected end of input";
		return 0;
	}

	/* Handles a word that is not the one required. */
	if (!word_is(parser, parser->index, text)) {
		parser->error = "unexpected word";
		return 0;
	}
	parser->index++;

	/* Reports successful completion. */
	return 1;
}

/* Supports the node new operation. */
static struct sh_node *
node_new(
	enum sh_node_kind kind)
{
	struct sh_node *node;

	node = calloc(1, sizeof(*node));

	/* Handles a failed calloc operation. */
	if (node == NULL)
		return NULL;
	node->kind = kind;

	/* An absent range is one that starts after it ends. */
	node->redirect_first = 1;
	node->redirect_last = 0;

	/* Returns the computed result. */
	return node;
}

/*
 * Supports the collect redirections operation.
 *
 * A compound command may be followed by redirections that apply to the
 * whole of it.  They are recorded as a range of tokens and applied when the
 * command runs.
 */
static void
collect_redirections(
	struct parser *parser,
	struct sh_node *node)
{
	size_t first;

	first = parser->index;

	/* Continue while the operation condition remains true. */
	for (;;) {
		enum sh_token_type type = token_type(parser, parser->index);

		/* Stops at anything that does not begin a redirection. */
		if (type != SH_TOKEN_INPUT && type != SH_TOKEN_OUTPUT &&
		    type != SH_TOKEN_APPEND && type != SH_TOKEN_DLESS &&
		    type != SH_TOKEN_DLESSDASH && type != SH_TOKEN_LESSAND &&
		    type != SH_TOKEN_GREATAND && type != SH_TOKEN_LESSGREAT &&
		    type != SH_TOKEN_CLOBBER)
			break;
		parser->index++;

		/* Each one is followed by the file or descriptor it names. */
		if (token_type(parser, parser->index) == SH_TOKEN_WORD)
			parser->index++;
	}

	/* Records the range, which is empty when none were written. */
	if (parser->index != first) {
		node->redirect_first = first;
		node->redirect_last = parser->index - 1U;
	}
}

/*
 * Implements the sh parse operation.
 */
int
sh_parse(
	const struct sh_token_list *list,
	struct sh_node **result,
	const char **error_text,
	int *incomplete)
{
	struct parser parser;
	struct sh_node *node;

	memset(&parser, 0, sizeof(parser));
	parser.list = list;
	*result = NULL;
	*error_text = NULL;
	*incomplete = 0;
	skip_newlines(&parser);

	/* An input that holds nothing at all runs nothing, and is not a fault. */
	if (token_type(&parser, parser.index) == SH_TOKEN_END)
		return 1;
	node = parse_list(&parser, 0);

	/* Handles a failed parse. */
	if (node == NULL) {
		*error_text = parser.error != NULL ? parser.error :
			      "syntax error";
		*incomplete = parser.incomplete;
		return 0;
	}

	/* Handles anything left over, which belongs to nothing. */
	skip_newlines(&parser);
	if (token_type(&parser, parser.index) != SH_TOKEN_END) {
		sh_node_free(node);
		*error_text = "unexpected token";
		return 0;
	}
	*result = node;

	/* Reports successful completion. */
	return 1;
}

/*
 * Supports the parse list operation.
 *
 * Reads commands joined by the operators that sequence them.  A list inside
 * a compound command stops at the reserved word that closes it, which the
 * caller then consumes.
 */
static struct sh_node *
parse_list(
	struct parser *parser,
	int stop_on_reserved)
{
	struct sh_node *node;
	struct sh_list_entry *grown;
	struct sh_node *child;
	enum sh_node_join join;
	enum sh_token_type type;

	node = node_new(SH_NODE_LIST);

	/* Handles a failed node new operation. */
	if (node == NULL) {
		parser->error = "out of memory";
		return NULL;
	}
	join = SH_JOIN_SEQUENTIAL;

	/* Continue while the operation condition remains true. */
	for (;;) {
		skip_newlines(parser);
		type = token_type(parser, parser->index);

		/* Stops at the end, or at the word that closes the caller. */
		if (type == SH_TOKEN_END)
			break;
		if (stop_on_reserved && reserved_closing(parser, parser->index))
			break;
		if (type == SH_TOKEN_RPAREN || type == SH_TOKEN_DSEMI)
			break;
		child = parse_and_or(parser);

		/* Handles a failed parse and or operation. */
		if (child == NULL) {
			sh_node_free(node);
			return NULL;
		}
		grown = realloc(node->u.list.entries,
				(node->u.list.count + 1U) * sizeof(*grown));

		/* Handles a failed realloc operation. */
		if (grown == NULL) {
			sh_node_free(child);
			sh_node_free(node);
			parser->error = "out of memory";
			return NULL;
		}
		node->u.list.entries = grown;
		memset(&grown[node->u.list.count], 0, sizeof(*grown));
		grown[node->u.list.count].join = join;
		grown[node->u.list.count].node = child;
		node->u.list.count++;

		/* Reads the operator that joins what follows. */
		type = token_type(parser, parser->index);
		if (type == SH_TOKEN_SEMI || type == SH_TOKEN_NEWLINE) {
			parser->index++;
			join = SH_JOIN_SEQUENTIAL;
		} else if (type == SH_TOKEN_AMP) {
			parser->index++;

			/* The command just read is the one put in the background. */
			grown[node->u.list.count - 1U].background = 1;
			join = SH_JOIN_SEQUENTIAL;
		} else {
			break;
		}
	}

	/* Handles a list that holds nothing at all. */
	if (node->u.list.count == 0) {
		sh_node_free(node);

		/*
		 * A body that has not been written yet is not an empty one:
		 * the input simply stopped before it, and more of it will
		 * say what the body holds.
		 */
		if (token_type(parser, parser->index) == SH_TOKEN_END)
			parser->incomplete = 1;
		parser->error = "empty command";
		return NULL;
	}

	/* Returns the computed result. */
	return node;
}

/*
 * Supports the parse and or operation.
 */
static struct sh_node *
parse_and_or(
	struct parser *parser)
{
	struct sh_node *node;
	struct sh_list_entry *grown;
	struct sh_node *child;
	enum sh_node_join join;
	enum sh_token_type type;

	child = parse_pipeline_node(parser);

	/* Handles a failed parse pipeline operation. */
	if (child == NULL)
		return NULL;
	type = token_type(parser, parser->index);

	/* A single pipeline needs no list around it. */
	if (type != SH_TOKEN_AND_IF && type != SH_TOKEN_OR_IF)
		return child;
	node = node_new(SH_NODE_LIST);

	/* Handles a failed node new operation. */
	if (node == NULL) {
		sh_node_free(child);
		parser->error = "out of memory";
		return NULL;
	}
	join = SH_JOIN_SEQUENTIAL;

	/* Continue while the operation condition remains true. */
	for (;;) {
		grown = realloc(node->u.list.entries,
				(node->u.list.count + 1U) * sizeof(*grown));

		/* Handles a failed realloc operation. */
		if (grown == NULL) {
			sh_node_free(child);
			sh_node_free(node);
			parser->error = "out of memory";
			return NULL;
		}
		node->u.list.entries = grown;
		memset(&grown[node->u.list.count], 0, sizeof(*grown));
		grown[node->u.list.count].join = join;
		grown[node->u.list.count].node = child;
		node->u.list.count++;
		type = token_type(parser, parser->index);

		/* Stops where the run of and-or operators ends. */
		if (type != SH_TOKEN_AND_IF && type != SH_TOKEN_OR_IF)
			break;
		join = type == SH_TOKEN_AND_IF ? SH_JOIN_AND : SH_JOIN_OR;
		parser->index++;
		skip_newlines(parser);
		child = parse_pipeline_node(parser);

		/* Handles a failed parse pipeline operation. */
		if (child == NULL) {
			sh_node_free(node);
			return NULL;
		}
	}

	/* Returns the computed result. */
	return node;
}

/*
 * Supports the parse pipeline node operation.
 *
 * A pipeline of nothing but simple commands is kept as one range, so that
 * it runs through the path that already knows how to build a pipeline and
 * put it in the foreground.  One that holds a compound command cannot, and
 * becomes a pipeline of its own commands.
 */
static struct sh_node *
parse_pipeline_node(
	struct parser *parser)
{
	struct sh_node *node;
	struct sh_node **grown;
	struct sh_node *child;
	struct sh_node *first;
	int negated;
	int compound_seen;

	negated = 0;
	skip_newlines(parser);

	/* A leading exclamation turns the whole pipeline's answer round. */
	if (word_is(parser, parser->index, "!")) {
		parser->index++;
		negated = 1;
		skip_newlines(parser);
	}
	first = parse_command(parser);

	/* Handles a failed parse command operation. */
	if (first == NULL)
		return NULL;
	compound_seen = first->kind != SH_NODE_SIMPLE;

	/* A pipeline of one command is that command. */
	if (token_type(parser, parser->index) != SH_TOKEN_PIPE) {
		first->negated = negated;
		return first;
	}
	node = node_new(SH_NODE_PIPELINE);

	/* Handles a failed node new operation. */
	if (node == NULL) {
		sh_node_free(first);
		parser->error = "out of memory";
		return NULL;
	}
	node->negated = negated;
	child = first;

	/* Continue while the operation condition remains true. */
	for (;;) {
		grown = realloc(node->u.pipeline.commands,
				(node->u.pipeline.count + 1U) * sizeof(*grown));

		/* Handles a failed realloc operation. */
		if (grown == NULL) {
			sh_node_free(child);
			sh_node_free(node);
			parser->error = "out of memory";
			return NULL;
		}
		node->u.pipeline.commands = grown;
		grown[node->u.pipeline.count] = child;
		node->u.pipeline.count++;

		/* Stops where the run of pipes ends. */
		if (token_type(parser, parser->index) != SH_TOKEN_PIPE)
			break;
		parser->index++;
		skip_newlines(parser);
		child = parse_command(parser);

		/* Handles a failed parse command operation. */
		if (child == NULL) {
			sh_node_free(node);
			return NULL;
		}
		if (child->kind != SH_NODE_SIMPLE)
			compound_seen = 1;
	}

	/*
	 * With nothing but simple commands the whole run is handed on as one
	 * range, which is what the existing pipeline builder expects.
	 */
	if (!compound_seen) {
		size_t position;
		size_t low = node->u.pipeline.commands[0]->u.simple.first;
		size_t high = node->u.pipeline.commands[
		    node->u.pipeline.count - 1U]->u.simple.last;

		for (position = 0; position < node->u.pipeline.count;
		     position++)
			sh_node_free(node->u.pipeline.commands[position]);
		free(node->u.pipeline.commands);
		node->kind = SH_NODE_SIMPLE;
		node->u.simple.first = low;
		node->u.simple.last = high;
	}

	/* Returns the computed result. */
	return node;
}

/*
 * Supports the parse command operation.
 */
static struct sh_node *
parse_command(
	struct parser *parser)
{
	struct sh_node *node;

	skip_newlines(parser);
	node = parse_compound(parser);

	/* A compound command was read, or one failed to be. */
	if (node != NULL || parser->error != NULL)
		return node;

	/* Returns the computed result. */
	return parse_simple(parser);
}

/*
 * Supports the parse compound operation.
 *
 * Reports a null result with no error when the input does not begin a
 * compound command at all, which is how a simple command is reached.
 */
static struct sh_node *
parse_compound(
	struct parser *parser)
{
	struct sh_node *node;

	/* Dispatch the selected kind of compound command. */
	if (word_is(parser, parser->index, "if"))
		node = parse_if(parser);
	else if (word_is(parser, parser->index, "while"))
		node = parse_loop(parser, SH_NODE_WHILE);
	else if (word_is(parser, parser->index, "until"))
		node = parse_loop(parser, SH_NODE_UNTIL);
	else if (word_is(parser, parser->index, "for"))
		node = parse_for(parser);
	else if (word_is(parser, parser->index, "case"))
		node = parse_case(parser);
	else if (word_is(parser, parser->index, "{"))
		node = parse_group(parser, SH_NODE_GROUP);
	else if (token_type(parser, parser->index) == SH_TOKEN_LPAREN)
		node = parse_group(parser, SH_NODE_SUBSHELL);
	else if (token_type(parser, parser->index) == SH_TOKEN_WORD &&
		 token_type(parser, parser->index + 1U) == SH_TOKEN_LPAREN &&
		 token_type(parser, parser->index + 2U) == SH_TOKEN_RPAREN) {
		/* A name followed by an empty pair of parentheses defines a function. */
		size_t name = parser->index;

		parser->index += 3U;
		skip_newlines(parser);
		node = node_new(SH_NODE_FUNCTION);
		if (node == NULL) {
			parser->error = "out of memory";
			return NULL;
		}
		node->u.function.name = name;
		node->u.function.body = parse_command(parser);
		if (node->u.function.body == NULL) {
			sh_node_free(node);
			return NULL;
		}
	} else {
		return NULL;
	}

	/* Handles a failed parse of the command that was begun. */
	if (node == NULL)
		return NULL;

	/* Redirections written after it apply to the whole of it. */
	if (node->kind != SH_NODE_FUNCTION)
		collect_redirections(parser, node);

	/* Returns the computed result. */
	return node;
}

/*
 * Supports the parse if operation.
 */
static struct sh_node *
parse_if(
	struct parser *parser)
{
	struct sh_node *node;
	struct sh_node **grown;
	struct sh_node *condition;
	struct sh_node *body;

	parser->index++;
	node = node_new(SH_NODE_IF);

	/* Handles a failed node new operation. */
	if (node == NULL) {
		parser->error = "out of memory";
		return NULL;
	}

	/* Continue while the operation condition remains true. */
	for (;;) {
		condition = parse_list(parser, 1);

		/* Handles a failed parse list operation. */
		if (condition == NULL) {
			sh_node_free(node);
			return NULL;
		}

		/* Handles a failed expect word operation. */
		if (!expect_word(parser, "then")) {
			sh_node_free(condition);
			sh_node_free(node);
			return NULL;
		}
		body = parse_list(parser, 1);

		/* Handles a failed parse list operation. */
		if (body == NULL) {
			sh_node_free(condition);
			sh_node_free(node);
			return NULL;
		}
		grown = realloc(node->u.branch.conditions,
				(node->u.branch.count + 1U) * sizeof(*grown));
		if (grown != NULL)
			node->u.branch.conditions = grown;
		else {
			sh_node_free(condition);
			sh_node_free(body);
			sh_node_free(node);
			parser->error = "out of memory";
			return NULL;
		}
		grown = realloc(node->u.branch.bodies,
				(node->u.branch.count + 1U) * sizeof(*grown));
		if (grown != NULL)
			node->u.branch.bodies = grown;
		else {
			sh_node_free(condition);
			sh_node_free(body);
			sh_node_free(node);
			parser->error = "out of memory";
			return NULL;
		}
		node->u.branch.conditions[node->u.branch.count] = condition;
		node->u.branch.bodies[node->u.branch.count] = body;
		node->u.branch.count++;
		skip_newlines(parser);

		/* Another condition follows an elif. */
		if (word_is(parser, parser->index, "elif")) {
			parser->index++;
			continue;
		}
		break;
	}

	/* An else supplies what is done when no condition held. */
	if (word_is(parser, parser->index, "else")) {
		parser->index++;
		node->u.branch.otherwise = parse_list(parser, 1);

		/* Handles a failed parse list operation. */
		if (node->u.branch.otherwise == NULL) {
			sh_node_free(node);
			return NULL;
		}
	}

	/* Handles a failed expect word operation. */
	if (!expect_word(parser, "fi")) {
		sh_node_free(node);
		return NULL;
	}

	/* Returns the computed result. */
	return node;
}

/*
 * Supports the parse loop operation.
 */
static struct sh_node *
parse_loop(
	struct parser *parser,
	enum sh_node_kind kind)
{
	struct sh_node *node;

	parser->index++;
	node = node_new(kind);

	/* Handles a failed node new operation. */
	if (node == NULL) {
		parser->error = "out of memory";
		return NULL;
	}
	node->u.loop.condition = parse_list(parser, 1);

	/* Handles a failed parse list operation. */
	if (node->u.loop.condition == NULL ||
	    !expect_word(parser, "do")) {
		sh_node_free(node);
		return NULL;
	}
	node->u.loop.body = parse_list(parser, 1);

	/* Handles a failed parse list operation. */
	if (node->u.loop.body == NULL || !expect_word(parser, "done")) {
		sh_node_free(node);
		return NULL;
	}

	/* Returns the computed result. */
	return node;
}

/*
 * Supports the parse for operation.
 */
static struct sh_node *
parse_for(
	struct parser *parser)
{
	struct sh_node *node;
	size_t *grown;

	parser->index++;
	node = node_new(SH_NODE_FOR);

	/* Handles a failed node new operation. */
	if (node == NULL) {
		parser->error = "out of memory";
		return NULL;
	}

	/* Handles a for that names no variable to set. */
	if (token_type(parser, parser->index) != SH_TOKEN_WORD) {
		sh_node_free(node);
		parser->error = "for requires a name";
		return NULL;
	}
	node->u.iterate.name = parser->index;
	parser->index++;
	skip_newlines(parser);

	/* Without an in, the loop walks the positional parameters. */
	if (!word_is(parser, parser->index, "in")) {
		node->u.iterate.words_absent = 1;
	} else {
		parser->index++;

		/* Process each remaining element. */
		while (token_type(parser, parser->index) == SH_TOKEN_WORD &&
		       !reserved_closing(parser, parser->index)) {
			grown = realloc(node->u.iterate.words,
					(node->u.iterate.word_count + 1U) *
					sizeof(*grown));

			/* Handles a failed realloc operation. */
			if (grown == NULL) {
				sh_node_free(node);
				parser->error = "out of memory";
				return NULL;
			}
			node->u.iterate.words = grown;
			grown[node->u.iterate.word_count] = parser->index;
			node->u.iterate.word_count++;
			parser->index++;
		}
	}

	/*
	 * What the loop walks is ended by a semicolon or by the end of the
	 * line, and a for that named no words ends the same way, so the
	 * semicolon is read here rather than in one arm alone.
	 */
	if (token_type(parser, parser->index) == SH_TOKEN_SEMI)
		parser->index++;

	/* Handles a failed expect word operation. */
	if (!expect_word(parser, "do")) {
		sh_node_free(node);
		return NULL;
	}
	node->u.iterate.body = parse_list(parser, 1);

	/* Handles a failed parse list operation. */
	if (node->u.iterate.body == NULL || !expect_word(parser, "done")) {
		sh_node_free(node);
		return NULL;
	}

	/* Returns the computed result. */
	return node;
}

/*
 * Supports the parse case operation.
 */
static struct sh_node *
parse_case(
	struct parser *parser)
{
	struct sh_node *node;
	struct sh_case_arm *grown;
	struct sh_case_arm *arm;
	size_t *patterns;

	parser->index++;
	node = node_new(SH_NODE_CASE);

	/* Handles a failed node new operation. */
	if (node == NULL) {
		parser->error = "out of memory";
		return NULL;
	}

	/* Handles a case that names nothing to match. */
	if (token_type(parser, parser->index) != SH_TOKEN_WORD) {
		sh_node_free(node);
		parser->error = "case requires a word";
		return NULL;
	}
	node->u.select.word = parser->index;
	parser->index++;
	skip_newlines(parser);

	/* Handles a failed expect word operation. */
	if (!expect_word(parser, "in")) {
		sh_node_free(node);
		return NULL;
	}
	skip_newlines(parser);

	/* Continue while the operation condition remains true. */
	while (!word_is(parser, parser->index, "esac")) {
		/* Handles the end of the input inside the command. */
		if (token_type(parser, parser->index) == SH_TOKEN_END) {
			sh_node_free(node);
			parser->incomplete = 1;
			parser->error = "unexpected end of input";
			return NULL;
		}
		grown = realloc(node->u.select.arms,
				(node->u.select.arm_count + 1U) *
				sizeof(*grown));

		/* Handles a failed realloc operation. */
		if (grown == NULL) {
			sh_node_free(node);
			parser->error = "out of memory";
			return NULL;
		}
		node->u.select.arms = grown;
		arm = &grown[node->u.select.arm_count];
		memset(arm, 0, sizeof(*arm));
		node->u.select.arm_count++;

		/* An arm may be written with a parenthesis before it. */
		if (token_type(parser, parser->index) == SH_TOKEN_LPAREN)
			parser->index++;

		/* Process each remaining element. */
		for (;;) {
			/* Handles an arm with no pattern at all. */
			if (token_type(parser, parser->index) !=
			    SH_TOKEN_WORD) {
				sh_node_free(node);
				parser->error = "case requires a pattern";
				return NULL;
			}
			patterns = realloc(arm->pattern_first,
					   (arm->pattern_count + 1U) *
					   sizeof(*patterns));

			/* Handles a failed realloc operation. */
			if (patterns == NULL) {
				sh_node_free(node);
				parser->error = "out of memory";
				return NULL;
			}
			arm->pattern_first = patterns;
			patterns[arm->pattern_count] = parser->index;
			arm->pattern_count++;
			parser->index++;

			/* Several patterns may select the same arm. */
			if (token_type(parser, parser->index) !=
			    SH_TOKEN_PIPE)
				break;
			parser->index++;
		}

		/* Handles an arm whose patterns are not closed. */
		if (token_type(parser, parser->index) != SH_TOKEN_RPAREN) {
			sh_node_free(node);
			parser->error = "case pattern requires )";
			return NULL;
		}
		parser->index++;
		skip_newlines(parser);

		/* An arm may select nothing at all. */
		if (word_is(parser, parser->index, "esac") ||
		    token_type(parser, parser->index) == SH_TOKEN_DSEMI) {
			arm->body = NULL;
		} else {
			arm->body = parse_list(parser, 1);

			/* Handles a failed parse list operation. */
			if (arm->body == NULL) {
				sh_node_free(node);
				return NULL;
			}
		}

		/* The arm ends with two semicolons, or with the esac. */
		if (token_type(parser, parser->index) == SH_TOKEN_DSEMI)
			parser->index++;
		skip_newlines(parser);
	}

	/* Handles a failed expect word operation. */
	if (!expect_word(parser, "esac")) {
		sh_node_free(node);
		return NULL;
	}

	/* Returns the computed result. */
	return node;
}

/*
 * Supports the parse group operation.
 */
static struct sh_node *
parse_group(
	struct parser *parser,
	enum sh_node_kind kind)
{
	struct sh_node *node;

	parser->index++;
	node = node_new(kind);

	/* Handles a failed node new operation. */
	if (node == NULL) {
		parser->error = "out of memory";
		return NULL;
	}
	node->u.body = parse_list(parser, 1);

	/* Handles a failed parse list operation. */
	if (node->u.body == NULL) {
		sh_node_free(node);
		return NULL;
	}

	/* A group in this shell is closed by a brace, a subshell by a paren. */
	if (kind == SH_NODE_GROUP) {
		if (!expect_word(parser, "}")) {
			sh_node_free(node);
			return NULL;
		}
	} else {
		skip_newlines(parser);
		if (token_type(parser, parser->index) == SH_TOKEN_END) {
			sh_node_free(node);
			parser->incomplete = 1;
			parser->error = "unexpected end of input";
			return NULL;
		}
		if (token_type(parser, parser->index) != SH_TOKEN_RPAREN) {
			sh_node_free(node);
			parser->error = "subshell requires )";
			return NULL;
		}
		parser->index++;
	}

	/* Returns the computed result. */
	return node;
}

/*
 * Supports the parse simple operation.
 *
 * Records where the words of one simple command are, without looking at
 * them.  The command ends at the first operator that separates commands;
 * the words between are whatever was written, reserved or not, because a
 * reserved word is only reserved where a command may begin.
 */
static struct sh_node *
parse_simple(
	struct parser *parser)
{
	struct sh_node *node;
	size_t first;
	enum sh_token_type type;

	first = parser->index;

	/* Continue while the operation condition remains true. */
	for (;;) {
		type = token_type(parser, parser->index);
		if (type == SH_TOKEN_END || type == SH_TOKEN_SEMI ||
		    type == SH_TOKEN_DSEMI || type == SH_TOKEN_AMP ||
		    type == SH_TOKEN_AND_IF || type == SH_TOKEN_OR_IF ||
		    type == SH_TOKEN_NEWLINE || type == SH_TOKEN_PIPE ||
		    type == SH_TOKEN_RPAREN)
			break;
		parser->index++;
	}

	/* Handles a command with no words in it. */
	if (parser->index == first) {
		parser->error = "expected a command";
		return NULL;
	}
	node = node_new(SH_NODE_SIMPLE);

	/* Handles a failed node new operation. */
	if (node == NULL) {
		parser->error = "out of memory";
		return NULL;
	}
	node->u.simple.first = first;
	node->u.simple.last = parser->index - 1U;

	/* Returns the computed result. */
	return node;
}

/*
 * Implements the sh node free operation.
 */
void
sh_node_free(
	struct sh_node *node)
{
	size_t index;

	/* Handles the node availability. */
	if (node == NULL)
		return;

	/* Dispatch the selected kind of command. */
	switch (node->kind) {
	case SH_NODE_LIST:
		for (index = 0; index < node->u.list.count; index++)
			sh_node_free(node->u.list.entries[index].node);
		free(node->u.list.entries);
		break;
	case SH_NODE_PIPELINE:
		for (index = 0; index < node->u.pipeline.count; index++)
			sh_node_free(node->u.pipeline.commands[index]);
		free(node->u.pipeline.commands);
		break;
	case SH_NODE_IF:
		for (index = 0; index < node->u.branch.count; index++) {
			sh_node_free(node->u.branch.conditions[index]);
			sh_node_free(node->u.branch.bodies[index]);
		}
		free(node->u.branch.conditions);
		free(node->u.branch.bodies);
		sh_node_free(node->u.branch.otherwise);
		break;
	case SH_NODE_WHILE:
	case SH_NODE_UNTIL:
		sh_node_free(node->u.loop.condition);
		sh_node_free(node->u.loop.body);
		break;
	case SH_NODE_FOR:
		free(node->u.iterate.words);
		sh_node_free(node->u.iterate.body);
		break;
	case SH_NODE_CASE:
		for (index = 0; index < node->u.select.arm_count; index++) {
			free(node->u.select.arms[index].pattern_first);
			sh_node_free(node->u.select.arms[index].body);
		}
		free(node->u.select.arms);
		break;
	case SH_NODE_GROUP:
	case SH_NODE_SUBSHELL:
		sh_node_free(node->u.body);
		break;
	case SH_NODE_FUNCTION:
		sh_node_free(node->u.function.body);
		break;
	case SH_NODE_SIMPLE:
	default:
		break;
	}
	free(node);
}
