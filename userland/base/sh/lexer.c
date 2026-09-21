/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Implements the zedBSD userland lexer component.
 */

#include "userland/base/sh/lexer.h"

#include <stdlib.h>
#include <string.h>

struct word_buffer {
	char *data;
	unsigned char *quote;
	size_t length;
	size_t capacity;
};

static int is_operator(char value);
static int token_is_number(const struct sh_token *token);
static int copy_substitution(const char **cursor, struct word_buffer *word, unsigned char mark, const char **error_text);
static int heredoc_collect(const char **cursor, struct sh_token_list *list, size_t token, int strip, const char **error_text);
static int lex_word(const char **cursor, struct sh_token_list *list, const char **error_text);
static int word_append(struct word_buffer *word, char value, enum sh_quote_type quote);
static int word_reserve(struct word_buffer *word);
static int token_append(struct sh_token_list *list, enum sh_token_type type, char *text, unsigned char *quote, size_t length);

/*
 * Implements the sh lex operation.
 */
int
sh_lex(
	const char *text,
	struct sh_token_list *list,
	const char **error_text)
{
	enum sh_token_type type;
	const char *word_end;
	size_t pending[SH_HEREDOC_MAX];
	int pending_strip[SH_HEREDOC_MAX];
	unsigned pending_count;
	unsigned index;
	int io_number;

	memset(list, 0, sizeof(*list));
	word_end = NULL;
	pending_count = 0;

	/* Continue while the operation condition remains true. */
	*error_text = NULL;
	while (*text != '\0') {
		/* Continue while the operation condition remains true. */
		while (*text == ' ' || *text == '\t')
			text++;

		/* A comment runs to the end of its line, not of the input. */
		if (*text == '#') {
			while (*text != '\0' && *text != '\n')
				text++;
			continue;
		}

		/* Validates the current text. */
		if (*text == '\0')
			break;

		/* A line continuation joins the two lines into one. */
		if (*text == '\\' && text[1] == '\n') {
			text += 2;
			continue;
		}

		/* Handles the end of a line, which ends a command. */
		if (*text == '\n') {
			text++;
			word_end = NULL;
			if (!token_append(list, SH_TOKEN_NEWLINE, NULL, NULL,
					  0)) {
				*error_text = "out of memory";
				goto failed;
			}

			/*
			 * The body of a here-document is the text that
			 * follows the line the redirection was written on,
			 * so it is read here rather than where the operator
			 * was seen.
			 */
			for (index = 0; index < pending_count; index++) {
				/* Handles a failed heredoc collect operation. */
				if (!heredoc_collect(&text, list,
						     pending[index],
						     pending_strip[index],
						     error_text))
					goto failed;
			}
			pending_count = 0;
			continue;
		}

		/* Handles a failed operator operation. */
		if (!is_operator(*text)) {
			/* Handles an operation failure. */
			if (!lex_word(&text, list, error_text))
				goto failed;
			word_end = text;
			continue;
		}
		io_number = -1;

		/*
		 * A number written against a redirection names the
		 * descriptor it changes.  Nothing may come between them: with
		 * a blank the number is a word of its own, and an argument.
		 */
		if ((*text == '<' || *text == '>') && text == word_end &&
		    list->count != 0 &&
		    token_is_number(&list->tokens[list->count - 1U])) {
			io_number = atoi(list->tokens[list->count - 1U].text);
			free(list->tokens[list->count - 1U].text);
			free(list->tokens[list->count - 1U].quote);
			list->count--;
		}
		word_end = NULL;

		/* Dispatch the selected operation case. */
		switch (*text++) {
		case ';':
			/* Two of them end an arm of a case command. */
			if (*text == ';') {
				text++;
				type = SH_TOKEN_DSEMI;
			} else {
				type = SH_TOKEN_SEMI;
			}
			break;
		case '&':
			/* Validates the current text. */
			if (*text == '&') {
				text++;
				type = SH_TOKEN_AND_IF;
			} else {
				type = SH_TOKEN_AMP;
			}
			break;
		case '|':
			/* Validates the current text. */
			if (*text == '|') {
				text++;
				type = SH_TOKEN_OR_IF;
			} else {
				type = SH_TOKEN_PIPE;
			}
			break;
		case '(':
			type = SH_TOKEN_LPAREN;
			break;
		case ')':
			type = SH_TOKEN_RPAREN;
			break;
		case '<':
			/* Dispatch the selected form of the redirection. */
			if (*text == '<') {
				text++;

				/*
				 * The dash form strips the indentation from
				 * the body, so that a body inside a compound
				 * command may be written indented with it.
				 */
				if (*text == '-') {
					text++;
					type = SH_TOKEN_DLESSDASH;
				} else {
					type = SH_TOKEN_DLESS;
				}
			} else if (*text == '&') {
				text++;
				type = SH_TOKEN_LESSAND;
			} else if (*text == '>') {
				text++;
				type = SH_TOKEN_LESSGREAT;
			} else {
				type = SH_TOKEN_INPUT;
			}
			break;
		default:
			/* Dispatch the selected form of the redirection. */
			if (*text == '>') {
				text++;
				type = SH_TOKEN_APPEND;
			} else if (*text == '&') {
				text++;
				type = SH_TOKEN_GREATAND;
			} else if (*text == '|') {
				text++;
				type = SH_TOKEN_CLOBBER;
			} else {
				type = SH_TOKEN_OUTPUT;
			}
			break;
		}

		/* Handles a failed token append operation. */
		if (!token_append(list, type, NULL, NULL, 0)) {
			*error_text = "out of memory";
			goto failed;
		}
		list->tokens[list->count - 1U].io_number = io_number;

		/* A here-document leaves its body to be read at the newline. */
		if (type == SH_TOKEN_DLESS || type == SH_TOKEN_DLESSDASH) {
			/* Checks the remaining item count. */
			if (pending_count == SH_HEREDOC_MAX) {
				*error_text = "too many here-documents";
				goto failed;
			}
			pending[pending_count] = list->count - 1U;
			pending_strip[pending_count] =
			    type == SH_TOKEN_DLESSDASH;
			pending_count++;
		}
	}

	/* Handles a here-document whose body the input never reached. */
	if (pending_count != 0) {
		*error_text = "unterminated here-document";
		goto failed;
	}

	/* Handles a failed token append operation. */
	if (!token_append(list, SH_TOKEN_END, NULL, NULL, 0)) {
		*error_text = "out of memory";
		goto failed;
	}

	/* Reports operation failure. */
	return 1;
failed:
	sh_tokens_free(list);

	/* Reports successful completion. */
	return 0;
}

