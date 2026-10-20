/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A check, in a test kernel, of the core's render path: a session of the
 * driver's own clears a 64x64 linear colour attachment to one colour and
 * draws one triangle of another over it, through its render context and
 * a render target, then reads the attachment back.
 *
 * Every step says one line, so a check that stops shows where: the
 * preparation, the kick, the wait for the fragment's fence with the
 * timelines it read, and the read back with the first pixel that
 * differed.  The TA and 3D stage lines come from the render's end.
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/lock.h>
#include <uapi/errno.h>

#include "drivers/gpu/powervr/render-private.h"
#include "drivers/gpu/powervr/interface.h"
#include "drivers/gpu/powervr/image-state.h"
#include "drivers/gpu/powervr/pds-encode.h"
#include "drivers/gpu/powervr/render-command.h"
#include "drivers/gpu/powervr/render-kick-layout.h"
#include "drivers/gpu/powervr/render-program.h"

/*
 * The frame: 64x64 R8G8B8A8 (VK_FORMAT_R8G8B8A8_UNORM 37), linear, rows
 * of 256 bytes; red to clear to and green for the triangle, as the
 * attachment holds them (red in the first byte).
 */
#define CHECK_EDGE		64U
#define CHECK_FORMAT		37U
#define CHECK_ROW_BYTES		(CHECK_EDGE * 4U)
#define CHECK_CLEAR		0xff0000ffU
#define CHECK_TRIANGLE		0xff00ff00U

/* How long the render may take. */
#define CHECK_WAIT_MS		2000U

/*
 * The general buffer: the vertex control stream, the primitive state
 * block, the three vertices, then the attachment.
 */
#define GENERAL_STREAM		0x0U
#define GENERAL_PPP		0x100U
#define GENERAL_VERTICES	0x200U
#define GENERAL_ATTACHMENT	0x1000U
#define GENERAL_BYTES		(GENERAL_ATTACHMENT + CHECK_EDGE * CHECK_ROW_BYTES)

/*
 * The data sequencer buffer, each program on 16 bytes: the end-of-tile
 * data; the clear's task program (data, then its code after eight
 * words), its shared register program's code and data; the triangle
 * fragment's task program and a shared register program that only ends;
 * the vertex program's data and code.
 */
#define PDS_EVENT		0x000U
#define PDS_CLEAR_KICK		0x040U
#define PDS_CLEAR_SHARED_CODE	0x080U
#define PDS_CLEAR_SHARED_DATA	0x100U
#define PDS_TRIANGLE_KICK	0x200U
#define PDS_NOTHING_CODE	0x240U
#define PDS_NOTHING_DATA	0x250U
#define PDS_VERTEX_DATA		0x300U
#define PDS_VERTEX_CODE		0x400U
#define PDS_BYTES		0x1000U
#define PDS_SHARED_CODE_ROOM	(PDS_CLEAR_SHARED_DATA - PDS_CLEAR_SHARED_CODE)
#define PDS_SHARED_DATA_ROOM	(PDS_TRIANGLE_KICK - PDS_CLEAR_SHARED_DATA)
#define PDS_VERTEX_DATA_ROOM	(PDS_VERTEX_CODE - PDS_VERTEX_DATA)
#define PDS_VERTEX_CODE_ROOM	(PDS_BYTES - PDS_VERTEX_CODE)

/* The shading cluster buffer: the end-of-tile, clear, colour and vertex programs. */
#define USC_EOT			0x000U
#define USC_CLEAR		0x100U
#define USC_COLOUR		0x200U
#define USC_VERTEX		0x300U
#define USC_BYTES		0x1000U

_Static_assert(POWERVR_RENDER_USC_MAX <= 0x100U, "shading cluster program room");

/* What the check holds while it runs. */
struct render_check {
	struct powervr_render *render;
	struct powervr_session *session;
	struct powervr_render_context *context;
	struct powervr_target *target;
	struct powervr_resource *general;
	struct powervr_buffer *pds;
	struct powervr_buffer *usc;
	uint64_t pds_place;
	uint64_t usc_place;
	struct powervr_fragment_setup fragment_setup;
	struct powervr_geometry_setup geometry_setup;
	struct powervr_render_usc program;
	struct powervr_render_kick kick;
	struct powervr_data_shared_program shared;
	struct powervr_data_vertex_program vertex;
	uint8_t geometry[POWERVR_GEOMETRY_BYTES];
	uint8_t fragment[POWERVR_FRAGMENT_BYTES];
	struct powervr_render_marks marks;
};

