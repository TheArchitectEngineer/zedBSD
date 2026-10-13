/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws216-p004: Notes' screen on the host -- render.c and geometry.c, linked
 * as they are with Notes' model (document.c, shape.c) and libpdf, over the
 * model of a Vulkan device in host-vulkan-mock.c -- checked against the
 * acceptance of plan/ws216/phase004/phase.md section 6:
 *
 *   C2  the grain's pictures: the tile (FNV-1a 2a644409) and its coarser
 *       sizes in all four channels, NOTES_GRAIN_MEAN the tile's mean
 *   C3  on i915's model (an allocation of more than 16 MiB refused) a frame
 *       of more than 600,000 vertices is drawn from chunks of at most
 *       4 MiB: every piece whole triangles inside its chunk, the pieces in
 *       order the draws' vertices, each draw's pieces one after another
 *       with its pipeline in its pass
 *   C4  a device that refuses chunks of 4 and 2 MiB is drawn with 1 MiB
 *       chunks; one that refuses 256 KiB fails as before
 *   C5  the groups are made only by the first frame that uses them, once
 *   C6  a device that refuses the groups' pipelines opens, draws, logs one
 *       fallback a group and draws them with the fringe and the cover
 *   C7  the geometry: the pen's vertices as notes_frame_polygon() gave
 *       them, no fringe on a pencil's seams, the fill's contours, the
 *       grain's and the watercolour's vertices, the grain's place and size
 *
 *   host-notes-render LOG   (the renderer's NOTES lines go to LOG)
 */

#include "app.h"
#include "shaders.h"
#include "host-vulkan-mock.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The window, the page picture and the page (A4). */
#define TEST_WIDTH		1280U
#define TEST_HEIGHT		800U
#define TEST_PICTURE_WIDTH	900U
#define TEST_PICTURE_HEIGHT	1274U
#define TEST_PAGE_WIDTH		595.0f
#define TEST_PAGE_HEIGHT	842.0f

/* i915's largest allocation (I915_MAX_RESOURCE_BYTES). */
#define TEST_I915_BYTES		(16U * 1024U * 1024U)

/* The corners of the large polygon: 9 vertices a corner, over 600,000 in all. */
#define TEST_LARGE_CORNERS	70000U

/* The tile's hash the PDF's grain has (p003 B9). */
#define TEST_GRAIN_HASH		0x2a644409U

/* The window the renderer opens on (only its size is read). */
static struct notes_window window;

/* The log the renderer's NOTES lines go to. */
static const char *log_path;

/* The checks run, and those that failed. */
static unsigned checks;
static unsigned failures;

static void check(int passed, const char *format, ...);
static size_t log_count(const char *needle);
static void leaks(const char *what);
static struct notes_stroke *make_tool(struct notes_document *document, unsigned tool, uint32_t color, float width, float x0, float y0, float x1, float y1, unsigned count, int seed);
static struct notes_stroke *make_fill(struct notes_document *document, float x, float y, float size);
static void page_frame_of(struct notes_frame *frame, struct notes_stroke **strokes, size_t count, float scale);
static void window_frame_of(struct notes_frame *frame, const struct notes_renderer *renderer, struct notes_stroke *live, const struct notes_view *view);
static unsigned expected_pipe(const struct notes_renderer *renderer, unsigned pipe);
static size_t verify_draws(const struct notes_renderer *renderer, const struct notes_frame *page_frame, const struct notes_frame *frame, const char *what);
static int verify_pass(const struct notes_renderer *renderer, const struct notes_frame *frame, uint32_t base, unsigned pass, size_t *recorded, size_t *split, const char *what);
static void test_geometry(struct notes_document *document);
static void test_lazy(struct notes_document *document);
static void test_large(struct notes_document *document);
static void test_halving(struct notes_document *document);
static void test_fallback(struct notes_document *document);
static void test_grain_pictures(const struct notes_renderer *renderer);

int
main(
	int argc,
	char **argv)
{
	struct notes_document document;

	if (argc != 2) {
		fprintf(stderr, "usage: host-notes-render LOG\n");
		return 2;
	}
	log_path = argv[1];
	if (freopen(log_path, "w", stdout) == NULL) {
		fprintf(stderr, "host-notes-render: cannot write %s\n", log_path);
		return 2;
	}
	window.width = TEST_WIDTH;
	window.height = TEST_HEIGHT;
	check(notes_document_init(&document, 1700000000000ULL) == 0, "the document");

	test_geometry(&document);
	test_lazy(&document);
	test_large(&document);
	test_halving(&document);
	test_fallback(&document);

	notes_document_free(&document);
	mock_reset();
	fprintf(stderr, "host-notes-render: %u checks, %u failed\n", checks, failures);
	if (failures != 0U)
		return 1;
	return 0;
}

/* Counts a check, and reports it when it fails. */
static void
check(
	int passed,
	const char *format,
	...)
{
	va_list arguments;

	checks++;
	if (passed)
		return;
	failures++;
	fprintf(stderr, "FAIL: ");
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fprintf(stderr, "\n");
}

/* Counts the renderer's log lines that hold a text. */
static size_t
log_count(
	const char *needle)
{
	char line[512];
	FILE *file;
	size_t count;

	fflush(stdout);
	file = fopen(log_path, "r");
	if (file == NULL)
		return 0;
	count = 0;
	while (fgets(line, sizeof(line), file) != NULL) {
		if (strstr(line, needle) != NULL)
			count++;
	}
	fclose(file);
	return count;
}

/* Checks that a closed renderer left none of its objects. */
static void
leaks(
	const char *what)
{
	check(mock_live(MOCK_BUFFER) == 0U, "%s: no buffer left (%lu)", what, (unsigned long)mock_live(MOCK_BUFFER));
	check(mock_live(MOCK_IMAGE) == 0U, "%s: no image left (%lu)", what, (unsigned long)mock_live(MOCK_IMAGE));
	check(mock_live(MOCK_MEMORY) == 0U, "%s: no memory left (%lu)", what, (unsigned long)mock_live(MOCK_MEMORY));
	check(mock_live(MOCK_PIPELINE) == 0U, "%s: no pipeline left (%lu)", what, (unsigned long)mock_live(MOCK_PIPELINE));
	check(mock_live(MOCK_VIEW) == 0U, "%s: no view left (%lu)", what, (unsigned long)mock_live(MOCK_VIEW));
	check(mock_live(MOCK_SAMPLER) == 0U, "%s: no sampler left (%lu)", what, (unsigned long)mock_live(MOCK_SAMPLER));
	check(mock_live(MOCK_MODULE) == 0U, "%s: no shader module left (%lu)", what, (unsigned long)mock_live(MOCK_MODULE));
}

/*
 * Makes a stroke of a tool along a wave with a pressure that rises and
 * falls and a speed that changes, put on the document's first page (and
 * so baked).
 */
static struct notes_stroke *
make_tool(
	struct notes_document *document,
	unsigned tool,
	uint32_t color,
	float width,
	float x0,
	float y0,
	float x1,
	float y1,
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
	stroke = notes_stroke_create(id, tool, color, width, document->time_base + 1000U * id);
	time = 0U;
	for (index = 0; index < count; index++) {
		share = (double)index / (double)(count - 1U);
		memset(&point, 0, sizeof(point));
		point.x = (float)(x0 + (x1 - x0) * share);
		point.y = (float)(y0 + (y1 - y0) * share + 12.0 * sin(share * 7.0 + seed));
		pressure = sin(share * 3.14159) * (0.8 + 0.2 * sin(share * 23.0 + seed));
		if (pressure < 0.0)
			pressure = 0.0;
		point.pressure = (uint16_t)(65535.0 * pressure);
		point.time_ms = time;
		time += 4U + (index * 7U + (unsigned)seed) % 9U;
		(void)notes_stroke_append(stroke, &point);
	}
	check(notes_document_add_stroke(document, 0, stroke) == 0, "a stroke of tool %u is put on the page", tool);
	check(notes_stroke_outline(stroke) == 0, "the stroke of tool %u has its shapes", tool);
	return stroke;
}

/* Makes a fill of a square with a square hole (two contours) on the first page. */
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
	check(notes_stroke_outline(stroke) == 0, "the fill has its shape");
	return stroke;
}