/*
 * Implements the sh tokens free operation.
 */
void
sh_tokens_free(
	struct sh_token_list *list)
{
	size_t index;

	/* Process each remaining element. */
	for (index = 0; index < list->count; index++)
		free(list->tokens[index].text);

	/* Process each remaining element. */
	for (index = 0; index < list->count; index++)
		free(list->tokens[index].quote);
	free(list->tokens);
	list->tokens = NULL;
	list->count = 0;
}

/* Supports the is operator operation. */
static int
is_operator(
	char value)
{
	/* Returns the computed result. */
	return value == ';' || value == '&' || value == '|' || value == '<' ||
	       value == '>' || value == '(' || value == ')';
}

/*
 * Supports the heredoc collect operation.
 *
 * Reads the body of a here-document, which runs from the line after the
 * one the redirection was written on to a line holding the delimiter and
 * nothing else.  The body is kept on the operator's own token, where the
 * word that named the delimiter still stands beside it.
 *
 * Whether what is written in the body is expanded is decided by how the
 * delimiter was written: quoted in any way, the body is taken as it
 * stands, and the marks say so by naming every character single-quoted.
 */
static int
heredoc_collect(
	const char **cursor,
	struct sh_token_list *list,
	size_t token,
	int strip,
	const char **error_text)
{
	const char *text;
	const char *line;
	const char *delimiter;
	char *body;
	char *grown;
	unsigned char *marks;
	size_t capacity;
	size_t length;
	size_t span;
	size_t index;
	size_t delimiter_length;
	int literal;

	/* Handles a redirection with no delimiter after it. */
	if (token + 1U >= list->count ||
	    list->tokens[token + 1U].type != SH_TOKEN_WORD ||
	    list->tokens[token + 1U].text == NULL) {
		*error_text = "a here-document requires a delimiter";
		return 0;
	}
	delimiter = list->tokens[token + 1U].text;
	delimiter_length = list->tokens[token + 1U].length;
	literal = 0;

	/* Process each remaining element. */
	for (index = 0; index < delimiter_length; index++)
		if (list->tokens[token + 1U].quote != NULL &&
		    list->tokens[token + 1U].quote[index] != SH_QUOTE_UNQUOTED)
			literal = 1;
	text = *cursor;
	capacity = 64;
	length = 0;
	body = malloc(capacity);

	/* Handles a failed malloc operation. */
	if (body == NULL) {
		*error_text = "out of memory";
		return 0;
	}

	/* Continue until the operation reaches a terminal state. */
	for (;;) {
		line = text;

		/* A dash on the operator takes the tabs off each line. */
		if (strip)
			while (*line == '\t')
				line++;
		span = 0;
		while (line[span] != '\0' && line[span] != '\n')
			span++;

		/* Handles the line that closes the body. */
		if (span == delimiter_length &&
		    memcmp(line, delimiter, span) == 0) {
			text = line[span] == '\n' ? line + span + 1U :
			       line + span;
			break;
		}

		/* Handles input that ended before the delimiter was seen. */
		if (line[span] == '\0') {
			free(body);
			*error_text = "unterminated here-document";
			return 0;
		}

		/* Handles a buffer with no room left. */
		while (length + span + 2U > capacity) {
			capacity *= 2U;
			grown = realloc(body, capacity);

			/* Handles a failed realloc operation. */
			if (grown == NULL) {
				free(body);
				*error_text = "out of memory";
				return 0;
			}
			body = grown;
		}
		memcpy(body + length, line, span);
		length += span;
		body[length++] = '\n';
		text = line + span + 1U;
	}
	body[length] = '\0';
	marks = malloc(length + 1U);

	/* Handles a failed malloc operation. */
	if (marks == NULL) {
		free(body);
		*error_text = "out of memory";
		return 0;
	}

	/*
	 * A backslash in an expanded body stands before the character it
	 * protects, and that character is marked as protected while the
	 * backslash itself is dropped.
	 */
	span = 0;

	/* Process each remaining element. */
	for (index = 0; index < length; index++) {
		if (!literal && body[index] == '\\' && index + 1U < length &&
		    (body[index + 1U] == '$' || body[index + 1U] == '`' ||
		     body[index + 1U] == '\\')) {
			index++;
			body[span] = body[index];
			marks[span] = SH_QUOTE_ESCAPED;
			span++;
			continue;
		}
		body[span] = body[index];
		marks[span] = literal ? SH_QUOTE_SINGLE : SH_QUOTE_DOUBLE;
		span++;
	}
	body[span] = '\0';
	marks[span] = 0;
	free(list->tokens[token].text);
	free(list->tokens[token].quote);
	list->tokens[token].text = body;
	list->tokens[token].quote = marks;
	list->tokens[token].length = span;
	*cursor = text;

	/* Reports operation failure. */
	return 1;
}

