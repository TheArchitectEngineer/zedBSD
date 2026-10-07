/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * libGL's view of the current context's desktop GL version (WS178): the
 * window-system binding (libGLX.so) attaches its answers through the
 * private interface of zgl-glx.h, and GL (fixed.c, immediate.c) asks
 * here.  Without a binding, a context is OpenGL 1.4 with no flags and no
 * profile.
 */

#include "fixed.h"
#include "zgl-glx.h"

#include <stddef.h>

/*
 * The binding's answers, NULL until libGLX attaches them.
 *
 * libGLX attaches the same instance before each context it makes, from
 * any thread, and the instance lives as long as the process; the atomic
 * store and load make the pointer whole to a GL call on another thread.
 */
static const struct zgl_glx_query *context_query;

/*
 * Takes a window-system binding's answers and turns on the fixed-function
 * layer.
 */
void
zgl_glx_attach(
	const struct zgl_glx_query *query)
{
	/* The answers GL asks from now on. */
	__atomic_store_n(&context_query, query, __ATOMIC_RELEASE);

	/* The translation draws without a program through the fixed-function hooks from now on. */
	fixed_install();
}

/*
 * Returns the desktop GL version of the calling thread's current context
 * (major * 10 + minor) and its GL_CONTEXT_FLAGS.
 */
unsigned
gl_context_version(
	GLint *flags)
{
	const struct zgl_glx_query *query;
	unsigned version;

	/* Without a binding: OpenGL 1.4, no flags. */
	query = __atomic_load_n(&context_query, __ATOMIC_ACQUIRE);
	if (query == NULL) {
		*flags = 0;
		return ZGL_GL_LEGACY;
	}

	/* The binding's answer for the thread's context. */
	version = query->version(flags);

	/* Succeeded: the version. */
	return version;
}

/*
 * Returns the GL_CONTEXT_PROFILE_MASK of the calling thread's current
 * context (0 before 3.2).
 */
GLint
gl_context_profile(void)
{
	const struct zgl_glx_query *query;
	GLint profile;

	/* Without a binding: no profile. */
	query = __atomic_load_n(&context_query, __ATOMIC_ACQUIRE);
	if (query == NULL)
		return 0;

	/* The binding's answer for the thread's context. */
	profile = query->profile();

	/* Succeeded: the profile. */
	return profile;
}
