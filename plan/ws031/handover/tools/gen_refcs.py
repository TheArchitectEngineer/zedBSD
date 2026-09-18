import os
M = os.path.expanduser("~/zedBSD/plan/ws031/mesa-refs/mesa")
dst = os.path.join(M, "src/intel/compiler/brw/refcs.c")

body = r'''/* Standalone: compile a minimal compute shader with one A64 data-cache store
 * to Gen12 (ADL-P) ISA, print the bytes as hex and the prog_data the driver
 * needs to program MEDIA_VFE_STATE / INTERFACE_DESCRIPTOR_DATA / GPGPU_WALKER. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dev/intel_device_info.h"
bool intel_get_device_info_for_build(int, struct intel_device_info *);
#include "compiler/brw/brw_compiler.h"
#include "compiler/brw/brw_nir.h"
#include "compiler/brw/brw_private.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/glsl_types.h"
#include "util/ralloc.h"
#include "dev/intel_debug.h"

static void reflog(void *d, unsigned *id, const char *fmt, ...) { (void)d;(void)id;(void)fmt; }

int
main(void)
{
   process_intel_debug_variable();
   void *mem_ctx = ralloc_context(NULL);
   struct intel_device_info devinfo;
   if (!intel_get_device_info_for_build(0x46a8, &devinfo)) {
      fprintf(stderr, "refcs: devinfo failed\n");
      return 1;
   }
   struct brw_compiler *compiler = brw_compiler_create(mem_ctx, &devinfo);
   compiler->shader_debug_log = reflog;
   compiler->shader_perf_log = reflog;

   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE,
      &compiler->nir_options[MESA_SHADER_COMPUTE],
      "marker_cs");
   b.shader->info.workgroup_size[0] = 1;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;

   /* One A64 data-cache store of the tag to the compute marker VA. */
   nir_store_global(&b, nir_imm_int(&b, 0xc0ffee02),
                    nir_imm_int64(&b, 0x100400c20ull), .align_mul = 4);

   nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));

   struct brw_nir_compiler_opts opts;
   memset(&opts, 0, sizeof(opts));
   brw_preprocess_nir(compiler, b.shader, &opts);
   nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));

   struct brw_cs_prog_key key;
   memset(&key, 0, sizeof(key));

   struct brw_cs_prog_data *pd = rzalloc(mem_ctx, struct brw_cs_prog_data);

   struct brw_compile_cs_params params;
   memset(&params, 0, sizeof(params));
   params.base.mem_ctx = mem_ctx;
   params.base.nir = b.shader;
   params.base.key = &key.base;
   params.base.prog_data = (struct brw_stage_prog_data *)pd;

   const unsigned *program = brw_compile_cs(compiler, &params);
   if (program == NULL) {
      fprintf(stderr, "refcs: brw_compile_cs NULL: %s\n", params.base.error_str);
      return 1;
   }

   fprintf(stderr,
      "refcs: size=%u grf_used=%u prog_mask=0x%x off8=%u off16=%u off32=%u "
      "local=%u,%u,%u uses_barrier=%d uses_sampler=%d\n",
      pd->base.program_size, pd->base.grf_used, pd->prog_mask,
      pd->prog_offset[0], pd->prog_offset[1], pd->prog_offset[2],
      pd->local_size[0], pd->local_size[1], pd->local_size[2],
      pd->uses_barrier, pd->uses_sampler);

   const unsigned char *bytes = (const unsigned char *)program;
   for (unsigned i = 0; i < pd->base.program_size; i++)
      printf("%02x", bytes[i]);
   printf("\n");

   ralloc_free(mem_ctx);
   return 0;
}
'''
open(dst, "w").write(body)
print("wrote", dst)

mb = os.path.join(M, "src/intel/compiler/brw/meson.build")
m = open(mb).read()
if "refcs" not in m:
    ref_block = ("refps = executable(\n  'refps',\n  'refps.c',\n"
                 "  include_directories : [inc_include, inc_src, inc_intel, inc_intel_compiler],\n"
                 "  link_with : libisl,\n  c_args : ['-fsanitize=address', '-g', '-O0'],\n"
                 "  link_args : ['-fsanitize=address'],\n"
                 "  dependencies : [idep_nir, idep_mesautil, idep_intel_dev, idep_intel_compiler_brw],\n"
                 "  install : false,\n)\n")
    assert ref_block in m, "refps meson block not found verbatim"
    cs_block = ref_block.replace("refps = executable(\n  'refps',\n  'refps.c',",
                                 "refcs = executable(\n  'refcs',\n  'refcs.c',")
    m = m.replace(ref_block, ref_block + "\n" + cs_block, 1)
    open(mb, "w").write(m)
    print("registered refcs in meson")
else:
    print("refcs already in meson")
