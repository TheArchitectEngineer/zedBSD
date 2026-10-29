/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * glxtest's OpenGL 3.3 scene, in a 3.3 compatibility context made by
 * glXCreateContextAttribsARB: the start checks OpenGL 3.3's calls in a
 * framebuffer object of 8x8 texels, and each frame shows one square per
 * outcome, green when it is the expected one and red otherwise, and one
 * square drawn with the fixed function:
 *
 *   the version (GL_VERSION, GLSL 3.30, the extensions), a GL_TIME_ELAPSED
 *   query around a draw, glQueryCounter and glGetInteger64v(GL_TIMESTAMP)
 *   in order, a dual-source blend whose second colour is layout(index =
 *   1), one whose second colour glBindFragDataLocationIndexed gave, a
 *   current attribute value from glVertexAttribP4ui (unsigned) and from
 *   glVertexAttribP3ui (signed), an RGB10_A2UI texture drawn into, and
 *   (when the context names GL_ARB_tessellation_shader; skipped, green,
 *   otherwise) a quad patch tessellated over the target, and a square
 *   drawn by glDrawArraysIndirect (GL_ARB_draw_indirect), and the debug
 *   output (GL_KHR_debug: a message inserted, an error, a group, a label,
 *   the log).
 *
 * The start's checks each print a line; a check whose value is not the
 * expected one fails the run.
 */

#include "gl33.h"

#include <GL/gl.h>

#include <stdio.h>
#include <string.h>

/* The outcomes shown, the side of the framebuffer object, and the attribute locations of the corners and of a colour. */
#define GL33_OUTCOMES		11U
#define GL33_SIDE		8
#define GL33_POSITION		0U
#define GL33_COLOUR		1U

/* The colours the checks read back, as 0xRRGGBB. */
#define GL33_RED		0xff0000U
#define GL33_GREEN		0x00ff00U

/*
 * The programs (one colour; the two dual-source blends; a colour from an
 * attribute, as it is and turned around) with the uniforms the scene
 * sets, the square's vertex array, the framebuffer object with its
 * texture, and each outcome (nonzero: the expected one); kept for the run
 * with the number of the start's checks that failed.
 */
static GLuint gl33_plain;
static GLint gl33_plain_rect;
static GLint gl33_plain_colour;
static GLuint gl33_dual_layout;
static GLuint gl33_dual_bound;
static GLuint gl33_attrib;
static GLuint gl33_attrib_turned;
static GLuint gl33_square;
static GLuint gl33_framebuffer;
static GLuint gl33_texture;
static int gl33_outcomes[GL33_OUTCOMES];
static int gl33_failures;

/* The vertex shader of the squares: a unit square placed by the rectangle (centre, half size). */
static const char gl33_vertex_source[] =
	"#version 330 core\n"
	"layout(location = 0) in vec2 a_position;\n"
	"uniform vec4 u_rect;\n"
	"\n"
	"void main()\n"
	"{\n"
	"\tgl_Position = vec4(u_rect.xy + a_position * u_rect.zw, 0.0, 1.0);\n"
	"}\n";

/* The fragment shader of one colour. */
static const char gl33_plain_source[] =
	"#version 330 core\n"
	"uniform vec4 u_colour;\n"
	"layout(location = 0) out vec4 o_colour;\n"
	"\n"
	"void main()\n"
	"{\n"
	"\to_colour = u_colour;\n"
	"}\n";

/* A dual-source blend's two colours by layout: green, and a factor of 1 that keeps it all. */
static const char gl33_dual_layout_source[] =
	"#version 330 core\n"
	"layout(location = 0, index = 0) out vec4 o_colour;\n"
	"layout(location = 0, index = 1) out vec4 o_factor;\n"
	"\n"
	"void main()\n"
	"{\n"
	"\to_colour = vec4(0.0, 1.0, 0.0, 1.0);\n"
	"\to_factor = vec4(1.0);\n"
	"}\n";

/* The same two colours without layouts, for glBindFragDataLocationIndexed. */
static const char gl33_dual_bound_source[] =
	"#version 150\n"
	"out vec4 o_colour;\n"
	"out vec4 o_factor;\n"
	"\n"
	"void main()\n"
	"{\n"
	"\to_colour = vec4(0.0, 1.0, 0.0, 1.0);\n"
	"\to_factor = vec4(1.0);\n"
	"}\n";

/* The vertex shaders of a colour from an attribute: as it is, and with x and y swapped. */
static const char gl33_attrib_vertex_source[] =
	"#version 330 core\n"
	"layout(location = 0) in vec2 a_position;\n"
	"layout(location = 1) in vec4 a_colour;\n"
	"out vec4 v_colour;\n"
	"\n"
	"void main()\n"
	"{\n"
	"\tv_colour = a_colour;\n"
	"\tgl_Position = vec4(a_position, 0.0, 1.0);\n"
	"}\n";