/* The check's state; the node's start runs it once, alone. */
static struct render_check check_state;

static int check_prepare(struct render_check *check, const char **step);
static int check_programs(struct render_check *check, const char **step);
static int check_triangle(struct render_check *check, uint32_t pds_base, uint32_t usc_base);
static int check_kick(struct render_check *check);
static int check_read(struct render_check *check);
static void check_release(struct render_check *check);
static int pixel_class(uint32_t x, uint32_t y);
static void put_words(struct powervr_buffer *buffer, uint32_t offset, const uint32_t *words, unsigned count);
static void put_bytes(struct powervr_buffer *buffer, uint32_t offset, const uint8_t *bytes, size_t count);
static uint32_t get_word(const struct powervr_buffer *buffer, uint32_t offset);

/*
 * Runs the driver's own render on the core and reports how it went.
 */
void
powervr_render_check(
	struct powervr_render *render)
{
	struct render_check *check;
	const char *step;
	int error;

	/* The session, its render context and target, the memory and the programs. */
	check = &check_state;
	kern_memset(check, 0, sizeof(*check));
	check->render = render;
	step = "open";
	error = check_prepare(check, &step);
	if (error == 0)
		error = check_programs(check, &step);
	if (error != 0) {
		kern_logf("powervr: render check: FAILED at %s (%d)\n", step, error);
		check_release(check);
		return;
	}
	kern_logf("powervr: render check: prepared (64x64 R8G8B8A8, clear and one triangle)\n");

	/* The kick and the wait for the fragment, then the read back; each says its own line. */
	error = check_kick(check);
	if (error == 0)
		(void)check_read(check);

	/* What the core may still run is kept with the session; otherwise everything goes. */
	check_release(check);
}

/* Opens the check's session, makes its render context and target, and maps its memory. */
static int
check_prepare(
	struct render_check *check,
	const char **step)
{
	struct powervr_target_shape shape;
	struct powervr_buffer *buffer;
	void *opaque;
	int error;

	/* A session of the check's own. */
	*step = "open";
	error = check->render->ops.open(check->render, &opaque);
	if (error != 0)
		return error;
	check->session = opaque;

	/* Its render context. */
	*step = "render context";
	error = powervr_session_render_context(check->render, check->session, &check->context);
	if (error != 0)
		return error;

	/* A render target of the frame, one sample, one layer. */
	*step = "target";
	shape.width = CHECK_EDGE;
	shape.height = CHECK_EDGE;
	shape.samples = 1;
	shape.layers = 1;
	error = powervr_session_target(check->render, check->session, &shape, &check->target);
	if (error != 0)
		return error;

	/* The general memory, mapped in the session's general window. */
	*step = "memory";
	error = powervr_buffer_create(check->render, GENERAL_BYTES, &buffer);
	if (error != 0)
		return error;

	error = powervr_resource_make(check->session, buffer, 1, 0, &check->general);
	powervr_buffer_put(check->render, buffer);
	if (error != 0) {
		check->general = NULL;
		return error;
	}

	/* The program buffers, mapped in their windows. */
	error = powervr_buffer_create(check->render, PDS_BYTES, &check->pds);
	if (error != 0) {
		check->pds = NULL;
		return error;
	}

	error = powervr_session_program_map(check->session, POWERVR_PROGRAM_PDS, check->pds, &check->pds_place);
	if (error != 0) {
		check->pds_place = 0;
		return error;
	}

	error = powervr_buffer_create(check->render, USC_BYTES, &check->usc);
	if (error != 0) {
		check->usc = NULL;
		return error;
	}

	error = powervr_session_program_map(check->session, POWERVR_PROGRAM_USC, check->usc, &check->usc_place);
	if (error != 0) {
		check->usc_place = 0;
		return error;
	}

	/* Succeeded: the check can write its programs. */
	return 0;
}

