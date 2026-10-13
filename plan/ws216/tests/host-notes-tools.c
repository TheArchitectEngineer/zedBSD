/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws216-p003: the host test of Notes' drawing tools in the model and the
 * file formats (plan/ws216/phase003/phase.md section 8).
 *
 *   host-notes-tools run DIR             the model, the edit data 3.0, the journal 3, the eraser, the PDF (B2, B5 to
 *                                        B8); writes DIR/t30.pdf, DIR/t30.bin, DIR/pens.bin, DIR/grain-new.pdf
 *   host-notes-tools old DIR             reads what host-notes-old made in DIR (B3, B5)
 *   host-notes-tools base DIR            writes DIR/base.pdf: another program's PDF of an A4 page and a smaller one
 *   host-notes-tools grain-pages DIR     writes on DIR/base-rot.pdf (pages of two sizes, the second turned), and
 *                                        saves DIR/grain-foreign.pdf
 *   host-notes-tools grain PPM X0 Y0 DPI checks the pencil's grain in a crop of a page drawn black at full pressure (B9)
 *   host-notes-tools m9 PDF              a 3.0 notebook the old Notes wrote a pen on (B4)
 *   host-notes-tools save BIN PDF        decodes edit data (or opens a notebook, BIN.pdf) and saves it as a PDF (the old Notes' identifier)
 *   host-notes-tools reencode BIN OUT    decodes edit data and encodes it again
 *   host-notes-tools ppmdiff A B         the mean difference of two pictures and the share of pixels apart by 64
 */

#include "notes.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* The time base of every test document. */
#define TEST_TIME_BASE		1760000000000ULL

/* The tolerance of the eraser's checks, in points. */
#define TEST_TOLERANCE		0.01

/*
 * A copy of the corners of a stroke's shapes, to compare after the
 * stroke has been cut: corners of the shapes of one kind (the
 * watercolour's column; every pencil range is of one kind).
 */
struct corner_set {
	double *x;
	double *y;
	unsigned *kind;
	size_t count;
};

static int failures;
static int checks;

static void check(int condition, const char *what);
static struct notes_stroke *make_tool(struct notes_document *document, unsigned tool, float width, float x0, float y0, float x1, float y1, float wave, unsigned count, int seed);
static struct notes_stroke *make_fill(struct notes_document *document, float x, float y, float size);
static int same_stroke(const struct notes_stroke *a, const struct notes_stroke *b);
static int same_document(const struct notes_document *a, const struct notes_document *b);
static int encode_equal(const struct notes_document *a, const struct notes_document *b);
static int write_bin(const struct notes_document *document, const char *path);
static unsigned char *read_all(const char *path, size_t *size);
static int save_fixed(struct notes_document *document, const char *path);
static void build_tools(struct notes_document *document);
static void check_round_trip(const char *dir);
static void check_versions(void);
static void check_journal(const char *dir);
static void check_eraser(void);
static void check_erase_tool(unsigned tool, float width, int seed);
static void check_pens(void);
static void check_pdf(const char *dir);
static void corners_copy(const struct notes_stroke *stroke, struct corner_set *set);
static void corners_free(struct corner_set *set);
static unsigned shape_kind(const struct notes_stroke *stroke, size_t shape);
static int corners_near(const struct corner_set *set, unsigned kind, double x, double y);
static int winding(const struct pdf_point *polygon, size_t count, double x, double y);
static double edge_distance(double px, double py, double ax, double ay, double bx, double by);
static int run_old(const char *dir);
static int run_base(const char *dir);
static int run_grain_pages(const char *dir);
static int run_grain(const char *path, long x0, long y0, double dpi);
static int run_m9(const char *path);
static unsigned char *read_ppm(const char *path, size_t *width, size_t *height);
static int journal_version(const char *path);

int
main(
	int argc,
	char **argv)
{
	struct notes_document document;
	unsigned char *data;
	size_t size;
	int error;

	/* The model and the formats. */
	if (argc == 3 && strcmp(argv[1], "run") == 0) {
		(void)mkdir(argv[2], 0700);
		check_versions();
		check_round_trip(argv[2]);
		check_journal(argv[2]);
		check_eraser();
		check_pens();
		check_pdf(argv[2]);
		printf("host-notes-tools: %d checks, %d failed\n", checks, failures);
		return failures != 0;
	}

	/* What the old Notes wrote. */
	if (argc == 3 && strcmp(argv[1], "old") == 0) {
		error = run_old(argv[2]);
		printf("host-notes-tools old: %d checks, %d failed\n", checks, failures);
		return error != 0 || failures != 0;
	}

	/* Another program's PDF of two sizes. */
	if (argc == 3 && strcmp(argv[1], "base") == 0)
		return run_base(argv[2]) != 0;

	/* The grain on pages of other sizes and turned. */
	if (argc == 3 && strcmp(argv[1], "grain-pages") == 0) {
		error = run_grain_pages(argv[2]);
		printf("host-notes-tools grain-pages: %d checks, %d failed\n", checks, failures);
		return error != 0 || failures != 0;
	}

	/* The grain in a drawn crop. */
	if (argc == 6 && strcmp(argv[1], "grain") == 0) {
		error = run_grain(argv[2], atol(argv[3]), atol(argv[4]), atof(argv[5]));
		return error != 0;
	}

	/* The 3.0 notebook the old Notes wrote on. */
	if (argc == 3 && strcmp(argv[1], "m9") == 0) {
		error = run_m9(argv[2]);
		printf("host-notes-tools m9: %d checks, %d failed\n", checks, failures);
		return error != 0 || failures != 0;
	}

	/* Edit data (or a notebook, opened) saved, or encoded again. */
	if (argc == 4 && (strcmp(argv[1], "save") == 0 || strcmp(argv[1], "reencode") == 0)) {
		size = strlen(argv[2]);
		if (size > 4U && strcmp(argv[2] + size - 4U, ".pdf") == 0) {
			unsigned opened;

			error = notes_open_pdf(argv[2], &document, &opened);
		} else {
			data = read_all(argv[2], &size);
			error = EINVAL;
			if (data != NULL)
				error = notes_decode_document(data, size, &document);
			free(data);
		}
		if (error == 0) {
			if (strcmp(argv[1], "save") == 0)
				error = save_fixed(&document, argv[3]);
			else
				error = write_bin(&document, argv[3]);
			notes_document_free(&document);
		}
		if (error != 0)
			printf("FAIL: %s (%d)\n", argv[1], error);
		return error != 0;
	}

	/* Two pictures compared. */
	if (argc == 4 && strcmp(argv[1], "ppmdiff") == 0) {
		unsigned char *a;
		unsigned char *b;
		size_t aw;
		size_t ah;
		size_t bw;
		size_t bh;
		size_t index;
		size_t apart;
		double sum;
		int difference;

		a = read_ppm(argv[2], &aw, &ah);
		b = read_ppm(argv[3], &bw, &bh);
		if (a == NULL || b == NULL || aw != bw || ah != bh) {
			printf("FAIL: ppmdiff of pictures that do not match\n");
			return 1;
		}
		sum = 0.0;
		apart = 0;
		for (index = 0; index < aw * ah * 3U; index++) {
			difference = abs((int)a[index] - (int)b[index]);
			sum += difference;
			if (difference > 64)
				apart++;
		}
		printf("ppmdiff mean=%.3f apart=%.4f%%\n", sum / (double)(aw * ah * 3U), 100.0 * (double)apart / (double)(aw * ah * 3U));
		free(a);
		free(b);
		return 0;
	}

	fprintf(stderr, "usage: host-notes-tools run DIR | old DIR | grain-pages DIR | grain PPM X0 Y0 DPI | m9 PDF | save BIN PDF | reencode BIN OUT | ppmdiff A B\n");
	return 2;
}

/* Counts a check, and reports it when it fails. */
static void
check(
	int condition,
	const char *what)
{
	checks++;
	if (condition)
		return;
	printf("FAIL: %s\n", what);
	failures++;
}

/*
 * Makes a stroke of a tool along a wave, with a pressure that rises and
 * falls (with a wavering of the seed's) and a speed that changes (the
 * samples' times), not yet baked.
 */
static struct notes_stroke *
make_tool(
	struct notes_document *document,
	unsigned tool,
	float width,
	float x0,
	float y0,
	float x1,
	float y1,
	float wave,
	unsigned count,
	int seed)
{
	struct notes_stroke *stroke;
	struct notes_point point;
	unsigned index;
	uint32_t time;
	double share;
	double pressure;
	uint32_t id;

	id = document->next_id;
	document->next_id++;
	stroke = notes_stroke_create(id, tool, 0x2a6fdbffU, width, document->time_base + 1000U * id);
	time = 0U;
	for (index = 0; index < count; index++) {
		share = (double)index / (double)(count - 1U);
		memset(&point, 0, sizeof(point));
		point.x = (float)(x0 + (x1 - x0) * share);
		point.y = (float)(y0 + (y1 - y0) * share + wave * sin(share * 7.0 + seed));
		pressure = sin(share * 3.14159) * (0.8 + 0.2 * sin(share * 23.0 + seed));
		if (pressure < 0.0)
			pressure = 0.0;
		point.pressure = (uint16_t)(65535.0 * pressure);
		point.tilt_x = (int16_t)(index * 5U);
		point.tilt_y = (int16_t)(seed * 3);
		point.time_ms = time;
		time += 4U + (index * 7U + (unsigned)seed) % 9U;
		(void)notes_stroke_append(stroke, &point);
	}
	stroke->has_tilt = 1;
	return stroke;
}

/* Makes a fill: a square of a size with a square hole, as two contours. */
static struct notes_stroke *
make_fill(
	struct notes_document *document,
	float x,
	float y,
	float size)
{
	static const float outer[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
	static const float inner[4][2] = { { 0.3f, 0.3f }, { 0.3f, 0.7f }, { 0.7f, 0.7f }, { 0.7f, 0.3f } };
	struct notes_stroke *stroke;
	struct notes_point point;
	unsigned index;
	uint32_t id;

	id = document->next_id;
	document->next_id++;
	stroke = notes_stroke_create(id, NOTES_TOOL_FILL, 0xe85d04ffU, 1.0f, document->time_base);
	memset(&point, 0, sizeof(point));
	for (index = 0; index < 4U; index++) {
		point.x = x + size * outer[index][0];
		point.y = y + size * outer[index][1];
		(void)notes_stroke_append(stroke, &point);
	}
	for (index = 0; index < 4U; index++) {
		point.x = x + size * inner[index][0];
		point.y = y + size * inner[index][1];
		(void)notes_stroke_append(stroke, &point);
	}
	stroke->contours = malloc(2U * sizeof(stroke->contours[0]));
	stroke->contours[0] = 4U;
	stroke->contours[1] = 4U;
	stroke->contour_count = 2U;
	return stroke;
}

/* Tells whether two strokes are the same: tool, samples, constants and baked values. */
static int
same_stroke(
	const struct notes_stroke *a,
	const struct notes_stroke *b)
{
	size_t index;

	if (a->id != b->id || a->tool != b->tool || a->color != b->color || a->width != b->width)
		return 0;
	if (a->constants != b->constants || a->has_tilt != b->has_tilt || a->start_ms != b->start_ms)
		return 0;
	if (a->point_count != b->point_count || a->factor_columns != b->factor_columns || a->contour_count != b->contour_count)
		return 0;
	for (index = 0; index < a->point_count; index++) {
		if (a->points[index].x != b->points[index].x || a->points[index].y != b->points[index].y)
			return 0;
		if (a->points[index].pressure != b->points[index].pressure || a->points[index].time_ms != b->points[index].time_ms)
			return 0;
		if (a->points[index].tilt_x != b->points[index].tilt_x || a->points[index].tilt_y != b->points[index].tilt_y)
			return 0;
	}
	if ((a->factors == NULL) != (b->factors == NULL) || (a->steps == NULL) != (b->steps == NULL))
		return 0;
	if (a->factors != NULL && memcmp(a->factors, b->factors, a->point_count * a->factor_columns * sizeof(a->factors[0])) != 0)
		return 0;
	if (a->steps != NULL && memcmp(a->steps, b->steps, a->point_count) != 0)
		return 0;
	for (index = 0; index < a->contour_count; index++) {
		if (a->contours[index] != b->contours[index])
			return 0;
	}
	return 1;
}

/* Tells whether two documents have the same pages and strokes. */
static int
same_document(
	const struct notes_document *a,
	const struct notes_document *b)
{
	size_t page;
	size_t stroke;

	if (a->page_count != b->page_count || a->time_base != b->time_base || a->next_id != b->next_id)
		return 0;
	for (page = 0; page < a->page_count; page++) {
		if (a->pages[page]->stroke_count != b->pages[page]->stroke_count)
			return 0;
		for (stroke = 0; stroke < a->pages[page]->stroke_count; stroke++) {
			if (!same_stroke(a->pages[page]->strokes[stroke], b->pages[page]->strokes[stroke]))
				return 0;
		}
	}
	return 1;
}

/* Tells whether two documents encode to the same bytes. */
static int
encode_equal(
	const struct notes_document *a,
	const struct notes_document *b)
{
	struct notes_buffer left;
	struct notes_buffer right;
	int equal;

	notes_buffer_init(&left);
	notes_buffer_init(&right);
	equal = 0;
	if (notes_encode_document(a, &left) == 0 && notes_encode_document(b, &right) == 0)
		equal = left.length == right.length && memcmp(left.data, right.data, left.length) == 0;
	notes_buffer_free(&left);
	notes_buffer_free(&right);
	return equal;
}

/* Builds the drawing tools' notebook: a brush, a pencil, a watercolour, a fill and a pen on page 1, a pencil on page 2. */
static void
build_tools(
	struct notes_document *document)
{
	check(notes_document_init(document, TEST_TIME_BASE) == 0, "tools: the document");
	check(notes_document_add_page(document, 1) == 0, "tools: page 2");
	check(notes_document_add_stroke(document, 0, make_tool(document, NOTES_TOOL_BRUSH, 6.0f, 60.0f, 100.0f, 520.0f, 160.0f, 20.0f, 140U, 1)) == 0, "tools: a brush stroke");
	check(notes_document_add_stroke(document, 0, make_tool(document, NOTES_TOOL_PENCIL, 2.4f, 60.0f, 250.0f, 520.0f, 260.0f, 12.0f, 160U, 2)) == 0, "tools: a pencil stroke");
	check(notes_document_add_stroke(document, 0, make_tool(document, NOTES_TOOL_WATERCOLOR, 16.0f, 80.0f, 380.0f, 500.0f, 420.0f, 25.0f, 120U, 3)) == 0, "tools: a watercolour stroke");
	check(notes_document_add_stroke(document, 0, make_fill(document, 100.0f, 500.0f, 120.0f)) == 0, "tools: a fill with a hole");
	check(notes_document_add_stroke(document, 0, make_tool(document, NOTES_TOOL_PEN, 3.0f, 60.0f, 700.0f, 520.0f, 720.0f, 10.0f, 80U, 4)) == 0, "tools: a pen stroke");
	check(notes_document_add_stroke(document, 1, make_tool(document, NOTES_TOOL_PENCIL, 1.4f, 60.0f, 100.0f, 500.0f, 140.0f, 30.0f, 200U, 5)) == 0, "tools: a pencil stroke on page 2");
}

/* The edit data 3.0 kept and read back (B2). */
static void
check_round_trip(
	const char *dir)
{
	struct notes_document document;
	struct notes_document copy;
	struct notes_buffer first;
	struct notes_buffer second;
	struct notes_stroke *stroke;
	char path[4096];
	size_t index;
	int error;

	build_tools(&document);

	/* Every stroke of the tools is baked, and has its shapes. */
	for (index = 0; index < document.pages[0]->stroke_count; index++) {
		stroke = document.pages[0]->strokes[index];
		check(notes_stroke_baked(stroke), "round trip: every stroke is baked when it is put on the page");
		check(notes_stroke_outline(stroke) == 0 && stroke->shape_count > 0U, "round trip: every stroke has shapes");
	}
	stroke = document.pages[0]->strokes[0];
	check(stroke->factor_columns == 1U && stroke->steps == NULL && stroke->constants == NOTES_TOOL_CONSTANTS && stroke->shape_count == 1U, "round trip: the brush has one column and one shape");
	stroke = document.pages[0]->strokes[1];
	check(stroke->factor_columns == 1U && stroke->steps != NULL && stroke->shapes[0].paint == NOTES_PAINT_GRAIN, "round trip: the pencil has steps and shapes of the grain");
	stroke = document.pages[0]->strokes[2];
	check(stroke->factor_columns == 3U && stroke->shape_count == 3U && stroke->shapes[0].blend == NOTES_BLEND_MULTIPLY, "round trip: the watercolour has three multiplied shapes");
	stroke = document.pages[0]->strokes[3];
	check(stroke->shape_count == 1U && stroke->shapes[0].contour_count == 2U && stroke->outline_count == 4U, "round trip: the fill has one shape of two contours");
	(void)notes_stroke_outline(document.pages[1]->strokes[0]);
	printf("pencil ranges: page 1 %lu, page 2 %lu\n", (unsigned long)document.pages[0]->strokes[1]->shape_count, (unsigned long)document.pages[1]->strokes[0]->shape_count);

	/* Encoded, decoded, the same; encoded again, the same bytes; version 3.0. */
	notes_buffer_init(&first);
	notes_buffer_init(&second);
	error = notes_encode_document(&document, &first);
	check(error == 0 && first.length > 12U && first.data[4] == 3U && first.data[5] == 0U, "round trip: version 3.0");
	error = notes_decode_document(first.data, first.length, &copy);
	check(error == 0, "round trip: decoded");
	if (error == 0) {
		check(same_document(&document, &copy), "round trip: the same model (samples, constants, factors, steps, contours)");
		check(notes_encode_document(&copy, &second) == 0 && second.length == first.length && memcmp(first.data, second.data, first.length) == 0, "round trip: the same bytes again");
		notes_document_free(&copy);
	}
	(void)snprintf(path, sizeof(path), "%s/t30.bin", dir);
	check(write_bin(&document, path) == 0, "round trip: t30.bin");
	notes_buffer_free(&first);
	notes_buffer_free(&second);
	notes_document_free(&document);
}

/* The versions refused (B3): the tools in 1.1 and 2.0, and constants of another version. */
static void
check_versions(void)
{
	struct notes_document document;
	struct notes_document copy;
	struct notes_buffer buffer;
	struct notes_stroke *stroke;
	size_t used;
	int error;

	build_tools(&document);

	/* Marked 2.0 or 1.1, a document with the tools is refused (L-13). */
	notes_buffer_init(&buffer);
	error = notes_encode_document(&document, &buffer);
	check(error == 0, "versions: encoded");
	buffer.data[4] = 2U;
	check(notes_decode_document(buffer.data, buffer.length, &copy) == EINVAL, "versions: the tools in 2.0 are refused");
	buffer.data[4] = 1U;
	buffer.data[6] = 1U;
	check(notes_decode_document(buffer.data, buffer.length, &copy) == EINVAL, "versions: the tools in 1.1 are refused");
	buffer.data[4] = 4U;
	buffer.data[6] = 0U;
	check(notes_decode_document(buffer.data, buffer.length, &copy) == EINVAL, "versions: 4.0 is refused");
	notes_buffer_free(&buffer);

	/* A brush of constants of another version: the document is refused as not known (M-10). */
	document.pages[0]->strokes[0]->constants = NOTES_TOOL_CONSTANTS + 1U;
	notes_buffer_init(&buffer);
	error = notes_encode_document(&document, &buffer);
	check(error == 0, "versions: encoded with other constants");
	check(notes_decode_document(buffer.data, buffer.length, &copy) == ENOTSUP, "versions: constants of another version are ENOTSUP");
	notes_buffer_free(&buffer);

	/* The same in a journal's stroke. */
	notes_buffer_init(&buffer);
	error = notes_encode_stroke(document.pages[0]->strokes[0], 3U, &buffer);
	check(error == 0, "versions: the journal's stroke encoded");
	check(notes_decode_stroke(buffer.data, buffer.length, 3U, &used, &stroke) == ENOTSUP, "versions: the journal's stroke of other constants is ENOTSUP");
	notes_buffer_free(&buffer);
	document.pages[0]->strokes[0]->constants = NOTES_TOOL_CONSTANTS;

	/* A journal of version 2 holds no tool of ws216. */
	notes_buffer_init(&buffer);
	check(notes_encode_stroke(document.pages[0]->strokes[0], 2U, &buffer) == EINVAL, "versions: a journal 2 refuses a brush");
	notes_buffer_free(&buffer);

	/* A pen in a journal 3: number, tool, constants 0. */
	notes_buffer_init(&buffer);
	error = notes_encode_stroke(document.pages[0]->strokes[4], 3U, &buffer);
	check(error == 0 && notes_decode_stroke(buffer.data, buffer.length, 3U, &used, &stroke) == 0, "versions: a pen in a journal 3");
	if (error == 0 && used == buffer.length) {
		check(same_stroke(stroke, document.pages[0]->strokes[4]), "versions: the pen of the journal 3 is the same");
		notes_stroke_free(stroke);
	}
	notes_buffer_free(&buffer);

	/* A stroke of the tools that is not baked cannot be kept. */
	stroke = make_tool(&document, NOTES_TOOL_BRUSH, 6.0f, 10.0f, 10.0f, 100.0f, 10.0f, 3.0f, 20U, 9);
	notes_buffer_init(&buffer);
	check(notes_encode_stroke(stroke, 3U, &buffer) == EINVAL, "versions: an unbaked brush is not encoded");
	notes_buffer_free(&buffer);
	notes_stroke_free(stroke);
	notes_document_free(&document);
}

/* The journal 3 (B5): written, recovered, and refused for constants not known. */
static void
check_journal(
	const char *dir)
{
	struct notes_document document;
	struct notes_document recovered;
	struct notes_stroke *stroke;
	char path[4096];
	char journal_path[4096];
	char document_path[4096];
	size_t records;
	size_t cut;
	int error;

	(void)snprintf(path, sizeof(path), "%s/data", dir);
	(void)mkdir(path, 0700);
	check(path[0] == '/', "journal: the data folder is absolute (else Notes uses the user's own)");
	if (path[0] != '/')
		return;
	setenv("XDG_DATA_HOME", path, 1);

	/* A pens' notebook journals in version 2. */
	(void)snprintf(document_path, sizeof(document_path), "%s/journal3.pdf", dir);
	check(notes_document_init(&document, TEST_TIME_BASE) == 0, "journal: the document");
	document.journal = notes_journal_create(document_path);
	check(document.journal != NULL && notes_journal_path(document_path, journal_path, sizeof(journal_path)) == 0, "journal: its journal");
	check(notes_document_add_stroke(&document, 0, make_tool(&document, NOTES_TOOL_PEN, 3.0f, 50.0f, 50.0f, 400.0f, 80.0f, 10.0f, 50U, 1)) == 0, "journal: a pen");
	check(journal_version(journal_path) == 2, "journal: a pens' notebook journals in version 2");

	/* A brush makes it version 3, written anew; then a pencil, a watercolour, a fill, a cut and an undo. */
	check(notes_document_add_stroke(&document, 0, make_tool(&document, NOTES_TOOL_BRUSH, 8.0f, 50.0f, 150.0f, 400.0f, 180.0f, 10.0f, 80U, 2)) == 0, "journal: a brush");
	check(journal_version(journal_path) == 3, "journal: a brush writes it anew as version 3");
	check(notes_document_add_stroke(&document, 0, make_tool(&document, NOTES_TOOL_PENCIL, 2.4f, 50.0f, 250.0f, 400.0f, 260.0f, 10.0f, 90U, 3)) == 0, "journal: a pencil");
	check(notes_document_add_stroke(&document, 0, make_tool(&document, NOTES_TOOL_WATERCOLOR, 16.0f, 50.0f, 350.0f, 400.0f, 380.0f, 10.0f, 70U, 4)) == 0, "journal: a watercolour");
	check(notes_document_add_stroke(&document, 0, make_fill(&document, 80.0f, 500.0f, 100.0f)) == 0, "journal: a fill");
	notes_document_erase_begin(&document);
	check(notes_document_erase_parts_at(&document, 0, 225.0f, 365.0f, 10.0f, &cut) == 0 && cut >= 1U, "journal: the watercolour cut");
	notes_document_erase_end(&document);
	check(notes_document_add_stroke(&document, 0, make_tool(&document, NOTES_TOOL_BRUSH, 8.0f, 50.0f, 650.0f, 400.0f, 680.0f, 10.0f, 30U, 5)) == 0, "journal: another brush");
	check(notes_document_undo(&document, &cut) == 0, "journal: the brush taken back");

	/* Recovered: the same model. */
	error = notes_journal_recover(journal_path, &recovered, path, sizeof(path), &records);
	check(error == 0, "journal: recovered");
	if (error == 0) {
		check(same_document(&document, &recovered) && strcmp(path, document_path) == 0, "journal: the recovered model is the same");
		notes_document_free(&recovered);
	}

	/* A stroke of other constants in the journal: the recovery is refused, to be set aside. */
	stroke = make_tool(&document, NOTES_TOOL_BRUSH, 8.0f, 50.0f, 750.0f, 400.0f, 780.0f, 10.0f, 30U, 6);
	check(notes_stroke_bake(stroke) == 0, "journal: a brush baked");
	stroke->constants = NOTES_TOOL_CONSTANTS + 1U;
	check(notes_document_add_stroke(&document, 0, stroke) == 0, "journal: a brush of other constants");
	check(notes_journal_recover(journal_path, &recovered, path, sizeof(path), &records) == ENOTSUP, "journal: a journal of other constants is ENOTSUP");
	notes_journal_destroy(document.journal);
	document.journal = NULL;
	notes_document_free(&document);
}

/* The eraser of parts on the brush, the pencil and the watercolour (B6). */
static void
check_eraser(void)
{
	int seed;

	for (seed = 0; seed < 6; seed++) {
		check_erase_tool(NOTES_TOOL_BRUSH, 12.0f, seed);
		check_erase_tool(NOTES_TOOL_PENCIL, 2.4f, seed);
		check_erase_tool(NOTES_TOOL_WATERCOLOR, 28.0f, seed);
	}
}

/*
 * Cuts one stroke of a tool with the eraser of parts and checks: outside
 * the circle, its reach and two samples' spacing, every corner is where it
 * was (and the pieces have no new corner there); no shape of a piece comes
 * into the circle; undo and redo give the stroke and the pieces back.
 */
static void
check_erase_tool(
	unsigned tool,
	float width,
	int seed)
{
	struct notes_document document;
	struct notes_stroke *stroke;
	struct notes_stroke *piece;
	struct corner_set before;
	struct corner_set after;
	const struct notes_shape *shape;
	size_t index;
	size_t corner;
	size_t page;
	size_t cut;
	size_t pieces;
	double cx;
	double cy;
	double radius;
	double spacing;
	double zone;
	double distance;
	double nearest;
	unsigned kind;
	int kept;
	int added;
	int inside;
	char what[160];

	check(notes_document_init(&document, TEST_TIME_BASE) == 0, "eraser: the document");
	stroke = make_tool(&document, tool, width, 40.0f, 300.0f, 540.0f, 340.0f, 30.0f, 150U, seed);
	check(notes_document_add_stroke(&document, 0, stroke) == 0, "eraser: the stroke");
	check(notes_stroke_outline(stroke) == 0, "eraser: its shapes");
	corners_copy(stroke, &before);

	/* The circle on a sample near the middle, a little beside the path. */
	index = 60U + (size_t)seed * 5U;
	cx = stroke->points[index].x + 1.5;
	cy = stroke->points[index].y - 1.0;
	radius = 10.0;
	spacing = 0.0;
	for (corner = 1; corner < stroke->point_count; corner++) {
		distance = hypot(stroke->points[corner].x - stroke->points[corner - 1U].x, stroke->points[corner].y - stroke->points[corner - 1U].y);
		if (distance > spacing)
			spacing = distance;
	}
	/*
	 * The cut is at the circle widened by the reach; a corner is the path
	 * moved by at most the reach, and the outline at a sample depends on
	 * the samples two either side: past the cut, the reach and two
	 * samples' spacing the corners are the stroke's own.
	 */
	zone = radius + 2.0 * notes_stroke_reach(stroke) + 2.0 * spacing;

	/* Cut. */
	notes_document_erase_begin(&document);
	check(notes_document_erase_parts_at(&document, 0, (float)cx, (float)cy, (float)radius, &cut) == 0 && cut == 1U, "eraser: the stroke is cut");
	notes_document_erase_end(&document);
	pieces = document.pages[0]->stroke_count;
	(void)snprintf(what, sizeof(what), "eraser: tool %u seed %d cuts into two pieces (%lu)", tool, seed, (unsigned long)pieces);
	check(pieces == 2U, what);

	/* The pieces' corners. */
	memset(&after, 0, sizeof(after));
	for (index = 0; index < pieces; index++) {
		struct corner_set one;

		piece = document.pages[0]->strokes[index];
		check(piece->constants == stroke->constants && notes_stroke_baked(piece) && piece->factor_columns == stroke->factor_columns, "eraser: a piece keeps the constants and is baked");
		check(notes_stroke_outline(piece) == 0, "eraser: a piece's shapes");
		corners_copy(piece, &one);
		after.x = realloc(after.x, (after.count + one.count) * sizeof(double));
		after.y = realloc(after.y, (after.count + one.count) * sizeof(double));
		after.kind = realloc(after.kind, (after.count + one.count) * sizeof(unsigned));
		memcpy(after.x + after.count, one.x, one.count * sizeof(double));
		memcpy(after.y + after.count, one.y, one.count * sizeof(double));
		memcpy(after.kind + after.count, one.kind, one.count * sizeof(unsigned));
		after.count += one.count;
		corners_free(&one);
	}

	/* Outside the zone every corner stands where it stood, and no new one is there. */
	kept = 1;
	for (corner = 0; corner < before.count; corner++) {
		if (hypot(before.x[corner] - cx, before.y[corner] - cy) <= zone)
			continue;
		if (!corners_near(&after, before.kind[corner], before.x[corner], before.y[corner])) {
			if (kept)
				printf("eraser: tool %u seed %d: corner %lu of kind %u at %.3f from the centre (zone %.3f) not kept\n", tool, seed, (unsigned long)corner, before.kind[corner], hypot(before.x[corner] - cx, before.y[corner] - cy), zone);
			kept = 0;
		}
	}
	added = 0;
	for (corner = 0; corner < after.count; corner++) {
		if (hypot(after.x[corner] - cx, after.y[corner] - cy) <= zone)
			continue;
		if (!corners_near(&before, after.kind[corner], after.x[corner], after.y[corner]))
			added = 1;
	}
	(void)snprintf(what, sizeof(what), "eraser: tool %u seed %d: the corners outside the circle, its reach and two samples are the same (0.01 pt)", tool, seed);
	check(kept && !added, what);

	/* No shape of a piece comes into the circle (M-5). */
	inside = 0;
	nearest = 1e9;
	for (index = 0; index < pieces; index++) {
		piece = document.pages[0]->strokes[index];
		for (kind = 0; kind < piece->shape_count; kind++) {
			shape = &piece->shapes[kind];
			for (corner = 0; corner < shape->corner_count; corner++) {
				distance = edge_distance(cx, cy, shape->polygon[corner].x, shape->polygon[corner].y,
				    shape->polygon[(corner + 1U) % shape->corner_count].x, shape->polygon[(corner + 1U) % shape->corner_count].y);
				if (distance < nearest)
					nearest = distance;
			}
			if (winding(shape->polygon, shape->corner_count, cx, cy) != 0)
				inside = 1;
		}
	}
	(void)snprintf(what, sizeof(what), "eraser: tool %u seed %d: no piece comes into the circle (nearest %.3f of %.1f)", tool, seed, nearest, radius);
	check(!inside && nearest >= radius - TEST_TOLERANCE, what);

	/* Undo gives the stroke back, redo the pieces. */
	check(notes_document_undo(&document, &page) == 0 && document.pages[0]->stroke_count == 1U && document.pages[0]->strokes[0] == stroke, "eraser: undo gives the stroke back");
	check(notes_document_redo(&document, &page) == 0 && document.pages[0]->stroke_count == pieces, "eraser: redo gives the pieces back");

	corners_free(&before);
	corners_free(&after);
	notes_document_free(&document);
}

/* The pen and the highlighter drawn as before (B7): their shape is pdf_outline_stroke()'s, bit for bit. */
static void
check_pens(void)
{
	struct notes_document document;
	struct notes_stroke *stroke;
	struct pdf_stroke_point *samples;
	struct pdf_point *outline;
	size_t outline_count;
	size_t index;
	unsigned tool;
	int seed;
	int same;

	check(notes_document_init(&document, TEST_TIME_BASE) == 0, "pens: the document");
	for (seed = 0; seed < 10; seed++) {
		for (tool = NOTES_TOOL_PEN; tool <= NOTES_TOOL_HIGHLIGHTER; tool++) {
			stroke = make_tool(&document, tool, 1.5f + (float)seed, 30.0f, 40.0f * (float)seed, 500.0f, 40.0f * (float)seed + 30.0f, 15.0f, 40U + (unsigned)seed * 13U, seed);
			check(notes_document_add_stroke(&document, 0, stroke) == 0 && notes_stroke_outline(stroke) == 0, "pens: a stroke and its outline");
			check(stroke->constants == 0U && stroke->factors == NULL && stroke->shape_count == 1U && stroke->shapes[0].polygon == stroke->outline, "pens: one shape, the outline");
			samples = malloc(stroke->point_count * sizeof(*samples));
			for (index = 0; index < stroke->point_count; index++) {
				samples[index].x = stroke->points[index].x;
				samples[index].y = stroke->points[index].y;
				samples[index].pressure = (double)stroke->points[index].pressure / (double)NOTES_PRESSURE_MAX;
				if (tool == NOTES_TOOL_HIGHLIGHTER)
					samples[index].pressure = 1.0;
			}
			same = 0;
			if (pdf_outline_stroke(samples, stroke->point_count, stroke->width, &outline, &outline_count) == 0) {
				same = outline_count == stroke->outline_count && memcmp(outline, stroke->outline, outline_count * sizeof(*outline)) == 0;
				pdf_outline_free(outline);
			}
			free(samples);
			check(same, "pens: the outline is pdf_outline_stroke()'s, bit for bit");
		}
	}
	notes_document_free(&document);
}

/* The PDF (B8): a 3.0 notebook saved, opened as Notes' own, a clean copy, a page of the grain. */
static void
check_pdf(
	const char *dir)
{
	struct notes_document document;
	struct notes_document opened;
	struct notes_stroke *stroke;
	struct notes_point point;
	char path[4096];
	char clean[4096];
	size_t bytes;
	size_t objects;
	size_t dropped;
	unsigned kind;
	unsigned index;
	int error;

	/* The tools' notebook saved and opened again: Notes' own, the same model. */
	build_tools(&document);
	(void)snprintf(path, sizeof(path), "%s/t30.pdf", dir);
	check(save_fixed(&document, path) == 0, "pdf: t30.pdf saved");
	error = notes_open_pdf(path, &opened, &kind);
	check(error == 0 && kind == NOTES_OPENED_NOTES, "pdf: the 3.0 notebook opens as Notes' own (its Flate content's hash matches)");
	if (error == 0) {
		check(same_document(&document, &opened), "pdf: the same model");
		notes_document_free(&opened);
	}
	(void)snprintf(clean, sizeof(clean), "%s/t30-clean.pdf", dir);
	check(notes_save_clean_copy(path, clean, &bytes, &objects, &dropped) == 0, "pdf: the clean copy");
	notes_document_free(&document);

	/* The pens' notebook. */
	check(notes_document_init(&document, TEST_TIME_BASE) == 0, "pdf: the pens' document");
	check(notes_document_add_stroke(&document, 0, make_tool(&document, NOTES_TOOL_PEN, 3.0f, 60.0f, 100.0f, 520.0f, 160.0f, 20.0f, 80U, 1)) == 0, "pdf: a pen");
	check(notes_document_add_stroke(&document, 0, make_tool(&document, NOTES_TOOL_HIGHLIGHTER, 16.0f, 60.0f, 300.0f, 520.0f, 320.0f, 5.0f, 60U, 2)) == 0, "pdf: a highlighter");
	(void)snprintf(path, sizeof(path), "%s/pens.bin", dir);
	check(write_bin(&document, path) == 0, "pdf: pens.bin");
	notes_document_free(&document);

	/* A page of the grain: a pencil 300 points wide at full pressure, black. */
	check(notes_document_init(&document, TEST_TIME_BASE) == 0, "pdf: the grain's document");
	stroke = notes_stroke_create(document.next_id, NOTES_TOOL_PENCIL, 0x000000ffU, 300.0f, TEST_TIME_BASE);
	document.next_id++;
	for (index = 0; index <= 40U; index++) {
		memset(&point, 0, sizeof(point));
		point.x = 100.0f + 5.0f * (float)index;
		point.y = 150.0f;
		point.pressure = NOTES_PRESSURE_MAX;
		point.time_ms = index * 10U;
		(void)notes_stroke_append(stroke, &point);
	}
	check(notes_document_add_stroke(&document, 0, stroke) == 0 && notes_stroke_outline(stroke) == 0 && stroke->shape_count == 1U && stroke->shapes[0].alpha == 1.0f, "pdf: the grain's pencil, one range at full darkness");
	(void)snprintf(path, sizeof(path), "%s/grain-new.pdf", dir);
	check(save_fixed(&document, path) == 0, "pdf: grain-new.pdf");
	notes_document_free(&document);
}

/* Copies the corners of a stroke's shapes. */
static void
corners_copy(
	const struct notes_stroke *stroke,
	struct corner_set *set)
{
	size_t shape;
	size_t corner;
	size_t total;

	total = 0;
	for (shape = 0; shape < stroke->shape_count; shape++)
		total += stroke->shapes[shape].corner_count;
	set->x = malloc((total + 1U) * sizeof(double));
	set->y = malloc((total + 1U) * sizeof(double));
	set->kind = malloc((total + 1U) * sizeof(unsigned));
	set->count = 0;
	for (shape = 0; shape < stroke->shape_count; shape++) {
		for (corner = 0; corner < stroke->shapes[shape].corner_count; corner++) {
			set->x[set->count] = stroke->shapes[shape].polygon[corner].x;
			set->y[set->count] = stroke->shapes[shape].polygon[corner].y;
			set->kind[set->count] = shape_kind(stroke, shape);
			set->count++;
		}
	}
}

/* Frees a copy of corners. */
static void
corners_free(
	struct corner_set *set)
{
	free(set->x);
	free(set->y);
	free(set->kind);
	memset(set, 0, sizeof(*set));
}

/* Tells the kind of a stroke's shape: the watercolour's column, 0 for every other shape. */
static unsigned
shape_kind(
	const struct notes_stroke *stroke,
	size_t shape)
{
	if (stroke->tool == NOTES_TOOL_WATERCOLOR)
		return (unsigned)shape;
	return 0U;
}

/* Tells whether a set has a corner of a kind within the tolerance of a point. */
static int
corners_near(
	const struct corner_set *set,
	unsigned kind,
	double x,
	double y)
{
	size_t index;

	for (index = 0; index < set->count; index++) {
		if (set->kind[index] != kind)
			continue;
		if (fabs(set->x[index] - x) <= TEST_TOLERANCE && fabs(set->y[index] - y) <= TEST_TOLERANCE)
			return 1;
	}
	return 0;
}

/* Counts the winding of a polygon around a point. */
static int
winding(
	const struct pdf_point *polygon,
	size_t count,
	double x,
	double y)
{
	size_t index;
	double ax;
	double ay;
	double bx;
	double by;
	double cross;
	int turns;

	turns = 0;
	for (index = 0; index < count; index++) {
		ax = polygon[index].x;
		ay = polygon[index].y;
		bx = polygon[(index + 1U) % count].x;
		by = polygon[(index + 1U) % count].y;
		cross = (bx - ax) * (y - ay) - (x - ax) * (by - ay);
		if (ay <= y && by > y && cross > 0.0)
			turns++;
		if (ay > y && by <= y && cross < 0.0)
			turns--;
	}
	return turns;
}

/* Measures the distance from a point to an edge. */
static double
edge_distance(
	double px,
	double py,
	double ax,
	double ay,
	double bx,
	double by)
{
	double dx;
	double dy;
	double length;
	double share;

	dx = bx - ax;
	dy = by - ay;
	length = dx * dx + dy * dy;
	share = 0.0;
	if (length > 0.0) {
		share = ((px - ax) * dx + (py - ay) * dy) / length;
		if (share < 0.0)
			share = 0.0;
		if (share > 1.0)
			share = 1.0;
	}
	return hypot(ax + dx * share - px, ay + dy * share - py);
}

/*
 * Reads what the old Notes wrote (B3, B5): its 1.1 and 2.0 notebooks open
 * as Notes' own and encode to the bytes it wrote; its journal 2 recovers,
 * and becomes a journal 3 when a brush is drawn.
 */
static int
run_old(
	const char *dir)
{
	struct notes_document document;
	struct notes_document recovered;
	struct notes_buffer buffer;
	unsigned char *expected;
	char path[4096];
	char journal_path[4096];
	char document_path[4096];
	char recovered_path[4096];
	const char *names[2];
	size_t size;
	size_t records;
	size_t index;
	unsigned kind;
	int error;

	(void)snprintf(path, sizeof(path), "%s/data", dir);
	check(path[0] == '/', "old: the data folder is absolute (else Notes uses the user's own)");
	if (path[0] != '/')
		return EINVAL;
	setenv("XDG_DATA_HOME", path, 1);

	/* The 1.1 and 2.0 notebooks. */
	names[0] = "old11";
	names[1] = "old20";
	for (index = 0; index < 2U; index++) {
		(void)snprintf(path, sizeof(path), "%s/%s.pdf", dir, names[index]);
		error = notes_open_pdf(path, &document, &kind);
		check(error == 0 && kind == NOTES_OPENED_NOTES, "old: the old notebook opens as Notes' own");
		if (error != 0)
			return error;
		(void)snprintf(path, sizeof(path), "%s/%s.bin", dir, names[index]);
		expected = read_all(path, &size);
		notes_buffer_init(&buffer);
		error = notes_encode_document(&document, &buffer);
		check(error == 0 && expected != NULL && buffer.length == size && memcmp(buffer.data, expected, size) == 0, "old: it encodes to the bytes the old Notes wrote");
		check(buffer.length > 6U && buffer.data[4] == 1U + index && buffer.data[6] == 1U - index, "old: the version is 1.1, or 2.0 with the edits");
		free(expected);
		notes_buffer_free(&buffer);
		notes_document_free(&document);
	}

	/*
	 * The journal 2 the old Notes left (in the copy of its folder this run
	 * has; it names the document's path where the old Notes made it):
	 * recovered, the document it was.
	 */
	check(notes_journal_newest(journal_path, sizeof(journal_path)) == 0, "old: the journal is found");
	check(journal_version(journal_path) == 2, "old: the old journal is version 2");
	error = notes_journal_recover(journal_path, &document, recovered_path, sizeof(recovered_path), &records);
	check(error == 0 && strstr(recovered_path, "/journal.pdf") != NULL, "old: the journal 2 recovers");
	if (error != 0)
		return error;
	memcpy(document_path, recovered_path, strlen(recovered_path) + 1U);
	(void)snprintf(path, sizeof(path), "%s/journal.bin", dir);
	expected = read_all(path, &size);
	notes_buffer_init(&buffer);
	error = notes_encode_document(&document, &buffer);
	check(error == 0 && expected != NULL && buffer.length == size && memcmp(buffer.data, expected, size) == 0, "old: the recovered notebook is the one the old Notes had");
	free(expected);
	notes_buffer_free(&buffer);

	/* A brush drawn after the recovery: the journal is written anew as version 3, and recovers. */
	document.journal = notes_journal_create(document_path);
	check(notes_document_add_stroke(&document, 0, make_tool(&document, NOTES_TOOL_PEN, 3.0f, 50.0f, 600.0f, 300.0f, 610.0f, 5.0f, 30U, 1)) == 0, "old: a pen after the recovery");
	check(journal_version(journal_path) == 2, "old: the new journal of pens is version 2");
	check(notes_document_add_stroke(&document, 0, make_tool(&document, NOTES_TOOL_BRUSH, 8.0f, 50.0f, 650.0f, 300.0f, 660.0f, 5.0f, 30U, 2)) == 0, "old: a brush after the recovery");
	check(journal_version(journal_path) == 3, "old: the journal becomes version 3");
	error = notes_journal_recover(journal_path, &recovered, recovered_path, sizeof(recovered_path), &records);
	check(error == 0, "old: the journal 3 recovers");
	if (error == 0) {
		check(same_document(&document, &recovered) && encode_equal(&document, &recovered), "old: the same notebook");
		notes_document_free(&recovered);
	}
	notes_journal_destroy(document.journal);
	document.journal = NULL;
	notes_document_free(&document);
	return 0;
}

/* Writes another program's PDF: an A4 page and a page of 420 by 600 points, each with a grey square. */
static int
run_base(
	const char *dir)
{
	struct pdf_writer *writer;
	static const struct pdf_point square[4] = { { 20.0, 20.0 }, { 60.0, 20.0 }, { 60.0, 60.0 }, { 20.0, 60.0 } };
	char path[4096];
	int error;

	(void)snprintf(path, sizeof(path), "%s/base.pdf", dir);
	writer = NULL;
	error = pdf_writer_create(&writer);
	if (error == 0)
		error = pdf_writer_begin_page(writer, 595.276, 841.89);
	if (error == 0)
		error = pdf_writer_set_fill_color(writer, 0.5, 0.5, 0.5, 1.0);
	if (error == 0)
		error = pdf_writer_fill_outline(writer, square, 4U);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	if (error == 0)
		error = pdf_writer_begin_page(writer, 420.0, 600.0);
	if (error == 0)
		error = pdf_writer_set_fill_color(writer, 0.5, 0.5, 0.5, 1.0);
	if (error == 0)
		error = pdf_writer_fill_outline(writer, square, 4U);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	if (error == 0)
		error = pdf_writer_save(writer, path);
	pdf_writer_destroy(writer);
	if (error != 0)
		printf("FAIL: base.pdf (%d)\n", error);
	return error;
}

/*
 * Writes the grain on another program's PDF (B9): its first page A4, its
 * second smaller and turned a quarter (base-rot.pdf, the run script's), a
 * pencil 300 points wide at full pressure on each, saved as a revision.
 */
static int
run_grain_pages(
	const char *dir)
{
	struct notes_document document;
	struct notes_stroke *stroke;
	struct notes_point point;
	char path[4096];
	size_t bytes;
	size_t page;
	unsigned index;
	unsigned kind;
	int error;

	(void)snprintf(path, sizeof(path), "%s/base-rot.pdf", dir);
	error = notes_open_pdf(path, &document, &kind);
	check(error == 0 && kind == NOTES_OPENED_FOREIGN && document.page_count == 2U, "grain pages: another program's PDF of two pages");
	if (error != 0)
		return error;
	printf("grain pages: page 1 %.1f x %.1f, page 2 %.1f x %.1f\n", document.pages[0]->width, document.pages[0]->height, document.pages[1]->width, document.pages[1]->height);
	for (page = 0; page < 2U; page++) {
		stroke = notes_stroke_create(document.next_id, NOTES_TOOL_PENCIL, 0x000000ffU, 300.0f, TEST_TIME_BASE);
		document.next_id++;
		for (index = 0; index <= 40U; index++) {
			memset(&point, 0, sizeof(point));
			point.x = 100.0f + 5.0f * (float)index;
			point.y = 150.0f;
			point.pressure = NOTES_PRESSURE_MAX;
			point.time_ms = index * 10U;
			(void)notes_stroke_append(stroke, &point);
		}
		check(notes_document_add_stroke(&document, page, stroke) == 0, "grain pages: the pencil");
	}
	(void)snprintf(path, sizeof(path), "%s/grain-foreign.pdf", dir);
	error = notes_save_pdf(&document, path, &bytes);
	check(error == 0, "grain pages: saved as a revision");
	notes_document_free(&document);
	return error;
}

/*
 * Checks the grain in a crop (x0, y0 in pixels at dpi) of a page with a
 * black pencil at full darkness over white: each pixel must be
 * 255 (1 - g)(1 - h) for the texel h of the tile under it, within the
 * values of the texels next to it and 4 levels (the drawing's sampling),
 * and the same check against the tile upside down or sideways must fail.
 */
static int
run_grain(
	const char *path,
	long x0,
	long y0,
	double dpi)
{
	unsigned char tile[NOTES_GRAIN_SIDE * NOTES_GRAIN_SIDE];
	unsigned char *picture;
	size_t width;
	size_t height;
	size_t px;
	size_t py;
	size_t passed[3];
	size_t total;
	double x;
	double y;
	double texel;
	double low;
	double high;
	double value;
	unsigned variant;
	long tx;
	long ty;
	long dx;
	long dy;
	long sx;
	long sy;
	uint32_t hash;
	size_t index;

	notes_grain_tile(tile);
	hash = 0x811c9dc5U;
	for (index = 0; index < sizeof(tile); index++) {
		hash ^= tile[index];
		hash *= 0x01000193U;
	}
	picture = read_ppm(path, &width, &height);
	if (picture == NULL) {
		printf("FAIL: grain: no picture %s\n", path);
		return 1;
	}
	texel = NOTES_GRAIN_CELL / (double)NOTES_GRAIN_SIDE;
	memset(passed, 0, sizeof(passed));
	total = 0;
	for (py = 2; py + 2 < height; py++) {
		for (px = 2; px + 2 < width; px++) {
			x = ((double)x0 + (double)px + 0.5) * 72.0 / dpi;
			y = ((double)y0 + (double)py + 0.5) * 72.0 / dpi;
			value = picture[(py * width + px) * 3U];
			total++;
			for (variant = 0; variant < 3U; variant++) {
				tx = (long)floor(x / texel);
				ty = (long)floor(y / texel);
				low = 1e9;
				high = -1e9;
				for (dy = -1; dy <= 1; dy++) {
					for (dx = -1; dx <= 1; dx++) {
						double expected;

						sx = ((tx + dx) % 256 + 256) % 256;
						sy = ((ty + dy) % 256 + 256) % 256;
						if (variant == 1U)
							sy = 255 - sy;
						if (variant == 2U) {
							long swap = sx;
							sx = sy;
							sy = swap;
						}
						expected = 255.0 * (1.0 - (double)NOTES_GRAIN_FLOOR) * (1.0 - (double)tile[sy * 256 + sx] / 255.0);
						if (expected < low)
							low = expected;
						if (expected > high)
							high = expected;
					}
				}
				if (value >= low - 4.0 && value <= high + 4.0)
					passed[variant]++;
			}
		}
	}
	printf("grain %s: tile hash %08x, %lu pixels, as drawn %.2f%%, upside down %.2f%%, sideways %.2f%%\n", path, hash, (unsigned long)total,
	    100.0 * (double)passed[0] / (double)total, 100.0 * (double)passed[1] / (double)total, 100.0 * (double)passed[2] / (double)total);
	free(picture);
	if ((double)passed[0] < 0.98 * (double)total) {
		printf("FAIL: grain: the pixels do not follow the grain\n");
		return 1;
	}
	if ((double)passed[1] > 0.9 * (double)total || (double)passed[2] > 0.9 * (double)total) {
		printf("FAIL: grain: the check does not tell the grain's orientation\n");
		return 1;
	}
	printf("grain: ok\n");
	return 0;
}

/*
 * Opens the 3.0 notebook the old Notes wrote a pen on (B4, M9): the old
 * Notes took it as another program's PDF, so its strokes of the tools are
 * the base's content now, and only its pen is a stroke.
 */
static int
run_m9(
	const char *path)
{
	struct notes_document document;
	unsigned kind;
	int error;

	error = notes_open_pdf(path, &document, &kind);
	check(error == 0 && kind == NOTES_OPENED_ANNOTATED, "m9: the old Notes' revision opens as written on another program's PDF");
	if (error != 0)
		return error;
	check(document.pages[0]->stroke_count == 1U && document.pages[0]->strokes[0]->tool == NOTES_TOOL_PEN && document.pages[0]->origin == NOTES_ORIGIN_OVER, "m9: page 1 has the old Notes' pen only, over the base");
	check(document.page_count == 2U && document.pages[1]->stroke_count == 0U, "m9: page 2 has no stroke (its pencil is the base's)");
	notes_document_free(&document);
	return 0;
}

/* Writes a document's edit data to a file. */
static int
write_bin(
	const struct notes_document *document,
	const char *path)
{
	struct notes_buffer buffer;
	FILE *file;
	int error;

	notes_buffer_init(&buffer);
	error = notes_encode_document(document, &buffer);
	if (error == 0) {
		file = fopen(path, "wb");
		if (file == NULL) {
			error = errno;
		} else {
			if (fwrite(buffer.data, 1U, buffer.length, file) != buffer.length)
				error = EIO;
			if (fclose(file) != 0)
				error = EIO;
		}
	}
	notes_buffer_free(&buffer);
	return error;
}

/* Saves a document under the fixed identifier host-notes-old uses. */
static int
save_fixed(
	struct notes_document *document,
	const char *path)
{
	size_t bytes;

	memset(document->pdf_id, 0x5a, sizeof(document->pdf_id));
	document->has_pdf_id = 1;
	return notes_save_pdf(document, path, &bytes);
}

/* Reads a whole file. */
static unsigned char *
read_all(
	const char *path,
	size_t *size)
{
	unsigned char *data;
	FILE *file;
	long length;

	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return NULL;
	}
	data = malloc((size_t)length + 1U);
	if (data != NULL && fread(data, 1U, (size_t)length, file) != (size_t)length) {
		free(data);
		data = NULL;
	}
	fclose(file);
	*size = (size_t)length;
	return data;
}

/* Reads a binary PPM (P6, 255) as RGB. */
static unsigned char *
read_ppm(
	const char *path,
	size_t *width,
	size_t *height)
{
	unsigned char *data;
	unsigned char *pixels;
	size_t size;
	size_t offset;
	unsigned long numbers[3];
	unsigned count;

	data = read_all(path, &size);
	if (data == NULL || size < 2U || data[0] != 'P' || data[1] != '6') {
		free(data);
		return NULL;
	}
	offset = 2;
	for (count = 0; count < 3U; count++) {
		while (offset < size && (data[offset] == ' ' || data[offset] == '\n' || data[offset] == '\r' || data[offset] == '\t'))
			offset++;
		numbers[count] = 0;
		while (offset < size && data[offset] >= '0' && data[offset] <= '9') {
			numbers[count] = numbers[count] * 10U + (unsigned long)(data[offset] - '0');
			offset++;
		}
	}
	offset++;
	*width = numbers[0];
	*height = numbers[1];
	if (numbers[2] != 255U || offset + *width * *height * 3U > size) {
		free(data);
		return NULL;
	}
	pixels = malloc(*width * *height * 3U);
	if (pixels != NULL)
		memcpy(pixels, data + offset, *width * *height * 3U);
	free(data);
	return pixels;
}

/* Reads a journal file's version (its fifth byte), or -1. */
static int
journal_version(
	const char *path)
{
	unsigned char *data;
	size_t size;
	int version;

	data = read_all(path, &size);
	version = -1;
	if (data != NULL && size >= 12U && memcmp(data, "ZNJL", 4U) == 0)
		version = data[4];
	free(data);
	return version;
}
