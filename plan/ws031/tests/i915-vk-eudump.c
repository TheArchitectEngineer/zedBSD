/*
 * WS031 E-128: host tool -- runs the executor's own compiler (spirv.c -> compile.c -> eu.c) on a SPIR-V
 * module and writes the kernel it produces, for Mesa's gentool to disassemble / re-assemble.
 *   i915-vk-eudump vertex|fragment module.spv kernel.bin
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *kern_calloc(size_t count, size_t size) { return calloc(count, size); }
void kern_free(void *pointer) { free(pointer); }

#include "../../../src/drivers/gpu/i915/vk/spirv.c"
#include "../../../src/drivers/gpu/i915/vk/eu.c"
#include "../../../src/drivers/gpu/i915/vk/compile.c"

int
main(int argc, char **argv)
{
	struct i915_vk_shader_ir *ir;
	struct i915_vk_shader_binary *binary;
	struct i915_vk_spirv_diag diag;
	uint32_t *words;
	long bytes;
	FILE *f;
	int error;

	if (argc != 4)
		return 2;
	f = fopen(argv[2], "rb");
	if (f == NULL)
		return 2;
	fseek(f, 0, SEEK_END);
	bytes = ftell(f);
	fseek(f, 0, SEEK_SET);
	words = malloc((size_t)bytes);
	if (fread(words, 1, (size_t)bytes, f) != (size_t)bytes)
		return 2;
	fclose(f);

	memset(&diag, 0, sizeof(diag));
	error = i915_vk_spirv_parse_diag(words, (size_t)bytes / 4U,
		strcmp(argv[1], "vertex") == 0 ? I915_VK_STAGE_VERTEX : I915_VK_STAGE_FRAGMENT, &ir, &diag);
	if (error != 0) {
		fprintf(stderr, "parse: error %d (%s, opcode %u at word %u)\n", error,
			diag.reason != NULL ? diag.reason : "?", diag.opcode, diag.word_offset);
		return 1;
	}
	error = i915_vk_compile(NULL, ir, &binary);
	if (error != 0) {
		fprintf(stderr, "compile: error %d\n", error);
		return 1;
	}
	fprintf(stderr, "%s: %u IR instructions, %u bytes of kernel, grf_used %u\n", argv[1],
		ir->instruction_count, binary->code_bytes, binary->grf_used);
	f = fopen(argv[3], "wb");
	fwrite(binary->code, 1, binary->code_bytes, f);
	fclose(f);
	return 0;
}