static const char gl33_turned_vertex_source[] =
	"#version 330 core\n"
	"layout(location = 0) in vec2 a_position;\n"
	"layout(location = 1) in vec4 a_colour;\n"
	"out vec4 v_colour;\n"
	"\n"
	"void main()\n"
	"{\n"
	"\tv_colour = vec4(a_colour.y, a_colour.x, a_colour.z, a_colour.w);\n"
	"\tgl_Position = vec4(a_position, 0.0, 1.0);\n"
	"}\n";

/* The fragment shader of the attribute's colour. */
static const char gl33_attrib_fragment_source[] =
	"#version 330 core\n"
	"in vec4 v_colour;\n"
	"layout(location = 0) out vec4 o_colour;\n"
	"\n"
	"void main()\n"
	"{\n"
	"\to_colour = v_colour;\n"
	"}\n";

/* The names of the outcomes. */
static const char *const gl33_names[GL33_OUTCOMES] = {
	"version", "time-elapsed", "timestamps", "dual-layout", "dual-bound", "packed-unsigned", "packed-signed", "rgb10-a2ui",
	"tessellation", "draw-indirect", "debug"
};

/* The debug messages the callback heard, by type: inserted markers, errors, pushes and pops. */
static int gl33_heard_markers;
static int gl33_heard_errors;
static int gl33_heard_groups;

/* The tessellation shaders: a patch of the square's four corners, its quad domain over them, green. */
static const char gl33_tess_vertex_source[] =
	"#version 330 core\n"
	"layout(location = 0) in vec2 a_position;\n"
	"\n"
	"void main()\n"
	"{\n"
	"\tgl_Position = vec4(a_position, 0.0, 1.0);\n"
	"}\n";
static const char gl33_tess_control_source[] =
	"#version 330 core\n"
	"#extension GL_ARB_tessellation_shader : require\n"
	"layout(vertices = 4) out;\n"
	"\n"
	"void main()\n"
	"{\n"
	"\tgl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;\n"
	"\tgl_TessLevelOuter[0] = 3.0;\n"
	"\tgl_TessLevelOuter[1] = 3.0;\n"
	"\tgl_TessLevelOuter[2] = 3.0;\n"
	"\tgl_TessLevelOuter[3] = 3.0;\n"
	"\tgl_TessLevelInner[0] = 3.0;\n"
	"\tgl_TessLevelInner[1] = 3.0;\n"
	"}\n";
static const char gl33_tess_evaluation_source[] =
	"#version 330 core\n"
	"#extension GL_ARB_tessellation_shader : require\n"
	"layout(quads, equal_spacing, ccw) in;\n"
	"\n"
	"void main()\n"
	"{\n"
	"\tvec4 below = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, gl_TessCoord.x);\n"
	"\tvec4 above = mix(gl_in[2].gl_Position, gl_in[3].gl_Position, gl_TessCoord.x);\n"
	"\tgl_Position = mix(below, above, gl_TessCoord.y);\n"
	"}\n";
static const char gl33_green_source[] =
	"#version 330 core\n"
	"layout(location = 0) out vec4 o_colour;\n"
	"\n"
	"void main()\n"
	"{\n"
	"\to_colour = vec4(0.0, 1.0, 0.0, 1.0);\n"
	"}\n";

static GLuint gl33_program(const char *vertex_source, const char *fragment_source);
static GLuint gl33_shader(GLenum type, const char *source);
static void gl33_quad(GLfloat x, GLfloat y, GLfloat width, GLfloat height, unsigned rgb);
static unsigned gl33_read(int x, int y);
static void gl33_clear(unsigned rgb);
static void gl33_arrays(void);
static void gl33_version(void);
static void gl33_time_elapsed(void);
static void gl33_timestamps(void);
static void gl33_dual(GLuint program, unsigned outcome, const char *what);
static void gl33_packed(GLuint program, GLenum type, GLuint value, int four, unsigned outcome, const char *what);
static void gl33_rgb10_a2ui(void);
static void gl33_tessellation(void);
static void gl33_draw_indirect(void);
static void gl33_debug(void);
static void APIENTRY gl33_debug_heard(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *user);
static int gl33_expect(const char *what, long got, long expected);

/*
 * Makes the programs, the vertex array and the framebuffer object, and
 * runs the checks.  Returns 0, or -1 with a line saying what failed.
 */
