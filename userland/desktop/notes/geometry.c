/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The geometry of a Notes frame: rectangles, stroke outlines and the
 * toolbar's picture as triangles, and the draw calls that show them.
 *
 * A stroke's outline is any closed polygon, which may cross itself, filled
 * by the nonzero rule as the PDF fills it.  It is drawn in three calls
 * through the stencil buffer (render.c):
 *
 *  1. a fan of triangles from the first corner counts the winding of every
 *     pixel into the stencil (front faces up, back faces down) without
 *     drawing colour;
 *  2. a fringe, a band one pixel wide on each side of every edge, is drawn
 *     where the stencil is still zero (just outside the polygon) with an
 *     alpha that falls from the edge outwards: the anti-aliasing, without
 *     multisampling;
 *  3. the polygon's bounding box is drawn where the stencil is not zero
 *     (inside), which sets those stencil values back to zero.
 *
 * Each pixel inside is drawn once, so a translucent highlighter does not
 * darken where it crosses itself.
 */

#include "app.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The fewest vertices and draws a frame's arrays grow to. */
#define GEOMETRY_VERTICES_MIN	4096U
#define GEOMETRY_DRAWS_MIN	256U

/* The half width of the anti-aliasing fringe, in pixels. */
#define GEOMETRY_FRINGE		1.0f

static float *frame_reserve(struct notes_frame *frame, size_t count);
static void frame_draw(struct notes_frame *frame, unsigned pipe, size_t first);
static float *frame_vertex(float *vertex, float x, float y, float u, float v, const float *color);
static void color_floats(uint32_t color, float *floats);

/*
 * Empties a frame for the next one, keeping its arrays.
 */
void
notes_frame_begin(
	struct notes_frame *frame)
{
	/* No vertices, no draws, no clipping, no failure. */
	frame->vertex_count = 0;
	frame->draw_count = 0;
	frame->clipped = 0;
	frame->error = 0;
}

/*
 * Frees a frame's arrays.
 */
void
notes_frame_free(
	struct notes_frame *frame)
{
	/* The arrays, and the frame is empty. */
	free(frame->vertices);
	free(frame->draws);
	memset(frame, 0, sizeof(*frame));
}

/*
 * Adds a filled rectangle in pixels, in a colour (0xRRGGBBAA).
 */
void
notes_frame_rect(
	struct notes_frame *frame,
	float x,
	float y,
	float width,
	float height,
	uint32_t color)
{
	float colors[4];
	float *vertex;
	size_t first;

	/* Room for two triangles. */
	first = frame->vertex_count;
	vertex = frame_reserve(frame, 6U);
	if (vertex == NULL)
		return;

	/* The two triangles, with no fringe distance. */
	color_floats(color, colors);
	vertex = frame_vertex(vertex, x, y, 0.0f, 0.0f, colors);
	vertex = frame_vertex(vertex, x + width, y, 0.0f, 0.0f, colors);
	vertex = frame_vertex(vertex, x, y + height, 0.0f, 0.0f, colors);
	vertex = frame_vertex(vertex, x + width, y, 0.0f, 0.0f, colors);
	vertex = frame_vertex(vertex, x + width, y + height, 0.0f, 0.0f, colors);
	(void)frame_vertex(vertex, x, y + height, 0.0f, 0.0f, colors);

	/* One plain draw. */
	frame_draw(frame, NOTES_PIPE_PLAIN, first);
}

/*
 * Turns clipping to a rectangle in pixels on or off for the draws that follow.
 */
void
notes_frame_clip(
	struct notes_frame *frame,
	int enabled,
	float x,
	float y,
	float width,
	float height)
{
	/* The rectangle, rounded outwards to whole pixels. */
	frame->clipped = enabled;
	frame->clip[0] = (int32_t)floor((double)x);
	frame->clip[1] = (int32_t)floor((double)y);
	frame->clip[2] = (int32_t)ceil((double)(x + width)) - frame->clip[0];
	frame->clip[3] = (int32_t)ceil((double)(y + height)) - frame->clip[1];
}

/*
 * Adds a polygon in page points, filled by the nonzero rule and
 * anti-aliased, in a colour (0xRRGGBBAA), placed by a view.
 */