/* Builds a page frame as main.c's app_page_frame() does: the white page, then the strokes at a scale, clipped to the page. */
static void
page_frame_of(
	struct notes_frame *frame,
	struct notes_stroke **strokes,
	size_t count,
	float scale)
{
	struct notes_view origin;
	size_t index;

	origin.x = 0.0f;
	origin.y = 0.0f;
	origin.scale = scale;
	notes_frame_begin(frame);
	notes_frame_rect(frame, 0.0f, 0.0f, TEST_PAGE_WIDTH * scale, TEST_PAGE_HEIGHT * scale, 0xffffffffU);
	notes_frame_clip(frame, 1, 0.0f, 0.0f, TEST_PAGE_WIDTH * scale, TEST_PAGE_HEIGHT * scale);
	for (index = 0; index < count; index++)
		notes_frame_stroke(frame, strokes[index], &origin);
}

/* Builds a window frame as main.c's app_draw() does: the desk, the page's picture, the stroke being drawn, the toolbar. */
static void
window_frame_of(
	struct notes_frame *frame,
	const struct notes_renderer *renderer,
	struct notes_stroke *live,
	const struct notes_view *view)
{
	notes_frame_begin(frame);
	notes_frame_rect(frame, 0.0f, 0.0f, (float)TEST_WIDTH, (float)TEST_HEIGHT, 0xe4edf7ffU);
	notes_frame_texture(frame, NOTES_TEXTURE_PAGE, view->x, view->y, (float)renderer->page_width, (float)renderer->page_height);
	notes_frame_clip(frame, 1, view->x, view->y, TEST_PAGE_WIDTH * view->scale, TEST_PAGE_HEIGHT * view->scale);
	if (live != NULL)
		notes_frame_stroke(frame, live, view);
	notes_frame_clip(frame, 0, 0.0f, 0.0f, 0.0f, 0.0f);
	notes_frame_texture(frame, NOTES_TEXTURE_TOOLBAR, 0.0f, 0.0f, (float)TEST_WIDTH, (float)NOTES_TOOLBAR_IMAGE_HEIGHT);
}

/* The pipeline a draw is expected to be drawn by: its own, or for a group not made, the fringe or the cover. */
static unsigned
expected_pipe(
	const struct notes_renderer *renderer,
	unsigned pipe)
{
	if (pipe == NOTES_PIPE_FRINGE_GRAIN || pipe == NOTES_PIPE_COVER_GRAIN) {
		if (renderer->groups[NOTES_GROUP_GRAIN] == NOTES_GROUP_MADE)
			return pipe;
	} else if (pipe == NOTES_PIPE_FRINGE_MULTIPLY || pipe == NOTES_PIPE_COVER_MULTIPLY) {
		if (renderer->groups[NOTES_GROUP_MULTIPLY] == NOTES_GROUP_MADE)
			return pipe;
	} else {
		return pipe;
	}
	if (pipe == NOTES_PIPE_FRINGE_GRAIN || pipe == NOTES_PIPE_FRINGE_MULTIPLY)
		return NOTES_PIPE_FRINGE;
	return NOTES_PIPE_COVER;
}

/*
 * Checks the draws recorded for a page frame and a frame against the
 * frames: in order, each draw's pieces one after another in its pass with
 * its pipeline, every piece whole triangles inside its chunk, the pieces'
 * vertices in the chunks the frames' vertices.  Returns the draws that
 * were cut at a chunk's edge.
 */
static size_t
verify_draws(
	const struct notes_renderer *renderer,
	const struct notes_frame *page_frame,
	const struct notes_frame *frame,
	const char *what)
{
	size_t recorded;
	size_t split;
	unsigned pass;
	uint32_t base;
	int good;

	recorded = 0;
	split = 0;
	pass = 1U;
	base = 0;
	good = 1;
	if (page_frame != NULL) {
		if (renderer->page != VK_NULL_HANDLE) {
			good = verify_pass(renderer, page_frame, 0U, pass, &recorded, &split, what);
			pass++;
		}
		base = (uint32_t)page_frame->vertex_count;
	}
	if (good)
		good = verify_pass(renderer, frame, base, pass, &recorded, &split, what);
	check(good, "%s: the draws are the frames' draws", what);
	check(recorded == mock.draw_count, "%s: every recorded draw is one of the frames' (%lu of %lu)", what, (unsigned long)recorded, (unsigned long)mock.draw_count);
	return split;
}

