/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * zedBSD's OpenGL ES 2.0 over EGL and Vulkan (WS068): a context's state,
 * its fixed-function settings, and the queries.
 *
 * A context's libGLESv2 state is made at its first GLES call and handed
 * to libEGL's context with two callbacks: one when eglSwapBuffers has
 * finished a frame (the frame's stream, descriptors and garbage are free
 * again), one when the context is destroyed.  Buffers (buffer.c),
 * textures (texture.c), shaders and programs (program.c, spirv.c) and
 * the draws (draw.c) translate into Vulkan; see gles.h.
 */

#include "gles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The extensions this library offers (glGetString's list; gles_extension_names has the same names one by one). */
#define GLES_EXTENSIONS \
	"GL_OES_element_index_uint GL_OES_texture_npot GL_EXT_texture_format_BGRA8888 GL_EXT_blend_minmax GL_EXT_color_buffer_float"

/* The largest uniform block a program may read, in bytes (OpenGL ES 3's minimum). */
#define GLES_UNIFORM_BLOCK_SIZE	16384

/*
 * The extensions this library offers, one name each, for glGetStringi.
 * They are the names of GLES_EXTENSIONS in the same order, and never
 * change.
 */
static const char *const gles_extension_names[] = {
	"GL_OES_element_index_uint",
	"GL_OES_texture_npot",
	"GL_EXT_texture_format_BGRA8888",
	"GL_EXT_blend_minmax",
	"GL_EXT_color_buffer_float"
};

/* The fixed-function layer, NULL without one (libGL sets it before its first context). */
const struct gles_fixed_hooks *gles_fixed;

/* The failures already reported (by the text naming them), so each is said once. */
static const char *gles_reported[16];

static void gles_frame_done(struct zegl_context *context);
static void gles_frame_closing(struct zegl_context *context);
static void gles_release(struct zegl_context *context);
static int gles_capability(struct gles_state *state, GLenum cap, int **flag);
static unsigned gles_integers(struct zegl_context *context, struct gles_state *state, GLenum pname, GLint *values);
static unsigned gles_integers_es3(struct gles_state *state, GLenum pname, GLint *values);
static GLint gles_buffer_name(const struct gles_buffer *buffer);
static GLenum gles_draw_buffer(struct gles_state *state, GLenum index);
static GLenum gles_read_buffer(struct gles_state *state);
static GLenum gles_read_pair(struct gles_state *state, GLenum pname);
static unsigned gles_floats(struct zegl_context *context, struct gles_state *state, GLenum pname, GLfloat *values);

/*
 * Returns the calling thread's current context, or NULL.
 */
struct zegl_context *
gles_context(void)
{
	struct zegl_context *context;

	/* libEGL keeps it per thread. */
	context = zegl_current_context();
	return context;
}

/*
 * Returns a context's libGLESv2 state, making it at the first call;
 * NULL without a context, or when there is no memory.
 */
struct gles_state *
gles_state(
	struct zegl_context *context)
{
	struct gles_state *state;
	VkPhysicalDeviceProperties properties;
	unsigned index;

	/* A context, and its state when made already. */
	if (context == NULL)
		return NULL;
	if (context->gles.state != NULL)
		return context->gles.state;

	/* The state, with the device's memory types and limits. */
	state = calloc(1U, sizeof(*state));
	if (state == NULL) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return NULL;
	}

	/* The display and its device. */
	state->context = context;
	state->display = context->display;
	state->device = context->display->device;
	vkGetPhysicalDeviceMemoryProperties(context->display->physical, &state->memory);
	vkGetPhysicalDeviceProperties(context->display->physical, &properties);
	state->limits = properties.limits;

	/* GL's initial state: blending off with ONE and ZERO, every channel written. */
	state->blend_src_rgb = GL_ONE;
	state->blend_dst_rgb = GL_ZERO;
	state->blend_src_alpha = GL_ONE;
	state->blend_dst_alpha = GL_ZERO;
	state->blend_equation_rgb = GL_FUNC_ADD;
	state->blend_equation_alpha = GL_FUNC_ADD;
	for (index = 0U; index < 4U; index++)
		state->color_mask[index] = GL_TRUE;

	/* The depth test off, LESS, writing, clearing to 1 over [0, 1]. */
	state->depth_func = GL_LESS;
	state->depth_mask = GL_TRUE;
	state->clear_depth = 1.0f;
	state->depth_near = 0.0f;
	state->depth_far = 1.0f;

	/* The stencil test off, ALWAYS with every bit, KEEP. */
	for (index = 0U; index < 2U; index++) {
		state->stencil_func[index] = GL_ALWAYS;
		state->stencil_value_mask[index] = 0xffffffffU;
		state->stencil_write_mask[index] = 0xffffffffU;
		state->stencil_fail[index] = GL_KEEP;
		state->stencil_zfail[index] = GL_KEEP;
		state->stencil_zpass[index] = GL_KEEP;
	}

	/* Culling off (back faces, counter-clockwise front), the scissor box the viewport. */
	state->cull_mode = GL_BACK;
	state->front_face = GL_CCW;
	memcpy(state->scissor, context->gles.viewport, sizeof(state->scissor));

	/* Lines one wide, dithering on, rows aligned to 4. */
	state->line_width = 1.0f;
	state->dither = 1;
	state->sample_coverage_value = 1.0f;
	state->unpack_alignment = 4;
	state->pack_alignment = 4;
	state->mipmap_hint = GL_DONT_CARE;

	/* The surfaces' framebuffer draws into and reads its one colour buffer. */
	state->default_draw_buffer = GL_BACK;
	state->default_read_buffer = GL_BACK;

	/* Every array four floats and disabled, every current value the floats (0, 0, 0, 1). */
	for (index = 0U; index < GLES_ATTRIBS; index++) {
		state->attribs[index].size = 4;
		state->attribs[index].type = GL_FLOAT;
		state->attribs[index].value[3] = 1.0f;
		state->attribs[index].value_type = GL_FLOAT;
	}

	/* Succeeded: the first frame, and the callbacks libEGL makes. */
	state->frame = 1U;
	context->gles.state = state;
	context->gles.frame_done = gles_frame_done;
	context->gles.release = gles_release;
	context->gles.frame_closing = gles_frame_closing;
	return state;
}

/*
 * Records an error, keeping the first one until glGetError reads it.
 */
void
gles_error(
	struct zegl_context *context,
	GLenum error)
{
	/* Only the first counts. */
	if (context->gles.error == GL_NO_ERROR)
		context->gles.error = error;
}

/*
 * Says on stderr, once for each kind, that a step of the translation
 * failed (a Vulkan call's result, or -1): what an application sees is only
 * GL_OUT_OF_MEMORY, which does not say which step.
 */
