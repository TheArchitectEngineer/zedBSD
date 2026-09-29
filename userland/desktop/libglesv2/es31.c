/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * OpenGL ES 3.1's calls outside its compute (ws101-p009), in libGLESv2
 * only.  A context that offers compute names itself OpenGL ES 3.1 for the
 * compute part (dispatches, shader storage buffers, barriers: compute.c);
 * the rest of 3.1 is not offered, and each of its calls records
 * GL_INVALID_OPERATION and does nothing (a value it returns says nothing
 * was found), so that an application linked against 3.1's API loads and
 * learns at once that the call was refused.  3.1's calls libGL's desktop
 * GL has (glGetBooleani_v, glSampleMaski, glGetMultisamplefv,
 * glTexStorage2DMultisample) are the ones libGLESv2 already builds.
 */

#include "gles.h"

static void es31_refuse(void);

/*
 * Refuses an indirect draw: not offered.
 */
GL_APICALL void GL_APIENTRY
glDrawArraysIndirect(
	GLenum mode,
	const void *indirect)
{
	/* Its arguments are not used. */
	(void)mode;
	(void)indirect;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses an indirect draw: not offered.
 */
GL_APICALL void GL_APIENTRY
glDrawElementsIndirect(
	GLenum mode,
	GLenum type,
	const void *indirect)
{
	/* Its arguments are not used. */
	(void)mode;
	(void)type;
	(void)indirect;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a framebuffer parameter (framebuffers without attachments): not offered.
 */
GL_APICALL void GL_APIENTRY
glFramebufferParameteri(
	GLenum target,
	GLenum pname,
	GLint param)
{
	/* Its arguments are not used. */
	(void)target;
	(void)pname;
	(void)param;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a framebuffer parameter (framebuffers without attachments): not offered.
 */
GL_APICALL void GL_APIENTRY
glGetFramebufferParameteriv(
	GLenum target,
	GLenum pname,
	GLint *params)
{
	/* Its arguments are not used. */
	(void)target;
	(void)pname;
	(void)params;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a program interface query: not offered.
 */
GL_APICALL void GL_APIENTRY
glGetProgramInterfaceiv(
	GLuint program,
	GLenum programInterface,
	GLenum pname,
	GLint *params)
{
	/* Its arguments are not used. */
	(void)program;
	(void)programInterface;
	(void)pname;
	(void)params;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a program interface query: not offered.
 */
GL_APICALL GLuint GL_APIENTRY
glGetProgramResourceIndex(
	GLuint program,
	GLenum programInterface,
	const GLchar *name)
{
	/* Its arguments are not used. */
	(void)program;
	(void)programInterface;
	(void)name;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();

	/* No resource was found. */
	return GL_INVALID_INDEX;
}

/*
 * Refuses a program interface query: not offered.
 */
GL_APICALL void GL_APIENTRY
glGetProgramResourceName(
	GLuint program,
	GLenum programInterface,
	GLuint index,
	GLsizei bufSize,
	GLsizei *length,
	GLchar *name)
{
	/* Its arguments are not used. */
	(void)program;
	(void)programInterface;
	(void)index;
	(void)bufSize;
	(void)length;
	(void)name;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a program interface query: not offered.
 */
GL_APICALL void GL_APIENTRY
glGetProgramResourceiv(
	GLuint program,
	GLenum programInterface,
	GLuint index,
	GLsizei propCount,
	const GLenum *props,
	GLsizei count,
	GLsizei *length,
	GLint *params)
{
	/* Its arguments are not used. */
	(void)program;
	(void)programInterface;
	(void)index;
	(void)propCount;
	(void)props;
	(void)count;
	(void)length;
	(void)params;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a program interface query: not offered.
 */
GL_APICALL GLint GL_APIENTRY
glGetProgramResourceLocation(
	GLuint program,
	GLenum programInterface,
	const GLchar *name)
{
	/* Its arguments are not used. */
	(void)program;
	(void)programInterface;
	(void)name;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();

	/* No location was found. */
	return -1;
}

/*
 * Refuses a separate shader object call (program pipelines): not offered.
 */
GL_APICALL void GL_APIENTRY
glUseProgramStages(
	GLuint pipeline,
	GLbitfield stages,
	GLuint program)
{
	/* Its arguments are not used. */
	(void)pipeline;
	(void)stages;
	(void)program;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a separate shader object call (program pipelines): not offered.
 */
GL_APICALL void GL_APIENTRY
glActiveShaderProgram(
	GLuint pipeline,
	GLuint program)
{
	/* Its arguments are not used. */
	(void)pipeline;
	(void)program;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a separate shader object call (program pipelines): not offered.
 */
GL_APICALL GLuint GL_APIENTRY
glCreateShaderProgramv(
	GLenum type,
	GLsizei count,
	const GLchar *const *strings)
{
	/* Its arguments are not used. */
	(void)type;
	(void)count;
	(void)strings;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();

	/* No program was made. */
	return 0U;
}

/*
 * Refuses a separate shader object call (program pipelines): not offered.
 */
GL_APICALL void GL_APIENTRY
glBindProgramPipeline(
	GLuint pipeline)
{
	/* Its arguments are not used. */
	(void)pipeline;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a separate shader object call (program pipelines): not offered.
 */
GL_APICALL void GL_APIENTRY
glDeleteProgramPipelines(
	GLsizei n,
	const GLuint *pipelines)
{
	/* Its arguments are not used. */
	(void)n;
	(void)pipelines;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a separate shader object call (program pipelines): not offered.
 */
GL_APICALL void GL_APIENTRY
glGenProgramPipelines(
	GLsizei n,
	GLuint *pipelines)
{
	/* Its arguments are not used. */
	(void)n;
	(void)pipelines;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a separate shader object call (program pipelines): not offered.
 */
GL_APICALL GLboolean GL_APIENTRY
glIsProgramPipeline(
	GLuint pipeline)
{
	/* Its arguments are not used. */
	(void)pipeline;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();

	/* The name is not a program pipeline. */
	return GL_FALSE;
}

/*
 * Refuses a separate shader object call (program pipelines): not offered.
 */
GL_APICALL void GL_APIENTRY
glGetProgramPipelineiv(
	GLuint pipeline,
	GLenum pname,
	GLint *params)
{
	/* Its arguments are not used. */
	(void)pipeline;
	(void)pname;
	(void)params;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a separate shader object call (program pipelines): not offered.
 */
GL_APICALL void GL_APIENTRY
glValidateProgramPipeline(
	GLuint pipeline)
{
	/* Its arguments are not used. */
	(void)pipeline;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a separate shader object call (program pipelines): not offered.
 */
GL_APICALL void GL_APIENTRY
glGetProgramPipelineInfoLog(
	GLuint pipeline,
	GLsizei bufSize,
	GLsizei *length,
	GLchar *infoLog)
{
	/* Its arguments are not used. */
	(void)pipeline;
	(void)bufSize;
	(void)length;
	(void)infoLog;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform1i(
	GLuint program,
	GLint location,
	GLint v0)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)v0;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform2i(
	GLuint program,
	GLint location,
	GLint v0,
	GLint v1)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)v0;
	(void)v1;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform3i(
	GLuint program,
	GLint location,
	GLint v0,
	GLint v1,
	GLint v2)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)v0;
	(void)v1;
	(void)v2;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform4i(
	GLuint program,
	GLint location,
	GLint v0,
	GLint v1,
	GLint v2,
	GLint v3)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)v0;
	(void)v1;
	(void)v2;
	(void)v3;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform1ui(
	GLuint program,
	GLint location,
	GLuint v0)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)v0;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform2ui(
	GLuint program,
	GLint location,
	GLuint v0,
	GLuint v1)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)v0;
	(void)v1;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform3ui(
	GLuint program,
	GLint location,
	GLuint v0,
	GLuint v1,
	GLuint v2)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)v0;
	(void)v1;
	(void)v2;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform4ui(
	GLuint program,
	GLint location,
	GLuint v0,
	GLuint v1,
	GLuint v2,
	GLuint v3)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)v0;
	(void)v1;
	(void)v2;
	(void)v3;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform1f(
	GLuint program,
	GLint location,
	GLfloat v0)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)v0;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform2f(
	GLuint program,
	GLint location,
	GLfloat v0,
	GLfloat v1)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)v0;
	(void)v1;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform3f(
	GLuint program,
	GLint location,
	GLfloat v0,
	GLfloat v1,
	GLfloat v2)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)v0;
	(void)v1;
	(void)v2;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform4f(
	GLuint program,
	GLint location,
	GLfloat v0,
	GLfloat v1,
	GLfloat v2,
	GLfloat v3)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)v0;
	(void)v1;
	(void)v2;
	(void)v3;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform1iv(
	GLuint program,
	GLint location,
	GLsizei count,
	const GLint *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform2iv(
	GLuint program,
	GLint location,
	GLsizei count,
	const GLint *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform3iv(
	GLuint program,
	GLint location,
	GLsizei count,
	const GLint *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform4iv(
	GLuint program,
	GLint location,
	GLsizei count,
	const GLint *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform1uiv(
	GLuint program,
	GLint location,
	GLsizei count,
	const GLuint *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform2uiv(
	GLuint program,
	GLint location,
	GLsizei count,
	const GLuint *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform3uiv(
	GLuint program,
	GLint location,
	GLsizei count,
	const GLuint *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform4uiv(
	GLuint program,
	GLint location,
	GLsizei count,
	const GLuint *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform1fv(
	GLuint program,
	GLint location,
	GLsizei count,
	const GLfloat *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform2fv(
	GLuint program,
	GLint location,
	GLsizei count,
	const GLfloat *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform3fv(
	GLuint program,
	GLint location,
	GLsizei count,
	const GLfloat *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniform4fv(
	GLuint program,
	GLint location,
	GLsizei count,
	const GLfloat *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniformMatrix2fv(
	GLuint program,
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)transpose;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniformMatrix3fv(
	GLuint program,
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)transpose;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniformMatrix4fv(
	GLuint program,
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)transpose;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniformMatrix2x3fv(
	GLuint program,
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)transpose;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniformMatrix3x2fv(
	GLuint program,
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)transpose;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniformMatrix2x4fv(
	GLuint program,
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)transpose;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniformMatrix4x2fv(
	GLuint program,
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)transpose;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniformMatrix3x4fv(
	GLuint program,
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)transpose;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a uniform of a program that need not be current (glProgramUniform*): not offered.
 */
GL_APICALL void GL_APIENTRY
glProgramUniformMatrix4x3fv(
	GLuint program,
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* Its arguments are not used. */
	(void)program;
	(void)location;
	(void)count;
	(void)transpose;
	(void)value;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses an image unit binding (image load and store): not offered.
 */
GL_APICALL void GL_APIENTRY
glBindImageTexture(
	GLuint unit,
	GLuint texture,
	GLint level,
	GLboolean layered,
	GLint layer,
	GLenum access,
	GLenum format)
{
	/* Its arguments are not used. */
	(void)unit;
	(void)texture;
	(void)level;
	(void)layered;
	(void)layer;
	(void)access;
	(void)format;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a query of a texture level: not offered.
 */
GL_APICALL void GL_APIENTRY
glGetTexLevelParameteriv(
	GLenum target,
	GLint level,
	GLenum pname,
	GLint *params)
{
	/* Its arguments are not used. */
	(void)target;
	(void)level;
	(void)pname;
	(void)params;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a query of a texture level: not offered.
 */
GL_APICALL void GL_APIENTRY
glGetTexLevelParameterfv(
	GLenum target,
	GLint level,
	GLenum pname,
	GLfloat *params)
{
	/* Its arguments are not used. */
	(void)target;
	(void)level;
	(void)pname;
	(void)params;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a vertex attribute binding call: not offered.
 */
GL_APICALL void GL_APIENTRY
glBindVertexBuffer(
	GLuint bindingindex,
	GLuint buffer,
	GLintptr offset,
	GLsizei stride)
{
	/* Its arguments are not used. */
	(void)bindingindex;
	(void)buffer;
	(void)offset;
	(void)stride;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a vertex attribute binding call: not offered.
 */
GL_APICALL void GL_APIENTRY
glVertexAttribFormat(
	GLuint attribindex,
	GLint size,
	GLenum type,
	GLboolean normalized,
	GLuint relativeoffset)
{
	/* Its arguments are not used. */
	(void)attribindex;
	(void)size;
	(void)type;
	(void)normalized;
	(void)relativeoffset;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a vertex attribute binding call: not offered.
 */
GL_APICALL void GL_APIENTRY
glVertexAttribIFormat(
	GLuint attribindex,
	GLint size,
	GLenum type,
	GLuint relativeoffset)
{
	/* Its arguments are not used. */
	(void)attribindex;
	(void)size;
	(void)type;
	(void)relativeoffset;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a vertex attribute binding call: not offered.
 */
GL_APICALL void GL_APIENTRY
glVertexAttribBinding(
	GLuint attribindex,
	GLuint bindingindex)
{
	/* Its arguments are not used. */
	(void)attribindex;
	(void)bindingindex;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/*
 * Refuses a vertex attribute binding call: not offered.
 */
GL_APICALL void GL_APIENTRY
glVertexBindingDivisor(
	GLuint bindingindex,
	GLuint divisor)
{
	/* Its arguments are not used. */
	(void)bindingindex;
	(void)divisor;

	/* GL_INVALID_OPERATION: the call is not offered. */
	es31_refuse();
}

/* Records the error of a call of OpenGL ES 3.1 that is not offered. */
static void
es31_refuse(void)
{
	struct zegl_context *context;

	/* The current context's error (none without a context). */
	context = gles_context();
	if (context == NULL)
		return;
	gles_error(context, GL_INVALID_OPERATION);
}