/*
 * Writes the render's programs and streams and fills its commands: the
 * end-of-tile program that stores to the attachment, the clear
 * background, the triangle, and the geometry and fragment commands.
 */
static int
check_programs(
	struct render_check *check,
	const char **step)
{
	struct powervr_render_surface surface;
	struct powervr_render_area area;
	struct powervr_render_background background;
	const struct powervr_target_layout *layout;
	uint32_t state[2];
	uint32_t event[POWERVR_RENDER_EVENT_DATA_WORDS];
	uint32_t pds_base;
	uint32_t usc_base;
	uint32_t code_word;
	uint64_t general;
	int error;

	/* The places as offsets from the windows' bases. */
	pds_base = (uint32_t)(check->pds_place - POWERVR_PDS_HEAP_BASE);
	usc_base = (uint32_t)(check->usc_place - POWERVR_USC_HEAP_BASE);
	general = check->general->address;

	/* The attachment's back end words, and the end-of-tile program that emits them. */
	*step = "end of tile";
	surface.format = powervr_format_find(CHECK_FORMAT);
	surface.address = general + GENERAL_ATTACHMENT;
	surface.width = CHECK_EDGE;
	surface.height = CHECK_EDGE;
	surface.linear = 1;
	surface.row_pitch = CHECK_ROW_BYTES;
	surface.padded_width = CHECK_EDGE;
	area.x = 0;
	area.y = 0;
	area.width = CHECK_EDGE;
	area.height = CHECK_EDGE;
	error = powervr_render_pbe_words(&surface, &area, state, check->fragment_setup.pbe);
	if (error != 0)
		return error;

	error = powervr_render_eot_usc(state, &check->program);
	if (error != 0)
		return error;

	put_bytes(check->usc, USC_EOT, check->program.code, check->program.length);
	error = powervr_render_event_data(usc_base + USC_EOT, check->program.temps, event);
	if (error != 0)
		return error;

	put_words(check->pds, PDS_EVENT, event, POWERVR_RENDER_EVENT_DATA_WORDS);
	error = powervr_render_event_words(pds_base + PDS_EVENT, &code_word, &check->fragment_setup.event_data, &check->fragment_setup.event_info);
	if (error != 0)
		return error;

	/* The clear background: its program, the task program, the shared register program with the colour. */
	*step = "clear";
	error = powervr_render_clear_usc(&check->program);
	if (error != 0)
		return error;

	put_bytes(check->usc, USC_CLEAR, check->program.code, check->program.length);
	error = powervr_render_kick_build(usc_base + USC_CLEAR, check->program.temps, &check->kick);
	if (error != 0)
		return error;

	put_words(check->pds, PDS_CLEAR_KICK, check->kick.data, POWERVR_RENDER_KICK_DATA_WORDS);
	put_words(check->pds, PDS_CLEAR_KICK + POWERVR_RENDER_KICK_DATA_WORDS * 4U, check->kick.code, POWERVR_RENDER_KICK_CODE_WORDS);
	error = powervr_render_clear_shared(CHECK_CLEAR, &check->shared);
	if (error != 0)
		return error;

	if (check->shared.code_words * 4U > PDS_SHARED_CODE_ROOM || check->shared.data_words * 4U > PDS_SHARED_DATA_ROOM)
		return E2BIG;

	put_words(check->pds, PDS_CLEAR_SHARED_CODE, check->shared.code, check->shared.code_words);
	put_words(check->pds, PDS_CLEAR_SHARED_DATA, check->shared.data, check->shared.data_words);
	background.kick_offset = pds_base + PDS_CLEAR_KICK;
	background.shared_code_offset = pds_base + PDS_CLEAR_SHARED_CODE;
	background.shared_data_offset = pds_base + PDS_CLEAR_SHARED_DATA;
	background.shared_data_words = check->shared.data_words;
	background.shared_registers = 1;
	error = powervr_render_background_words(&background, check->fragment_setup.background);
	if (error != 0)
		return error;

	/* After a partial render the same clear runs again; the frame is small enough not to need one. */
	kern_memcpy(check->fragment_setup.partial_background, check->fragment_setup.background, sizeof(check->fragment_setup.background));

	/* The triangle's programs and streams. */
	*step = "triangle";
	error = check_triangle(check, pds_base, usc_base);
	if (error != 0)
		return error;

	/* The commands: the fragment's registers, the geometry's with the target's region header stride. */
	*step = "commands";
	layout = powervr_target_layout_of(check->target);
	check->fragment_setup.tiles_x = layout->tiles_x;
	check->fragment_setup.tiles_y = layout->tiles_y;
	kern_memset(check->fragment, 0, sizeof(check->fragment));
	powervr_fragment_command(&check->fragment_setup, check->fragment);
	check->geometry_setup.stream = general + GENERAL_STREAM;
	check->geometry_setup.region_header_bytes = layout->region_header_bytes;
	check->geometry_setup.shared_registers = 1;
	kern_memset(check->geometry, 0, sizeof(check->geometry));
	powervr_geometry_command(&check->geometry_setup, check->geometry);

	/* Succeeded: the render is ready to kick. */
	return 0;
}