void
gles_report(
	const char *what,
	int code)
{
	unsigned index;

	/* Each kind once. */
	for (index = 0U; index < sizeof(gles_reported) / sizeof(gles_reported[0]); index++) {
		if (gles_reported[index] == what)
			return;
		if (gles_reported[index] == NULL)
			break;
	}

	/* Remembered while there is room. */
	if (index < sizeof(gles_reported) / sizeof(gles_reported[0]))
		gles_reported[index] = what;

	/* The line. */
	fprintf(stderr, "GLES: %s failed (%d)\n", what, code);
}

/*
 * Puts an object under a name, growing the table.  Returns 0, or -1 when
 * there is no memory.
 */
int
gles_names_add(
	struct gles_names *names,
	GLuint name,
	void *object)
{
	void **grown;
	GLuint capacity;

	/* The table grows by doubling until the name fits. */
	if (name >= names->capacity) {
		capacity = names->capacity;
		if (capacity == 0U)
			capacity = 64U;
		while (capacity <= name)
			capacity *= 2U;
		grown = realloc(names->objects, capacity * sizeof(*grown));
		if (grown == NULL)
			return -1;
		memset(grown + names->capacity, 0, (capacity - names->capacity) * sizeof(*grown));
		names->objects = grown;
		names->capacity = capacity;
	}

	/* Succeeded: the object under its name. */
	names->objects[name] = object;
	return 0;
}

/*
 * Returns the lowest name no object has.
 */
GLuint
gles_names_free(
	struct gles_names *names)
{
	GLuint name;

	/* The first empty slot after 0, else the first past the table. */
	for (name = 1U; name < names->capacity; name++) {
		if (names->objects[name] == NULL)
			return name;
	}

	/* Past the table (at least 1). */
	if (names->capacity == 0U)
		return 1U;
	return names->capacity;
}

/*
 * Returns the object under a name, or NULL.
 */
void *
gles_names_get(
	struct gles_names *names,
	GLuint name)
{
	/* Names outside the table have no object. */
	if (name == 0U || name >= names->capacity)
		return NULL;
	return names->objects[name];
}

/*
 * Takes the object away from a name.
 */
void
gles_names_remove(
	struct gles_names *names,
	GLuint name)
{
	/* Only a name inside the table has one. */
	if (name != 0U && name < names->capacity)
		names->objects[name] = NULL;
}

/*
 * Sets the colour glClear clears the colour buffer to (each channel clamped to [0, 1]).
 */
GL_APICALL void GL_APIENTRY
glClearColor(
	GLfloat red,
	GLfloat green,
	GLfloat blue,
	GLfloat alpha)
{
	struct zegl_context *context;
	GLfloat channels[4];
	unsigned index;

	/* Without a current context the call does nothing. */
	context = gles_context();
	if (context == NULL)
		return;

	/* Each channel, clamped. */
	channels[0] = red;
	channels[1] = green;
	channels[2] = blue;
	channels[3] = alpha;
	for (index = 0U; index < 4U; index++) {
		if (channels[index] < 0.0f)
			channels[index] = 0.0f;
		if (channels[index] > 1.0f)
			channels[index] = 1.0f;
		context->gles.clear_color[index] = channels[index];
	}
}

/*
 * Sets the depth glClear clears the depth buffer to.
 */
GL_APICALL void GL_APIENTRY
glClearDepthf(
	GLfloat d)
{
	struct gles_state *state;

	/* Clamped to [0, 1]. */
	state = gles_state(gles_context());
	if (state == NULL)
		return;
	if (d < 0.0f)
		d = 0.0f;
	if (d > 1.0f)
		d = 1.0f;
	state->clear_depth = d;
}

/*
 * Sets the value glClear clears the stencil buffer to.
 */
GL_APICALL void GL_APIENTRY
glClearStencil(
	GLint s)
{
	struct gles_state *state;

	/* The value. */
	state = gles_state(gles_context());
	if (state == NULL)
		return;
	state->clear_stencil = s;
}

/*
 * Sets the viewport.
 */
GL_APICALL void GL_APIENTRY
glViewport(
	GLint x,
	GLint y,
	GLsizei width,
	GLsizei height)
{
	struct zegl_context *context;

	/* Without a current context the call does nothing. */
	context = gles_context();
	if (context == NULL)
		return;

	/* A negative size is an error. */
	if (width < 0 || height < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The viewport, for the draws to come. */
	context->gles.viewport[0] = x;
	context->gles.viewport[1] = y;
	context->gles.viewport[2] = width;
	context->gles.viewport[3] = height;
	context->gles.viewport_set = 1;
}

/*
 * Sets the depth range the viewport maps to.
 */
GL_APICALL void GL_APIENTRY
glDepthRangef(
	GLfloat n,
	GLfloat f)
{
	struct gles_state *state;

	/* Each end clamped to [0, 1]. */
	state = gles_state(gles_context());
	if (state == NULL)
		return;
	if (n < 0.0f)
		n = 0.0f;
	if (n > 1.0f)
		n = 1.0f;
	if (f < 0.0f)
		f = 0.0f;
	if (f > 1.0f)
		f = 1.0f;
	state->depth_near = n;
	state->depth_far = f;
}

/*
 * Turns a capability on.
 */
GL_APICALL void GL_APIENTRY
glEnable(
	GLenum cap)
{
	struct zegl_context *context;
	struct gles_state *state;
	int *flag;
	int status;

	/* The capability's flag. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	status = gles_capability(state, cap, &flag);
	if (status != 0) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* On. */
	*flag = 1;
}

/*
 * Turns a capability off.
 */
GL_APICALL void GL_APIENTRY
glDisable(
	GLenum cap)
{
	struct zegl_context *context;
	struct gles_state *state;
	int *flag;
	int status;

	/* The capability's flag. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	status = gles_capability(state, cap, &flag);
	if (status != 0) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* Off. */
	*flag = 0;
}

/*
 * Reports whether a capability is on.
 */
GL_APICALL GLboolean GL_APIENTRY
glIsEnabled(
	GLenum cap)
{
	struct zegl_context *context;
	struct gles_state *state;
	int *flag;
	int status;

	/* The capability's flag. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return GL_FALSE;
	status = gles_capability(state, cap, &flag);
	if (status != 0) {
		gles_error(context, GL_INVALID_ENUM);
		return GL_FALSE;
	}

	/* Its value. */
	if (*flag)
		return GL_TRUE;
	return GL_FALSE;
}

/*
 * Sets the blend factors of colour and alpha together.
 */
GL_APICALL void GL_APIENTRY
glBlendFunc(
	GLenum sfactor,
	GLenum dfactor)
{
	/* The same for both. */
	glBlendFuncSeparate(sfactor, dfactor, sfactor, dfactor);
}

/*
 * Sets the blend factors of colour and alpha.
 */
GL_APICALL void GL_APIENTRY
glBlendFuncSeparate(
	GLenum sfactorRGB,
	GLenum dfactorRGB,
	GLenum sfactorAlpha,
	GLenum dfactorAlpha)
{
	struct gles_state *state;

	/* The four factors. */
	state = gles_state(gles_context());
	if (state == NULL)
		return;
	state->blend_src_rgb = sfactorRGB;
	state->blend_dst_rgb = dfactorRGB;
	state->blend_src_alpha = sfactorAlpha;
	state->blend_dst_alpha = dfactorAlpha;
}

/*
 * Sets the blend equation of colour and alpha together.
 */
GL_APICALL void GL_APIENTRY
glBlendEquation(
	GLenum mode)
{
	/* The same for both. */
	glBlendEquationSeparate(mode, mode);
}

/*
 * Sets the blend equations of colour and alpha.
 */
GL_APICALL void GL_APIENTRY
glBlendEquationSeparate(
	GLenum modeRGB,
	GLenum modeAlpha)
{
	struct gles_state *state;

	/* The two equations. */
	state = gles_state(gles_context());
	if (state == NULL)
		return;
	state->blend_equation_rgb = modeRGB;
	state->blend_equation_alpha = modeAlpha;
}

/*
 * Sets the constant blend colour.
 */
GL_APICALL void GL_APIENTRY
glBlendColor(
	GLfloat red,
	GLfloat green,
	GLfloat blue,
	GLfloat alpha)
{
	struct gles_state *state;

	/* The colour. */
	state = gles_state(gles_context());
	if (state == NULL)
		return;
	state->blend_color[0] = red;
	state->blend_color[1] = green;
	state->blend_color[2] = blue;
	state->blend_color[3] = alpha;
}

/*
 * Sets which colour channels draws write.
 */
GL_APICALL void GL_APIENTRY
glColorMask(
	GLboolean red,
	GLboolean green,
	GLboolean blue,
	GLboolean alpha)
{
	struct gles_state *state;

	/* The four channels. */
	state = gles_state(gles_context());
	if (state == NULL)
		return;
	state->color_mask[0] = red;
	state->color_mask[1] = green;
	state->color_mask[2] = blue;
	state->color_mask[3] = alpha;
}

/*
 * Sets the depth test's comparison.
 */
GL_APICALL void GL_APIENTRY
glDepthFunc(
	GLenum func)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* One of the eight comparisons. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (func < GL_NEVER || func > GL_ALWAYS) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* The comparison. */
	state->depth_func = func;
}