int
glxtest_gl33_start(void)
{
	GLenum complete;
	GLenum error;
	GLint index;

	/* The programs and the uniforms the scene sets; the bound dual-source blend's outputs before its link. */
	gl33_plain = gl33_program(gl33_vertex_source, gl33_plain_source);
	gl33_dual_layout = gl33_program(gl33_vertex_source, gl33_dual_layout_source);
	gl33_attrib = gl33_program(gl33_attrib_vertex_source, gl33_attrib_fragment_source);
	gl33_attrib_turned = gl33_program(gl33_turned_vertex_source, gl33_attrib_fragment_source);
	gl33_dual_bound = gl33_program(gl33_vertex_source, gl33_dual_bound_source);
	if (gl33_plain == 0U ||
	    gl33_dual_layout == 0U ||
	    gl33_attrib == 0U ||
	    gl33_attrib_turned == 0U ||
	    gl33_dual_bound == 0U)
		return -1;
	gl33_plain_rect = glGetUniformLocation(gl33_plain, "u_rect");
	gl33_plain_colour = glGetUniformLocation(gl33_plain, "u_colour");

	/* The layouts' index, and the bound one's after its outputs were bound and it was linked again. */
	index = glGetFragDataIndex(gl33_dual_layout, "o_factor");
	(void)gl33_expect("layout-index", (long)index, 1L);
	glBindFragDataLocationIndexed(gl33_dual_bound, 0U, 0U, "o_colour");
	glBindFragDataLocationIndexed(gl33_dual_bound, 0U, 1U, "o_factor");
	glLinkProgram(gl33_dual_bound);
	index = glGetFragDataIndex(gl33_dual_bound, "o_factor");
	(void)gl33_expect("bound-index", (long)index, 1L);
	index = glGetFragDataLocation(gl33_dual_bound, "o_factor");
	(void)gl33_expect("bound-location", (long)index, 0L);

	/* The squares cover the target. */
	glUseProgram(gl33_dual_layout);
	glUniform4f(glGetUniformLocation(gl33_dual_layout, "u_rect"), 0.0f, 0.0f, 1.0f, 1.0f);
	glUseProgram(gl33_dual_bound);
	glUniform4f(glGetUniformLocation(gl33_dual_bound, "u_rect"), 0.0f, 0.0f, 1.0f, 1.0f);

	/* The vertex array. */
	gl33_arrays();

	/* The framebuffer object of an RGBA8 texture. */
	glGenTextures(1, &gl33_texture);
	glBindTexture(GL_TEXTURE_2D, gl33_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GL33_SIDE, GL33_SIDE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glGenFramebuffers(1, &gl33_framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, gl33_framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl33_texture, 0);
	glViewport(0, 0, GL33_SIDE, GL33_SIDE);
	complete = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (complete != GL_FRAMEBUFFER_COMPLETE) {
		printf("GLXTEST GL33 framebuffer incomplete 0x%x\n", (unsigned)complete);
		return -1;
	}

	/* The checks, each setting its outcome. */
	gl33_version();
	gl33_time_elapsed();
	gl33_timestamps();
	gl33_dual(gl33_dual_layout, 3U, "dual-layout");
	gl33_dual(gl33_dual_bound, 4U, "dual-bound");
	gl33_packed(gl33_attrib, GL_UNSIGNED_INT_2_10_10_10_REV, (3U << 30) | (1023U << 10), 1, 5U, "packed-unsigned");
	gl33_packed(gl33_attrib_turned, GL_INT_2_10_10_10_REV, 511U, 0, 6U, "packed-signed");
	gl33_rgb10_a2ui();
	gl33_tessellation();
	gl33_draw_indirect();
	gl33_debug();

	/* The window again, and no error the checks did not expect. */
	glBindFramebuffer(GL_FRAMEBUFFER, 0U);
	glBindVertexArray(0U);
	error = glGetError();
	(void)gl33_expect("start-glerror", (long)error, (long)GL_NO_ERROR);

	/* Succeeded: ready to draw. */
	printf("GLXTEST GL33 ready failures=%d\n", gl33_failures);
	fflush(stdout);
	return 0;
}

/*
 * Draws one square per outcome over a window of a size, green when it is
 * the expected one and red otherwise, and a green square with glBegin in
 * the cell after them.
 */
void
glxtest_gl33_draw(
	int width,
	int height)
{
	unsigned index;
	unsigned rgb;
	GLfloat x;
	GLfloat y;

	/* The whole window, cleared to dark grey. */
	glBindFramebuffer(GL_FRAMEBUFFER, 0U);
	glViewport(0, 0, width, height);
	glDisable(GL_BLEND);
	glClearColor(0.125f, 0.125f, 0.125f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glBindVertexArray(gl33_square);

	/* Each outcome in a cell of a grid of four columns. */
	for (index = 0U; index < GL33_OUTCOMES; index++) {
		rgb = GL33_RED;
		if (gl33_outcomes[index])
			rgb = GL33_GREEN;
		x = -0.75f + 0.5f * (GLfloat)(index % 4U);
		y = 0.66f - 0.66f * (GLfloat)(index / 4U);
		gl33_quad(x, y, 0.2f, 0.25f, rgb);
	}

	/* The next cell (the last of the third row) with the fixed function, as the compatibility profile keeps it. */
	glBindVertexArray(0U);
	glUseProgram(0U);
	glColor3f(0.0f, 1.0f, 0.0f);
	glBegin(GL_QUADS);
	glVertex2f(0.55f, -0.91f);
	glVertex2f(0.95f, -0.91f);
	glVertex2f(0.95f, -0.41f);
	glVertex2f(0.55f, -0.41f);
	glEnd();
}

/*
 * Reads back each square's centre and prints the colours; returns how
 * many are not green, with the start's failed checks and any error.
 */
int
glxtest_gl33_check(
	int width,
	int height,
	const char *token)
{
	GLenum error;
	const char *verdict;
	const char *name;
	unsigned got;
	unsigned index;
	int failures;
	int x;
	int y;

	/* Each square's centre (the fixed-function one last), in GL's coordinates. */
	failures = gl33_failures;
	glBindFramebuffer(GL_FRAMEBUFFER, 0U);
	for (index = 0U; index <= GL33_OUTCOMES; index++) {
		x = (int)((-0.75f + 0.5f * (float)(index % 4U) + 1.0f) * 0.5f * (float)width);
		y = (int)((0.66f - 0.66f * (float)(index / 4U) + 1.0f) * 0.5f * (float)height);
		got = gl33_read(x, y);

		/* Green is the expected outcome. */
		verdict = "ok";
		if (got != GL33_GREEN) {
			verdict = "DIFFERS";
			failures++;
		}

		/* The square's line. */
		name = "fixed-function";
		if (index < GL33_OUTCOMES)
			name = gl33_names[index];
		printf("EGLTEST PIXEL run=%s name=%s got=%06x expected=00ff00 %s\n", token, name, got, verdict);
	}

	/* The readbacks raised no error. */
	error = glGetError();
	printf("EGLTEST CHECK run=%s failures=%d glerror=0x%x\n", token, failures, (unsigned)error);
	fflush(stdout);
	if (error != GL_NO_ERROR)
		failures++;

	/* Succeeded: how many differed. */
	return failures;
}

/* Makes a program of GLSL sources, the corners at location 0; 0 with a line on failure. */
static GLuint
gl33_program(
	const char *vertex_source,
	const char *fragment_source)
{
	GLuint program;
	GLuint vertex;
	GLuint fragment;
	GLint linked;
	char log[512];

	/* The vertex and the fragment shader. */
	vertex = gl33_shader(GL_VERTEX_SHADER, vertex_source);
	if (vertex == 0U)
		return 0U;
	fragment = gl33_shader(GL_FRAGMENT_SHADER, fragment_source);
	if (fragment == 0U)
		return 0U;

	/* The program. */
	program = glCreateProgram();
	glAttachShader(program, vertex);
	glAttachShader(program, fragment);
	glBindAttribLocation(program, GL33_POSITION, "a_position");
	glLinkProgram(program);
	glDeleteShader(vertex);
	glDeleteShader(fragment);

	/* Linked. */
	linked = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked) {
		log[0] = '\0';
		glGetProgramInfoLog(program, (GLsizei)sizeof(log), NULL, log);
		printf("GLXTEST GL33 link failed: %s\n", log);
		return 0U;
	}

	/* Succeeded: the program. */
	return program;
}

/* Makes a shader from GLSL source; 0 with a line when it does not compile. */
static GLuint
gl33_shader(
	GLenum type,
	const char *source)
{
	GLuint shader;
	GLint compiled;
	char log[512];

	/* The shader compiled from the source. */
	shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	compiled = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (!compiled) {
		log[0] = '\0';
		glGetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
		printf("GLXTEST GL33 compile failed: %s\n", log);
		return 0U;
	}

	/* Succeeded: the shader. */
	return shader;
}

/* Draws the square placed by a rectangle (centre, half size) in a colour, with the plain program. */
static void
gl33_quad(
	GLfloat x,
	GLfloat y,
	GLfloat width,
	GLfloat height,
	unsigned rgb)
{
	GLfloat red;
	GLfloat green;
	GLfloat blue;

	/* The colour's channels. */
	red = (GLfloat)((rgb >> 16) & 0xffU) / 255.0f;
	green = (GLfloat)((rgb >> 8) & 0xffU) / 255.0f;
	blue = (GLfloat)(rgb & 0xffU) / 255.0f;

	/* The uniforms, then the strip. */
	glUseProgram(gl33_plain);
	glUniform4f(gl33_plain_rect, x, y, width, height);
	glUniform4f(gl33_plain_colour, red, green, blue, 1.0f);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/* Returns a texel of the read framebuffer's read buffer as 0xRRGGBB. */
static unsigned
gl33_read(
	int x,
	int y)
{
	GLubyte pixel[4];
	unsigned rgb;

	/* The texel. */
	memset(pixel, 0, sizeof(pixel));
	glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	rgb = ((unsigned)pixel[0] << 16) | ((unsigned)pixel[1] << 8) | (unsigned)pixel[2];

	/* Succeeded: the colour. */
	return rgb;
}

/* Clears the draw framebuffer to a colour. */
static void
gl33_clear(
	unsigned rgb)
{
	GLfloat red;
	GLfloat green;
	GLfloat blue;

	/* The colour's channels, cleared with. */
	red = (GLfloat)((rgb >> 16) & 0xffU) / 255.0f;
	green = (GLfloat)((rgb >> 8) & 0xffU) / 255.0f;
	blue = (GLfloat)(rgb & 0xffU) / 255.0f;
	glClearColor(red, green, blue, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

/* Makes the vertex array of the unit square, without an array for the colour attribute (its current value is read). */
static void
gl33_arrays(void)
{
	static const GLfloat corners[8] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
	GLuint buffer;

	/* The square. */
	glGenBuffers(1, &buffer);
	glGenVertexArrays(1, &gl33_square);
	glBindVertexArray(gl33_square);
	glBindBuffer(GL_ARRAY_BUFFER, buffer);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(corners), corners, GL_STATIC_DRAW);
	glVertexAttribPointer(GL33_POSITION, 2, GL_FLOAT, GL_FALSE, 0, NULL);
	glEnableVertexAttribArray(GL33_POSITION);
	glDisableVertexAttribArray(GL33_COLOUR);
	glBindBuffer(GL_ARRAY_BUFFER, 0U);
}

/* Checks the version the context reports (OpenGL 3.3, GLSL 3.30) and OpenGL 3.3's extensions and limits. */
static void
gl33_version(void)
{
	static const char *const wanted[] = {
		"GL_ARB_timer_query", "GL_ARB_blend_func_extended", "GL_ARB_vertex_type_2_10_10_10_rev",
		"GL_ARB_texture_rgb10_a2ui", "GL_ARB_explicit_attrib_location", "GL_ARB_shader_bit_encoding"
	};
	const char *version;
	const char *language;
	const char *extensions;
	const char *found;
	GLint major;
	GLint minor;
	GLint buffers;
	unsigned index;
	int passed;
	int differs;
	int named;

	/* The strings. */
	version = (const char *)glGetString(GL_VERSION);
	language = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
	extensions = (const char *)glGetString(GL_EXTENSIONS);
	printf("GLXTEST GL33 version=\"%s\" glsl=\"%s\" extensions=\"%s\"\n", version, language, extensions);
	passed = 1;
	differs = strncmp(version, "3.3 ", 4U);
	passed &= gl33_expect("version-string", (long)differs, 0L);
	differs = strcmp(language, "3.30");
	passed &= gl33_expect("glsl-version", (long)differs, 0L);

	/* The numbers. */
	major = 0;
	minor = -1;
	glGetIntegerv(GL_MAJOR_VERSION, &major);
	glGetIntegerv(GL_MINOR_VERSION, &minor);
	passed &= gl33_expect("major-version", (long)major, 3L);
	passed &= gl33_expect("minor-version", (long)minor, 3L);

	/* OpenGL 3.3's extensions. */
	for (index = 0U; index < sizeof(wanted) / sizeof(wanted[0]); index++) {
		found = strstr(extensions, wanted[index]);
		named = 0;
		if (found != NULL)
			named = 1;
		passed &= gl33_expect(wanted[index], (long)named, 1L);
	}

	/* One draw buffer a dual-source blend writes. */
	buffers = 0;
	glGetIntegerv(GL_MAX_DUAL_SOURCE_DRAW_BUFFERS, &buffers);
	passed &= gl33_expect("max-dual-source-draw-buffers", (long)buffers, 1L);
	gl33_outcomes[0] = passed;
}

/* Checks a GL_TIME_ELAPSED query around a draw: its counter has bits, its result is available and not negative. */
static void
gl33_time_elapsed(void)
{
	GLuint query;
	GLint bits;
	GLint current;
	GLuint64 elapsed;
	GLuint available;
	int passed;

	/* The counter's bits, and the query's name while it is active. */
	passed = 1;
	bits = 0;
	glGetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &bits);
	passed &= gl33_expect("elapsed-bits-nonzero", (long)(bits > 0), 1L);
	glGenQueries(1, &query);
	glBeginQuery(GL_TIME_ELAPSED, query);
	current = 0;
	glGetQueryiv(GL_TIME_ELAPSED, GL_CURRENT_QUERY, &current);
	passed &= gl33_expect("elapsed-current", (long)current, (long)query);

	/* A draw timed. */
	gl33_clear(GL33_RED);
	gl33_quad(0.0f, 0.0f, 1.0f, 1.0f, GL33_GREEN);
	glEndQuery(GL_TIME_ELAPSED);

	/* The result, waited for, and under a second. */
	available = GL_FALSE;
	elapsed = ~(GLuint64)0;
	glGetQueryObjectui64v(query, GL_QUERY_RESULT, &elapsed);
	glGetQueryObjectuiv(query, GL_QUERY_RESULT_AVAILABLE, &available);
	printf("GLXTEST GL33 elapsed=%llu ns\n", (unsigned long long)elapsed);
	passed &= gl33_expect("elapsed-available", (long)available, (long)GL_TRUE);
	passed &= gl33_expect("elapsed-under-a-second", (long)(elapsed < 1000000000ULL), 1L);
	glDeleteQueries(1, &query);
	gl33_outcomes[1] = passed;
}

/* Checks glQueryCounter before and after a draw and glGetInteger64v(GL_TIMESTAMP) after both: the times keep their order. */
static void
gl33_timestamps(void)
{
	GLuint queries[2];
	GLint64 before;
	GLint64 after;
	GLint64 now;
	GLint bits;
	int passed;

	/* The counter's bits. */
	passed = 1;
	bits = 0;
	glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
	passed &= gl33_expect("timestamp-bits-nonzero", (long)(bits > 0), 1L);

	/* The two counters around a draw, then the GPU's time now. */
	glGenQueries(2, queries);
	glQueryCounter(queries[0], GL_TIMESTAMP);
	gl33_quad(0.0f, 0.0f, 1.0f, 1.0f, GL33_GREEN);
	glQueryCounter(queries[1], GL_TIMESTAMP);
	now = 0;
	glGetInteger64v(GL_TIMESTAMP, &now);

	/* The counters' times. */
	before = -1;
	after = -1;
	glGetQueryObjecti64v(queries[0], GL_QUERY_RESULT, &before);
	glGetQueryObjecti64v(queries[1], GL_QUERY_RESULT, &after);
	printf("GLXTEST GL33 timestamps before=%lld after=%lld now=%lld\n", (long long)before, (long long)after, (long long)now);
	passed &= gl33_expect("timestamp-positive", (long)(before > 0), 1L);
	passed &= gl33_expect("timestamp-order", (long)(after >= before), 1L);
	passed &= gl33_expect("timestamp-now-after", (long)(now >= after), 1L);
	glDeleteQueries(2, queries);
	gl33_outcomes[2] = passed;
}

/*
 * Checks a dual-source blend: a red target blended with the shader's
 * green colour by its second colour (1: all of the source, none of the
 * target) is green; read as the first colour it would not be.
 */
static void
gl33_dual(
	GLuint program,
	unsigned outcome,
	const char *what)
{
	GLenum error;
	unsigned got;
	int passed;

	/* The target red, the square blended over it. */
	gl33_clear(GL33_RED);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR);
	glUseProgram(program);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ZERO);
	error = glGetError();

	/* Its centre green, and no error. */
	got = gl33_read(GL33_SIDE / 2, GL33_SIDE / 2);
	printf("GLXTEST GL33 %s got=%06x\n", what, got);
	passed = gl33_expect(what, (long)got, (long)GL33_GREEN);
	passed &= gl33_expect("dual-glerror", (long)error, (long)GL_NO_ERROR);
	gl33_outcomes[outcome] = passed;
}