/* Checks one pass's draws (see verify_draws). */
static int
verify_pass(
	const struct notes_renderer *renderer,
	const struct notes_frame *frame,
	uint32_t base,
	unsigned pass,
	size_t *recorded,
	size_t *split,
	const char *what)
{
	const struct notes_draw *draw;
	const struct mock_draw *piece;
	const unsigned char *chunk_bytes;
	size_t index;
	size_t chunk;
	size_t pieces;
	size_t place;
	uint32_t left;
	unsigned pipe;

	for (index = 0; index < frame->draw_count; index++) {
		draw = &frame->draws[index];
		if (draw->count == 0U)
			continue;
		if (draw->first % 3U != 0U || draw->count % 3U != 0U) {
			check(0, "%s: draw %lu is not whole triangles (%u, %u)", what, (unsigned long)index, draw->first, draw->count);
			return 0;
		}

		/* A picture the renderer lacks is not drawn. */
		if (draw->pipe == NOTES_PIPE_TEXTURE && draw->texture == NOTES_TEXTURE_BACKGROUND && renderer->background == VK_NULL_HANDLE)
			continue;
		if (draw->pipe == NOTES_PIPE_TEXTURE && draw->texture == NOTES_TEXTURE_OVERLAY && renderer->overlay == VK_NULL_HANDLE)
			continue;

		/* Its pieces. */
		pipe = expected_pipe(renderer, draw->pipe);
		place = (size_t)base + draw->first;
		left = draw->count;
		pieces = 0;
		while (left > 0U) {
			if (*recorded >= mock.draw_count) {
				check(0, "%s: draw %lu has %u vertices not recorded", what, (unsigned long)index, left);
				return 0;
			}
			piece = &mock.draws[*recorded];
			(*recorded)++;
			pieces++;
			chunk = place / renderer->chunk_vertices;
			if (piece->pass != pass ||
			    piece->pipeline != mock_number(renderer->pipes[pipe]) ||
			    piece->buffer != mock_number(renderer->chunks[chunk].buffer) ||
			    piece->first != place % renderer->chunk_vertices ||
			    piece->count % 3U != 0U ||
			    piece->count > left ||
			    (size_t)piece->first + piece->count > renderer->chunk_vertices) {
				check(0, "%s: draw %lu piece %lu: pass %u (%u) pipeline %lu (%lu) first %u count %u (place %lu left %u)", what,
				      (unsigned long)index, (unsigned long)pieces, piece->pass, pass, (unsigned long)piece->pipeline,
				      (unsigned long)mock_number(renderer->pipes[pipe]), piece->first, piece->count, (unsigned long)place, left);
				return 0;
			}

			/* Its vertices in the chunk are the frame's. */
			chunk_bytes = renderer->chunks[chunk].map;
			if (memcmp(chunk_bytes + (size_t)piece->first * NOTES_VERTEX_FLOATS * sizeof(float),
				   frame->vertices + (place - base) * NOTES_VERTEX_FLOATS,
				   (size_t)piece->count * NOTES_VERTEX_FLOATS * sizeof(float)) != 0) {
				check(0, "%s: draw %lu piece %lu: the vertices differ", what, (unsigned long)index, (unsigned long)pieces);
				return 0;
			}
			place += piece->count;
			left -= piece->count;
		}
		if (pieces > 1U)
			(*split)++;
	}
	return 1;
}

/* C2, C7: the geometry of the shapes, and the grain's constants. */
static void
test_geometry(
	struct notes_document *document)
{
	static unsigned char tile[NOTES_GRAIN_SIDE * NOTES_GRAIN_SIDE];
	struct notes_frame polygon_frame;
	struct notes_frame stroke_frame;
	struct notes_frame frame;
	struct notes_stroke *stroke;
	struct notes_view view;
	const struct notes_shape *shape;
	const float *vertex;
	unsigned long sum;
	uint32_t hash;
	size_t seams;
	size_t index;
	size_t vertex_index;
	unsigned tools[2];
	unsigned tool;
	float alpha;
	float flat;
	int same;
	int good;

	memset(&polygon_frame, 0, sizeof(polygon_frame));
	memset(&stroke_frame, 0, sizeof(stroke_frame));
	memset(&frame, 0, sizeof(frame));
	view.x = 37.0f;
	view.y = 91.0f;
	view.scale = 1.37f;

	/* The tile and its mean. */
	notes_grain_tile(tile);
	hash = 0x811c9dc5U;
	sum = 0;
	for (index = 0; index < sizeof(tile); index++) {
		hash ^= tile[index];
		hash *= 0x01000193U;
		sum += tile[index];
	}
	check(hash == TEST_GRAIN_HASH, "the grain's tile is the PDF's: %08x", (unsigned)hash);
	check(fabs((double)sum / (double)sizeof(tile) / 255.0 - (double)NOTES_GRAIN_MEAN) < 1e-7, "NOTES_GRAIN_MEAN is the tile's mean %.9f",
	      (double)sum / (double)sizeof(tile) / 255.0);