/*
 * Sets whether draws write the depth buffer.
 */
GL_APICALL void GL_APIENTRY
glDepthMask(
	GLboolean flag)
{
	struct gles_state *state;

	/* The flag. */
	state = gles_state(gles_context());
	if (state == NULL)
		return;
	state->depth_mask = flag;
}

/*
 * Sets the stencil test of both faces.
 */
GL_APICALL void GL_APIENTRY
glStencilFunc(
	GLenum func,
	GLint ref,
	GLuint mask)
{
	/* Front and back. */
	glStencilFuncSeparate(GL_FRONT_AND_BACK, func, ref, mask);
}

/*
 * Sets the stencil test of one face or both.
 */
GL_APICALL void GL_APIENTRY
glStencilFuncSeparate(
	GLenum face,
	GLenum func,
	GLint ref,
	GLuint mask)
{
	struct zegl_context *context;
	struct gles_state *state;
	unsigned index;

	/* One of the eight comparisons. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (func < GL_NEVER || func > GL_ALWAYS) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* Each face named: 0 front, 1 back. */
	for (index = 0U; index < 2U; index++) {
		if (face == GL_FRONT && index == 1U)
			continue;
		if (face == GL_BACK && index == 0U)
			continue;
		state->stencil_func[index] = func;
		state->stencil_ref[index] = ref;
		state->stencil_value_mask[index] = mask;
	}
}

/*
 * Sets the stencil operations of both faces.
 */
GL_APICALL void GL_APIENTRY
glStencilOp(
	GLenum fail,
	GLenum zfail,
	GLenum zpass)
{
	/* Front and back. */
	glStencilOpSeparate(GL_FRONT_AND_BACK, fail, zfail, zpass);
}

/*
 * Sets the stencil operations of one face or both.
 */
GL_APICALL void GL_APIENTRY
glStencilOpSeparate(
	GLenum face,
	GLenum sfail,
	GLenum dpfail,
	GLenum dppass)
{
	struct gles_state *state;
	unsigned index;

	/* Each face named: 0 front, 1 back. */
	state = gles_state(gles_context());
	if (state == NULL)
		return;
	for (index = 0U; index < 2U; index++) {
		if (face == GL_FRONT && index == 1U)
			continue;
		if (face == GL_BACK && index == 0U)
			continue;
		state->stencil_fail[index] = sfail;
		state->stencil_zfail[index] = dpfail;
		state->stencil_zpass[index] = dppass;
	}
}

/*
 * Sets the stencil bits draws write, both faces.
 */
GL_APICALL void GL_APIENTRY
glStencilMask(
	GLuint mask)
{
	/* Front and back. */
	glStencilMaskSeparate(GL_FRONT_AND_BACK, mask);
}

/*
 * Sets the stencil bits draws write, one face or both.
 */
GL_APICALL void GL_APIENTRY
glStencilMaskSeparate(
	GLenum face,
	GLuint mask)
{
	struct gles_state *state;
	unsigned index;

	/* Each face named: 0 front, 1 back. */
	state = gles_state(gles_context());
	if (state == NULL)
		return;
	for (index = 0U; index < 2U; index++) {
		if (face == GL_FRONT && index == 1U)
			continue;
		if (face == GL_BACK && index == 0U)
			continue;
		state->stencil_write_mask[index] = mask;
	}
}

/*
 * Sets which faces culling removes.
 */
GL_APICALL void GL_APIENTRY
glCullFace(
	GLenum mode)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* Front, back or both. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* The mode. */
	state->cull_mode = mode;
}

/*
 * Sets which winding is a front face.
 */
GL_APICALL void GL_APIENTRY
glFrontFace(
	GLenum mode)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* Clockwise or counter-clockwise. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (mode != GL_CW && mode != GL_CCW) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* The winding. */
	state->front_face = mode;
}

/*
 * Sets the scissor box.
 */
GL_APICALL void GL_APIENTRY
glScissor(
	GLint x,
	GLint y,
	GLsizei width,
	GLsizei height)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* A size that is not negative. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (width < 0 || height < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The box. */
	state->scissor[0] = x;
	state->scissor[1] = y;
	state->scissor[2] = width;
	state->scissor[3] = height;
}

/*
 * Sets the width of lines (drawn one pixel wide whatever it is).
 */
GL_APICALL void GL_APIENTRY
glLineWidth(
	GLfloat width)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* A positive width. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (width <= 0.0f) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Kept for glGet. */
	state->line_width = width;
}

/*
 * Sets the polygon offset.
 */
GL_APICALL void GL_APIENTRY
glPolygonOffset(
	GLfloat factor,
	GLfloat units)
{
	struct gles_state *state;

	/* The factor and the units. */
	state = gles_state(gles_context());
	if (state == NULL)
		return;
	state->polygon_factor = factor;
	state->polygon_units = units;
}

/*
 * Sets the sample coverage (kept; surfaces have one sample).
 */
