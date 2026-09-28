/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The content interpreter of libpdf's reader: a page's content streams
 * run into a display list (stage 1 of design-pdf.md).
 *
 * It keeps the graphics state stack (q, Q, cm, the line style, the fill and
 * stroke colours in DeviceGray, DeviceRGB and DeviceCMYK and the spaces
 * built on them, the ExtGState's alphas and the Normal and Multiply blend
 * modes), builds paths (m l c v y h re), fills them by either rule, strokes
 * them through the stroker (stroke.c) into fills, clips (W W* n), and draws
 * image XObjects and runs form XObjects (Do).  Text, shadings and inline
 * images are left out and the list says so (PDF_DISPLAY_SKIPPED).
 *
 * The content is not trusted.  Operators, operands, the q nesting, the
 * clips, the forms' nesting and the list's size are bounded; a malformed
 * token ends the content (PDF_DISPLAY_DAMAGED) and a limit stops it
 * (PDF_DISPLAY_LIMITED), and in both cases what came before is drawn.
 */

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <pdf.h>

#include "internal.h"

/* The most operands one operator may be given. */
#define PDF_CONTENT_OPERANDS_MAX 64

/* How deep q may nest, and how deep form XObjects may. */
#define PDF_CONTENT_STACK_MAX 64
#define PDF_CONTENT_FORMS_MAX 12

/* The most operators one page runs, its forms included. */
#define PDF_CONTENT_OPERATORS_MAX 16777216UL

/* The most entries of a dash pattern, and how deep clips may nest. */
#define PDF_CONTENT_DASH_MAX 16
#define PDF_CONTENT_CLIPS_MAX 64

/* How far a flattened curve may stray from the true one, and the width of a zero-width line, in page points. */
#define PDF_CONTENT_TOLERANCE 0.05
#define PDF_CONTENT_HAIRLINE 0.25

/*
 * The operators the interpreter knows.
 */
enum content_operator {
	OP_UNKNOWN = 0,
	OP_SAVE,
	OP_RESTORE,
	OP_CONCAT,
	OP_LINE_WIDTH,
	OP_LINE_CAP,
	OP_LINE_JOIN,
	OP_MITER_LIMIT,
	OP_DASH,
	OP_EXTGSTATE,
	OP_MOVE,
	OP_LINE,
	OP_CURVE,
	OP_CURVE_V,
	OP_CURVE_Y,
	OP_CLOSE,
	OP_RECTANGLE,
	OP_STROKE,
	OP_CLOSE_STROKE,
	OP_FILL,
	OP_FILL_EVEN_ODD,
	OP_FILL_STROKE,
	OP_FILL_STROKE_EVEN_ODD,
	OP_CLOSE_FILL_STROKE,
	OP_CLOSE_FILL_STROKE_EVEN_ODD,
	OP_END_PATH,
	OP_CLIP,
	OP_CLIP_EVEN_ODD,
	OP_GRAY_FILL,
	OP_GRAY_STROKE,
	OP_RGB_FILL,
	OP_RGB_STROKE,
	OP_CMYK_FILL,
	OP_CMYK_STROKE,
	OP_SPACE_FILL,
	OP_SPACE_STROKE,
	OP_COLOR_FILL,
	OP_COLOR_STROKE,
	OP_XOBJECT,
	OP_INLINE_IMAGE,
	OP_SHADING,
	OP_TEXT_SHOW,
	OP_IGNORED
};

/*
 * One operator's name and meaning, for the lookup table.
 */
struct content_name {
	const char *name;
	enum content_operator code;
};

/*
 * The kinds of operand.
 */
enum content_operand_type {
	OPERAND_NUMBER = 0,
	OPERAND_NAME,
	OPERAND_OTHER
};

/*
 * One operand: a number, a name (its decoded bytes), or any other object.
 */
struct content_operand {
	enum content_operand_type type;
	double number;
	const unsigned char *bytes;
	size_t length;
	struct pdf_object *object;
};

/*
 * One level of the graphics state.
 *
 * The colours are RGB from 0 to 1; a colour space the interpreter cannot
 * read leaves its colour unusable (fill_usable, stroke_usable), and what
 * would be painted with it is left out.  clips counts the clips pushed on
 * the list at this level, which its Q pops.
 */
struct content_state {
	double ctm[6];
	double fill[3];
	double stroke[3];
	int fill_components;
	int stroke_components;
	int fill_usable;
	int stroke_usable;
	double fill_alpha;
	double stroke_alpha;
	enum pdf_blend_mode blend;
	double line_width;
	int line_cap;
	int line_join;
	double miter_limit;
	double dash[PDF_CONTENT_DASH_MAX];
	size_t dash_count;
	double dash_phase;
	size_t clips;
};

/*
 * The path being built, in user space.
 */
struct content_path {
	unsigned char *verbs;
	size_t verb_count;
	size_t verb_capacity;
	struct pdf_point *points;
	size_t point_count;
	size_t point_capacity;
	struct pdf_point start;
	struct pdf_point current;
	int has_current;
};

/*
 * One page's interpretation: the document, the list being built, the
 * memory tokens are decoded into, the graphics state stack (the level in
 * force is stack[depth]), the path, the clip waiting for the next painting
 * operator, the operands of the next operator, the counts that the limits
 * bound, and the flags the list gets.
 *
 * base_depth is the level a running form started at, below which its Q
 * does not go; ignored_saves counts q past the stack's limit, which the
 * matching Q only uncount.  scratch holds the transformed copy of a path.
 */
struct content_run {
	struct pdf_document *document;
	struct pdf_display_builder *builder;
	struct pdf_arena arena;
	struct content_state stack[PDF_CONTENT_STACK_MAX];
	size_t depth;
	size_t base_depth;
	size_t ignored_saves;
	struct content_path path;
	int clip_pending;
	enum pdf_fill_rule clip_rule;
	struct content_operand operands[PDF_CONTENT_OPERANDS_MAX];
	size_t operand_count;
	unsigned long operators;
	size_t clip_depth;
	int form_depth;
	int stopped;
	unsigned flags;
	struct pdf_point *scratch;
	size_t scratch_capacity;
};

/*
 * The operators by name, the ones a page uses most first.
 */
static const struct content_name content_names[] = {
	{ "m", OP_MOVE },
	{ "l", OP_LINE },
	{ "c", OP_CURVE },
	{ "h", OP_CLOSE },
	{ "f", OP_FILL },
	{ "re", OP_RECTANGLE },
	{ "q", OP_SAVE },
	{ "Q", OP_RESTORE },
	{ "cm", OP_CONCAT },
	{ "rg", OP_RGB_FILL },
	{ "gs", OP_EXTGSTATE },
	{ "S", OP_STROKE },
	{ "RG", OP_RGB_STROKE },
	{ "w", OP_LINE_WIDTH },
	{ "Do", OP_XOBJECT },
	{ "g", OP_GRAY_FILL },
	{ "G", OP_GRAY_STROKE },
	{ "n", OP_END_PATH },
	{ "W", OP_CLIP },
	{ "W*", OP_CLIP_EVEN_ODD },
	{ "v", OP_CURVE_V },
	{ "y", OP_CURVE_Y },
	{ "f*", OP_FILL_EVEN_ODD },
	{ "F", OP_FILL },
	{ "s", OP_CLOSE_STROKE },
	{ "B", OP_FILL_STROKE },
	{ "B*", OP_FILL_STROKE_EVEN_ODD },
	{ "b", OP_CLOSE_FILL_STROKE },
	{ "b*", OP_CLOSE_FILL_STROKE_EVEN_ODD },
	{ "J", OP_LINE_CAP },
	{ "j", OP_LINE_JOIN },
	{ "M", OP_MITER_LIMIT },
	{ "d", OP_DASH },
	{ "k", OP_CMYK_FILL },
	{ "K", OP_CMYK_STROKE },
	{ "cs", OP_SPACE_FILL },
	{ "CS", OP_SPACE_STROKE },
	{ "sc", OP_COLOR_FILL },
	{ "scn", OP_COLOR_FILL },
	{ "SC", OP_COLOR_STROKE },
	{ "SCN", OP_COLOR_STROKE },
	{ "BI", OP_INLINE_IMAGE },
	{ "sh", OP_SHADING },
	{ "Tj", OP_TEXT_SHOW },
	{ "TJ", OP_TEXT_SHOW },
	{ "'", OP_TEXT_SHOW },
	{ "\"", OP_TEXT_SHOW },
	{ "ri", OP_IGNORED },
	{ "i", OP_IGNORED },
	{ "BT", OP_IGNORED },
	{ "ET", OP_IGNORED },
	{ "Tc", OP_IGNORED },
	{ "Tw", OP_IGNORED },
	{ "Tz", OP_IGNORED },
	{ "TL", OP_IGNORED },
	{ "Tf", OP_IGNORED },
	{ "Tr", OP_IGNORED },
	{ "Ts", OP_IGNORED },
	{ "Td", OP_IGNORED },
	{ "TD", OP_IGNORED },
	{ "Tm", OP_IGNORED },
	{ "T*", OP_IGNORED },
	{ "d0", OP_IGNORED },
	{ "d1", OP_IGNORED },
	{ "BMC", OP_IGNORED },
	{ "BDC", OP_IGNORED },
	{ "EMC", OP_IGNORED },
	{ "MP", OP_IGNORED },
	{ "DP", OP_IGNORED },
	{ "BX", OP_IGNORED },
	{ "EX", OP_IGNORED }
};