/*
 * Writes the triangle: its fragment program and task program, a shared
 * register program that only ends, its vertex program and data
 * sequencer program, its three vertices (x and y in pixels as 32-bit
 * floats: (8, 8), (56, 8), (32, 56)), its primitive state block and its
 * vertex control stream.
 */
static int
check_triangle(
	struct render_check *check,
	uint32_t pds_base,
	uint32_t usc_base)
{
	static const uint32_t vertices[3U * POWERVR_RENDER_VERTEX_WORDS] = {
		0x41000000U, 0x41000000U, 0,
		0x42600000U, 0x41000000U, 0,
		0x42000000U, 0x42600000U, 0,
	};
	struct powervr_render_triangle triangle;
	uint32_t ppp[POWERVR_RENDER_PPP_WORDS];
	uint32_t vdm[POWERVR_RENDER_VDM_WORDS];
	uint32_t halt;
	uint64_t general;
	int error;

	/* The fragment program and its task program. */
	general = check->general->address;
	error = powervr_render_colour_usc(CHECK_TRIANGLE, &check->program);
	if (error != 0)
		return error;

	put_bytes(check->usc, USC_COLOUR, check->program.code, check->program.length);
	error = powervr_render_kick_build(usc_base + USC_COLOUR, check->program.temps, &check->kick);
	if (error != 0)
		return error;

	put_words(check->pds, PDS_TRIANGLE_KICK, check->kick.data, POWERVR_RENDER_KICK_DATA_WORDS);
	put_words(check->pds, PDS_TRIANGLE_KICK + POWERVR_RENDER_KICK_DATA_WORDS * 4U, check->kick.code, POWERVR_RENDER_KICK_CODE_WORDS);

	/* A shared register program with nothing to load: one halt. */
	error = powervr_pds_halt(0, &halt);
	if (error != 0)
		return error;

	put_words(check->pds, PDS_NOTHING_CODE, &halt, 1);

	/* The vertex program and its data sequencer program over the three vertices. */
	error = powervr_render_vertex_usc(&check->program);
	if (error != 0)
		return error;

	put_bytes(check->usc, USC_VERTEX, check->program.code, check->program.length);
	error = powervr_render_vertex_pds(general + GENERAL_VERTICES, usc_base + USC_VERTEX, check->program.temps, &check->vertex);
	if (error != 0)
		return error;

	if (check->vertex.data_words * 4U > PDS_VERTEX_DATA_ROOM || check->vertex.code_words * 4U > PDS_VERTEX_CODE_ROOM)
		return E2BIG;

	put_words(check->pds, PDS_VERTEX_DATA, check->vertex.data, check->vertex.data_words);
	put_words(check->pds, PDS_VERTEX_CODE, check->vertex.code, check->vertex.code_words);
	put_words(check->general->buffer, GENERAL_VERTICES, vertices, 3U * POWERVR_RENDER_VERTEX_WORDS);

	/* The primitive state block and the stream that names it and draws. */
	triangle.ppp_address = general + GENERAL_PPP;
	triangle.fragment.kick_offset = pds_base + PDS_TRIANGLE_KICK;
	triangle.fragment.shared_code_offset = pds_base + PDS_NOTHING_CODE;
	triangle.fragment.shared_data_offset = pds_base + PDS_NOTHING_DATA;
	triangle.fragment.shared_data_words = 0;
	triangle.fragment.shared_registers = 0;
	triangle.vertex_data_offset = pds_base + PDS_VERTEX_DATA;
	triangle.vertex_code_offset = pds_base + PDS_VERTEX_CODE;
	triangle.vertex_data_words = check->vertex.data_words;
	triangle.vertex_temps = check->vertex.temps;
	error = powervr_render_triangle_ppp(&triangle, ppp);
	if (error != 0)
		return error;

	error = powervr_render_triangle_vdm(&triangle, vdm);
	if (error != 0)
		return error;

	put_words(check->general->buffer, GENERAL_PPP, ppp, POWERVR_RENDER_PPP_WORDS);
	put_words(check->general->buffer, GENERAL_STREAM, vdm, POWERVR_RENDER_VDM_WORDS);

	/* Succeeded: the triangle is written. */
	return 0;
}