	/* The pen's and the highlighter's vertices are those notes_frame_polygon() gives their outline. */
	tools[0] = NOTES_TOOL_PEN;
	tools[1] = NOTES_TOOL_HIGHLIGHTER;
	for (tool = 0; tool < 2U; tool++) {
		for (index = 0; index < 10U; index++) {
			stroke = make_tool(document, tools[tool], tool == 0U ? 0x1d4ed8ffU : 0xfacc1566U, 3.0f + (float)index, 40.0f, 60.0f + 50.0f * (float)index,
					   500.0f, 90.0f + 50.0f * (float)index, 40U + 13U * (unsigned)index, (int)index);
			notes_frame_begin(&polygon_frame);
			notes_frame_begin(&stroke_frame);
			notes_frame_polygon(&polygon_frame, stroke->outline, stroke->outline_count, &view, stroke->color);
			notes_frame_stroke(&stroke_frame, stroke, &view);
			same = polygon_frame.vertex_count == stroke_frame.vertex_count && polygon_frame.draw_count == stroke_frame.draw_count;
			if (same)
				same = memcmp(polygon_frame.vertices, stroke_frame.vertices, polygon_frame.vertex_count * NOTES_VERTEX_FLOATS * sizeof(float)) == 0;
			for (vertex_index = 0; same && vertex_index < polygon_frame.draw_count; vertex_index++) {
				if (polygon_frame.draws[vertex_index].pipe != stroke_frame.draws[vertex_index].pipe ||
				    polygon_frame.draws[vertex_index].first != stroke_frame.draws[vertex_index].first ||
				    polygon_frame.draws[vertex_index].count != stroke_frame.draws[vertex_index].count)
					same = 0;
			}
			check(same, "tool %u stroke %lu: the same vertices and draws as its outline's polygon", tools[tool], (unsigned long)index);
		}
	}

	/* A pencil: its ranges' fringes leave out their seams, its vertices carry the flat and the grain's opacity, its draws the page's place. */
	stroke = make_tool(document, NOTES_TOOL_PENCIL, 0x111111ffU, 2.4f, 50.0f, 600.0f, 540.0f, 650.0f, 160U, 3);
	check(stroke->shape_count >= 3U, "the pencil has ranges: %lu", (unsigned long)stroke->shape_count);
	check(stroke->outline == stroke->shapes[0].polygon, "the pencil's outline is its first range");
	good = 1;
	for (index = 0; index < stroke->shape_count; index++) {
		shape = &stroke->shapes[index];
		seams = 0;
		if (shape->seam_start != PDF_OUTLINE_NO_SEAM)
			seams++;
		if (shape->seam_end != PDF_OUTLINE_NO_SEAM && shape->seam_end != shape->seam_start)
			seams++;
		if (index > 0U && shape->seam_start == PDF_OUTLINE_NO_SEAM)
			good = 0;
		if (index + 1U < stroke->shape_count && shape->seam_end == PDF_OUTLINE_NO_SEAM)
			good = 0;
		notes_frame_begin(&frame);
		notes_frame_shape(&frame, shape, &view, stroke->color);
		if (frame.draw_count != 3U ||
		    frame.draws[0].pipe != NOTES_PIPE_STENCIL ||
		    frame.draws[1].pipe != NOTES_PIPE_FRINGE_GRAIN ||
		    frame.draws[2].pipe != NOTES_PIPE_COVER_GRAIN ||
		    frame.draws[0].count != (shape->corner_count - 2U) * 3U ||
		    frame.draws[1].count != (shape->corner_count - seams) * 6U ||
		    frame.draws[2].count != 6U) {
			good = 0;
			continue;
		}
		if (frame.draws[1].place[0] != view.x || frame.draws[1].place[1] != view.y || frame.draws[1].place[2] != view.scale)
			good = 0;
		if (frame.draws[2].place[2] != view.scale || frame.draws[0].place[2] != 0.0f)
			good = 0;
		alpha = shape->alpha;
		flat = 1.0f - (1.0f - alpha * NOTES_GRAIN_FLOOR) * (1.0f - alpha * NOTES_GRAIN_MEAN);
		for (vertex_index = 0; vertex_index < frame.vertex_count; vertex_index++) {
			vertex = frame.vertices + vertex_index * NOTES_VERTEX_FLOATS;
			if (vertex[3] != alpha || vertex[7] != flat || fabsf(vertex[4] - 17.0f / 255.0f) > 1e-6f)
				good = 0;
		}
	}
	check(good, "the pencil's ranges: seams between them, no fringe there, the grain's vertices and place");

	/* A watercolour: its three shapes multiplied, in its colour at each shape's opacity. */
	stroke = make_tool(document, NOTES_TOOL_WATERCOLOR, 0x2563ebffU, 16.0f, 60.0f, 700.0f, 500.0f, 760.0f, 120U, 5);
	notes_frame_begin(&frame);
	notes_frame_stroke(&frame, stroke, &view);
	good = stroke->shape_count == 3U && frame.draw_count == 9U;
	for (index = 0; good && index < 3U; index++) {
		if (frame.draws[index * 3U].pipe != NOTES_PIPE_STENCIL ||
		    frame.draws[index * 3U + 1U].pipe != NOTES_PIPE_FRINGE_MULTIPLY ||
		    frame.draws[index * 3U + 2U].pipe != NOTES_PIPE_COVER_MULTIPLY)
			good = 0;
		vertex = frame.vertices + (size_t)frame.draws[index * 3U + 1U].first * NOTES_VERTEX_FLOATS;
		if (vertex[7] != stroke->shapes[index].alpha || vertex[3] != 0.0f || fabsf(vertex[6] - 235.0f / 255.0f) > 1e-6f)
			good = 0;
		if (frame.draws[index * 3U + 1U].place[2] != 0.0f)
			good = 0;
	}
	check(good, "the watercolour: three multiplied shapes at their opacities");

	/* A fill of two contours: a fan for each, the fringe of both, one cover. */
	stroke = make_fill(document, 100.0f, 100.0f, 200.0f);
	notes_frame_begin(&frame);
	notes_frame_stroke(&frame, stroke, &view);
	check(frame.draw_count == 3U &&
	      frame.draws[0].count == 2U * (4U - 2U) * 3U &&
	      frame.draws[1].pipe == NOTES_PIPE_FRINGE &&
	      frame.draws[1].count == 8U * 6U &&
	      frame.draws[2].count == 6U,
	      "the fill: fans of both contours (%u), the fringe of 8 edges (%u), one cover", frame.draws[0].count, frame.draws[1].count);
	vertex = frame.vertices + 6U * NOTES_VERTEX_FLOATS;
	check(vertex[0] == view.x + 160.0f * view.scale && vertex[1] == view.y + 160.0f * view.scale, "the hole's fan starts at its own first corner");
	vertex = frame.vertices + (size_t)frame.draws[2].first * NOTES_VERTEX_FLOATS;
	check(vertex[0] == view.x + 100.0f * view.scale - 1.0f && vertex[1] == view.y + 100.0f * view.scale - 1.0f, "the cover is the outer contour's box");
	notes_stroke_free(stroke);