static int read_contents(struct pdf_document *document, struct pdf_object *page, unsigned char **content, size_t *size, unsigned *flags);
static int append_stream(struct pdf_document *document, struct pdf_object *stream, unsigned char **content, size_t *size, unsigned *flags);
static void base_matrix(const struct pdf_page_box *box, double matrix[6]);
static void run_content(struct content_run *run, const unsigned char *data, size_t size, struct pdf_object *resources);
static int read_operand(struct content_run *run, struct pdf_lexer *lexer, const struct pdf_token *token, size_t token_start);
static enum content_operator operator_code(const struct pdf_token *token);
static void execute(struct content_run *run, enum content_operator code, struct pdf_lexer *lexer, struct pdf_object *resources);
static void execute_state(struct content_run *run, enum content_operator code, struct pdf_object *resources);
static void execute_path(struct content_run *run, enum content_operator code);
static void execute_paint(struct content_run *run, enum content_operator code);
static void execute_color(struct content_run *run, enum content_operator code, struct pdf_object *resources);
static void save_state(struct content_run *run);
static void restore_state(struct content_run *run);
static void unwind_to(struct content_run *run, size_t depth);
static void end_clips(struct content_run *run);
static void concat_matrix(double ctm[6], const double matrix[6]);
static int read_numbers(const struct content_run *run, size_t count, double *numbers);
static void set_dash(struct content_state *state, const struct pdf_object *array, double phase);
static void apply_extgstate(struct content_run *run, struct pdf_object *resources);
static int state_number(struct content_run *run, struct pdf_object *dictionary, const char *key, double *number);
static int state_integer(struct content_run *run, struct pdf_object *dictionary, const char *key, long *integer);
static void state_dash(struct content_run *run, struct pdf_object *dictionary);
static void state_blend(struct content_run *run, struct pdf_object *dictionary);
static void apply_blend(struct content_run *run, struct pdf_object *mode);
static int blend_known(struct pdf_object *mode);
static int find_resource(struct content_run *run, struct pdf_object *resources, const char *category, struct pdf_object **found);
static int space_components(struct content_run *run, struct pdf_object *resources, int *components);
static int device_components(const unsigned char *name, size_t length);
static void set_color(struct content_run *run, int stroke, int components, const double *values);
static int path_add(struct content_run *run, enum pdf_path_verb verb, const double *coordinates, size_t count);
static int add_rectangle(struct content_run *run, double left, double bottom, double right, double top);
static void path_clear(struct content_run *run);
static void paint_path(struct content_run *run, int fill, enum pdf_fill_rule rule, int stroke);
static void fill_path(struct content_run *run, enum pdf_fill_rule rule);
static void stroke_path(struct content_run *run);
static void push_clip(struct content_run *run);
static int transform_path(struct content_run *run, const struct pdf_point *points, size_t count, struct pdf_point **transformed);
static double matrix_scale(const double matrix[6]);
static void draw_xobject(struct content_run *run, struct pdf_object *resources);
static void draw_image(struct content_run *run, struct pdf_object *image);
static void run_form(struct content_run *run, struct pdf_object *form, struct pdf_object *resources);
static void skip_inline_image(struct content_run *run, struct pdf_lexer *lexer);
static int is_white(unsigned char character);
static void stop_for(struct content_run *run, int error);
static double clamp_unit(double value);

/*
 * Interprets a page's content into a display list.
 *
 * The list is the page's even when a part of it could not be drawn; its
 * flags say so.  An error is reported only for a page that cannot be
 * started at all (no such page, a malformed page box, no memory).
 */
int
pdf_page_render(
	struct pdf_document *document,
	size_t index,
	struct pdf_display_list **list)
{
	struct pdf_display_builder *builder;
	struct pdf_page_box box;
	struct pdf_object *page;
	struct pdf_object *resources;
	struct content_run *run;
	unsigned char *content;
	size_t size;
	int error;

	/* Refuses a missing document or list. */
	if (document == NULL)
		return EINVAL;
	if (list == NULL)
		return EINVAL;

	/* Reads the page's boxes, which place the content on the page. */
	error = pdf_document_page_box(document, index, &box);
	if (error != 0)
		return error;

	/* Finds the page and its resources. */
	error = pdf_reader_page(document, index, &page, &resources);
	if (error != 0)
		return error;

	/* Makes the list the page is drawn into. */
	error = pdf_display_create(&builder);
	if (error != 0)
		return error;
	builder->list.width = box.width;
	builder->list.height = box.height;

	/* Makes the interpretation's state, which is too large for the stack. */
	run = calloc(1, sizeof(*run));
	if (run == NULL) {
		pdf_display_free(builder);
		return ENOMEM;
	}

	/* Starts with the initial graphics state on the page's shown space. */
	run->document = document;
	run->builder = builder;
	base_matrix(&box, run->stack[0].ctm);
	run->stack[0].fill_components = 1;
	run->stack[0].stroke_components = 1;
	run->stack[0].fill_usable = 1;
	run->stack[0].stroke_usable = 1;
	run->stack[0].fill_alpha = 1.0;
	run->stack[0].stroke_alpha = 1.0;
	run->stack[0].blend = PDF_BLEND_NORMAL;
	run->stack[0].line_width = 1.0;
	run->stack[0].miter_limit = 10.0;

	/* Reads the page's content streams, joined. */
	content = NULL;
	size = 0;
	error = read_contents(document, page, &content, &size, &run->flags);
	if (error == ENOMEM) {
		free(run);
		pdf_display_free(builder);
		return ENOMEM;
	}

	/* Runs the content, then ends every level it left open and the page's own clips. */
	if (error == 0)
		run_content(run, content, size, resources);
	unwind_to(run, 0);
	end_clips(run);
	free(content);

	/* Frees the interpretation, keeping its flags for the list. */
	builder->list.flags = run->flags;
	pdf_arena_free(&run->arena);
	free(run->path.verbs);
	free(run->path.points);
	free(run->scratch);
	free(run);

	/* Succeeded: the list is the page's drawing. */
	pdf_display_finish(builder);
	*list = &builder->list;
	return 0;
}

/*
 * Reads a page's content, one stream or an array of them, decoded and
 * joined with a line end between them (a malloc'd buffer, NULL for none).
 *
 * A stream whose filter is not read yet is left out (PDF_DISPLAY_SKIPPED);
 * a malformed one ends the content there (PDF_DISPLAY_DAMAGED).  Only
 * running out of memory is an error.
 */
static int
read_contents(
	struct pdf_document *document,
	struct pdf_object *page,
	unsigned char **content,
	size_t *size,
	unsigned *flags)
{
	struct pdf_object *contents;
	struct pdf_object *stream;
	size_t item;
	int error;

	/* Finds the page's content. */
	error = pdf_reader_resolve_key(document, page, "Contents", &contents);
	if (error != 0) {
		*flags |= PDF_DISPLAY_DAMAGED;
		return 0;
	}

	/* One stream. */
	if (contents->type == PDF_OBJECT_STREAM) {
		error = append_stream(document, contents, content, size, flags);
		if (error != 0)
			return error;
		return 0;
	}

	/* An array of streams, in order; anything else is no content. */
	if (contents->type != PDF_OBJECT_ARRAY)
		return 0;
	for (item = 0; item < contents->count; item++) {
		/* Finds the stream; a missing or malformed one ends the content. */
		error = pdf_reader_resolve(document, contents->values[item], &stream);
		if (error != 0) {
			*flags |= PDF_DISPLAY_DAMAGED;
			return 0;
		}
		if (stream->type != PDF_OBJECT_STREAM) {
			*flags |= PDF_DISPLAY_DAMAGED;
			return 0;
		}

		/* Adds its decoded bytes. */
		error = append_stream(document, stream, content, size, flags);
		if (error != 0)
			return error;
	}

	/* Succeeded: the content is joined. */
	return 0;
}

/* Decodes a content stream and appends it, with a line end, to the content read so far. */
static int
append_stream(
	struct pdf_document *document,
	struct pdf_object *stream,
	unsigned char **content,
	size_t *size,
	unsigned *flags)
{
	const unsigned char *data;
	unsigned char *owned;
	unsigned char *grown;
	size_t data_size;
	int dct;
	int error;

	/* Decodes the stream; one that cannot be decoded is left out. */
	error = pdf_filter_decode(document, stream, 0, &data, &data_size, &owned, &dct);
	if (error == ENOMEM)
		return ENOMEM;
	if (error == ENOTSUP) {
		*flags |= PDF_DISPLAY_SKIPPED;
		return 0;
	}
	if (error != 0) {
		*flags |= PDF_DISPLAY_DAMAGED;
		return 0;
	}

	/* Refuses content past the decode limit. */
	if (data_size > PDF_FILTER_OUTPUT_MAX - *size - 1) {
		free(owned);
		*flags |= PDF_DISPLAY_LIMITED;
		return 0;
	}

	/* Grows the content by the stream and a line end. */
	grown = realloc(*content, *size + data_size + 1);
	if (grown == NULL) {
		free(owned);
		return ENOMEM;
	}
	*content = grown;
	memcpy(*content + *size, data, data_size);
	(*content)[*size + data_size] = '\n';
	*size += data_size + 1;
	free(owned);

	/* Succeeded: the stream's bytes are appended. */
	return 0;
}