/*
 * Kicks the render and waits for its fragment, saying each step: the
 * kick with its timeline values, then the timelines the wait read.
 */
static int
check_kick(
	struct render_check *check)
{
	struct powervr_render_context *context;
	uint32_t geometry_now;
	uint32_t fragment_now;
	int error;

	/* The kicks, under the core's lock; the partial render runs the fragment's own command. */
	context = check->context;
	mutex_lock(&check->render->core->lock);

	error = powervr_session_kick_render(check->render, check->session, check->target, 0, check->geometry, check->fragment, check->fragment, NULL, 0, &check->marks);

	mutex_unlock(&check->render->core->lock);

	if (error != 0) {
		kern_logf("powervr: render check: FAILED at kick (%d)\n", error);
		return error;
	}
	kern_logf("powervr: render check: kicked (geometry to %u, fragment to %u)\n", check->marks.geometry_value, check->marks.fragment_value);

	/* The wait; the render's end says the TA and 3D stages. */
	error = powervr_render_finish(check->render, check->session, &check->marks, CHECK_WAIT_MS);
	geometry_now = powervr_object_get32(&context->geometry.timeline, POWERVR_SYNC_ADDRESS);
	fragment_now = powervr_object_get32(&context->fragment.timeline, POWERVR_SYNC_ADDRESS);
	if (error != 0) {
		kern_logf("powervr: render check: FAILED at fence (%d; geometry %u of %u, fragment %u of %u)\n", error, geometry_now, check->marks.geometry_value, fragment_now, check->marks.fragment_value);
		check->session->abandoned = 1;
		return error;
	}
	kern_logf("powervr: render check: fence reached (geometry %u, fragment %u)\n", geometry_now, fragment_now);

	/* Succeeded: the render finished. */
	return 0;
}

/*
 * Reads the attachment back: pixels well inside the triangle hold its
 * colour, pixels well outside the clear colour; those near an edge are
 * not judged.  Says the first pixel that differed.
 */
static int
check_read(
	struct render_check *check)
{
	struct powervr_buffer *buffer;
	uint32_t want;
	uint32_t got;
	uint32_t x;
	uint32_t y;
	unsigned judged;
	int inside;

	/* Every pixel the triangle's edges leave clear of doubt, row by row. */
	buffer = check->general->buffer;
	judged = 0;
	for (y = 0; y < CHECK_EDGE; y++) {
		for (x = 0; x < CHECK_EDGE; x++) {
			inside = pixel_class(x, y);
			if (inside < 0)
				continue;

			/* The colour the pixel should hold, and the one it holds. */
			want = CHECK_CLEAR;
			if (inside)
				want = CHECK_TRIANGLE;
			got = get_word(buffer, GENERAL_ATTACHMENT + y * CHECK_ROW_BYTES + x * 4U);
			judged++;
			if (got != want) {
				kern_logf("powervr: render check: FAILED at read back: pixel (%u, %u) is 0x%08x, want 0x%08x\n", x, y, got, want);
				return EIO;
			}
		}
	}

	/* Succeeded: every judged pixel holds what it should. */
	kern_logf("powervr: render check: OK (%u pixels judged)\n", judged);
	return 0;
}