/*
 * Checks a current attribute value from a packed word (the colour
 * attribute has no array): drawn over a red target, the square is green
 * when the word unpacks as it should.
 */
static void
gl33_packed(
	GLuint program,
	GLenum type,
	GLuint value,
	int four,
	unsigned outcome,
	const char *what)
{
	unsigned got;

	/* The value: four components, or three with w 1. */
	if (four) {
		glVertexAttribP4ui(GL33_COLOUR, type, GL_TRUE, value);
	} else {
		glVertexAttribP3ui(GL33_COLOUR, type, GL_TRUE, value);
	}

	/* The square drawn with it over red. */
	gl33_clear(GL33_RED);
	glUseProgram(program);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	got = gl33_read(GL33_SIDE / 2, GL33_SIDE / 2);
	printf("GLXTEST GL33 %s got=%06x\n", what, got);
	gl33_outcomes[outcome] = gl33_expect(what, (long)got, (long)GL33_GREEN);
}

/* Checks an RGB10_A2UI texture: complete as a framebuffer object's colour, cleared with integers and read back as them. */
static void
gl33_rgb10_a2ui(void)
{
	static const GLuint clear[4] = { 1023U, 512U, 7U, 3U };
	GLuint framebuffer;
	GLuint texture;
	GLuint texel[4];
	GLenum complete;
	int passed;

	/* The texture and its framebuffer object. */
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB10_A2UI, GL33_SIDE, GL33_SIDE, 0, GL_RGBA_INTEGER, GL_UNSIGNED_INT_2_10_10_10_REV, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
	complete = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	passed = gl33_expect("rgb10-a2ui-complete", (long)complete, (long)GL_FRAMEBUFFER_COMPLETE);

	/* Cleared with integers, read back as integers. */
	glClearBufferuiv(GL_COLOR, 0, clear);
	memset(texel, 0, sizeof(texel));
	glReadPixels(GL33_SIDE / 2, GL33_SIDE / 2, 1, 1, GL_RGBA_INTEGER, GL_UNSIGNED_INT, texel);
	printf("GLXTEST GL33 rgb10-a2ui texel=%u,%u,%u,%u\n", texel[0], texel[1], texel[2], texel[3]);
	passed &= gl33_expect("rgb10-a2ui-red", (long)texel[0], 1023L);
	passed &= gl33_expect("rgb10-a2ui-green", (long)texel[1], 512L);
	passed &= gl33_expect("rgb10-a2ui-blue", (long)texel[2], 7L);
	passed &= gl33_expect("rgb10-a2ui-alpha", (long)texel[3], 3L);

	/* The test's framebuffer object again. */
	glBindFramebuffer(GL_FRAMEBUFFER, gl33_framebuffer);
	glDeleteFramebuffers(1, &framebuffer);
	glDeleteTextures(1, &texture);
	gl33_outcomes[7] = passed;
}

