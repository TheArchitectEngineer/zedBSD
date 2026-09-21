/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Declares the zedBSD userland shell parser interface.
 */

#ifndef KERN_USERLAND_SH_PARSER_H
#define KERN_USERLAND_SH_PARSER_H

#include "userland/base/sh/lexer.h"

#include <stddef.h>

/*
 * The shape of a command, without its words.
 *
 * A simple command is kept as the range of tokens it was written with
 * rather than as expanded words, because the body of a loop is written
 * once and run many times: expanding it when it is parsed would fix the
 * values it saw on the first pass.  Expansion therefore belongs to
 * execution, and the parser records only where each command's words are.
 */
enum sh_node_kind {
	SH_NODE_SIMPLE,		/* One pipeline of simple commands. */
	SH_NODE_LIST,		/* Commands joined by ; & && || or newline. */
	SH_NODE_PIPELINE,	/* Commands joined by |, at least one compound. */
	SH_NODE_IF,
	SH_NODE_WHILE,
	SH_NODE_UNTIL,
	SH_NODE_FOR,
	SH_NODE_CASE,
	SH_NODE_GROUP,		/* { ...; } in this shell. */
	SH_NODE_SUBSHELL,	/* ( ... ) in a shell of its own. */
	SH_NODE_FUNCTION
};

/* How a command in a list is joined to the one before it. */
enum sh_node_join {
	SH_JOIN_SEQUENTIAL,	/* ; or a newline. */
	SH_JOIN_BACKGROUND,	/* & */
	SH_JOIN_AND,		/* && */
	SH_JOIN_OR		/* || */
};

struct sh_node;

/*
 * One entry of a list, with the operator that introduced it.
 *
 * Whether the command is to be run in the background is a property of the
 * command itself rather than of what follows, because an ampersand may be
 * the last thing written; it is therefore recorded here and not read from
 * the entry that comes next.
 */
struct sh_list_entry {
	enum sh_node_join join;
	int background;
	struct sh_node *node;
};

/* One arm of a case command: the patterns and what they select. */
struct sh_case_arm {
	size_t *pattern_first;	/* Token index of each pattern word. */
	size_t pattern_count;
	struct sh_node *body;
};

struct sh_node {
	enum sh_node_kind kind;

	/* Set when the whole command is to be negated by a leading !. */
	int negated;

	/* Redirections written after a compound command, as a token range. */
	size_t redirect_first;
	size_t redirect_last;

	union {
		/* SH_NODE_SIMPLE: the tokens of one pipeline. */
		struct {
			size_t first;
			size_t last;
		} simple;

		/* SH_NODE_LIST */
		struct {
			struct sh_list_entry *entries;
			size_t count;
		} list;

		/* SH_NODE_PIPELINE */
		struct {
			struct sh_node **commands;
			size_t count;
		} pipeline;

		/* SH_NODE_IF: conditions and their bodies, then the else. */
		struct {
			struct sh_node **conditions;
			struct sh_node **bodies;
			size_t count;
			struct sh_node *otherwise;
		} branch;

		/* SH_NODE_WHILE and SH_NODE_UNTIL */
		struct {
			struct sh_node *condition;
			struct sh_node *body;
		} loop;

		/*
		 * SH_NODE_FOR.  The words are token indices; a for with no
		 * "in" walks the positional parameters instead, which is
		 * what absent marks.
		 */
		struct {
			size_t name;
			size_t *words;
			size_t word_count;
			int words_absent;
			struct sh_node *body;
		} iterate;

		/* SH_NODE_CASE */
		struct {
			size_t word;
			struct sh_case_arm *arms;
			size_t arm_count;
		} select;

		/* SH_NODE_GROUP and SH_NODE_SUBSHELL */
		struct sh_node *body;

		/* SH_NODE_FUNCTION */
		struct {
			size_t name;
			struct sh_node *body;
		} function;
	} u;
};

/*
 * Builds the tree for a whole input.
 *
 * A construct that has been opened and not closed is not an error: the
 * caller is told the input is incomplete so that it can read more, which is
 * what lets a command be written across several lines.  An input that holds
 * no command at all succeeds with no tree.
 */
int sh_parse(const struct sh_token_list *, struct sh_node **,
	     const char **error_text, int *incomplete);
void sh_node_free(struct sh_node *);

#endif
