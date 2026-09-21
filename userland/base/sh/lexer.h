/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Declares the zedBSD userland lexer interface.
 */

#ifndef KERN_USERLAND_SH_LEXER_H
#define KERN_USERLAND_SH_LEXER_H

#include <stddef.h>

/* How many here-documents one line may open. */
#define SH_HEREDOC_MAX 8

enum sh_token_type {
	SH_TOKEN_WORD,
	SH_TOKEN_SEMI,

	/* The end of one arm of a case command. */
	SH_TOKEN_DSEMI,
	SH_TOKEN_AMP,
	SH_TOKEN_AND_IF,
	SH_TOKEN_OR_IF,
	SH_TOKEN_PIPE,
	SH_TOKEN_INPUT,
	SH_TOKEN_OUTPUT,
	SH_TOKEN_APPEND,

	/*
	 * A newline ends a command as a semicolon does, but it is a separate
	 * token because the places it may appear are not the same: it is
	 * allowed, and ignored, after the words that open a compound command.
	 */
	SH_TOKEN_NEWLINE,

	/* Grouping, and the parentheses a case pattern and a function use. */
	SH_TOKEN_LPAREN,
	SH_TOKEN_RPAREN,

	/* A body that follows in the input rather than naming a file. */
	SH_TOKEN_DLESS,
	SH_TOKEN_DLESSDASH,

	/* Redirections that name another descriptor instead of a file. */
	SH_TOKEN_LESSAND,
	SH_TOKEN_GREATAND,
	SH_TOKEN_LESSGREAT,

	/* A truncating redirection that overrides the noclobber setting. */
	SH_TOKEN_CLOBBER,
	SH_TOKEN_END
};

enum sh_quote_type {
	SH_QUOTE_UNQUOTED,
	SH_QUOTE_SINGLE,
	SH_QUOTE_DOUBLE,
	SH_QUOTE_ESCAPED
};

struct sh_token {
	enum sh_token_type type;
	char *text;
	unsigned char *quote;
	size_t length;

	/*
	 * The descriptor a redirection was written with, as in the 2 of
	 * `2>file', or -1 where none was.  A number counts as part of the
	 * redirection only when nothing separates it from the operator,
	 * which is what tells `2>file' from `echo 2 >file'.
	 */
	int io_number;
};

struct sh_token_list {
	struct sh_token *tokens;
	size_t count;
};

/* Returns zero and leaves error_text pointing at a static diagnostic on error.
 */
int sh_lex(const char *, struct sh_token_list *, const char **error_text);
void sh_tokens_free(struct sh_token_list *);

#endif