/*
 * Supports the token is number operation.
 *
 * Reports whether the word is a plain run of digits short enough to be a
 * descriptor, which is what may stand before a redirection.
 */
static int
token_is_number(
	const struct sh_token *token)
{
	size_t index;

	/* Handles a token that is not a plain word. */
	if (token->type != SH_TOKEN_WORD || token->text == NULL ||
	    token->length == 0 || token->length > 2U)
		return 0;

	/* Process each remaining element. */
	for (index = 0; index < token->length; index++) {
		/* Handles anything that is not an unquoted digit. */
		if (token->text[index] < '0' || token->text[index] > '9')
			return 0;
		if (token->quote != NULL &&
		    token->quote[index] != SH_QUOTE_UNQUOTED)
			return 0;
	}

	/* Reports successful completion. */
	return 1;
}

/*
 * Supports the copy substitution operation.
 *
 * Copies a $( ) substitution into the word as it stands, from the dollar
 * to the parenthesis that closes it.  What it holds is a command, and is
 * left alone: the quotations inside it are the command's own, and are read
 * when the command is, not now.
 */
static int
copy_substitution(
	const char **cursor,
	struct word_buffer *word,
	unsigned char mark,
	const char **error_text)
{
	const char *text;
	char inner_quote;
	char value;
	int depth;

	text = *cursor;
	depth = 1;
	inner_quote = '\0';

	/* Handles a failed word append operation. */
	if (!word_append(word, *text++, mark) ||
	    !word_append(word, *text++, mark)) {
		*error_text = "out of memory";

		/* Reports successful completion. */
		return 0;
	}

	/* Continue while the operation condition remains true. */
	while (*text != '\0' && depth != 0) {
		value = *text++;

		/* A backslash carries the character after it through. */
		if (value == '\\' && *text != '\0') {
			/* Handles a failed word append operation. */
			if (!word_append(word, value, mark) ||
			    !word_append(word, *text++, mark)) {
				*error_text = "out of memory";

				/* Reports successful completion. */
				return 0;
			}
			continue;
		}

		/* A parenthesis inside a quotation closes nothing. */
		if ((value == '\'' || value == '"') &&
		    (inner_quote == '\0' || inner_quote == value))
			inner_quote = inner_quote == '\0' ? value : '\0';

		/* Handles the inner quote condition. */
		if (inner_quote == '\0') {
			if (value == '(')
				depth++;
			else if (value == ')')
				depth--;
		}

		/* The closing parenthesis is the last thing copied. */
		if (depth == 0) {
			/* Handles a failed word append operation. */
			if (!word_append(word, value, mark)) {
				*error_text = "out of memory";

				/* Reports successful completion. */
				return 0;
			}
			break;
		}

		/* Handles a failed word append operation. */
		if (!word_append(word, value, mark)) {
			*error_text = "out of memory";

			/* Reports successful completion. */
			return 0;
		}
	}

	/* Handles a substitution that was opened and never closed. */
	if (depth != 0) {
		*error_text = "unterminated command substitution";

		/* Reports successful completion. */
		return 0;
	}
	*cursor = text;

	/* Reports operation failure. */
	return 1;
}