/*
 * Makes the matrix from the page's user space to its shown space: the crop
 * box's top left at the origin, y downward, the page's rotation applied.
 */
static void
base_matrix(
	const struct pdf_page_box *box,
	double matrix[6])
{
	/* Chooses the matrix by the rotation (clockwise, as shown). */
	switch (box->rotation) {
	case 90:
		matrix[0] = 0.0;
		matrix[1] = 1.0;
		matrix[2] = 1.0;
		matrix[3] = 0.0;
		matrix[4] = -box->crop_bottom;
		matrix[5] = -box->crop_left;
		break;
	case 180:
		matrix[0] = -1.0;
		matrix[1] = 0.0;
		matrix[2] = 0.0;
		matrix[3] = 1.0;
		matrix[4] = box->crop_right;
		matrix[5] = -box->crop_bottom;
		break;
	case 270:
		matrix[0] = 0.0;
		matrix[1] = -1.0;
		matrix[2] = -1.0;
		matrix[3] = 0.0;
		matrix[4] = box->crop_top;
		matrix[5] = box->crop_right;
		break;
	default:
		matrix[0] = 1.0;
		matrix[1] = 0.0;
		matrix[2] = 0.0;
		matrix[3] = -1.0;
		matrix[4] = -box->crop_left;
		matrix[5] = box->crop_top;
		break;
	}
}

/*
 * Runs a content stream: operands gathered until their operator, and the
 * operator executed.
 */
static void
run_content(
	struct content_run *run,
	const unsigned char *data,
	size_t size,
	struct pdf_object *resources)
{
	struct pdf_lexer lexer;
	struct pdf_token token;
	enum content_operator code;
	size_t token_start;
	int error;

	/* Reads the content's tokens with the run's memory. */
	memset(&lexer, 0, sizeof(lexer));
	lexer.data = data;
	lexer.size = size;
	lexer.arena = &run->arena;
	run->operand_count = 0;

	/* Reads tokens until the end, an error or a limit. */
	while (!run->stopped) {
		/* Reads the next token, remembering where it starts. */
		pdf_lexer_skip_space(&lexer);
		token_start = lexer.position;
		error = pdf_lexer_next(&lexer, &token);
		if (error != 0) {
			stop_for(run, error);
			break;
		}
		if (token.type == PDF_TOKEN_END)
			break;

		/* Gathers an operand. */
		if (token.type != PDF_TOKEN_KEYWORD) {
			error = read_operand(run, &lexer, &token, token_start);
			if (error != 0) {
				stop_for(run, error);
				break;
			}
			continue;
		}

		/* Counts the operator against the page's limit. */
		run->operators++;
		if (run->operators > PDF_CONTENT_OPERATORS_MAX) {
			run->flags |= PDF_DISPLAY_LIMITED;
			run->stopped = 1;
			break;
		}

		/* Executes the operator with its operands, which it uses up. */
		code = operator_code(&token);
		execute(run, code, &lexer, resources);
		run->operand_count = 0;
	}
}

/* Reads one operand: a number, a name, or an array, dictionary or string object. */
static int
read_operand(
	struct content_run *run,
	struct pdf_lexer *lexer,
	const struct pdf_token *token,
	size_t token_start)
{
	struct content_operand *operand;
	struct pdf_object *object;
	int error;

	/* Refuses more operands than any operator takes. */
	if (run->operand_count == PDF_CONTENT_OPERANDS_MAX)
		return PDF_EFORMAT;
	operand = &run->operands[run->operand_count];
	memset(operand, 0, sizeof(*operand));

	/* A number, kept as it is. */
	if (token->type == PDF_TOKEN_INTEGER) {
		operand->type = OPERAND_NUMBER;
		operand->number = (double)token->integer;
		run->operand_count++;
		return 0;
	}
	if (token->type == PDF_TOKEN_REAL) {
		operand->type = OPERAND_NUMBER;
		operand->number = token->real;
		run->operand_count++;
		return 0;
	}

	/* A name, by its decoded bytes. */
	if (token->type == PDF_TOKEN_NAME) {
		operand->type = OPERAND_NAME;
		operand->bytes = token->bytes;
		operand->length = token->length;
		run->operand_count++;
		return 0;
	}

	/* Anything else is parsed as an object from where its token starts. */
	lexer->position = token_start;
	error = pdf_parse_object(lexer, 0, &object);
	if (error != 0)
		return error;
	operand->type = OPERAND_OTHER;
	operand->object = object;
	run->operand_count++;

	/* Succeeded: the operand waits for its operator. */
	return 0;
}

/* Finds an operator's meaning by its name. */
static enum content_operator
operator_code(
	const struct pdf_token *token)
{
	size_t index;
	size_t length;
	int difference;

	/* Compares the name with each known one. */
	for (index = 0; index < sizeof(content_names) / sizeof(content_names[0]); index++) {
		length = strlen(content_names[index].name);
		if (length != token->length)
			continue;
		difference = memcmp(content_names[index].name, token->bytes, length);
		if (difference == 0)
			return content_names[index].code;
	}

	/* An operator the interpreter does not know. */
	return OP_UNKNOWN;
}

/* Executes one operator by its group. */
static void
execute(
	struct content_run *run,
	enum content_operator code,
	struct pdf_lexer *lexer,
	struct pdf_object *resources)
{
	/* Chooses the group of the operator. */
	switch (code) {
	case OP_SAVE:
	case OP_RESTORE:
	case OP_CONCAT:
	case OP_LINE_WIDTH:
	case OP_LINE_CAP:
	case OP_LINE_JOIN:
	case OP_MITER_LIMIT:
	case OP_DASH:
	case OP_EXTGSTATE:
		execute_state(run, code, resources);
		break;
	case OP_MOVE:
	case OP_LINE:
	case OP_CURVE:
	case OP_CURVE_V:
	case OP_CURVE_Y:
	case OP_CLOSE:
	case OP_RECTANGLE:
		execute_path(run, code);
		break;
	case OP_STROKE:
	case OP_CLOSE_STROKE:
	case OP_FILL:
	case OP_FILL_EVEN_ODD:
	case OP_FILL_STROKE:
	case OP_FILL_STROKE_EVEN_ODD:
	case OP_CLOSE_FILL_STROKE:
	case OP_CLOSE_FILL_STROKE_EVEN_ODD:
	case OP_END_PATH:
	case OP_CLIP:
	case OP_CLIP_EVEN_ODD:
		execute_paint(run, code);
		break;
	case OP_GRAY_FILL:
	case OP_GRAY_STROKE:
	case OP_RGB_FILL:
	case OP_RGB_STROKE:
	case OP_CMYK_FILL:
	case OP_CMYK_STROKE:
	case OP_SPACE_FILL:
	case OP_SPACE_STROKE:
	case OP_COLOR_FILL:
	case OP_COLOR_STROKE:
		execute_color(run, code, resources);
		break;
	case OP_XOBJECT:
		draw_xobject(run, resources);
		break;
	case OP_INLINE_IMAGE:
		skip_inline_image(run, lexer);
		break;
	case OP_SHADING:
	case OP_TEXT_SHOW:
		/* Shadings and text are drawn from stage 2. */
		run->flags |= PDF_DISPLAY_SKIPPED;
		break;
	case OP_IGNORED:
	case OP_UNKNOWN:
		break;
	}
}

/* Executes an operator of the graphics state. */
static void
execute_state(
	struct content_run *run,
	enum content_operator code,
	struct pdf_object *resources)
{
	struct content_state *state;
	double numbers[6];
	int error;

	/* The level in force. */
	state = &run->stack[run->depth];

	/* Changes the state by the operator. */
	switch (code) {
	case OP_SAVE:
		save_state(run);
		break;
	case OP_RESTORE:
		restore_state(run);
		break;
	case OP_CONCAT:
		error = read_numbers(run, 6, numbers);
		if (error == 0)
			concat_matrix(state->ctm, numbers);
		break;
	case OP_LINE_WIDTH:
		/* A width that is not negative. */
		error = read_numbers(run, 1, numbers);
		if (error != 0)
			break;
		if (numbers[0] >= 0.0)
			state->line_width = numbers[0];
		break;
	case OP_LINE_CAP:
		/* One of the three caps. */
		error = read_numbers(run, 1, numbers);
		if (error != 0)
			break;
		if (numbers[0] >= 0.0 && numbers[0] <= 2.0)
			state->line_cap = (int)numbers[0];
		break;
	case OP_LINE_JOIN:
		/* One of the three joins. */
		error = read_numbers(run, 1, numbers);
		if (error != 0)
			break;
		if (numbers[0] >= 0.0 && numbers[0] <= 2.0)
			state->line_join = (int)numbers[0];
		break;
	case OP_MITER_LIMIT:
		/* A limit of at least 1. */
		error = read_numbers(run, 1, numbers);
		if (error != 0)
			break;
		if (numbers[0] >= 1.0)
			state->miter_limit = numbers[0];
		break;
	case OP_DASH:
		/* An array and a phase. */
		if (run->operand_count != 2)
			break;
		if (run->operands[0].type != OPERAND_OTHER)
			break;
		if (run->operands[1].type != OPERAND_NUMBER)
			break;
		set_dash(state, run->operands[0].object, run->operands[1].number);
		break;
	case OP_EXTGSTATE:
		apply_extgstate(run, resources);
		break;
	default:
		break;
	}
}

