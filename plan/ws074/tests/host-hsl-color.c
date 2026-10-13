/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Exercises HSL syntax and its actual packed-color output through the CSS tokenizer. */

#include "css/internal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

struct color_case {
	const char *text;
	uint32_t expected;
	int valid;
};

static const struct color_case cases[] = {
	{"hsla(0, 0%, 0%, 1.0)", 0xff000000U, 1},
	{"hsl(0, 100%, 50%)", 0xffff0000U, 1},
	{"hsla(120, 100%, 50%, 0.5)", 0x8000ff00U, 1},
	{"hsl(240deg 100% 50%)", 0xff0000ffU, 1},
	{"hsl(0.5turn 100% 50% / 25%)", 0x4000ffffU, 1},
	{"hsl(-120 100 50)", 0xff0000ffU, 1},
	{"hsl(400grad 100% 50%)", 0xffff0000U, 1},
	{"hsl(3.141592653589793rad 100% 50%)", 0xff00ffffU, 1},
	{"hsl(0, 0, 0)", 0, 0},
	{"hsl(0, 100%, 50%, 10px)", 0, 0},
	{"hsl(0, 100%, 50% / 1)", 0, 0},
	{"hsl(0 100% 50% 1)", 0, 0},
	{"hsl(0, 100%, 50%,)", 0, 0},
	{"rgba(255, 0, 0, 1)", 0xffff0000U, 1}
};

static int color_run(const struct color_case *test, int *valid, uint32_t *color);

int
main(
	void)
{
	size_t index;
	unsigned failures;
	int valid;
	uint32_t color;
	int error;

	failures = 0;
	for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		error = color_run(&cases[index], &valid, &color);
		if (error != 0)
			return 2;
		if (valid != cases[index].valid || (valid && color != cases[index].expected)) {
			failures++;
			fprintf(stderr, "FAIL %s: valid=%d color=%08x\n", cases[index].text, valid, color);
		}
	}
	printf("HSL colors: %zu/%zu passed\n", index - failures, index);
	return failures == 0 ? 0 : 1;
}

/* Tokenizes one CSS value before calling the production color parser. */
static int
color_run(
	const struct color_case *test,
	int *valid,
	uint32_t *color)
{
	struct wb_arena arena;
	struct wb_units units;
	struct css_token *tokens;
	size_t count;
	int error;

	wb_arena_init(&arena, 256);
	wb_units_init(&units);
	error = wb_utf8_to_units((const unsigned char *)test->text, strlen(test->text), &units);
	if (error == 0)
		error = css_tokenize(&arena, units.data, units.length, &tokens, &count);
	if (error == 0) {
		if (count != 0 && tokens[count - 1U].type == CSS_TOKEN_EOF)
			count--;
		*color = 0;
		error = css_parse_color(tokens, count, color);
		*valid = error == 0;
		if (error == EINVAL)
			error = 0;
	}
	wb_units_release(&units);
	wb_arena_release(&arena);
	return error;
}