/*
 * Checks GL_ARB_tessellation_shader when the context names it: the
 * square's corners as one patch of four vertices, tessellated as a quad
 * over the red target, make it green; the program's layouts and the
 * patch's vertices read back.  Skipped (green) without the extension.
 */
static void
gl33_tessellation(void)
{
	const char *extensions;
	const char *found;
	GLuint program;
	GLuint shaders[4];
	GLint value;
	GLint linked;
	GLenum error;
	unsigned got;
	unsigned index;
	int passed;
	char log[512];

	/* Without the extension, nothing to check. */
	extensions = (const char *)glGetString(GL_EXTENSIONS);
	found = strstr(extensions, "GL_ARB_tessellation_shader");
	if (found == NULL) {
		printf("GLXTEST GL33 tessellation skipped (no GL_ARB_tessellation_shader)\n");
		gl33_outcomes[8] = 1;
		return;
	}

	/* The four stages' program. */
	shaders[0] = gl33_shader(GL_VERTEX_SHADER, gl33_tess_vertex_source);
	shaders[1] = gl33_shader(GL_TESS_CONTROL_SHADER, gl33_tess_control_source);
	shaders[2] = gl33_shader(GL_TESS_EVALUATION_SHADER, gl33_tess_evaluation_source);
	shaders[3] = gl33_shader(GL_FRAGMENT_SHADER, gl33_green_source);
	program = glCreateProgram();
	for (index = 0U; index < 4U; index++) {
		if (shaders[index] != 0U)
			glAttachShader(program, shaders[index]);
	}
	glLinkProgram(program);
	linked = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked) {
		log[0] = '\0';
		glGetProgramInfoLog(program, (GLsizei)sizeof(log), NULL, log);
		printf("GLXTEST GL33 tessellation link failed: %s\n", log);
	}
	passed = gl33_expect("tess-linked", (long)linked, (long)GL_TRUE);

	/* The program's layouts. */
	value = 0;
	glGetProgramiv(program, GL_TESS_CONTROL_OUTPUT_VERTICES, &value);
	passed &= gl33_expect("tess-output-vertices", (long)value, 4L);
	glGetProgramiv(program, GL_TESS_GEN_MODE, &value);
	passed &= gl33_expect("tess-mode", (long)value, (long)GL_QUADS);

	/* A triangle draw is refused; a patch of the four corners covers the red target. */
	gl33_clear(GL33_RED);
	glUseProgram(program);
	(void)glGetError();
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	error = glGetError();
	passed &= gl33_expect("tess-triangles-refused", (long)error, (long)GL_INVALID_OPERATION);
	glPatchParameteri(GL_PATCH_VERTICES, 4);
	glGetIntegerv(GL_PATCH_VERTICES, &value);
	passed &= gl33_expect("tess-patch-vertices", (long)value, 4L);
	glDrawArrays(GL_PATCHES, 0, 4);
	error = glGetError();
	passed &= gl33_expect("tess-draw-glerror", (long)error, (long)GL_NO_ERROR);
	got = gl33_read(GL33_SIDE / 2, GL33_SIDE / 2);
	printf("GLXTEST GL33 tessellation got=%06x\n", got);
	passed &= gl33_expect("tessellation", (long)got, (long)GL33_GREEN);
	got = gl33_read(0, 0);
	passed &= gl33_expect("tessellation-corner", (long)got, (long)GL33_GREEN);

	/* The program and its shaders go. */
	glPatchParameteri(GL_PATCH_VERTICES, 3);
	glUseProgram(0U);
	glDeleteProgram(program);
	for (index = 0U; index < 4U; index++) {
		if (shaders[index] != 0U)
			glDeleteShader(shaders[index]);
	}
	gl33_outcomes[8] = passed;
}