/* Executes a path construction operator. */
static void
execute_path(
	struct content_run *run,
	enum content_operator code)
{
	struct content_path *path;
	double numbers[6];
	int error;

	/* The path being built. */
	path = &run->path;

	/* Adds to the path by the operator. */
	error = 0;
	switch (code) {
	case OP_MOVE:
		error = read_numbers(run, 2, numbers);
		if (error == 0)
			error = path_add(run, PDF_PATH_MOVE, numbers, 1);
		break;
	case OP_LINE:
		/* A line from the current point, which it needs. */
		error = read_numbers(run, 2, numbers);
		if (error != 0)
			break;
		if (path->has_current)
			error = path_add(run, PDF_PATH_LINE, numbers, 1);
		break;
	case OP_CURVE:
		/* A curve from the current point, which it needs. */
		error = read_numbers(run, 6, numbers);
		if (error != 0)
			break;
		if (path->has_current)
			error = path_add(run, PDF_PATH_CUBIC, numbers, 3);
		break;
	case OP_CURVE_V:
		/* The first control point is the current point. */
		error = read_numbers(run, 4, numbers + 2);
		if (error != 0)
			break;
		if (!path->has_current)
			break;
		numbers[0] = path->current.x;
		numbers[1] = path->current.y;
		error = path_add(run, PDF_PATH_CUBIC, numbers, 3);
		break;
	case OP_CURVE_Y:
		/* The second control point is the end point. */
		error = read_numbers(run, 4, numbers);
		if (error != 0)
			break;
		if (!path->has_current)
			break;
		numbers[4] = numbers[2];
		numbers[5] = numbers[3];
		error = path_add(run, PDF_PATH_CUBIC, numbers, 3);
		break;
	case OP_CLOSE:
		if (path->has_current)
			error = path_add(run, PDF_PATH_CLOSE, numbers, 0);
		break;
	case OP_RECTANGLE:
		/* A rectangle is a closed subpath of its four corners, from its origin. */
		error = read_numbers(run, 4, numbers);
		if (error != 0)
			break;
		error = add_rectangle(run, numbers[0], numbers[1], numbers[0] + numbers[2], numbers[1] + numbers[3]);
		break;
	default:
		break;
	}

	/* A path past the limit stops the page. */
	if (error == ENOMEM)
		stop_for(run, ENOMEM);
}

/* Executes a painting or clipping operator; each painting operator ends the path. */
static void
execute_paint(
	struct content_run *run,
	enum content_operator code)
{
	double none[2];

	/* Paints, or marks the clip that the next painting operator applies. */
	none[0] = 0.0;
	none[1] = 0.0;
	switch (code) {
	case OP_CLIP:
		run->clip_pending = 1;
		run->clip_rule = PDF_FILL_NONZERO;
		return;
	case OP_CLIP_EVEN_ODD:
		run->clip_pending = 1;
		run->clip_rule = PDF_FILL_EVEN_ODD;
		return;
	case OP_STROKE:
		paint_path(run, 0, PDF_FILL_NONZERO, 1);
		break;
	case OP_CLOSE_STROKE:
		if (run->path.has_current)
			(void)path_add(run, PDF_PATH_CLOSE, none, 0);
		paint_path(run, 0, PDF_FILL_NONZERO, 1);
		break;
	case OP_FILL:
		paint_path(run, 1, PDF_FILL_NONZERO, 0);
		break;
	case OP_FILL_EVEN_ODD:
		paint_path(run, 1, PDF_FILL_EVEN_ODD, 0);
		break;
	case OP_FILL_STROKE:
		paint_path(run, 1, PDF_FILL_NONZERO, 1);
		break;
	case OP_FILL_STROKE_EVEN_ODD:
		paint_path(run, 1, PDF_FILL_EVEN_ODD, 1);
		break;
	case OP_CLOSE_FILL_STROKE:
		if (run->path.has_current)
			(void)path_add(run, PDF_PATH_CLOSE, none, 0);
		paint_path(run, 1, PDF_FILL_NONZERO, 1);
		break;
	case OP_CLOSE_FILL_STROKE_EVEN_ODD:
		if (run->path.has_current)
			(void)path_add(run, PDF_PATH_CLOSE, none, 0);
		paint_path(run, 1, PDF_FILL_EVEN_ODD, 1);
		break;
	case OP_END_PATH:
		paint_path(run, 0, PDF_FILL_NONZERO, 0);
		break;
	default:
		break;
	}
}

/* Executes a colour operator. */
static void
execute_color(
	struct content_run *run,
	enum content_operator code,
	struct pdf_object *resources)
{
	struct content_state *state;
	double numbers[4];
	int components;
	int stroke;
	int error;

	/* The level in force, and whether the operator sets the stroke's colour. */
	state = &run->stack[run->depth];
	stroke = 0;
	if (code == OP_GRAY_STROKE ||
	    code == OP_RGB_STROKE ||
	    code == OP_CMYK_STROKE ||
	    code == OP_SPACE_STROKE ||
	    code == OP_COLOR_STROKE)
		stroke = 1;

	/* Sets the colour, and the device space it is in, by the operator. */
	switch (code) {
	case OP_GRAY_FILL:
	case OP_GRAY_STROKE:
		error = read_numbers(run, 1, numbers);
		if (error == 0)
			set_color(run, stroke, 1, numbers);
		break;
	case OP_RGB_FILL:
	case OP_RGB_STROKE:
		error = read_numbers(run, 3, numbers);
		if (error == 0)
			set_color(run, stroke, 3, numbers);
		break;
	case OP_CMYK_FILL:
	case OP_CMYK_STROKE:
		error = read_numbers(run, 4, numbers);
		if (error == 0)
			set_color(run, stroke, 4, numbers);
		break;
	case OP_SPACE_FILL:
	case OP_SPACE_STROKE:
		/* A new space starts at its initial colour, black. */
		error = space_components(run, resources, &components);
		numbers[0] = 0.0;
		numbers[1] = 0.0;
		numbers[2] = 0.0;
		numbers[3] = 1.0;
		if (error != 0) {
			/* A space the interpreter cannot read makes the colour unusable. */
			if (stroke) {
				state->stroke_usable = 0;
			} else {
				state->fill_usable = 0;
			}
			break;
		}
		if (components == 1)
			numbers[0] = 0.0;
		set_color(run, stroke, components, numbers);
		break;
	case OP_COLOR_FILL:
	case OP_COLOR_STROKE:
		/* The values of the space in force; a pattern's name or a wrong count is not read. */
		components = state->fill_components;
		if (stroke)
			components = state->stroke_components;
		error = read_numbers(run, (size_t)components, numbers);
		if (error == 0)
			set_color(run, stroke, components, numbers);
		break;
	default:
		break;
	}
}

/* Pushes a copy of the graphics state (q). */
static void
save_state(
	struct content_run *run)
{
	/* Past the limit, a q is only counted, so that its Q is matched. */
	if (run->depth + 1 >= PDF_CONTENT_STACK_MAX) {
		run->ignored_saves++;
		run->flags |= PDF_DISPLAY_LIMITED;
		return;
	}

	/* The new level starts as a copy of the old, with no clips of its own. */
	run->stack[run->depth + 1] = run->stack[run->depth];
	run->stack[run->depth + 1].clips = 0;
	run->depth++;
}

/* Pops the graphics state (Q), ending the clips pushed at the level. */
static void
restore_state(
	struct content_run *run)
{
	/* A Q of a q past the limit only uncounts it. */
	if (run->ignored_saves > 0) {
		run->ignored_saves--;
		return;
	}

	/* A Q without its q (or below a running form's start) does nothing. */
	if (run->depth <= run->base_depth)
		return;

	/* Pops the level. */
	unwind_to(run, run->depth - 1);
}

/* Pops the levels above a depth, ending each popped level's clips on the list. */
static void
unwind_to(
	struct content_run *run,
	size_t depth)
{
	/* Pops each level above the depth. */
	while (run->depth > depth) {
		end_clips(run);
		run->depth--;
	}
}

/* Ends the clips pushed at the level in force. */
static void
end_clips(
	struct content_run *run)
{
	size_t clip;
	int error;

	/* Pops each of the level's clips; the pops balance the pushes even past a limit. */
	for (clip = 0; clip < run->stack[run->depth].clips; clip++) {
		error = pdf_display_add_clip_pop(run->builder);
		if (error != 0)
			run->flags |= PDF_DISPLAY_LIMITED;
		run->clip_depth--;
	}
	run->stack[run->depth].clips = 0;
}