void
notes_frame_polygon(
	struct notes_frame *frame,
	const struct pdf_point *points,
	size_t count,
	const struct notes_view *view,
	uint32_t color)
{
	float colors[4];
	float *vertex;
	float x0;
	float y0;
	float xa;
	float ya;
	float xb;
	float yb;
	float nx;
	float ny;
	float length;
	float box[4];
	size_t first;
	size_t index;
	size_t next;

	/* A polygon needs three corners. */
	if (count < 3U)
		return;
	color_floats(color, colors);

	/* The fan from the first corner, which counts the winding into the stencil. */
	first = frame->vertex_count;
	vertex = frame_reserve(frame, (count - 2U) * 3U);
	if (vertex == NULL)
		return;
	x0 = view->x + (float)points[0].x * view->scale;
	y0 = view->y + (float)points[0].y * view->scale;
	box[0] = x0;
	box[1] = y0;
	box[2] = x0;
	box[3] = y0;
	for (index = 1; index + 1U < count; index++) {
		xa = view->x + (float)points[index].x * view->scale;
		ya = view->y + (float)points[index].y * view->scale;
		xb = view->x + (float)points[index + 1U].x * view->scale;
		yb = view->y + (float)points[index + 1U].y * view->scale;
		vertex = frame_vertex(vertex, x0, y0, 0.0f, 0.0f, colors);
		vertex = frame_vertex(vertex, xa, ya, 0.0f, 0.0f, colors);
		vertex = frame_vertex(vertex, xb, yb, 0.0f, 0.0f, colors);

		/* The bounding box grows to each corner. */
		if (xa < box[0])
			box[0] = xa;
		if (xa > box[2])
			box[2] = xa;
		if (ya < box[1])
			box[1] = ya;
		if (ya > box[3])
			box[3] = ya;
	}

	/* The last corner, which the loop only reached as a triangle's far end. */
	xb = view->x + (float)points[count - 1U].x * view->scale;
	yb = view->y + (float)points[count - 1U].y * view->scale;
	if (xb < box[0])
		box[0] = xb;
	if (xb > box[2])
		box[2] = xb;
	if (yb < box[1])
		box[1] = yb;
	if (yb > box[3])
		box[3] = yb;
	frame_draw(frame, NOTES_PIPE_STENCIL, first);

	/* The fringe: a band across every edge, its distance -1 on one side and +1 on the other. */
	first = frame->vertex_count;
	vertex = frame_reserve(frame, count * 6U);
	if (vertex == NULL)
		return;
	for (index = 0; index < count; index++) {
		/* The edge from this corner to the next (the last closes the polygon). */
		next = index + 1U;
		if (next == count)
			next = 0;
		xa = view->x + (float)points[index].x * view->scale;
		ya = view->y + (float)points[index].y * view->scale;
		xb = view->x + (float)points[next].x * view->scale;
		yb = view->y + (float)points[next].y * view->scale;

		/* The edge's unit normal, scaled to the fringe; a degenerate edge gives no band. */
		nx = ya - yb;
		ny = xb - xa;
		length = (float)sqrt((double)(nx * nx + ny * ny));
		if (length < 1e-4f) {
			nx = 0.0f;
			ny = 0.0f;
		} else {
			nx = nx / length * GEOMETRY_FRINGE;
			ny = ny / length * GEOMETRY_FRINGE;
		}

		/* The band's two triangles. */
		vertex = frame_vertex(vertex, xa + nx, ya + ny, 1.0f, 0.0f, colors);
		vertex = frame_vertex(vertex, xa - nx, ya - ny, -1.0f, 0.0f, colors);
		vertex = frame_vertex(vertex, xb + nx, yb + ny, 1.0f, 0.0f, colors);
		vertex = frame_vertex(vertex, xa - nx, ya - ny, -1.0f, 0.0f, colors);
		vertex = frame_vertex(vertex, xb - nx, yb - ny, -1.0f, 0.0f, colors);
		vertex = frame_vertex(vertex, xb + nx, yb + ny, 1.0f, 0.0f, colors);
	}

	/* One draw of the whole fringe. */
	frame_draw(frame, NOTES_PIPE_FRINGE, first);

	/* The cover: the box, drawn inside and clearing the stencil. */
	first = frame->vertex_count;
	vertex = frame_reserve(frame, 6U);
	if (vertex == NULL)
		return;
	box[0] -= 1.0f;
	box[1] -= 1.0f;
	box[2] += 1.0f;
	box[3] += 1.0f;
	vertex = frame_vertex(vertex, box[0], box[1], 0.0f, 0.0f, colors);
	vertex = frame_vertex(vertex, box[2], box[1], 0.0f, 0.0f, colors);
	vertex = frame_vertex(vertex, box[0], box[3], 0.0f, 0.0f, colors);
	vertex = frame_vertex(vertex, box[2], box[1], 0.0f, 0.0f, colors);
	vertex = frame_vertex(vertex, box[2], box[3], 0.0f, 0.0f, colors);
	(void)frame_vertex(vertex, box[0], box[3], 0.0f, 0.0f, colors);
	frame_draw(frame, NOTES_PIPE_COVER, first);
}

/*
 * Adds the toolbar's picture over a rectangle in pixels.
 */
void
notes_frame_texture(
	struct notes_frame *frame,
	float x,
	float y,
	float width,
	float height)
{
	float white[4];
	float *vertex;
	size_t first;

	/* Room for two triangles. */
	first = frame->vertex_count;
	vertex = frame_reserve(frame, 6U);
	if (vertex == NULL)
		return;

	/* The two triangles, with the picture's corners, untinted. */
	white[0] = 1.0f;
	white[1] = 1.0f;
	white[2] = 1.0f;
	white[3] = 1.0f;
	vertex = frame_vertex(vertex, x, y, 0.0f, 0.0f, white);
	vertex = frame_vertex(vertex, x + width, y, 1.0f, 0.0f, white);
	vertex = frame_vertex(vertex, x, y + height, 0.0f, 1.0f, white);
	vertex = frame_vertex(vertex, x + width, y, 1.0f, 0.0f, white);
	vertex = frame_vertex(vertex, x + width, y + height, 1.0f, 1.0f, white);
	(void)frame_vertex(vertex, x, y + height, 0.0f, 1.0f, white);

	/* One textured draw. */
	frame_draw(frame, NOTES_PIPE_TEXTURE, first);
}