/*
 * Checks GL_ARB_draw_indirect: the square drawn green over the red
 * target by a command (4 vertices, 1 instance, from 0) in the indirect
 * buffer, and the buffer's binding read back.
 */
static void
gl33_draw_indirect(void)
{
	static const GLuint command[4] = { 4U, 1U, 0U, 0U };
	GLuint buffer;
	GLint bound;
	GLenum error;
	unsigned got;
	int passed;

	/* The command in a buffer bound to the target. */
	glGenBuffers(1, &buffer);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, buffer);
	glBufferData(GL_DRAW_INDIRECT_BUFFER, (GLsizeiptr)sizeof(command), command, GL_STATIC_DRAW);
	bound = 0;
	glGetIntegerv(GL_DRAW_INDIRECT_BUFFER_BINDING, &bound);
	passed = gl33_expect("indirect-binding", (long)bound, (long)buffer);

	/* The square over red, drawn by the command. */
	gl33_clear(GL33_RED);
	glUseProgram(gl33_plain);
	glUniform4f(gl33_plain_rect, 0.0f, 0.0f, 1.0f, 1.0f);
	glUniform4f(gl33_plain_colour, 0.0f, 1.0f, 0.0f, 1.0f);
	glDrawArraysIndirect(GL_TRIANGLE_STRIP, NULL);
	error = glGetError();
	passed &= gl33_expect("indirect-glerror", (long)error, (long)GL_NO_ERROR);
	got = gl33_read(GL33_SIDE / 2, GL33_SIDE / 2);
	printf("GLXTEST GL33 draw-indirect got=%06x\n", got);
	passed &= gl33_expect("draw-indirect", (long)got, (long)GL33_GREEN);

	/* A misaligned offset is refused. */
	glDrawArraysIndirect(GL_TRIANGLE_STRIP, (const void *)2);
	error = glGetError();
	passed &= gl33_expect("indirect-misaligned", (long)error, (long)GL_INVALID_VALUE);

	/* The buffer goes. */
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0U);
	glDeleteBuffers(1, &buffer);
	gl33_outcomes[9] = passed;
}