/* Concatenates a matrix before the CTM (the new user space is the matrix applied in the old one). */
static void
concat_matrix(
	double ctm[6],
	const double matrix[6])
{
	double result[6];

	/* result = matrix x ctm, in PDF's row-vector convention. */
	result[0] = matrix[0] * ctm[0] + matrix[1] * ctm[2];
	result[1] = matrix[0] * ctm[1] + matrix[1] * ctm[3];
	result[2] = matrix[2] * ctm[0] + matrix[3] * ctm[2];
	result[3] = matrix[2] * ctm[1] + matrix[3] * ctm[3];
	result[4] = matrix[4] * ctm[0] + matrix[5] * ctm[2] + ctm[4];
	result[5] = matrix[4] * ctm[1] + matrix[5] * ctm[3] + ctm[5];

	/* The CTM becomes the product. */
	memcpy(ctm, result, sizeof(result));
}

/*
 * Reads the operator's last count operands as finite numbers.
 *
 * Extra operands before them are ignored; fewer, or anything but numbers,
 * reports PDF_EFORMAT and the operator does nothing.
 */
static int
read_numbers(
	const struct content_run *run,
	size_t count,
	double *numbers)
{
	size_t first;
	size_t index;
	double value;

	/* Refuses too few operands. */
	if (run->operand_count < count)
		return PDF_EFORMAT;
	first = run->operand_count - count;

	/* Takes each number, refusing anything else, infinities and NaN. */
	for (index = 0; index < count; index++) {
		if (run->operands[first + index].type != OPERAND_NUMBER)
			return PDF_EFORMAT;
		value = run->operands[first + index].number;
		if (!(value > -1e30 && value < 1e30))
			return PDF_EFORMAT;
		numbers[index] = value;
	}

	/* Succeeded: numbers holds the operands. */
	return 0;
}

/*
 * Sets the dash pattern from an array and a phase; a pattern that is
 * empty, negative, too long or of no length makes the line solid.
 */
static void
set_dash(
	struct content_state *state,
	const struct pdf_object *array,
	double phase)
{
	double total;
	double value;
	size_t index;
	int error;

	/* A solid line unless the array is a usable pattern. */
	state->dash_count = 0;
	state->dash_phase = 0.0;
	if (array->type != PDF_OBJECT_ARRAY)
		return;
	if (array->count == 0 || array->count > PDF_CONTENT_DASH_MAX)
		return;

	/* Reads each entry, which must be a number that is not negative. */
	total = 0.0;
	for (index = 0; index < array->count; index++) {
		error = pdf_object_number(array->values[index], &value);
		if (error != 0)
			return;
		if (!(value >= 0.0 && value < 1e6))
			return;
		state->dash[index] = value;
		total += value;
	}

	/* A pattern of no length is solid; the phase is kept within one period. */
	if (total < 1e-3)
		return;
	if (!(phase >= 0.0 && phase < 1e9))
		phase = 0.0;
	phase = fmod(phase, total);

	/* An odd count repeats itself to make the dashes and the gaps alternate. */
	if (array->count % 2 == 1 && array->count * 2 <= PDF_CONTENT_DASH_MAX) {
		for (index = 0; index < array->count; index++)
			state->dash[array->count + index] = state->dash[index];
		state->dash_count = array->count * 2;
		state->dash_phase = fmod(phase, total * 2.0);
		return;
	}
	if (array->count % 2 == 1)
		return;

	/* The pattern in force. */
	state->dash_count = array->count;
	state->dash_phase = phase;
}

/* Applies an ExtGState resource (gs): the alphas, the blend mode and the line style it sets. */
static void
apply_extgstate(
	struct content_run *run,
	struct pdf_object *resources)
{
	struct content_state *state;
	struct pdf_object *dictionary;
	struct pdf_object *value;
	double number;
	long integer;
	int found;
	int error;

	/* Finds the named ExtGState; a missing one does nothing. */
	state = &run->stack[run->depth];
	error = find_resource(run, resources, "ExtGState", &dictionary);
	if (error != 0)
		return;
	if (dictionary->type != PDF_OBJECT_DICTIONARY)
		return;

	/* The fill alpha. */
	found = state_number(run, dictionary, "ca", &number);
	if (found)
		state->fill_alpha = clamp_unit(number);

	/* The stroke alpha. */
	found = state_number(run, dictionary, "CA", &number);
	if (found)
		state->stroke_alpha = clamp_unit(number);

	/* The line width, which cannot be negative. */
	found = state_number(run, dictionary, "LW", &number);
	if (found && number >= 0.0)
		state->line_width = number;

	/* The line cap, one of the three. */
	found = state_integer(run, dictionary, "LC", &integer);
	if (found && integer >= 0 && integer <= 2)
		state->line_cap = (int)integer;

	/* The line join, one of the three. */
	found = state_integer(run, dictionary, "LJ", &integer);
	if (found && integer >= 0 && integer <= 2)
		state->line_join = (int)integer;

	/* The miter limit, at least 1. */
	found = state_number(run, dictionary, "ML", &number);
	if (found && number >= 1.0)
		state->miter_limit = number;

	/* The dash pattern: an array and a phase. */
	state_dash(run, dictionary);

	/* The blend mode. */
	state_blend(run, dictionary);

	/* A soft mask is drawn from stage 3. */
	error = pdf_reader_resolve_key(run->document, dictionary, "SMask", &value);
	if (error != 0)
		return;
	if (value->type == PDF_OBJECT_NULL)
		return;
	found = pdf_object_is_name(value, "None");
	if (!found)
		run->flags |= PDF_DISPLAY_SKIPPED;
}

/* Reads a number of an ExtGState; reports whether it has one. */
static int
state_number(
	struct content_run *run,
	struct pdf_object *dictionary,
	const char *key,
	double *number)
{
	struct pdf_object *value;
	int error;

	/* Finds the key's value. */
	error = pdf_reader_resolve_key(run->document, dictionary, key, &value);
	if (error != 0)
		return 0;

	/* Only a number counts. */
	error = pdf_object_number(value, number);
	if (error != 0)
		return 0;

	/* The ExtGState has the number. */
	return 1;
}

/* Reads an integer of an ExtGState; reports whether it has one. */
static int
state_integer(
	struct content_run *run,
	struct pdf_object *dictionary,
	const char *key,
	long *integer)
{
	struct pdf_object *value;
	int error;

	/* Finds the key's value. */
	error = pdf_reader_resolve_key(run->document, dictionary, key, &value);
	if (error != 0)
		return 0;

	/* Only an integer counts. */
	if (value->type != PDF_OBJECT_INTEGER)
		return 0;

	/* The ExtGState has the integer. */
	*integer = value->integer;
	return 1;
}

/* Applies an ExtGState's dash pattern (D: an array and a phase), when it has a usable one. */
static void
state_dash(
	struct content_run *run,
	struct pdf_object *dictionary)
{
	struct pdf_object *value;
	struct pdf_object *pattern;
	struct pdf_object *phase;
	double number;
	int error;

	/* The value must be a pair. */
	error = pdf_reader_resolve_key(run->document, dictionary, "D", &value);
	if (error != 0)
		return;
	if (value->type != PDF_OBJECT_ARRAY)
		return;
	if (value->count != 2)
		return;

	/* Its phase, a number. */
	error = pdf_reader_resolve(run->document, value->values[1], &phase);
	if (error != 0)
		return;
	error = pdf_object_number(phase, &number);
	if (error != 0)
		return;

	/* Its pattern, which set_dash checks. */
	error = pdf_reader_resolve(run->document, value->values[0], &pattern);
	if (error != 0)
		return;
	set_dash(&run->stack[run->depth], pattern, number);
}

/*
 * Applies an ExtGState's blend mode (BM): a name, or an array whose first
 * mode the interpreter draws wins.  Normal and Compatible draw normally,
 * Multiply multiplies; any other mode draws normally and the list says so.
 */
static void
state_blend(
	struct content_run *run,
	struct pdf_object *dictionary)
{
	struct pdf_object *value;
	struct pdf_object *mode;
	size_t index;
	int known;
	int error;

	/* No mode leaves the blend mode as it is. */
	error = pdf_reader_resolve_key(run->document, dictionary, "BM", &value);
	if (error != 0)
		return;
	if (value->type == PDF_OBJECT_NULL)
		return;

	/* A name is the mode itself. */
	if (value->type != PDF_OBJECT_ARRAY) {
		apply_blend(run, value);
		return;
	}

	/* An array's first mode the interpreter knows. */
	for (index = 0; index < value->count; index++) {
		error = pdf_reader_resolve(run->document, value->values[index], &mode);
		if (error != 0)
			return;
		known = blend_known(mode);
		if (known) {
			apply_blend(run, mode);
			return;
		}
	}

	/* An array of unknown modes draws normally. */
	run->stack[run->depth].blend = PDF_BLEND_NORMAL;
	run->flags |= PDF_DISPLAY_SKIPPED;
}

/* Sets the blend mode a name gives; an unknown one draws normally and marks the list. */
static void
apply_blend(
	struct content_run *run,
	struct pdf_object *mode)
{
	int is_multiply;
	int known;

	/* Multiply multiplies. */
	is_multiply = pdf_object_is_name(mode, "Multiply");
	if (is_multiply) {
		run->stack[run->depth].blend = PDF_BLEND_MULTIPLY;
		return;
	}