/* Supports the lex word operation. */
static int
lex_word(
	const char **cursor,
	struct sh_token_list *list,
	const char **error_text)
{
	int escaped;
	char quote;
	const char *text;
	struct word_buffer word = {0};
	int quoted;
	int produced;

	/* Continue while the operation condition remains true. */
	text = *cursor;
	quoted = 0;
	produced = 0;
	/*
	 * A newline ends a word as a blank does.  It is not one of the
	 * operator characters, so it is named here: what follows it is a
	 * command of its own rather than more of this word.
	 */
	while (*text != '\0' && *text != ' ' && *text != '\t' &&
	       *text != '\n' && !is_operator(*text)) {
		/* Validates the current text. */
		if (text[0] == '$' && text[1] == '(') {
			/* Handles a failed copy substitution operation. */
			if (!copy_substitution(&text, &word,
					       SH_QUOTE_UNQUOTED,
					       error_text)) {
				free(word.data);
				free(word.quote);

				/* Reports successful completion. */
				return 0;
			}
			produced = 1;
			continue;
		}

		/* Validates the current text. */
		if (*text == '\\') {
			text++;

			/* Validates the current text. */
			if (*text == '\0') {
				*error_text = "trailing backslash";
				free(word.data);
				free(word.quote);

				/* Reports successful completion. */
				return 0;
			}

			/*
			 * A backslash before a newline joins the two lines
			 * and contributes nothing of itself, which is how a
			 * long word is written across lines.
			 */
			if (*text == '\n') {
				text++;
				continue;
			}

			/* Handles a failed word append operation. */
			if (!word_append(&word, *text++, SH_QUOTE_ESCAPED))
				goto no_memory;
			produced = 1;
			continue;
		}

		/* Validates the current text. */
		if (*text == '\'' || *text == '"') {
			/* Continue while the operation condition remains true. */
			quote = *text++;
			quoted = 1;
			while (*text != '\0' && *text != quote) {
				escaped = 0;

				/*
				 * A substitution written inside a double
				 * quotation is a command of its own, and
				 * what is quoted within it is its own
				 * business: it is copied as it stands so
				 * that the quotation around it does not
				 * reach inside.
				 */
				if (quote == '"' && text[0] == '$' &&
				    text[1] == '(') {
					/* Handles a failed copy operation. */
					if (!copy_substitution(&text, &word,
							       SH_QUOTE_DOUBLE,
							       error_text)) {
						free(word.data);
						free(word.quote);

						/* Reports successful completion. */
						return 0;
					}
					produced = 1;
					continue;
				}

				/* Handles the quote condition. */
				if (quote == '"' && *text == '\\') {
					/* Validates the current text. */
					if (text[1] == '$' || text[1] == '`' ||
					    text[1] == '"' || text[1] == '\\') {
						text++;
						escaped = 1;
					} else if (text[1] == '\n') {
						text += 2;
						continue;
					} else {
						/* Handles a failed word append operation. */
						if (!word_append(
							&word, *text++,
							SH_QUOTE_DOUBLE))
							goto no_memory;
						produced = 1;
						continue;
					}
				}

				/* Handles a failed word append operation. */
				if (!word_append(&word, *text++,
						 escaped ? SH_QUOTE_ESCAPED
						 : quote == '\''
						     ? SH_QUOTE_SINGLE
						     : SH_QUOTE_DOUBLE))
					goto no_memory;
				produced = 1;
			}

			/* Validates the current text. */
			if (*text != quote) {
				*error_text = "unterminated quote";
				free(word.data);
				free(word.quote);

				/* Reports successful completion. */
				return 0;
			}
			text++;
			continue;
		}

		/* Handles a failed word append operation. */
		if (!word_append(&word, *text++, SH_QUOTE_UNQUOTED))
			goto no_memory;
		produced = 1;
	}

	/* Handles the produced condition. */
	if (!produced && !quoted) {
		*error_text = "empty word";
		free(word.data);
		free(word.quote);

		/* Reports successful completion. */
		return 0;
	}

	/* Handles a failed word reserve operation. */
	if (!word_reserve(&word))
		goto no_memory;
	word.data[word.length] = '\0';

	/* Handles a failed token append operation. */
	if (!token_append(list, SH_TOKEN_WORD, word.data, word.quote,
			  word.length))
		goto no_memory;
	*cursor = text;
	/* Reports operation failure. */
	return 1;
no_memory:
	*error_text = "out of memory";
	free(word.data);
	free(word.quote);

	/* Reports successful completion. */
	return 0;
}