	/* The grain's size for a scale: the finest of at most 1.5 texels a pixel, the coarsest below. */
	check(notes_frame_grain_level(4.0f) == 0U, "4 px/pt draws the full tile");
	check(notes_frame_grain_level(256.0f / (48.0f * 1.5f)) == 0U, "the full tile down to 1.5 texels a pixel");
	check(notes_frame_grain_level(3.5f) == 1U, "3.5 px/pt draws the half tile");
	check(notes_frame_grain_level(2.0f) == 1U, "2 px/pt draws the half tile");
	check(notes_frame_grain_level(1.5f) == 2U, "1.5 px/pt draws the quarter tile");
	check(notes_frame_grain_level(1.0f) == 2U, "1 px/pt draws the quarter tile");
	check(notes_frame_grain_level(0.5f) == 2U, "0.5 px/pt draws the quarter tile (the coarsest)");

	notes_frame_free(&polygon_frame);
	notes_frame_free(&stroke_frame);
	notes_frame_free(&frame);
}

/* C2: the grain's pictures hold the tile and its coarser sizes, in all four channels, named in their sets with the repeating sampler. */
static void
test_grain_pictures(
	const struct notes_renderer *renderer)
{
	static unsigned char tiles[NOTES_GRAIN_LEVELS][NOTES_GRAIN_SIDE * NOTES_GRAIN_SIDE];
	const struct mock_object *image;
	const struct mock_object *memory;
	const struct mock_object *set;
	const struct mock_object *sampler;
	const unsigned char *pixel;
	unsigned level;
	uint32_t side;
	uint32_t x;
	uint32_t y;
	int good;

	notes_grain_tile(tiles[0]);
	notes_grain_level(tiles[0], NOTES_GRAIN_SIDE, tiles[1]);
	notes_grain_level(tiles[1], NOTES_GRAIN_SIDE / 2U, tiles[2]);
	sampler = mock_get(renderer->grain_sampler);
	check(sampler != NULL && sampler->kind == MOCK_SAMPLER && sampler->address == VK_SAMPLER_ADDRESS_MODE_REPEAT && sampler->filter == VK_FILTER_LINEAR,
	      "the grain's sampler repeats and blends");
	for (level = 0; level < NOTES_GRAIN_LEVELS; level++) {
		side = NOTES_GRAIN_SIDE >> level;
		image = mock_get(renderer->grain[level].image);
		check(image != NULL && image->kind == MOCK_IMAGE && image->width == side && image->height == side && image->tiling == VK_IMAGE_TILING_LINEAR,
		      "the grain's picture %u is %u texels a side", level, side);
		if (image == NULL)
			continue;
		memory = &((const struct mock_object *)mock_get(renderer->grain[level].memory))[0];
		good = memory->bytes != NULL;
		for (y = 0; good && y < side; y++) {
			for (x = 0; x < side; x++) {
				pixel = memory->bytes + (size_t)y * side * 4U + x * 4U;
				if (pixel[0] != tiles[level][y * side + x] || pixel[1] != pixel[0] || pixel[2] != pixel[0] || pixel[3] != pixel[0])
					good = 0;
			}
		}
		check(good, "the grain's picture %u holds its texels in all four channels", level);
		set = mock_get(renderer->sets[NOTES_TEXTURE_GRAIN + level]);
		check(set->view == mock_number(renderer->grain[level].view) && set->sampler == mock_number(renderer->grain_sampler),
		      "the grain's set %u names its picture with the grain's sampler", level);
	}
}

/* C5 (and C2, C7): the groups are made by the frames that first use them, once each. */
static void
test_lazy(
	struct notes_document *document)
{
	struct notes_renderer renderer;
	struct notes_frame page_frame;
	struct notes_frame frame;
	struct notes_stroke *strokes[4];
	struct notes_view view;
	const struct mock_object *pipeline;
	const struct mock_draw *draw;
	unsigned images;
	float scale;
	size_t index;
	int good;
	VkResult error;

	mock_reset();
	mock.grain_code = notes_grain_frag;
	mock.grain_size = sizeof(notes_grain_frag);
	memset(&page_frame, 0, sizeof(page_frame));
	memset(&frame, 0, sizeof(frame));
	error = notes_renderer_open(&renderer, &window);
	check(error == VK_SUCCESS, "lazy: the renderer opens (%d)", (int)error);
	check(mock.pipelines_made == NOTES_PIPES_FIRST, "lazy: five pipelines are made with the renderer (%u)", mock.pipelines_made);
	check(renderer.chunk_count == 1U && renderer.chunk_bytes == NOTES_CHUNK_BYTES && renderer.chunk_vertices == 131070U,
	      "lazy: one chunk of 4 MiB, 131070 vertices (%lu)", (unsigned long)renderer.chunk_vertices);
	error = notes_renderer_page(&renderer, TEST_PICTURE_WIDTH, TEST_PICTURE_HEIGHT);
	check(error == VK_SUCCESS, "lazy: the page's picture");
	scale = (float)TEST_PICTURE_WIDTH / TEST_PAGE_WIDTH;
	view.x = 190.0f;
	view.y = 70.0f;
	view.scale = scale;

	/* Pens only: no group is made, no grain picture allocated. */
	strokes[0] = make_tool(document, NOTES_TOOL_PEN, 0x1d4ed8ffU, 3.0f, 40.0f, 100.0f, 500.0f, 140.0f, 60U, 1);
	strokes[1] = make_tool(document, NOTES_TOOL_HIGHLIGHTER, 0xfacc1566U, 16.0f, 40.0f, 200.0f, 500.0f, 240.0f, 60U, 2);
	page_frame_of(&page_frame, strokes, 2U, scale);
	window_frame_of(&frame, &renderer, strokes[0], &view);
	error = notes_renderer_draw(&renderer, &frame, &page_frame, 1);
	check(error == VK_SUCCESS, "lazy: a frame of pens is drawn");
	check(mock.pipelines_made == NOTES_PIPES_FIRST && renderer.groups[0] == NOTES_GROUP_UNTRIED && renderer.groups[1] == NOTES_GROUP_UNTRIED,
	      "lazy: pens make no group");
	check(mock_live(MOCK_SAMPLER) == 1U && renderer.grain[0].image == VK_NULL_HANDLE, "lazy: pens allocate no grain");
	(void)verify_draws(&renderer, &page_frame, &frame, "lazy: pens");