	/* Everything else draws normally; a mode other than Normal is left out. */
	run->stack[run->depth].blend = PDF_BLEND_NORMAL;
	known = blend_known(mode);
	if (!known)
		run->flags |= PDF_DISPLAY_SKIPPED;
}

/* Tells whether a blend mode is one the interpreter draws (Normal, Compatible, Multiply). */
static int
blend_known(
	struct pdf_object *mode)
{
	int is_name;

	/* The three names. */
	is_name = pdf_object_is_name(mode, "Normal");
	if (is_name)
		return 1;
	is_name = pdf_object_is_name(mode, "Compatible");
	if (is_name)
		return 1;
	is_name = pdf_object_is_name(mode, "Multiply");
	if (is_name)
		return 1;

	/* Any other mode. */
	return 0;
}

/*
 * Finds the resource the operator's last operand names in a category of
 * the resources (ExtGState, XObject, ColorSpace).
 */
static int
find_resource(
	struct content_run *run,
	struct pdf_object *resources,
	const char *category,
	struct pdf_object **found)
{
	const struct content_operand *operand;
	struct pdf_object *dictionary;
	size_t index;
	int differs;
	int error;

	/* The name is the last operand. */
	if (run->operand_count == 0)
		return PDF_EFORMAT;
	operand = &run->operands[run->operand_count - 1];
	if (operand->type != OPERAND_NAME)
		return PDF_EFORMAT;

	/* Finds the category's dictionary. */
	if (resources == NULL)
		return ENOENT;
	if (resources->type != PDF_OBJECT_DICTIONARY)
		return ENOENT;
	error = pdf_reader_resolve_key(run->document, resources, category, &dictionary);
	if (error != 0)
		return error;
	if (dictionary->type != PDF_OBJECT_DICTIONARY)
		return ENOENT;

	/* Finds the name among its keys (compared by bytes, since a name may hold a NUL). */
	for (index = 0; index < dictionary->count; index++) {
		if (dictionary->keys[index]->length != operand->length)
			continue;
		differs = memcmp(dictionary->keys[index]->bytes, operand->bytes, operand->length);
		if (differs != 0)
			continue;
		error = pdf_reader_resolve(run->document, dictionary->values[index], found);
		if (error != 0)
			return error;
		return 0;
	}

	/* The resources do not have the name. */
	return ENOENT;
}

/*
 * Tells how many components the colour space named by the operand has:
 * a device space by its name, or a space of the resources built on one.
 */
static int
space_components(
	struct content_run *run,
	struct pdf_object *resources,
	int *components)
{
	const struct content_operand *operand;
	struct pdf_object *space;
	struct pdf_object *family;
	struct pdf_object *count;
	int is_name;
	int error;

	/* The name is the last operand. */
	if (run->operand_count == 0)
		return PDF_EFORMAT;
	operand = &run->operands[run->operand_count - 1];
	if (operand->type != OPERAND_NAME)
		return PDF_EFORMAT;

	/* The device spaces by name. */
	*components = device_components(operand->bytes, operand->length);
	if (*components != 0)
		return 0;

	/* Any other name is a resource: a device space's name, or an array whose family decides. */
	error = find_resource(run, resources, "ColorSpace", &space);
	if (error != 0)
		return error;
	if (space->type == PDF_OBJECT_NAME) {
		*components = device_components(space->bytes, space->length);
		if (*components == 0)
			return ENOTSUP;
		return 0;
	}
	if (space->type != PDF_OBJECT_ARRAY)
		return PDF_EFORMAT;
	if (space->count == 0)
		return PDF_EFORMAT;
	error = pdf_reader_resolve(run->document, space->values[0], &family);
	if (error != 0)
		return error;

	/* The calibrated spaces. */
	is_name = pdf_object_is_name(family, "CalGray");
	if (is_name) {
		*components = 1;
		return 0;
	}
	is_name = pdf_object_is_name(family, "CalRGB");
	if (is_name) {
		*components = 3;
		return 0;
	}

	/* An ICC-based space by its profile's component count; anything else is from later stages. */
	is_name = pdf_object_is_name(family, "ICCBased");
	if (!is_name)
		return ENOTSUP;
	if (space->count < 2)
		return ENOTSUP;
	error = pdf_reader_resolve(run->document, space->values[1], &family);
	if (error != 0)
		return error;
	if (family->type != PDF_OBJECT_STREAM)
		return PDF_EFORMAT;
	error = pdf_reader_resolve_key(run->document, family, "N", &count);
	if (error != 0)
		return error;
	if (count->type != PDF_OBJECT_INTEGER)
		return PDF_EFORMAT;
	if (count->integer != 1 &&
	    count->integer != 3 &&
	    count->integer != 4)
		return ENOTSUP;

	/* Succeeded: the space's component count. */
	*components = (int)count->integer;
	return 0;
}

/* Tells how many components a device colour space's name has (0 for another name). */
static int
device_components(
	const unsigned char *name,
	size_t length)
{
	int differs;

	/* DeviceGray. */
	if (length == 10) {
		differs = memcmp(name, "DeviceGray", 10);
		if (differs == 0)
			return 1;
	}

	/* DeviceRGB. */
	if (length == 9) {
		differs = memcmp(name, "DeviceRGB", 9);
		if (differs == 0)
			return 3;
	}

	/* DeviceCMYK. */
	if (length == 10) {
		differs = memcmp(name, "DeviceCMYK", 10);
		if (differs == 0)
			return 4;
	}

	/* Another name. */
	return 0;
}

/* Sets the fill or stroke colour from a device space's values (1 gray, 3 RGB, 4 CMYK). */
static void
set_color(
	struct content_run *run,
	int stroke,
	int components,
	const double *values)
{
	struct content_state *state;
	double rgb[3];

	/* Converts the values to RGB. */
	if (components == 1) {
		rgb[0] = clamp_unit(values[0]);
		rgb[1] = rgb[0];
		rgb[2] = rgb[0];
	} else if (components == 3) {
		rgb[0] = clamp_unit(values[0]);
		rgb[1] = clamp_unit(values[1]);
		rgb[2] = clamp_unit(values[2]);
	} else {
		rgb[0] = (1.0 - clamp_unit(values[0])) * (1.0 - clamp_unit(values[3]));
		rgb[1] = (1.0 - clamp_unit(values[1])) * (1.0 - clamp_unit(values[3]));
		rgb[2] = (1.0 - clamp_unit(values[2])) * (1.0 - clamp_unit(values[3]));
	}

	/* Stores the colour and its space in the level in force. */
	state = &run->stack[run->depth];
	if (stroke) {
		memcpy(state->stroke, rgb, sizeof(rgb));
		state->stroke_components = components;
		state->stroke_usable = 1;
	} else {
		memcpy(state->fill, rgb, sizeof(rgb));
		state->fill_components = components;
		state->fill_usable = 1;
	}
}

/* Adds a segment to the path (count points from coordinates), tracking the current point. */
static int
path_add(
	struct content_run *run,
	enum pdf_path_verb verb,
	const double *coordinates,
	size_t count)
{
	struct content_path *path;
	unsigned char *verbs;
	struct pdf_point *points;
	double start[2];
	size_t capacity;
	size_t index;
	int after_close;
	int error;

	/* Whether the path's last segment closed its subpath. */
	path = &run->path;
	after_close = 0;
	if (path->verb_count > 0 && path->verbs[path->verb_count - 1] == PDF_PATH_CLOSE)
		after_close = 1;

	/* A close right after a close adds nothing. */
	if (verb == PDF_PATH_CLOSE && after_close)
		return 0;

	/* A line or a curve after a close starts a new subpath at the closed subpath's start, as PDF defines. */
	if (after_close && (verb == PDF_PATH_LINE || verb == PDF_PATH_CUBIC)) {
		start[0] = path->start.x;
		start[1] = path->start.y;
		error = path_add(run, PDF_PATH_MOVE, start, 1);
		if (error != 0)
			return error;
	}

	/* Refuses a path past the list's point limit. */
	if (path->point_count + count > PDF_DISPLAY_POINTS_MAX)
		return ENOMEM;
	if (path->verb_count + 1 > PDF_DISPLAY_POINTS_MAX)
		return ENOMEM;

	/* Grows the verbs when full. */
	if (path->verb_count == path->verb_capacity) {
		capacity = path->verb_capacity * 2;
		if (capacity == 0)
			capacity = 64;
		verbs = realloc(path->verbs, capacity);
		if (verbs == NULL)
			return ENOMEM;
		path->verbs = verbs;
		path->verb_capacity = capacity;
	}

	/* Grows the points when they do not fit. */
	if (path->point_count + count > path->point_capacity) {
		capacity = path->point_capacity * 2;
		if (capacity == 0)
			capacity = 64;
		while (capacity < path->point_count + count)
			capacity *= 2;
		points = realloc(path->points, capacity * sizeof(*points));
		if (points == NULL)
			return ENOMEM;
		path->points = points;
		path->point_capacity = capacity;
	}

	/* Appends the verb and its points. */
	path->verbs[path->verb_count] = (unsigned char)verb;
	path->verb_count++;
	for (index = 0; index < count; index++) {
		path->points[path->point_count].x = coordinates[index * 2];
		path->points[path->point_count].y = coordinates[index * 2 + 1];
		path->point_count++;
	}

	/* The current point: the segment's end, a move's start, or a closed subpath's start. */
	if (verb == PDF_PATH_CLOSE) {
		path->current = path->start;
	} else {
		path->current = path->points[path->point_count - 1];
	}
	if (verb == PDF_PATH_MOVE)
		path->start = path->current;
	path->has_current = 1;

	/* Succeeded: the segment is part of the path. */
	return 0;
}