/*
 * Checks GL_KHR_debug: with the output on and a callback, a marker
 * inserted, an error and a group's push and pop are heard; a buffer
 * takes a label; without the callback a message waits in the log.
 */
static void
gl33_debug(void)
{
	static const char marker[] = "glxtest marker";
	char label[32];
	char text[64];
	GLuint buffer;
	GLenum type;
	GLsizei length;
	GLuint taken;
	GLint depth;
	int passed;

	/* The output on, the callback ours. */
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(gl33_debug_heard, NULL);

	/* A marker, an error (a capability that is not one), a group. */
	glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 1U, GL_DEBUG_SEVERITY_NOTIFICATION, -1, marker);
	glEnable(0xffffU);
	(void)glGetError();
	glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 2U, -1, "glxtest group");
	depth = 0;
	glGetIntegerv(GL_DEBUG_GROUP_STACK_DEPTH, &depth);
	glPopDebugGroup();
	passed = gl33_expect("debug-markers", (long)gl33_heard_markers, 1L);
	passed &= gl33_expect("debug-errors", (long)gl33_heard_errors, 1L);
	passed &= gl33_expect("debug-groups", (long)gl33_heard_groups, 2L);
	passed &= gl33_expect("debug-group-depth", (long)depth, 2L);

	/* A buffer's label. */
	glGenBuffers(1, &buffer);
	glBindBuffer(GL_ARRAY_BUFFER, buffer);
	glObjectLabel(GL_BUFFER, buffer, -1, "glxtest buffer");
	memset(label, 0, sizeof(label));
	length = 0;
	glGetObjectLabel(GL_BUFFER, buffer, (GLsizei)sizeof(label), &length, label);
	passed &= gl33_expect("debug-label", (long)strcmp(label, "glxtest buffer"), 0L);
	passed &= gl33_expect("debug-label-length", (long)length, 14L);
	glBindBuffer(GL_ARRAY_BUFFER, 0U);
	glDeleteBuffers(1, &buffer);

	/* Without the callback, the log. */
	glDebugMessageCallback(NULL, NULL);
	glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_OTHER, 3U, GL_DEBUG_SEVERITY_HIGH, -1, marker);
	memset(text, 0, sizeof(text));
	type = GL_NONE;
	taken = glGetDebugMessageLog(1U, (GLsizei)sizeof(text), NULL, &type, NULL, NULL, NULL, text);
	passed &= gl33_expect("debug-log-taken", (long)taken, 1L);
	passed &= gl33_expect("debug-log-type", (long)type, (long)GL_DEBUG_TYPE_OTHER);
	passed &= gl33_expect("debug-log-text", (long)strcmp(text, marker), 0L);
	glDisable(GL_DEBUG_OUTPUT);
	gl33_outcomes[10] = passed;
}

/* Counts a debug message the callback hears, by its type. */
static void APIENTRY
gl33_debug_heard(
	GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	GLsizei length,
	const GLchar *message,
	const void *user)
{
	/* Only the type counts. */
	(void)source;
	(void)id;
	(void)severity;
	(void)length;
	(void)message;
	(void)user;
	if (type == GL_DEBUG_TYPE_MARKER)
		gl33_heard_markers++;
	if (type == GL_DEBUG_TYPE_ERROR)
		gl33_heard_errors++;
	if (type == GL_DEBUG_TYPE_PUSH_GROUP || type == GL_DEBUG_TYPE_POP_GROUP)
		gl33_heard_groups++;
}

/* Prints one of the start's checks, counting it when the value is not the one expected; returns 1 when it is. */
static int
gl33_expect(
	const char *what,
	long got,
	long expected)
{
	/* A value that differs is a failure. */
	if (got != expected) {
		gl33_failures++;
		printf("GLXTEST GL33 check %s got=%ld expected=%ld FAILED\n", what, got, expected);
		return 0;
	}

	/* Succeeded: the line. */
	printf("GLXTEST GL33 check %s got=%ld expected=%ld ok\n", what, got, expected);
	return 1;
}