	/* A pencil makes the grain's group, its pictures and its sampler. */
	strokes[2] = make_tool(document, NOTES_TOOL_PENCIL, 0x111111ffU, 2.4f, 40.0f, 300.0f, 500.0f, 340.0f, 160U, 3);
	page_frame_of(&page_frame, strokes, 3U, scale);
	window_frame_of(&frame, &renderer, strokes[2], &view);
	error = notes_renderer_draw(&renderer, &frame, &page_frame, 1);
	check(error == VK_SUCCESS, "lazy: a frame with a pencil is drawn");
	check(renderer.groups[NOTES_GROUP_GRAIN] == NOTES_GROUP_MADE && renderer.groups[NOTES_GROUP_MULTIPLY] == NOTES_GROUP_UNTRIED &&
	      mock.pipelines_made == NOTES_PIPES_FIRST + 2U, "lazy: the pencil makes the grain's group only (%u pipelines)", mock.pipelines_made);
	images = 0;
	for (index = 0; index < NOTES_GRAIN_LEVELS; index++) {
		if (renderer.grain[index].image != VK_NULL_HANDLE)
			images++;
	}
	check(images == NOTES_GRAIN_LEVELS, "lazy: the grain's three pictures");
	test_grain_pictures(&renderer);
	pipeline = mock_get(renderer.pipes[NOTES_PIPE_FRINGE_GRAIN]);
	check(pipeline != NULL && pipeline->stencil && pipeline->compare == VK_COMPARE_OP_EQUAL &&
	      mock_same_code(pipeline->fragment, pipeline->fragment_size, notes_grain_frag, sizeof(notes_grain_frag)) &&
	      pipeline->dst_color == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, "lazy: the grain's fringe draws outside, through the grain, source over");
	pipeline = mock_get(renderer.pipes[NOTES_PIPE_COVER_GRAIN]);
	check(pipeline != NULL && pipeline->compare == VK_COMPARE_OP_NOT_EQUAL && pipeline->pass_op == VK_STENCIL_OP_ZERO,
	      "lazy: the grain's cover draws inside and clears the stencil");
	(void)verify_draws(&renderer, &page_frame, &frame, "lazy: pencil");

	/* The grain's draws bind the size for their scale and push their page's place. */
	good = 1;
	for (index = 0; index < mock.draw_count; index++) {
		draw = &mock.draws[index];
		if (draw->pipeline != mock_number(renderer.pipes[NOTES_PIPE_FRINGE_GRAIN]) && draw->pipeline != mock_number(renderer.pipes[NOTES_PIPE_COVER_GRAIN]))
			continue;
		if (draw->set != mock_number(renderer.sets[NOTES_TEXTURE_GRAIN + notes_frame_grain_level(scale)]))
			good = 0;
		if (draw->pass == 1U && (draw->push[4] != 0.0f || draw->push[5] != 0.0f))
			good = 0;
		if (draw->pass == 2U && (draw->push[4] != view.x || draw->push[5] != view.y))
			good = 0;
		if (fabsf(draw->push[6] - (float)(1.0 / (NOTES_GRAIN_CELL * (double)scale))) > 1e-9f)
			good = 0;
		if (draw->push[0] != (draw->pass == 1U ? (float)TEST_PICTURE_WIDTH : (float)TEST_WIDTH))
			good = 0;
	}
	check(good, "lazy: the grain's draws bind the grain's size %u and push the page's place", notes_frame_grain_level(scale));

	/* A watercolour makes the Multiply's group. */
	strokes[3] = make_tool(document, NOTES_TOOL_WATERCOLOR, 0x2563ebffU, 16.0f, 40.0f, 400.0f, 500.0f, 440.0f, 120U, 4);
	page_frame_of(&page_frame, strokes, 4U, scale);
	window_frame_of(&frame, &renderer, strokes[3], &view);
	error = notes_renderer_draw(&renderer, &frame, &page_frame, 1);
	check(error == VK_SUCCESS, "lazy: a frame with a watercolour is drawn");
	check(renderer.groups[NOTES_GROUP_MULTIPLY] == NOTES_GROUP_MADE && mock.pipelines_made == NOTES_PIPES_FIRST + 4U,
	      "lazy: the watercolour makes the Multiply's group (%u pipelines)", mock.pipelines_made);
	pipeline = mock_get(renderer.pipes[NOTES_PIPE_FRINGE_MULTIPLY]);
	check(pipeline != NULL &&
	      pipeline->src_color == VK_BLEND_FACTOR_ZERO && pipeline->dst_color == VK_BLEND_FACTOR_SRC_COLOR &&
	      pipeline->src_alpha == VK_BLEND_FACTOR_ZERO && pipeline->dst_alpha == VK_BLEND_FACTOR_ONE &&
	      pipeline->compare == VK_COMPARE_OP_EQUAL &&
	      mock_same_code(pipeline->fragment, pipeline->fragment_size, notes_multiply_frag, sizeof(notes_multiply_frag)),
	      "lazy: the Multiply's fringe multiplies the target and keeps its alpha");
	pipeline = mock_get(renderer.pipes[NOTES_PIPE_COVER_MULTIPLY]);
	check(pipeline != NULL && pipeline->dst_color == VK_BLEND_FACTOR_SRC_COLOR && pipeline->compare == VK_COMPARE_OP_NOT_EQUAL,
	      "lazy: the Multiply's cover multiplies inside");
	(void)verify_draws(&renderer, &page_frame, &frame, "lazy: watercolour");