/* Adds a closed rectangle subpath from one corner to the opposite one. */
static int
add_rectangle(
	struct content_run *run,
	double left,
	double bottom,
	double right,
	double top)
{
	double corners[8];
	int error;

	/* The corners from the first, around. */
	corners[0] = left;
	corners[1] = bottom;
	corners[2] = right;
	corners[3] = bottom;
	corners[4] = right;
	corners[5] = top;
	corners[6] = left;
	corners[7] = top;

	/* The move to the first corner. */
	error = path_add(run, PDF_PATH_MOVE, corners, 1);
	if (error != 0)
		return error;

	/* The three sides to the other corners. */
	error = path_add(run, PDF_PATH_LINE, corners + 2, 1);
	if (error != 0)
		return error;
	error = path_add(run, PDF_PATH_LINE, corners + 4, 1);
	if (error != 0)
		return error;
	error = path_add(run, PDF_PATH_LINE, corners + 6, 1);
	if (error != 0)
		return error;

	/* The close back to the first corner. */
	error = path_add(run, PDF_PATH_CLOSE, corners, 0);
	if (error != 0)
		return error;

	/* Succeeded: the rectangle is a subpath. */
	return 0;
}

/* Ends the path being built. */
static void
path_clear(
	struct content_run *run)
{
	/* The arrays are kept for the next path. */
	run->path.verb_count = 0;
	run->path.point_count = 0;
	run->path.has_current = 0;
}

/* Paints the path (fill, stroke, both, or neither), applies a pending clip, and ends the path. */
static void
paint_path(
	struct content_run *run,
	int fill,
	enum pdf_fill_rule rule,
	int stroke)
{
	/* Paints only a path that has something. */
	if (run->path.verb_count > 0 && !run->stopped) {
		if (fill)
			fill_path(run, rule);
		if (stroke && !run->stopped)
			stroke_path(run);
		if (run->clip_pending && !run->stopped)
			push_clip(run);
	}

	/* The clip and the path are used up. */
	run->clip_pending = 0;
	path_clear(run);
}

/* Fills the path in the fill colour. */
static void
fill_path(
	struct content_run *run,
	enum pdf_fill_rule rule)
{
	struct content_state *state;
	struct pdf_display_item style;
	struct pdf_point *transformed;
	int error;

	/* A colour the interpreter cannot read is not painted. */
	state = &run->stack[run->depth];
	if (!state->fill_usable) {
		run->flags |= PDF_DISPLAY_SKIPPED;
		return;
	}

	/* Transforms the path to the page. */
	error = transform_path(run, run->path.points, run->path.point_count, &transformed);
	if (error != 0) {
		stop_for(run, error);
		return;
	}

	/* Adds the fill. */
	memset(&style, 0, sizeof(style));
	style.rule = rule;
	style.red = state->fill[0];
	style.green = state->fill[1];
	style.blue = state->fill[2];
	style.alpha = state->fill_alpha;
	style.blend = state->blend;
	error = pdf_display_add_path(run->builder, PDF_ITEM_FILL, run->path.verbs, run->path.verb_count, transformed, run->path.point_count, &style);
	if (error != 0)
		stop_for(run, error);
}

/* Strokes the path in the stroke colour: its outline, from the stroker, filled. */
static void
stroke_path(
	struct content_run *run)
{
	struct content_state *state;
	struct pdf_stroke_style stroke;
	struct pdf_display_item style;
	struct pdf_point *transformed;
	unsigned char *verbs;
	struct pdf_point *points;
	size_t verb_count;
	size_t point_count;
	double scale;
	int error;

	/* A colour the interpreter cannot read is not painted. */
	state = &run->stack[run->depth];
	if (!state->stroke_usable) {
		run->flags |= PDF_DISPLAY_SKIPPED;
		return;
	}

	/* A user space squashed to nothing draws nothing. */
	scale = matrix_scale(state->ctm);
	if (scale < 1e-9)
		return;

	/* The style in user space: the tolerance and the thinnest line are the page's, scaled back. */
	memset(&stroke, 0, sizeof(stroke));
	stroke.width = state->line_width;
	if (stroke.width * scale < PDF_CONTENT_HAIRLINE && stroke.width <= 0.0)
		stroke.width = PDF_CONTENT_HAIRLINE / scale;
	stroke.cap = state->line_cap;
	stroke.join = state->line_join;
	stroke.miter_limit = state->miter_limit;
	stroke.dash = state->dash;
	stroke.dash_count = state->dash_count;
	stroke.dash_phase = state->dash_phase;
	stroke.tolerance = PDF_CONTENT_TOLERANCE / scale;

	/* Makes the outline. */
	error = pdf_stroke_path(run->path.verbs, run->path.verb_count, run->path.points, run->path.point_count, &stroke, &verbs, &verb_count, &points, &point_count);
	if (error == PDF_EFORMAT)
		return;
	if (error != 0) {
		stop_for(run, error);
		return;
	}

	/* Transforms it to the page. */
	error = transform_path(run, points, point_count, &transformed);
	if (error != 0) {
		free(verbs);
		free(points);
		stop_for(run, error);
		return;
	}

	/* Adds the outline as a nonzero fill in the stroke's colour and alpha. */
	memset(&style, 0, sizeof(style));
	style.rule = PDF_FILL_NONZERO;
	style.red = state->stroke[0];
	style.green = state->stroke[1];
	style.blue = state->stroke[2];
	style.alpha = state->stroke_alpha;
	style.blend = state->blend;
	error = 0;
	if (verb_count > 0)
		error = pdf_display_add_path(run->builder, PDF_ITEM_FILL, verbs, verb_count, transformed, point_count, &style);
	free(verbs);
	free(points);
	if (error != 0)
		stop_for(run, error);
}

/* Pushes the path as a clip, which the level's Q ends. */
static void
push_clip(
	struct content_run *run)
{
	struct pdf_display_item style;
	struct pdf_point *transformed;
	int error;

	/* Refuses clips nested past the limit. */
	if (run->clip_depth >= PDF_CONTENT_CLIPS_MAX) {
		run->flags |= PDF_DISPLAY_LIMITED;
		return;
	}

	/* Transforms the path to the page. */
	error = transform_path(run, run->path.points, run->path.point_count, &transformed);
	if (error != 0) {
		stop_for(run, error);
		return;
	}

	/* Adds the clip and counts it at the level. */
	memset(&style, 0, sizeof(style));
	style.rule = run->clip_rule;
	error = pdf_display_add_path(run->builder, PDF_ITEM_CLIP_PUSH, run->path.verbs, run->path.verb_count, transformed, run->path.point_count, &style);
	if (error != 0) {
		stop_for(run, error);
		return;
	}
	run->stack[run->depth].clips++;
	run->clip_depth++;
}

/* Transforms points by the CTM into the run's scratch array. */
static int
transform_path(
	struct content_run *run,
	const struct pdf_point *points,
	size_t count,
	struct pdf_point **transformed)
{
	const double *ctm;
	struct pdf_point *grown;
	size_t index;

	/* Grows the scratch array when the points do not fit. */
	if (count > run->scratch_capacity) {
		if (count > PDF_DISPLAY_POINTS_MAX)
			return ENOMEM;
		grown = realloc(run->scratch, count * sizeof(*grown));
		if (grown == NULL)
			return ENOMEM;
		run->scratch = grown;
		run->scratch_capacity = count;
	}

	/* Applies the CTM to each point. */
	ctm = run->stack[run->depth].ctm;
	for (index = 0; index < count; index++) {
		run->scratch[index].x = ctm[0] * points[index].x + ctm[2] * points[index].y + ctm[4];
		run->scratch[index].y = ctm[1] * points[index].x + ctm[3] * points[index].y + ctm[5];
	}

	/* Succeeded: the transformed copy. */
	*transformed = run->scratch;
	return 0;
}

/* Reports how much a matrix scales lengths on average: the square root of its determinant's size. */
static double
matrix_scale(
	const double matrix[6])
{
	double determinant;

	/* The area scale of the linear part. */
	determinant = matrix[0] * matrix[3] - matrix[1] * matrix[2];
	if (determinant < 0.0)
		determinant = -determinant;

	/* Reports its square root. */
	return sqrt(determinant);
}

/* Draws the XObject the operand names: an image, or a form's content. */
static void
draw_xobject(
	struct content_run *run,
	struct pdf_object *resources)
{
	struct pdf_object *xobject;
	struct pdf_object *subtype;
	int is_image;
	int is_form;
	int error;

	/* Finds the XObject; a missing one is left out. */
	error = find_resource(run, resources, "XObject", &xobject);
	if (error != 0) {
		run->flags |= PDF_DISPLAY_SKIPPED;
		return;
	}
	if (xobject->type != PDF_OBJECT_STREAM) {
		run->flags |= PDF_DISPLAY_SKIPPED;
		return;
	}