/*
 * Places a page in a window below the toolbar: as large as fits with a
 * margin, centred.
 */
void
notes_view_layout(
	struct notes_view *view,
	uint32_t width,
	uint32_t height,
	float page_width,
	float page_height)
{
	float room_width;
	float room_height;
	float scale;

	/* The room below the toolbar, inside the margin. */
	room_width = (float)width - 2.0f * NOTES_PAGE_MARGIN;
	room_height = (float)height - (float)NOTES_TOOLBAR_HEIGHT - 2.0f * NOTES_PAGE_MARGIN;
	if (room_width < 16.0f)
		room_width = 16.0f;
	if (room_height < 16.0f)
		room_height = 16.0f;

	/* The larger scale at which both sides fit. */
	scale = room_width / page_width;
	if (room_height / page_height < scale)
		scale = room_height / page_height;

	/* Centred in the room, on whole pixels. */
	view->scale = scale;
	view->x = (float)floor(((double)width - (double)(page_width * scale)) / 2.0);
	view->y = (float)floor((double)NOTES_TOOLBAR_HEIGHT + ((double)height - (double)NOTES_TOOLBAR_HEIGHT - (double)(page_height * scale)) / 2.0);
}

/* Makes room for vertices and returns where they go, or NULL once the frame has failed. */
static float *
frame_reserve(
	struct notes_frame *frame,
	size_t count)
{
	float *larger;
	size_t capacity;
	float *vertex;

	/* A failed frame takes nothing more. */
	if (frame->error != 0)
		return NULL;

	/* Grows the array, doubling from a small start, when the vertices do not fit. */
	if (frame->vertex_count + count > frame->vertex_capacity) {
		capacity = frame->vertex_capacity;
		if (capacity < GEOMETRY_VERTICES_MIN)
			capacity = GEOMETRY_VERTICES_MIN;
		while (capacity < frame->vertex_count + count)
			capacity *= 2U;
		larger = realloc(frame->vertices, capacity * NOTES_VERTEX_FLOATS * sizeof(float));
		if (larger == NULL) {
			frame->error = 1;
			return NULL;
		}

		/* The larger array. */
		frame->vertices = larger;
		frame->vertex_capacity = capacity;
	}

	/* Succeeded: the vertices go after the ones already there. */
	vertex = frame->vertices + frame->vertex_count * NOTES_VERTEX_FLOATS;
	frame->vertex_count += count;
	return vertex;
}

/* Adds a draw of the vertices from first to the end, with the current clipping. */
static void
frame_draw(
	struct notes_frame *frame,
	unsigned pipe,
	size_t first)
{
	struct notes_draw *larger;
	size_t capacity;

	/* Grows the array when it is full. */
	if (frame->draw_count == frame->draw_capacity) {
		capacity = frame->draw_capacity * 2U;
		if (capacity < GEOMETRY_DRAWS_MIN)
			capacity = GEOMETRY_DRAWS_MIN;
		larger = realloc(frame->draws, capacity * sizeof(*larger));
		if (larger == NULL) {
			frame->error = 1;
			return;
		}

		/* The larger array. */
		frame->draws = larger;
		frame->draw_capacity = capacity;
	}

	/* Succeeded: the draw is the frame's last. */
	frame->draws[frame->draw_count].pipe = pipe;
	frame->draws[frame->draw_count].first = (uint32_t)first;
	frame->draws[frame->draw_count].count = (uint32_t)(frame->vertex_count - first);
	frame->draws[frame->draw_count].clipped = frame->clipped;
	memcpy(frame->draws[frame->draw_count].clip, frame->clip, sizeof(frame->clip));
	frame->draw_count++;
}

/* Writes one vertex and returns where the next goes. */
static float *
frame_vertex(
	float *vertex,
	float x,
	float y,
	float u,
	float v,
	const float *color)
{
	/* The position, the extra pair, the colour. */
	vertex[0] = x;
	vertex[1] = y;
	vertex[2] = u;
	vertex[3] = v;
	vertex[4] = color[0];
	vertex[5] = color[1];
	vertex[6] = color[2];
	vertex[7] = color[3];

	/* Reports the next vertex's place. */
	return vertex + NOTES_VERTEX_FLOATS;
}

/* Converts a colour 0xRRGGBBAA to four floats from 0 to 1. */
static void
color_floats(
	uint32_t color,
	float *floats)
{
	/* Each channel. */
	floats[0] = (float)((color >> 24) & 0xffU) / 255.0f;
	floats[1] = (float)((color >> 16) & 0xffU) / 255.0f;
	floats[2] = (float)((color >> 8) & 0xffU) / 255.0f;
	floats[3] = (float)(color & 0xffU) / 255.0f;
}