	/* Again: nothing more is made. */
	error = notes_renderer_draw(&renderer, &frame, &page_frame, 1);
	check(error == VK_SUCCESS && mock.pipelines_made == NOTES_PIPES_FIRST + 4U && mock_live(MOCK_SAMPLER) == 2U,
	      "lazy: a second frame makes nothing more");
	check(log_count("NOTES PIPE made name=grain") == 1U && log_count("NOTES PIPE made name=multiply") == 1U, "lazy: each group is logged once");
	check(log_count("NOTES GRAIN hash=2a644409") == 1U, "lazy: the grain's hash is logged");
	check(log_count("NOTES PIPE fallback") == 0U, "lazy: no fallback");

	notes_renderer_close(&renderer);
	leaks("lazy");
	notes_frame_free(&page_frame);
	notes_frame_free(&frame);
}

/* Adds to a page frame a polygon of many corners round a centre, and pencils: more than 600,000 vertices with the window frame. */
static void
large_frame(
	struct notes_frame *page_frame,
	struct notes_stroke **pencils,
	size_t pencil_count,
	float scale,
	unsigned corners)
{
	struct pdf_point *points;
	struct notes_view origin;
	double angle;
	unsigned index;

	points = malloc(corners * sizeof(*points));
	for (index = 0; index < corners; index++) {
		angle = 2.0 * 3.14159265358979 * (double)index / (double)corners;
		points[index].x = 300.0 + (250.0 + 20.0 * sin(angle * 331.0)) * cos(angle);
		points[index].y = 420.0 + (250.0 + 20.0 * sin(angle * 331.0)) * sin(angle);
	}
	page_frame_of(page_frame, pencils, pencil_count, scale);
	origin.x = 0.0f;
	origin.y = 0.0f;
	origin.scale = scale;
	notes_frame_polygon(page_frame, points, corners, &origin, 0x0f766eccU);
	free(points);
}

/* C3: on i915's model a frame of more than 600,000 vertices is drawn from chunks within the device's allocations. */
static void
test_large(
	struct notes_document *document)
{
	struct notes_renderer renderer;
	struct notes_frame page_frame;
	struct notes_frame frame;
	struct notes_stroke *strokes[3];
	struct notes_view view;
	size_t total;
	size_t split;
	VkResult error;
	float scale;

	mock_reset();
	mock.buffer_limit = TEST_I915_BYTES;
	mock.image_limit = TEST_I915_BYTES;
	memset(&page_frame, 0, sizeof(page_frame));
	memset(&frame, 0, sizeof(frame));
	error = notes_renderer_open(&renderer, &window);
	check(error == VK_SUCCESS, "large: the renderer opens on i915's model");
	error = notes_renderer_page(&renderer, TEST_PICTURE_WIDTH, TEST_PICTURE_HEIGHT);
	check(error == VK_SUCCESS, "large: the page's picture");
	scale = (float)TEST_PICTURE_WIDTH / TEST_PAGE_WIDTH;
	view.x = 190.0f;
	view.y = 70.0f;
	view.scale = scale;
	strokes[0] = make_tool(document, NOTES_TOOL_PENCIL, 0x111111ffU, 2.4f, 40.0f, 700.0f, 500.0f, 740.0f, 160U, 6);
	strokes[1] = make_tool(document, NOTES_TOOL_WATERCOLOR, 0x2563ebffU, 16.0f, 40.0f, 760.0f, 500.0f, 800.0f, 120U, 7);
	strokes[2] = make_tool(document, NOTES_TOOL_BRUSH, 0x7c3aedffU, 8.0f, 40.0f, 60.0f, 500.0f, 90.0f, 200U, 8);
	large_frame(&page_frame, strokes, 3U, scale, TEST_LARGE_CORNERS);
	window_frame_of(&frame, &renderer, strokes[0], &view);
	total = page_frame.vertex_count + frame.vertex_count;
	check(total > 600000U, "large: the frame has more than 600,000 vertices (%lu)", (unsigned long)total);
	check(total * NOTES_VERTEX_FLOATS * sizeof(float) > TEST_I915_BYTES, "large: one buffer of them is more than i915 allocates");
	error = notes_renderer_draw(&renderer, &frame, &page_frame, 1);
	check(error == VK_SUCCESS, "large: the frame is drawn (%d)", (int)error);
	check(renderer.chunk_count == (total + 131069U) / 131070U, "large: %lu chunks of 4 MiB", (unsigned long)renderer.chunk_count);
	check(mock.largest <= TEST_I915_BYTES && mock.refused == 0U, "large: no allocation over 16 MiB (largest %lu)", (unsigned long)mock.largest);
	split = verify_draws(&renderer, &page_frame, &frame, "large");
	check(split >= 1U, "large: draws are cut at the chunks' edges (%lu)", (unsigned long)split);
	fprintf(stderr, "large: %lu vertices, %lu chunks, %lu draws recorded, %lu draws cut\n", (unsigned long)total, (unsigned long)renderer.chunk_count,
		(unsigned long)mock.draw_count, (unsigned long)split);
	check(log_count("NOTES FRAME vertices=") >= 2U, "large: the peak is logged");

	notes_renderer_close(&renderer);
	leaks("large");
	notes_frame_free(&page_frame);
	notes_frame_free(&frame);
}

/* C4: a device that refuses chunks gets them smaller; one that refuses the smallest fails as before. */
static void
test_halving(
	struct notes_document *document)
{
	struct notes_renderer renderer;
	struct notes_frame page_frame;
	struct notes_frame frame;
	struct notes_stroke *strokes[1];
	struct notes_view view;
	size_t refused;
	size_t total;
	float scale;
	VkResult error;