	/* Draws it by its subtype. */
	error = pdf_reader_resolve_key(run->document, xobject, "Subtype", &subtype);
	if (error != 0) {
		run->flags |= PDF_DISPLAY_DAMAGED;
		return;
	}
	is_image = pdf_object_is_name(subtype, "Image");
	is_form = pdf_object_is_name(subtype, "Form");
	if (is_image)
		draw_image(run, xobject);
	if (is_form)
		run_form(run, xobject, resources);
}

/* Draws an image XObject on the unit square of the user space. */
static void
draw_image(
	struct content_run *run,
	struct pdf_object *image)
{
	struct content_state *state;
	struct pdf_display_item style;
	unsigned char *pixels;
	size_t width;
	size_t height;
	int interpolate;
	int error;

	/* Decodes the image; one the reader cannot decode is left out. */
	state = &run->stack[run->depth];
	error = pdf_image_decode(run->document, image, state->fill, &pixels, &width, &height, &interpolate, &run->flags);
	if (error == ENOMEM) {
		stop_for(run, ENOMEM);
		return;
	}
	if (error != 0) {
		run->flags |= PDF_DISPLAY_SKIPPED;
		return;
	}

	/*
	 * Places it: the image's first row is at the top of the unit square,
	 * where y is 1 in user space, so (u, v) maps to the user point (u, 1 - v).
	 */
	memset(&style, 0, sizeof(style));
	style.image_width = width;
	style.image_height = height;
	style.matrix[0] = state->ctm[0];
	style.matrix[1] = state->ctm[1];
	style.matrix[2] = -state->ctm[2];
	style.matrix[3] = -state->ctm[3];
	style.matrix[4] = state->ctm[2] + state->ctm[4];
	style.matrix[5] = state->ctm[3] + state->ctm[5];
	style.alpha = state->fill_alpha;
	style.blend = state->blend;
	style.interpolate = interpolate;
	error = pdf_display_add_image(run->builder, pixels, &style);
	if (error != 0)
		stop_for(run, error);
}

/*
 * Runs a form XObject's content in a level of its own: its matrix
 * concatenated, its bounding box as a clip, its own resources (or the
 * caller's when it has none).
 */
static void
run_form(
	struct content_run *run,
	struct pdf_object *form,
	struct pdf_object *resources)
{
	struct pdf_object *matrix_object;
	struct pdf_object *box_object;
	struct pdf_object *form_resources;
	const unsigned char *data;
	unsigned char *owned;
	double matrix[6];
	double box[4];
	size_t data_size;
	size_t saved_base;
	size_t saved_ignored;
	size_t form_depth;
	size_t index;
	int dct;
	int error;

	/* Refuses forms nested past the limit (a form that draws itself ends here), or without a level left for them. */
	if (run->form_depth >= PDF_CONTENT_FORMS_MAX) {
		run->flags |= PDF_DISPLAY_LIMITED;
		return;
	}
	if (run->depth + 1 >= PDF_CONTENT_STACK_MAX) {
		run->flags |= PDF_DISPLAY_LIMITED;
		return;
	}

	/* Reads the form's matrix, the identity by default. */
	matrix[0] = 1.0;
	matrix[1] = 0.0;
	matrix[2] = 0.0;
	matrix[3] = 1.0;
	matrix[4] = 0.0;
	matrix[5] = 0.0;
	error = pdf_reader_resolve_key(run->document, form, "Matrix", &matrix_object);
	if (error != 0)
		return;
	if (matrix_object->type == PDF_OBJECT_ARRAY && matrix_object->count == 6) {
		for (index = 0; index < 6; index++) {
			error = pdf_object_number(matrix_object->values[index], &matrix[index]);
			if (error != 0)
				return;
		}
	}

	/* Reads its bounding box, which it needs. */
	error = pdf_reader_resolve_key(run->document, form, "BBox", &box_object);
	if (error != 0) {
		run->flags |= PDF_DISPLAY_DAMAGED;
		return;
	}
	if (box_object->type != PDF_OBJECT_ARRAY || box_object->count != 4) {
		run->flags |= PDF_DISPLAY_DAMAGED;
		return;
	}
	for (index = 0; index < 4; index++) {
		error = pdf_object_number(box_object->values[index], &box[index]);
		if (error != 0) {
			run->flags |= PDF_DISPLAY_DAMAGED;
			return;
		}
	}

	/* Its resources, or the caller's. */
	error = pdf_reader_resolve_key(run->document, form, "Resources", &form_resources);
	if (error != 0)
		form_resources = resources;
	else if (form_resources->type != PDF_OBJECT_DICTIONARY)
		form_resources = resources;

	/* Decodes its content; one that cannot be decoded is left out. */
	error = pdf_filter_decode(run->document, form, 0, &data, &data_size, &owned, &dct);
	if (error == ENOMEM) {
		stop_for(run, ENOMEM);
		return;
	}
	if (error != 0) {
		run->flags |= PDF_DISPLAY_SKIPPED;
		return;
	}

	/* Opens the form's level, which starts from the caller's state; the form's own q past the limit are its own. */
	save_state(run);
	form_depth = run->depth;
	saved_base = run->base_depth;
	saved_ignored = run->ignored_saves;
	run->base_depth = form_depth;
	run->ignored_saves = 0;
	run->form_depth++;

	/* Applies the form's matrix and clips to its bounding box. */
	concat_matrix(run->stack[run->depth].ctm, matrix);
	path_clear(run);
	error = add_rectangle(run, box[0], box[1], box[2], box[3]);
	if (error == 0) {
		run->clip_pending = 1;
		run->clip_rule = PDF_FILL_NONZERO;
		paint_path(run, 0, PDF_FILL_NONZERO, 0);
	}

	/* Runs the content, then closes every level it opened and the form's own. */
	run_content(run, data, data_size, form_resources);
	free(owned);
	unwind_to(run, form_depth - 1);
	run->base_depth = saved_base;
	run->ignored_saves = saved_ignored;
	run->form_depth--;

	/* The caller's operands were the form's name; the form's own are gone with its content. */
	run->operand_count = 0;
}

/*
 * Skips an inline image (BI, its dictionary, ID, its data, EI), which is
 * drawn from stage 2.
 *
 * The data ends at an EI that stands alone between white space (or the
 * content's end); the dictionary's keys and values are read as tokens up
 * to ID.
 */
static void
skip_inline_image(
	struct content_run *run,
	struct pdf_lexer *lexer)
{
	struct pdf_token token;
	const unsigned char *data;
	size_t position;
	int is_id;
	int before_space;
	int after_space;
	int error;

	/* The image is left out. */
	run->flags |= PDF_DISPLAY_SKIPPED;

	/* Reads the dictionary's tokens up to ID. */
	for (;;) {
		error = pdf_lexer_next(lexer, &token);
		if (error != 0) {
			stop_for(run, PDF_EFORMAT);
			return;
		}
		if (token.type == PDF_TOKEN_END) {
			stop_for(run, PDF_EFORMAT);
			return;
		}
		is_id = pdf_token_is_keyword(&token, "ID");
		if (is_id)
			break;
	}

	/* Finds the EI that ends the data, after the one white-space byte that follows ID. */
	data = lexer->data;
	for (position = lexer->position + 1; position + 1 < lexer->size; position++) {
		/* Only an E followed by an I can end the data. */
		if (data[position] != 'E')
			continue;
		if (data[position + 1] != 'I')
			continue;

		/* The EI must follow white space. */
		before_space = is_white(data[position - 1]);
		if (!before_space)
			continue;

		/* And be followed by white space or the end. */
		after_space = 1;
		if (position + 2 < lexer->size)
			after_space = is_white(data[position + 2]);
		if (!after_space)
			continue;

		/* The content goes on after the EI. */
		lexer->position = position + 2;
		return;
	}

	/* An image without its end damages the rest of the content. */
	lexer->position = lexer->size;
	run->flags |= PDF_DISPLAY_DAMAGED;
}

/* Tells whether a byte is white space around an inline image's EI. */
static int
is_white(
	unsigned char character)
{
	/* The white-space bytes of PDF. */
	switch (character) {
	case ' ':
	case '\n':
	case '\r':
	case '\t':
	case '\f':
	case '\0':
		return 1;
	default:
		break;
	}

	/* Anything else is part of a token. */
	return 0;
}

/*
 * Stops the page: a limit or no memory marks the list LIMITED, anything
 * else DAMAGED.
 */
static void
stop_for(
	struct content_run *run,
	int error)
{
	/* Marks why the page stops. */
	if (error == ENOMEM) {
		run->flags |= PDF_DISPLAY_LIMITED;
	} else {
		run->flags |= PDF_DISPLAY_DAMAGED;
	}

	/* Nothing more of the page is run. */
	run->stopped = 1;
}

/* Clamps a colour or alpha value to 0..1, NaN to 0. */
static double
clamp_unit(
	double value)
{
	/* Below the range, NaN included. */
	if (!(value > 0.0))
		return 0.0;

	/* Above the range. */
	if (value > 1.0)
		return 1.0;

	/* Within the range as it is. */
	return value;
}