GL_APICALL void GL_APIENTRY
glSampleCoverage(
	GLfloat value,
	GLboolean invert)
{
	struct gles_state *state;

	/* Kept for glGet. */
	state = gles_state(gles_context());
	if (state == NULL)
		return;
	state->sample_coverage_value = value;
	state->sample_coverage_invert = invert;
}

/*
 * Sets a hint: only the mipmap hint, which glGenerateMipmap does not need.
 */
GL_APICALL void GL_APIENTRY
glHint(
	GLenum target,
	GLenum mode)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* The one target. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (target != GL_GENERATE_MIPMAP_HINT) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* Kept for glGet. */
	state->mipmap_hint = mode;
}

/*
 * Sets the row alignment of the pixels glTexImage2D reads or glReadPixels
 * writes.
 */
GL_APICALL void GL_APIENTRY
glPixelStorei(
	GLenum pname,
	GLint param)
{
	struct zegl_context *context;
	struct gles_state *state;
	GLint *value;
	int alignment;

	/* A context with its state. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;

	/* The parameter's place: the alignments, and OpenGL ES 3's lengths and skips. */
	alignment = 0;
	switch (pname) {
	case GL_UNPACK_ALIGNMENT:
		value = &state->unpack_alignment;
		alignment = 1;
		break;
	case GL_PACK_ALIGNMENT:
		value = &state->pack_alignment;
		alignment = 1;
		break;
	case GL_UNPACK_ROW_LENGTH:
		value = &state->unpack_row_length;
		break;
	case GL_UNPACK_SKIP_ROWS:
		value = &state->unpack_skip_rows;
		break;
	case GL_UNPACK_SKIP_PIXELS:
		value = &state->unpack_skip_pixels;
		break;
	case GL_UNPACK_IMAGE_HEIGHT:
		value = &state->unpack_image_height;
		break;
	case GL_UNPACK_SKIP_IMAGES:
		value = &state->unpack_skip_images;
		break;
	case GL_PACK_ROW_LENGTH:
		value = &state->pack_row_length;
		break;
	case GL_PACK_SKIP_ROWS:
		value = &state->pack_skip_rows;
		break;
	case GL_PACK_SKIP_PIXELS:
		value = &state->pack_skip_pixels;
		break;
	default:
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* An alignment of 1, 2, 4 or 8. */
	if (alignment && param != 1 && param != 2 && param != 4 && param != 8) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A length or a skip that is not negative. */
	if (param < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Succeeded: the parameter. */
	*value = param;
}

/*
 * Reports integer states.
 */
GL_APICALL void GL_APIENTRY
glGetIntegerv(
	GLenum pname,
	GLint *data)
{
	struct zegl_context *context;
	struct gles_state *state;
	GLfloat floats[16];
	unsigned count;
	unsigned index;

	/* A context with its state. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL || data == NULL)
		return;

	/* An integer state. */
	count = gles_integers(context, state, pname, data);
	if (count != 0U)
		return;

	/* A float state (the fixed-function layer's too), rounded. */
	count = gles_floats(context, state, pname, floats);
	for (index = 0U; index < count; index++)
		data[index] = (GLint)(floats[index] + 0.5f);
	if (count == 0U)
		gles_error(context, GL_INVALID_ENUM);
}

/*
 * Reports integer states as 64-bit integers.
 */
GL_APICALL void GL_APIENTRY
glGetInteger64v(
	GLenum pname,
	GLint64 *data)
{
	struct zegl_context *context;
	struct gles_state *state;
	GLint integers[16];
	GLfloat floats[16];
	unsigned count;
	unsigned index;

	/* A context with its state. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL || data == NULL)
		return;

	/* The states only 64 bits hold: the largest index and wait. */
	if (pname == GL_MAX_ELEMENT_INDEX) {
		data[0] = (GLint64)state->limits.maxDrawIndexedIndexValue;
		return;
	}

	/* A server wait (glClientWaitSync) is never refused for its length. */
	if (pname == GL_MAX_SERVER_WAIT_TIMEOUT) {
		data[0] = INT64_MAX;
		return;
	}

	/* An integer state. */
	count = gles_integers(context, state, pname, integers);
	for (index = 0U; index < count; index++)
		data[index] = integers[index];
	if (count != 0U)
		return;

	/* A float state, rounded. */
	count = gles_floats(context, state, pname, floats);
	for (index = 0U; index < count; index++)
		data[index] = (GLint64)(floats[index] + 0.5f);
	if (count == 0U)
		gles_error(context, GL_INVALID_ENUM);
}

/*
 * Reports float states.
 */
GL_APICALL void GL_APIENTRY
glGetFloatv(
	GLenum pname,
	GLfloat *data)
{
	struct zegl_context *context;
	struct gles_state *state;
	GLint integers[16];
	unsigned count;
	unsigned index;

	/* A context with its state. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL || data == NULL)
		return;

	/* A float state. */
	count = gles_floats(context, state, pname, data);
	if (count != 0U)
		return;

	/* An integer state, converted. */
	count = gles_integers(context, state, pname, integers);
	for (index = 0U; index < count; index++)
		data[index] = (GLfloat)integers[index];
	if (count == 0U)
		gles_error(context, GL_INVALID_ENUM);
}

/*
 * Reports states as booleans.
 */
GL_APICALL void GL_APIENTRY
glGetBooleanv(
	GLenum pname,
	GLboolean *data)
{
	struct zegl_context *context;
	struct gles_state *state;
	GLfloat floats[16];
	GLint integers[16];
	unsigned count;
	unsigned index;

	/* A context with its state. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL || data == NULL)
		return;

	/* The colour mask and the depth mask are booleans already. */
	if (pname == GL_COLOR_WRITEMASK) {
		memcpy(data, state->color_mask, 4U);
		return;
	}

	/* The depth mask is one. */
	if (pname == GL_DEPTH_WRITEMASK) {
		data[0] = state->depth_mask;
		return;
	}

	/* An integer state, else a float state: nonzero is true. */
	count = gles_integers(context, state, pname, integers);
	for (index = 0U; index < count; index++)
		data[index] = (GLboolean)(integers[index] != 0);
	if (count != 0U)
		return;
	count = gles_floats(context, state, pname, floats);
	for (index = 0U; index < count; index++)
		data[index] = (GLboolean)(floats[index] != 0.0f);
	if (count == 0U)
		gles_error(context, GL_INVALID_ENUM);
}

/*
 * Returns one of GLES's strings.
 */
GL_APICALL const GLubyte *GL_APIENTRY
glGetString(
	GLenum name)
{
	struct zegl_context *context;
	const GLubyte *layer;

	/* Without a current context there are no strings. */
	context = gles_context();
	if (context == NULL)
		return NULL;

	/* The fixed-function layer's own (desktop GL's version). */
	if (gles_fixed != NULL) {
		layer = gles_fixed->string(name);
		if (layer != NULL)
			return layer;
	}

	/* The string asked for. */
	switch (name) {
	case GL_VENDOR:
		return (const GLubyte *)"zedBSD";
	case GL_RENDERER:
		return (const GLubyte *)"zedBSD OpenGL ES on Vulkan";
	case GL_VERSION:
		return (const GLubyte *)"OpenGL ES 2.0 zedBSD";
	case GL_SHADING_LANGUAGE_VERSION:
		return (const GLubyte *)"OpenGL ES GLSL ES 1.00";
	case GL_EXTENSIONS:
		return (const GLubyte *)GLES_EXTENSIONS;
	default:
		break;
	}

	/* Any other name is an error. */
	gles_error(context, GL_INVALID_ENUM);
	return NULL;
}

/*
 * Returns one of the extensions' names (the only indexed string).
 */
GL_APICALL const GLubyte *GL_APIENTRY
glGetStringi(
	GLenum name,
	GLuint index)
{
	struct zegl_context *context;

	/* Without a current context there are no strings. */
	context = gles_context();
	if (context == NULL)
		return NULL;

	/* The extensions are the only list. */
	if (name != GL_EXTENSIONS) {
		gles_error(context, GL_INVALID_ENUM);
		return NULL;
	}

	/* An index inside it. */
	if (index >= sizeof(gles_extension_names) / sizeof(gles_extension_names[0])) {
		gles_error(context, GL_INVALID_VALUE);
		return NULL;
	}

	/* Succeeded: the name. */
	return (const GLubyte *)gles_extension_names[index];
}

/*
 * Returns the first error since the last call, and clears it.
 */
GL_APICALL GLenum GL_APIENTRY
glGetError(void)
{
	struct zegl_context *context;
	GLenum error;

	/* Without a current context there is no error. */
	context = gles_context();
	if (context == NULL)
		return GL_NO_ERROR;

	/* Reading it clears it. */
	error = (GLenum)context->gles.error;
	context->gles.error = GL_NO_ERROR;
	return error;
}

/*
 * Sends the recorded commands on: eglSwapBuffers sends each frame, so there is nothing to send.
 */
GL_APICALL void GL_APIENTRY
glFlush(void)
{
	/* Nothing is waiting. */
	return;
}

/*
 * Waits for the recorded commands: nothing becomes visible before
 * eglSwapBuffers or glReadPixels, which wait themselves.
 */
GL_APICALL void GL_APIENTRY
glFinish(void)
{
	/* Nothing is running. */
	return;
}

/* Frees what the frame that eglSwapBuffers just finished held, and counts the next frame. */
static void
gles_frame_done(
	struct zegl_context *context)
{
	struct gles_state *state;

	/* The state, when there is one. */
	state = context->gles.state;
	if (state == NULL)
		return;

	/* The next frame; the finished one's memory is free. */
	state->frame++;
	gles_collect(state);
}

/* Ends a framebuffer object's render pass left open in the frame, before libEGL submits the frame or the context stops being current. */
static void
gles_frame_closing(
	struct zegl_context *context)
{
	struct gles_state *state;

	/* The state, when there is one. */
	state = context->gles.state;
	if (state == NULL)
		return;

	/* The pass, if one is open. */
	gles_target_close(state);
}

/* Frees a context's libGLESv2 state and every object and Vulkan object in it. */
static void
gles_release(
	struct zegl_context *context)
{
	struct gles_state *state;
	struct gles_pipeline *pipeline;
	struct gles_sampler *sampler;
	struct gles_pool *pool;
	struct gles_chunk *chunk;
	struct gles_program *program;
	struct gles_shader *shader;
	GLuint name;
	unsigned kind;
	unsigned shape;

	/* The state, when there is one; nothing may still run. */
	state = context->gles.state;
	if (state == NULL)
		return;
	(void)vkDeviceWaitIdle(state->device);

	/* The fixed-function layer's state (its programs are among the objects freed below). */
	if (gles_fixed != NULL)
		gles_fixed->release(state);

	/* The programs, then the shaders left. */
	state->program = NULL;
	for (name = 1U; name < state->objects.capacity; name++) {
		program = state->objects.objects[name];
		if (program != NULL && program->kind == GLES_KIND_PROGRAM) {
			state->objects.objects[name] = NULL;
			gles_program_release(state, program);
		}
	}

	/* The shaders no program took with it. */
	for (name = 1U; name < state->objects.capacity; name++) {
		shader = state->objects.objects[name];
		if (shader != NULL)
			gles_shader_release(shader);
	}

	/* The buffers and the textures. */
	for (name = 1U; name < state->buffers.capacity; name++) {
		if (state->buffers.objects[name] != NULL)
			gles_buffer_free(state, state->buffers.objects[name]);
	}

	/* The textures. */
	for (name = 1U; name < state->textures.capacity; name++) {
		if (state->textures.objects[name] != NULL)
			gles_texture_free(state, state->textures.objects[name]);
	}

	/* The black textures of each shape and kind. */
	for (shape = 0U; shape < GLES_SHAPES; shape++) {
		for (kind = 0U; kind < GLES_BLACK_KINDS; kind++) {
			if (state->blacks[shape][kind] != NULL)
				gles_texture_free(state, state->blacks[shape][kind]);
		}
	}

	/* The sampler objects. */
	gles_samplers_release(state);

	/* The framebuffer objects and renderbuffers, and the vertex array objects. */
	gles_framebuffers_release(state);
	gles_vertex_arrays_release(state);

	/* The garbage, which the frees above added to. */
	gles_collect(state);

	/* The pipelines, samplers, descriptor pools and stream. */
	while (state->pipelines != NULL) {
		pipeline = state->pipelines;
		state->pipelines = pipeline->next;
		vkDestroyPipeline(state->device, pipeline->pipeline, NULL);
		free(pipeline);
	}

	/* The samplers. */
	while (state->samplers != NULL) {
		sampler = state->samplers;
		state->samplers = sampler->next;
		vkDestroySampler(state->device, sampler->sampler, NULL);
		free(sampler);
	}
	while (state->pools != NULL) {
		pool = state->pools;
		state->pools = pool->next;
		vkDestroyDescriptorPool(state->device, pool->pool, NULL);
		free(pool);
	}
	while (state->chunks != NULL) {
		chunk = state->chunks;
		state->chunks = chunk->next;
		vkDestroyBuffer(state->device, chunk->buffer, NULL);
		vkFreeMemory(state->device, chunk->memory, NULL);
		free(chunk);
	}

	/* The upload's objects. */
	if (state->upload_fence != VK_NULL_HANDLE)
		vkDestroyFence(state->device, state->upload_fence, NULL);
	if (state->upload_pool != VK_NULL_HANDLE)
		vkDestroyCommandPool(state->device, state->upload_pool, NULL);

	/* The tables and the state. */
	free(state->objects.objects);
	free(state->buffers.objects);
	free(state->textures.objects);
	free(state);
	context->gles.state = NULL;
}

/* Returns the flag of a capability glEnable takes; nonzero for one that is not. */
static int
gles_capability(
	struct gles_state *state,
	GLenum cap,
	int **flag)
{
	int status;

	/* The capabilities of OpenGL ES 2. */
	switch (cap) {
	case GL_BLEND:
		*flag = &state->blend;
		return 0;
	case GL_CULL_FACE:
		*flag = &state->cull;
		return 0;
	case GL_DEPTH_TEST:
		*flag = &state->depth_test;
		return 0;
	case GL_DITHER:
		*flag = &state->dither;
		return 0;
	case GL_POLYGON_OFFSET_FILL:
		*flag = &state->polygon_offset;
		return 0;
	case GL_SAMPLE_ALPHA_TO_COVERAGE:
		*flag = &state->sample_alpha_to_coverage;
		return 0;
	case GL_SAMPLE_COVERAGE:
		*flag = &state->sample_coverage;
		return 0;
	case GL_SCISSOR_TEST:
		*flag = &state->scissor_test;
		return 0;
	case GL_STENCIL_TEST:
		*flag = &state->stencil_test;
		return 0;
	case GL_PRIMITIVE_RESTART_FIXED_INDEX:
		*flag = &state->primitive_restart;
		return 0;
	default:
		break;
	}

	/* One of the fixed-function layer's, when there is the layer. */
	if (gles_fixed == NULL)
		return -1;
	status = gles_fixed->capability(state->context, cap, flag);
	return status;
}

/* Writes an integer state's values; returns how many, 0 when the name is not an integer state. */
static unsigned
gles_integers(
	struct zegl_context *context,
	struct gles_state *state,
	GLenum pname,
	GLint *values)
{
	struct zegl_config *config;
	struct gles_texture *texture;
	uint32_t samples;
	int *flag;
	unsigned count;
	int status;

	/* A capability is an integer 0 or 1. */
	status = gles_capability(state, pname, &flag);
	if (status == 0) {
		values[0] = *flag;
		return 1U;
	}

	/* The rest, one by one. */
	config = context->config;
	switch (pname) {
	case GL_VIEWPORT:
		memcpy(values, context->gles.viewport, 4U * sizeof(GLint));
		return 4U;
	case GL_SCISSOR_BOX:
		memcpy(values, state->scissor, 4U * sizeof(GLint));
		return 4U;
	case GL_MAX_VIEWPORT_DIMS:
		values[0] = (GLint)state->limits.maxViewportDimensions[0];
		values[1] = (GLint)state->limits.maxViewportDimensions[1];
		return 2U;
	case GL_MAX_TEXTURE_SIZE:
	case GL_MAX_RENDERBUFFER_SIZE:
	case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
		values[0] = (GLint)state->limits.maxImageDimension2D;
		if (values[0] > 16384)
			values[0] = 16384;
		return 1U;
	case GL_MAX_VERTEX_ATTRIBS:
		values[0] = (GLint)GLES_ATTRIBS;
		return 1U;
	case GL_MAX_TEXTURE_IMAGE_UNITS:
	case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
	case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
		values[0] = (GLint)GLES_UNITS;
		return 1U;
	case GL_MAX_VERTEX_UNIFORM_VECTORS:
	case GL_MAX_FRAGMENT_UNIFORM_VECTORS:
		values[0] = 256;
		return 1U;
	case GL_MAX_VARYING_VECTORS:
		values[0] = 15;
		return 1U;
	case GL_ARRAY_BUFFER_BINDING:
		values[0] = 0;
		if (state->array_buffer != NULL)
			values[0] = (GLint)state->array_buffer->name;
		return 1U;
	case GL_ELEMENT_ARRAY_BUFFER_BINDING:
		values[0] = 0;
		if (state->element_buffer != NULL)
			values[0] = (GLint)state->element_buffer->name;
		return 1U;
	case GL_CURRENT_PROGRAM:
		values[0] = 0;
		if (state->program != NULL)
			values[0] = (GLint)state->program->name;
		return 1U;
	case GL_ACTIVE_TEXTURE:
		values[0] = (GLint)(GL_TEXTURE0 + state->active_unit);
		return 1U;
	case GL_TEXTURE_BINDING_2D:
		texture = state->units[state->active_unit];
		values[0] = 0;
		if (texture != NULL)
			values[0] = (GLint)texture->name;
		return 1U;
	case GL_TEXTURE_BINDING_CUBE_MAP:
		texture = state->cube_units[state->active_unit];
		values[0] = 0;
		if (texture != NULL)
			values[0] = (GLint)texture->name;
		return 1U;
	case GL_TEXTURE_BINDING_3D:
		texture = state->volume_units[state->active_unit];
		values[0] = 0;
		if (texture != NULL)
			values[0] = (GLint)texture->name;
		return 1U;
	case GL_TEXTURE_BINDING_2D_ARRAY:
		texture = state->array_units[state->active_unit];
		values[0] = 0;
		if (texture != NULL)
			values[0] = (GLint)texture->name;
		return 1U;
	case GL_MAX_3D_TEXTURE_SIZE:
		values[0] = (GLint)state->limits.maxImageDimension3D;
		if (values[0] > GLES_MAX_3D_SIZE)
			values[0] = GLES_MAX_3D_SIZE;
		return 1U;
	case GL_MAX_ARRAY_TEXTURE_LAYERS:
		values[0] = (GLint)state->limits.maxImageArrayLayers;
		if (values[0] > GLES_MAX_LAYERS)
			values[0] = GLES_MAX_LAYERS;
		return 1U;
	case GL_FRAMEBUFFER_BINDING:
		values[0] = (GLint)state->framebuffer;
		return 1U;
	case GL_READ_FRAMEBUFFER_BINDING:
		values[0] = (GLint)state->read_framebuffer;
		return 1U;
	case GL_MAX_COLOR_ATTACHMENTS:
		values[0] = (GLint)GLES_COLOR_ATTACHMENTS;
		return 1U;
	case GL_MAX_DRAW_BUFFERS:
		values[0] = (GLint)GLES_DRAW_BUFFERS;
		return 1U;
	case GL_DRAW_BUFFER0:
	case GL_DRAW_BUFFER1:
	case GL_DRAW_BUFFER2:
	case GL_DRAW_BUFFER3:
		values[0] = (GLint)gles_draw_buffer(state, pname - GL_DRAW_BUFFER0);
		return 1U;
	case GL_READ_BUFFER:
		values[0] = (GLint)gles_read_buffer(state);
		return 1U;
	case GL_RENDERBUFFER_BINDING:
		values[0] = (GLint)state->renderbuffer;
		return 1U;
	case GL_RED_BITS:
	case GL_GREEN_BITS:
	case GL_BLUE_BITS:
		values[0] = 8;
		return 1U;
	case GL_ALPHA_BITS:
		values[0] = 0;
		if (config != NULL)
			values[0] = config->alpha;
		return 1U;
	case GL_DEPTH_BITS:
		values[0] = 0;
		if (config != NULL)
			values[0] = config->depth;
		return 1U;
	case GL_STENCIL_BITS:
		values[0] = 0;
		if (config != NULL)
			values[0] = config->stencil;
		return 1U;
	case GL_SUBPIXEL_BITS:
		values[0] = 4;
		return 1U;
	case GL_SAMPLES:
		values[0] = (GLint)gles_framebuffer_samples(state, state->framebuffer);
		return 1U;
	case GL_SAMPLE_BUFFERS:
		samples = gles_framebuffer_samples(state, state->framebuffer);
		values[0] = 0;
		if (samples > 1U)
			values[0] = 1;
		return 1U;
	case GL_MAX_SAMPLES:
		values[0] = (GLint)gles_samples_max(state);
		return 1U;
	case GL_NUM_COMPRESSED_TEXTURE_FORMATS:
	case GL_COMPRESSED_TEXTURE_FORMATS:
		values[0] = 0;
		return 1U;
	case GL_UNPACK_ALIGNMENT:
		values[0] = state->unpack_alignment;
		return 1U;
	case GL_PACK_ALIGNMENT:
		values[0] = state->pack_alignment;
		return 1U;
	case GL_UNPACK_ROW_LENGTH:
		values[0] = state->unpack_row_length;
		return 1U;
	case GL_UNPACK_SKIP_ROWS:
		values[0] = state->unpack_skip_rows;
		return 1U;
	case GL_UNPACK_SKIP_PIXELS:
		values[0] = state->unpack_skip_pixels;
		return 1U;
	case GL_UNPACK_IMAGE_HEIGHT:
		values[0] = state->unpack_image_height;
		return 1U;
	case GL_UNPACK_SKIP_IMAGES:
		values[0] = state->unpack_skip_images;
		return 1U;
	case GL_PACK_ROW_LENGTH:
		values[0] = state->pack_row_length;
		return 1U;
	case GL_PACK_SKIP_ROWS:
		values[0] = state->pack_skip_rows;
		return 1U;
	case GL_PACK_SKIP_PIXELS:
		values[0] = state->pack_skip_pixels;
		return 1U;
	case GL_NUM_SHADER_BINARY_FORMATS:
		values[0] = 1;
		return 1U;
	case GL_SHADER_BINARY_FORMATS:
		values[0] = GL_SHADER_BINARY_FORMAT_SPIR_V;
		return 1U;
	case GL_SHADER_COMPILER:
		values[0] = GL_TRUE;
		return 1U;
	case GL_IMPLEMENTATION_COLOR_READ_FORMAT:
	case GL_IMPLEMENTATION_COLOR_READ_TYPE:
		values[0] = (GLint)gles_read_pair(state, pname);
		return 1U;
	case GL_BLEND_SRC_RGB:
		values[0] = (GLint)state->blend_src_rgb;
		return 1U;
	case GL_BLEND_DST_RGB:
		values[0] = (GLint)state->blend_dst_rgb;
		return 1U;
	case GL_BLEND_SRC_ALPHA:
		values[0] = (GLint)state->blend_src_alpha;
		return 1U;
	case GL_BLEND_DST_ALPHA:
		values[0] = (GLint)state->blend_dst_alpha;
		return 1U;
	case GL_BLEND_EQUATION_RGB:
		values[0] = (GLint)state->blend_equation_rgb;
		return 1U;
	case GL_BLEND_EQUATION_ALPHA:
		values[0] = (GLint)state->blend_equation_alpha;
		return 1U;
	case GL_DEPTH_FUNC:
		values[0] = (GLint)state->depth_func;
		return 1U;
	case GL_STENCIL_FUNC:
		values[0] = (GLint)state->stencil_func[0];
		return 1U;
	case GL_STENCIL_REF:
		values[0] = state->stencil_ref[0];
		return 1U;
	case GL_STENCIL_VALUE_MASK:
		values[0] = (GLint)state->stencil_value_mask[0];
		return 1U;
	case GL_STENCIL_WRITEMASK:
		values[0] = (GLint)state->stencil_write_mask[0];
		return 1U;
	case GL_STENCIL_FAIL:
		values[0] = (GLint)state->stencil_fail[0];
		return 1U;
	case GL_STENCIL_PASS_DEPTH_FAIL:
		values[0] = (GLint)state->stencil_zfail[0];
		return 1U;
	case GL_STENCIL_PASS_DEPTH_PASS:
		values[0] = (GLint)state->stencil_zpass[0];
		return 1U;
	case GL_STENCIL_BACK_FUNC:
		values[0] = (GLint)state->stencil_func[1];
		return 1U;
	case GL_STENCIL_BACK_REF:
		values[0] = state->stencil_ref[1];
		return 1U;
	case GL_STENCIL_BACK_VALUE_MASK:
		values[0] = (GLint)state->stencil_value_mask[1];
		return 1U;
	case GL_STENCIL_BACK_WRITEMASK:
		values[0] = (GLint)state->stencil_write_mask[1];
		return 1U;
	case GL_STENCIL_BACK_FAIL:
		values[0] = (GLint)state->stencil_fail[1];
		return 1U;
	case GL_STENCIL_BACK_PASS_DEPTH_FAIL:
		values[0] = (GLint)state->stencil_zfail[1];
		return 1U;
	case GL_STENCIL_BACK_PASS_DEPTH_PASS:
		values[0] = (GLint)state->stencil_zpass[1];
		return 1U;
	case GL_STENCIL_CLEAR_VALUE:
		values[0] = state->clear_stencil;
		return 1U;
	case GL_CULL_FACE_MODE:
		values[0] = (GLint)state->cull_mode;
		return 1U;
	case GL_FRONT_FACE:
		values[0] = (GLint)state->front_face;
		return 1U;
	case GL_GENERATE_MIPMAP_HINT:
		values[0] = (GLint)state->mipmap_hint;
		return 1U;
	default:
		break;
	}

	/* One of OpenGL ES 3's, or not an integer state. */
	count = gles_integers_es3(state, pname, values);
	return count;
}

/* Writes one of OpenGL ES 3's integer states (buffers, vertex arrays, uniform blocks, limits); returns how many, 0 when the name is not one. */
static unsigned
gles_integers_es3(
	struct gles_state *state,
	GLenum pname,
	GLint *values)
{
	/* The states, one by one. */
	switch (pname) {
	case GL_VERTEX_ARRAY_BINDING:
		values[0] = (GLint)state->vertex_array;
		return 1U;
	case GL_SAMPLER_BINDING:
		values[0] = 0;
		if (state->unit_samplers[state->active_unit] != NULL)
			values[0] = (GLint)state->unit_samplers[state->active_unit]->name;
		return 1U;
	case GL_COPY_READ_BUFFER_BINDING:
		values[0] = gles_buffer_name(state->copy_read_buffer);
		return 1U;
	case GL_COPY_WRITE_BUFFER_BINDING:
		values[0] = gles_buffer_name(state->copy_write_buffer);
		return 1U;
	case GL_UNIFORM_BUFFER_BINDING:
		values[0] = gles_buffer_name(state->uniform_buffer);
		return 1U;
	case GL_PIXEL_PACK_BUFFER_BINDING:
		values[0] = gles_buffer_name(state->pixel_pack_buffer);
		return 1U;
	case GL_PIXEL_UNPACK_BUFFER_BINDING:
		values[0] = gles_buffer_name(state->pixel_unpack_buffer);
		return 1U;
	case GL_TRANSFORM_FEEDBACK_BUFFER_BINDING:
		values[0] = gles_buffer_name(state->feedback_buffer);
		return 1U;
	case GL_MAX_UNIFORM_BUFFER_BINDINGS:
		values[0] = (GLint)GLES_UNIFORM_BINDINGS;
		return 1U;
	case GL_MAX_UNIFORM_BLOCK_SIZE:
		values[0] = GLES_UNIFORM_BLOCK_SIZE;
		if ((GLint64)state->limits.maxUniformBufferRange > GLES_UNIFORM_BLOCK_SIZE)
			values[0] = (GLint)state->limits.maxUniformBufferRange;
		return 1U;
	case GL_MAX_VERTEX_UNIFORM_BLOCKS:
	case GL_MAX_FRAGMENT_UNIFORM_BLOCKS:
		values[0] = (GLint)GLES_STAGE_BLOCKS;
		return 1U;
	case GL_MAX_COMBINED_UNIFORM_BLOCKS:
		values[0] = (GLint)GLES_NAMED_BLOCKS;
		return 1U;
	case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT:
		values[0] = (GLint)state->limits.minUniformBufferOffsetAlignment;
		return 1U;
	case GL_MAX_VERTEX_UNIFORM_COMPONENTS:
	case GL_MAX_FRAGMENT_UNIFORM_COMPONENTS:
		values[0] = 1024;
		return 1U;
	case GL_MAX_VERTEX_OUTPUT_COMPONENTS:
	case GL_MAX_FRAGMENT_INPUT_COMPONENTS:
		values[0] = 64;
		return 1U;
	case GL_MAX_VARYING_COMPONENTS:
		values[0] = 60;
		return 1U;
	case GL_MAX_ELEMENTS_VERTICES:
	case GL_MAX_ELEMENTS_INDICES:
		values[0] = 1048576;
		return 1U;
	case GL_MAX_ELEMENT_INDEX:
		values[0] = INT32_MAX;
		if (state->limits.maxDrawIndexedIndexValue < (uint32_t)INT32_MAX)
			values[0] = (GLint)state->limits.maxDrawIndexedIndexValue;
		return 1U;
	case GL_NUM_EXTENSIONS:
		values[0] = (GLint)(sizeof(gles_extension_names) / sizeof(gles_extension_names[0]));
		return 1U;
	default:
		break;
	}

	/* Not an integer state. */
	return 0U;
}

/* Returns what draw buffer i of the draw framebuffer writes (GL_BACK, GL_COLOR_ATTACHMENTi or GL_NONE). */
static GLenum
gles_draw_buffer(
	struct gles_state *state,
	GLenum index)
{
	struct gles_framebuffer *fbo;

	/* The surfaces' one buffer. */
	if (state->framebuffer == 0U) {
		if (index == 0U)
			return state->default_draw_buffer;
		return GL_NONE;
	}

	/* A framebuffer object's buffer. */
	fbo = gles_names_get(&state->framebuffers, state->framebuffer);
	if (fbo == NULL || index >= GLES_DRAW_BUFFERS)
		return GL_NONE;

	/* Succeeded: the attachment it writes. */
	return fbo->draw_buffers[index];
}

/* Returns what the read framebuffer's reads read (GL_BACK, GL_COLOR_ATTACHMENTi or GL_NONE). */
static GLenum
gles_read_buffer(
	struct gles_state *state)
{
	struct gles_framebuffer *fbo;

	/* The surfaces' buffer. */
	if (state->read_framebuffer == 0U)
		return state->default_read_buffer;

	/* A framebuffer object's. */
	fbo = gles_names_get(&state->framebuffers, state->read_framebuffer);
	if (fbo == NULL)
		return GL_NONE;

	/* Succeeded: the attachment it reads. */
	return fbo->read_buffer;
}

/* Returns the format (GL_IMPLEMENTATION_COLOR_READ_FORMAT) or the type glReadPixels reads the read buffer as besides RGBA bytes. */
static GLenum
gles_read_pair(
	struct gles_state *state,
	GLenum pname)
{
	const struct gles_format *format;
	GLenum read_format;
	GLenum read_type;

	/* The read buffer's format's pair (RGBA bytes without one). */
	format = gles_read_buffer_format(state);
	read_format = GL_RGBA;
	read_type = GL_UNSIGNED_BYTE;
	if (format != NULL)
		gles_read_format(format, &read_format, &read_type);

	/* The format asked for. */
	if (pname == GL_IMPLEMENTATION_COLOR_READ_FORMAT)
		return read_format;

	/* Succeeded: the type. */
	return read_type;
}

/* Returns a buffer's name, 0 for none. */
static GLint
gles_buffer_name(
	const struct gles_buffer *buffer)
{
	/* No buffer. */
	if (buffer == NULL)
		return 0;

	/* The buffer's name. */
	return (GLint)buffer->name;
}

/* Writes a float state's values; returns how many, 0 when the name is not a float state. */
static unsigned
gles_floats(
	struct zegl_context *context,
	struct gles_state *state,
	GLenum pname,
	GLfloat *values)
{
	unsigned count;

	/* The float states, one by one. */
	switch (pname) {
	case GL_COLOR_CLEAR_VALUE:
		memcpy(values, context->gles.clear_color, 4U * sizeof(GLfloat));
		return 4U;
	case GL_BLEND_COLOR:
		memcpy(values, state->blend_color, 4U * sizeof(GLfloat));
		return 4U;
	case GL_DEPTH_CLEAR_VALUE:
		values[0] = state->clear_depth;
		return 1U;
	case GL_DEPTH_RANGE:
		values[0] = state->depth_near;
		values[1] = state->depth_far;
		return 2U;
	case GL_LINE_WIDTH:
		values[0] = state->line_width;
		return 1U;
	case GL_POLYGON_OFFSET_FACTOR:
		values[0] = state->polygon_factor;
		return 1U;
	case GL_POLYGON_OFFSET_UNITS:
		values[0] = state->polygon_units;
		return 1U;
	case GL_SAMPLE_COVERAGE_VALUE:
		values[0] = state->sample_coverage_value;
		return 1U;
	case GL_SAMPLE_COVERAGE_INVERT:
		values[0] = (GLfloat)state->sample_coverage_invert;
		return 1U;
	case GL_ALIASED_LINE_WIDTH_RANGE:
		values[0] = 1.0f;
		values[1] = 1.0f;
		return 2U;
	case GL_ALIASED_POINT_SIZE_RANGE:
		values[0] = 1.0f;
		values[1] = state->limits.pointSizeRange[1];
		return 2U;
	default:
		break;
	}

	/* The fixed-function layer's states (matrices, lights, ...), when there is the layer. */
	if (gles_fixed == NULL)
		return 0U;
	count = gles_fixed->get(context, pname, values);
	return count;
}