	mock_reset();
	mock.buffer_limit = 1024U * 1024U;
	mock.image_limit = TEST_I915_BYTES;
	memset(&page_frame, 0, sizeof(page_frame));
	memset(&frame, 0, sizeof(frame));
	refused = log_count("NOTES FRAME chunk refused");
	error = notes_renderer_open(&renderer, &window);
	check(error == VK_SUCCESS, "halving: the renderer opens on a device of 1 MiB buffers (%d)", (int)error);
	check(renderer.chunk_bytes == 1024U * 1024U && renderer.chunk_vertices == 32766U, "halving: chunks of 1 MiB (%lu)", (unsigned long)renderer.chunk_bytes);
	check(log_count("NOTES FRAME chunk refused") == refused + 2U, "halving: the two refusals are logged");
	error = notes_renderer_page(&renderer, TEST_PICTURE_WIDTH, TEST_PICTURE_HEIGHT);
	scale = (float)TEST_PICTURE_WIDTH / TEST_PAGE_WIDTH;
	view.x = 190.0f;
	view.y = 70.0f;
	view.scale = scale;
	strokes[0] = make_tool(document, NOTES_TOOL_PENCIL, 0x111111ffU, 2.4f, 40.0f, 700.0f, 500.0f, 740.0f, 160U, 9);
	large_frame(&page_frame, strokes, 1U, scale, TEST_LARGE_CORNERS);
	window_frame_of(&frame, &renderer, NULL, &view);
	total = page_frame.vertex_count + frame.vertex_count;
	error = notes_renderer_draw(&renderer, &frame, &page_frame, 1);
	check(error == VK_SUCCESS, "halving: the large frame is drawn");
	check(renderer.chunk_count == (total + 32765U) / 32766U, "halving: %lu chunks of 1 MiB", (unsigned long)renderer.chunk_count);
	(void)verify_draws(&renderer, &page_frame, &frame, "halving");

	/* A device that then refuses anything over 128 KiB: more chunks cannot be made at any size. */
	mock.buffer_limit = 128U * 1024U;
	large_frame(&page_frame, strokes, 1U, scale, 2U * TEST_LARGE_CORNERS);
	error = notes_renderer_draw(&renderer, &frame, &page_frame, 1);
	check(error == VK_ERROR_OUT_OF_DEVICE_MEMORY, "halving: a frame the smallest chunks cannot hold fails (%d)", (int)error);
	notes_renderer_close(&renderer);
	leaks("halving: the failed frame");

	/* A renderer cannot open on a device that refuses the smallest chunk. */
	mock_reset();
	mock.buffer_limit = 128U * 1024U;
	error = notes_renderer_open(&renderer, &window);
	check(error == VK_ERROR_OUT_OF_DEVICE_MEMORY, "halving: a device of 128 KiB buffers does not open (%d)", (int)error);
	notes_renderer_close(&renderer);
	leaks("halving: the device that does not open");

	notes_frame_free(&page_frame);
	notes_frame_free(&frame);
}

/* C6: a device that refuses the groups' pipelines opens, draws and logs each fallback once. */
static void
test_fallback(
	struct notes_document *document)
{
	struct notes_renderer renderer;
	struct notes_frame page_frame;
	struct notes_frame frame;
	struct notes_stroke *strokes[3];
	struct notes_view view;
	unsigned round;
	unsigned refused;
	float scale;
	VkResult error;

	memset(&page_frame, 0, sizeof(page_frame));
	memset(&frame, 0, sizeof(frame));
	scale = (float)TEST_PICTURE_WIDTH / TEST_PAGE_WIDTH;
	view.x = 190.0f;
	view.y = 70.0f;
	view.scale = scale;
	strokes[0] = make_tool(document, NOTES_TOOL_PEN, 0x1d4ed8ffU, 3.0f, 40.0f, 100.0f, 500.0f, 140.0f, 60U, 10);
	strokes[1] = make_tool(document, NOTES_TOOL_PENCIL, 0x111111ffU, 2.4f, 40.0f, 300.0f, 500.0f, 340.0f, 160U, 11);
	strokes[2] = make_tool(document, NOTES_TOOL_WATERCOLOR, 0x2563ebffU, 16.0f, 40.0f, 400.0f, 500.0f, 440.0f, 120U, 12);

	/* Both refused (round 0), the grain only (round 1), the Multiply only (round 2). */
	for (round = 0; round < 3U; round++) {
		mock_reset();
		mock.grain_code = notes_grain_frag;
		mock.grain_size = sizeof(notes_grain_frag);
		mock.refuse_grain = round != 2U;
		mock.refuse_multiply = round != 1U;
		error = notes_renderer_open(&renderer, &window);
		check(error == VK_SUCCESS, "fallback %u: the renderer opens", round);
		error = notes_renderer_page(&renderer, TEST_PICTURE_WIDTH, TEST_PICTURE_HEIGHT);
		page_frame_of(&page_frame, strokes, 3U, scale);
		window_frame_of(&frame, &renderer, strokes[2], &view);
		error = notes_renderer_draw(&renderer, &frame, &page_frame, 1);
		check(error == VK_SUCCESS, "fallback %u: the frame is drawn (%d)", round, (int)error);
		check((renderer.groups[NOTES_GROUP_GRAIN] == NOTES_GROUP_REFUSED) == mock.refuse_grain, "fallback %u: the grain's group", round);
		check((renderer.groups[NOTES_GROUP_MULTIPLY] == NOTES_GROUP_REFUSED) == mock.refuse_multiply, "fallback %u: the Multiply's group", round);
		if (mock.refuse_grain) {
			check(renderer.grain[0].image == VK_NULL_HANDLE && renderer.grain_sampler == VK_NULL_HANDLE &&
			      renderer.pipes[NOTES_PIPE_FRINGE_GRAIN] == VK_NULL_HANDLE && renderer.pipes[NOTES_PIPE_COVER_GRAIN] == VK_NULL_HANDLE,
			      "fallback %u: a refused grain keeps nothing", round);
		}
		(void)verify_draws(&renderer, &page_frame, &frame, "fallback");
		refused = mock.pipelines_refused;
		check(refused == (unsigned)(mock.refuse_grain + mock.refuse_multiply), "fallback %u: each refused group is tried once (%u)", round, refused);

		/* A second frame tries nothing again. */
		error = notes_renderer_draw(&renderer, &frame, &page_frame, 1);
		check(error == VK_SUCCESS && mock.pipelines_refused == refused, "fallback %u: a second frame tries no group again", round);
		(void)verify_draws(&renderer, &page_frame, &frame, "fallback: again");
		notes_renderer_close(&renderer);
		leaks("fallback");
	}
	check(log_count("NOTES PIPE fallback name=grain error=") == 2U, "fallback: the grain's fallback is logged once each time");
	check(log_count("NOTES PIPE fallback name=multiply error=") == 2U, "fallback: the Multiply's fallback is logged once each time");

	notes_frame_free(&page_frame);
	notes_frame_free(&frame);
}
