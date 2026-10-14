/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws236-p011: Video Player's shaders through the Raspberry Pi 4's native
 * shader compiler (bcm2711 shader.c), on the host.
 *
 * Compiles the picture's vertex shader (coordinate and vertex variants) and
 * the picture's and the overlay's fragment shaders with the pipeline key the
 * kernel builds, and prints each program's size and its texture and sampler
 * uniforms.  It checks only that the compiler takes them and that the
 * picture's fragment shader reads two textures; what the programs compute
 * is seen on the board.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/gpu/bcm2711/shader.h"
#include <uapi/errno.h>
#include "userland/desktop/videoplayer/shaders.h"

static int failures;

static void compile_stage(const char *name, const uint32_t *words, size_t word_count, enum bcm2711_shader_stage stage, const struct bcm2711_shader_key *key, unsigned textures);

/* Supplies the kernel's zeroing allocation through the host's. */
void *
kern_calloc(
	size_t count,
	size_t bytes)
{
	return calloc(count, bytes);
}

/* Supplies the kernel's allocation through the host's. */
void *
kern_malloc(
	size_t bytes)
{
	return malloc(bytes);
}

/* Supplies the kernel's growing allocation through the host's. */
void *
kern_realloc(
	void *pointer,
	size_t bytes)
{
	return realloc(pointer, bytes);
}

/* Supplies the kernel's release through the host's. */
void
kern_free(
	void *pointer)
{
	free(pointer);
}

/* Compiles every stage and reports one PASS or FAIL line. */
int
main(
	void)
{
	struct bcm2711_shader_key key;

	/* The fragment shaders read one varying of two components at location 0, as the kernel keys it. */
	memset(&key, 0, sizeof(key));
	key.varyings[0].location = 0;
	key.varyings[0].component = 0;
	key.varyings[1].location = 0;
	key.varyings[1].component = 1;
	key.varying_count = 2;

	/* The vertex shader's two native variants, and the two fragment shaders. */
	compile_stage("picture.vert", videoplayer_picture_vert, sizeof(videoplayer_picture_vert) / 4U, BCM2711_SHADER_COORDINATE, &key, 0);
	compile_stage("picture.vert", videoplayer_picture_vert, sizeof(videoplayer_picture_vert) / 4U, BCM2711_SHADER_VERTEX, &key, 0);
	compile_stage("picture.frag", videoplayer_picture_frag, sizeof(videoplayer_picture_frag) / 4U, BCM2711_SHADER_FRAGMENT, &key, 2);
	compile_stage("overlay.frag", videoplayer_overlay_frag, sizeof(videoplayer_overlay_frag) / 4U, BCM2711_SHADER_FRAGMENT, &key, 1);

	/* One line for the runner. */
	if (failures != 0) {
		printf("ws236 p011 videoplayer shaders: FAIL (%d)\n", failures);
		return 1;
	}
	printf("ws236 p011 videoplayer shaders: PASS\n");
	return 0;
}

/* Compiles one stage and checks the textures it reads. */
static void
compile_stage(
	const char *name,
	const uint32_t *words,
	size_t word_count,
	enum bcm2711_shader_stage stage,
	const struct bcm2711_shader_key *key,
	unsigned textures)
{
	struct bcm2711_shader_binary *binary;
	struct bcm2711_shader_diagnostic diagnostic;
	unsigned found;
	unsigned bindings;
	unsigned index;
	int error;

	/* The preflight and the compile, as pipeline creation does them. */
	memset(&diagnostic, 0, sizeof(diagnostic));
	error = bcm2711_shader_preflight(words, word_count, stage, &diagnostic);
	if (error == 0)
		error = bcm2711_shader_compile(words, word_count, stage, key, &binary, &diagnostic);
	if (error != 0) {
		printf("FAIL: %s stage %u refused (%d) at instruction %u: %s\n", name, (unsigned)stage, error, diagnostic.instruction,
		       diagnostic.reason != NULL ? diagnostic.reason : "?");
		failures++;
		return;
	}

	/* The texture uniforms and the distinct bindings they name. */
	found = 0;
	bindings = 0;
	for (index = 0; index < binary->uniform_count; index++) {
		if (binary->uniforms[index].kind == BCM2711_SHADER_TEXTURE) {
			found++;
			bindings |= 1U << binary->uniforms[index].binding;
		}
	}
	printf("%s stage %u: %u instructions, %u uniforms, %u texture uniforms, bindings 0x%x, push %u bytes, threads %u\n",
	       name, (unsigned)stage, binary->code_count, binary->uniform_count, found, bindings, binary->push_bytes, binary->threads);

	/* The fragment shaders read exactly their textures. */
	if ((unsigned)__builtin_popcount(bindings) != textures) {
		printf("FAIL: %s reads %u bindings, not %u\n", name, (unsigned)__builtin_popcount(bindings), textures);
		failures++;
	}
	bcm2711_shader_binary_free(binary);
}