/* Unmaps and returns the check's memory and closes its session, unless the core may still run it. */
static void
check_release(
	struct render_check *check)
{
	struct powervr_render *render;

	/* A render that ran out of time may still run: its memory stays held, and the close keeps the session. */
	render = check->render;
	if (check->session != NULL && check->session->abandoned) {
		render->ops.close(render, check->session);
		return;
	}

	/* The program buffers and the general memory. */
	if (check->usc != NULL) {
		if (check->usc_place != 0U)
			powervr_session_program_unmap(check->session, POWERVR_PROGRAM_USC, check->usc, check->usc_place);
		powervr_buffer_put(render, check->usc);
	}

	if (check->pds != NULL) {
		if (check->pds_place != 0U)
			powervr_session_program_unmap(check->session, POWERVR_PROGRAM_PDS, check->pds, check->pds_place);
		powervr_buffer_put(render, check->pds);
	}

	if (check->general != NULL)
		powervr_resource_unmake(check->session, check->general);

	/* The session, whose close returns the context and the target. */
	if (check->session != NULL)
		render->ops.close(render, check->session);
}

/*
 * Says whether a pixel's centre lies inside the triangle (1), outside it
 * (0), or within about a pixel of an edge (-1).  The vertices are (8, 8),
 * (56, 8) and (32, 56); each edge's function is taken at twice the
 * pixel grid and compared against a margin of the edge's length.
 */
static int
pixel_class(
	uint32_t x,
	uint32_t y)
{
	static const int32_t corner[3][2] = { { 8, 8 }, { 56, 8 }, { 32, 56 } };
	int32_t px;
	int32_t py;
	int32_t ex;
	int32_t ey;
	int32_t side;
	int32_t margin;
	unsigned edge;
	int inside;

	/* The centre at twice the grid. */
	px = (int32_t)x * 2 + 1;
	py = (int32_t)y * 2 + 1;
	inside = 1;

	/* Each edge: the signed area against the edge's length, a pixel's margin either way. */
	for (edge = 0; edge < 3U; edge++) {
		ex = corner[(edge + 1U) % 3U][0] - corner[edge][0];
		ey = corner[(edge + 1U) % 3U][1] - corner[edge][1];
		side = ex * (py - corner[edge][1] * 2) - ey * (px - corner[edge][0] * 2);
		margin = 4 * ((ex < 0 ? -ex : ex) + (ey < 0 ? -ey : ey));
		if (side > -margin && side < margin)
			return -1;
		if (side < 0)
			inside = 0;
	}

	/* Succeeded: inside or outside. */
	return inside;
}

/* Writes words through a buffer's uncached view, low byte first. */
static void
put_words(
	struct powervr_buffer *buffer,
	uint32_t offset,
	const uint32_t *words,
	unsigned count)
{
	unsigned index;

	/* One word at a time. */
	for (index = 0; index < count; index++) {
		buffer->view[offset + index * 4U + 0U] = (uint8_t)words[index];
		buffer->view[offset + index * 4U + 1U] = (uint8_t)(words[index] >> 8);
		buffer->view[offset + index * 4U + 2U] = (uint8_t)(words[index] >> 16);
		buffer->view[offset + index * 4U + 3U] = (uint8_t)(words[index] >> 24);
	}
}

/* Writes bytes through a buffer's uncached view. */
static void
put_bytes(
	struct powervr_buffer *buffer,
	uint32_t offset,
	const uint8_t *bytes,
	size_t count)
{
	size_t index;

	/* One byte at a time. */
	for (index = 0; index < count; index++)
		buffer->view[offset + index] = bytes[index];
}

/* Reads a little-endian word through a buffer's uncached view. */
static uint32_t
get_word(
	const struct powervr_buffer *buffer,
	uint32_t offset)
{
	uint32_t word;

	/* Low byte first. */
	word = buffer->view[offset];
	word |= (uint32_t)buffer->view[offset + 1U] << 8;
	word |= (uint32_t)buffer->view[offset + 2U] << 16;
	word |= (uint32_t)buffer->view[offset + 3U] << 24;

	/* Succeeded: the word. */
	return word;
}
