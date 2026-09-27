/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws074-p004: the batch driver of the HTML tokenizer for the html5lib
 * tokenizer tests (plan/ws074/tests/run-html5lib-tokenizer.py).
 *
 *   host-tokenizer < CASES > RESULTS
 *
 * Each input line is one case: MODE STATE LAST-START-TAG INPUT, tab
 * separated, where the last two are UTF-16 code units as four hex digits
 * each ("-" for none), STATE is an html_tokenizer_start number and MODE is
 * "whole" (all input, then close) or "units" (one unit at a time, running
 * the tokenizer dry after each).  Each output line is a JSON object with
 * the tokens in html5lib's form and the parse errors.
 */

#include "html/html.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_units(const uint16_t *units, size_t length);
static void print_token(const struct html_token *token, int *first);
static int parse_hex(const char *text, struct wb_units *units);
static void run_case(int by_units, int state, const struct wb_units *last, const struct wb_units *input);

int
main(void)
{
	static char line[1 << 20];
	struct wb_units last;
	struct wb_units input;
	char *fields[4];
	char *cursor;
	int count;

	wb_units_init(&last);
	wb_units_init(&input);
	while (fgets(line, sizeof(line), stdin) != NULL) {
		line[strcspn(line, "\n")] = '\0';
		count = 0;
		cursor = line;
		while (count < 4) {
			fields[count++] = cursor;
			cursor = strchr(cursor, '\t');
			if (cursor == NULL)
				break;
			*cursor++ = '\0';
		}
		if (count != 4) {
			printf("{\"bad\":1}\n");
			continue;
		}
		wb_units_clear(&last);
		wb_units_clear(&input);
		parse_hex(fields[2], &last);
		parse_hex(fields[3], &input);
		run_case(strcmp(fields[0], "units") == 0, atoi(fields[1]), &last, &input);
		fflush(stdout);
	}
	wb_units_release(&last);
	wb_units_release(&input);
	return 0;
}

static int
parse_hex(
	const char *text,
	struct wb_units *units)
{
	unsigned value;
	uint16_t unit;

	if (strcmp(text, "-") == 0)
		return 0;
	while (text[0] != '\0') {
		if (sscanf(text, "%4x", &value) != 1)
			return -1;
		unit = (uint16_t)value;
		wb_units_append(units, &unit, 1);
		text += 4;
	}
	return 0;
}

static void
run_case(
	int by_units,
	int state,
	const struct wb_units *last,
	const struct wb_units *input)
{
	struct html_tokenizer tokenizer;
	struct html_input stream;
	const struct html_token *token;
	enum html_token_type type;
	size_t fed;
	size_t index;
	int first;

	html_input_init(&stream);
	html_tokenizer_init(&tokenizer, &stream);
	html_tokenizer_set_state(&tokenizer, (enum html_tokenizer_start)state);
	if (last->length != 0)
		html_tokenizer_set_last_start_tag(&tokenizer, last->data, last->length);

	printf("{\"tokens\":[");
	first = 1;
	fed = 0;
	for (;;) {
		if (!by_units && fed == 0) {
			html_input_append(&stream, input->data, input->length);
			fed = input->length;
			html_input_close(&stream);
		}
		type = html_tokenizer_next(&tokenizer, &token);
		if (type == HTML_TOKEN_NONE) {
			if (fed < input->length) {
				html_input_append(&stream, input->data + fed, 1);
				fed++;
			} else {
				html_input_close(&stream);
			}
			continue;
		}
		if (type == HTML_TOKEN_EOF)
			break;
		print_token(token, &first);
	}
	printf("],\"errors\":%zu,\"codes\":[", tokenizer.error_count);
	for (index = 0; index < tokenizer.error_count && index < HTML_ERRORS_KEPT; index++)
		printf("%s\"%s\"", index == 0 ? "" : ",", html_error_name(tokenizer.errors[index]));
	printf("]}\n");

	html_tokenizer_release(&tokenizer);
	html_input_release(&stream);
}

static void
print_units(
	const uint16_t *units,
	size_t length)
{
	size_t index;

	putchar('"');
	for (index = 0; index < length; index++) {
		if (units[index] >= 0x20 && units[index] < 0x7f && units[index] != '"' && units[index] != '\\')
			putchar(units[index]);
		else
			printf("\\u%04x", units[index]);
	}
	putchar('"');
}

static void
print_token(
	const struct html_token *token,
	int *first)
{
	size_t index;
	int first_attribute;

	if (!*first)
		putchar(',');
	*first = 0;
	switch (token->type) {
	case HTML_TOKEN_CHARACTERS:
		printf("[\"Character\",");
		print_units(token->data.data, token->data.length);
		putchar(']');
		break;
	case HTML_TOKEN_COMMENT:
		printf("[\"Comment\",");
		print_units(token->data.data, token->data.length);
		putchar(']');
		break;
	case HTML_TOKEN_START_TAG:
		printf("[\"StartTag\",");
		print_units(token->name.data, token->name.length);
		printf(",{");
		first_attribute = 1;
		for (index = 0; index < token->attribute_count; index++) {
			if (token->attributes[index].dropped)
				continue;
			if (!first_attribute)
				putchar(',');
			first_attribute = 0;
			print_units(token->attributes[index].name.data, token->attributes[index].name.length);
			putchar(':');
			print_units(token->attributes[index].value.data, token->attributes[index].value.length);
		}
		putchar('}');
		if (token->self_closing)
			printf(",true");
		putchar(']');
		break;
	case HTML_TOKEN_END_TAG:
		printf("[\"EndTag\",");
		print_units(token->name.data, token->name.length);
		putchar(']');
		break;
	case HTML_TOKEN_DOCTYPE:
		printf("[\"DOCTYPE\",");
		if (token->has_name)
			print_units(token->name.data, token->name.length);
		else
			printf("null");
		putchar(',');
		if (token->has_public_id)
			print_units(token->public_id.data, token->public_id.length);
		else
			printf("null");
		putchar(',');
		if (token->has_system_id)
			print_units(token->system_id.data, token->system_id.length);
		else
			printf("null");
		printf(",%s]", token->force_quirks ? "false" : "true");
		break;
	default:
		printf("[\"Unknown\"]");
		break;
	}
}