/* Supports the word append operation. */
static int
word_append(
	struct word_buffer *word,
	char value,
	enum sh_quote_type quote)
{
	/* Handles a failed word reserve operation. */
	if (!word_reserve(word))
		return 0;
	word->data[word->length++] = value;
	word->quote[word->length - 1U] = (unsigned char)quote;

	/* Reports operation failure. */
	return 1;
}

/* Supports the word reserve operation. */
static int
word_reserve(
	struct word_buffer *word)
{
	char *larger;
	unsigned char *quote;
	size_t capacity;

	/* Handles the word condition. */
	if (word->length + 1U < word->capacity)
		return 1;
	capacity = word->capacity == 0 ? 16U : word->capacity * 2U;

	/* Handles the capacity condition. */
	if (capacity <= word->capacity)
		return 0;
	larger = realloc(word->data, capacity);

	/* Handles the larger availability. */
	if (larger == NULL)
		return 0;
	word->data = larger;
	quote = realloc(word->quote, capacity);

	/* Handles the quote availability. */
	if (quote == NULL)
		return 0;
	word->data = larger;
	word->quote = quote;
	word->capacity = capacity;

	/* Reports operation failure. */
	return 1;
}

/* Supports the token append operation. */
static int
token_append(
	struct sh_token_list *list,
	enum sh_token_type type,
	char *text,
	unsigned char *quote,
	size_t length)
{
	struct sh_token *larger;

	/* Handles the list condition. */
	if (list->count == (size_t)-1 / sizeof(*list->tokens))
		return 0;
	larger =
	    realloc(list->tokens, (list->count + 1U) * sizeof(*list->tokens));

	/* Handles the larger availability. */
	if (larger == NULL)
		return 0;
	list->tokens = larger;
	list->tokens[list->count].type = type;
	list->tokens[list->count].text = text;
	list->tokens[list->count].quote = quote;
	list->tokens[list->count].length = length;
	list->tokens[list->count].io_number = -1;
	list->count++;

	/* Reports operation failure. */
	return 1;
}
