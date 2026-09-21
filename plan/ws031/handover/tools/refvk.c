/* WS031 E-127: reference kernels for a Vulkan vertex + fragment shader pair, from the fixed Mesa tree.
 *
 *   refvk <vertex.spv> <fragment.spv> > vkref-generated.inc
 *
 * The two SPIR-V modules are the application's own (userland/base/vkdemo/shaders/cuboid.{vert,frag}.spv);
 * nothing about them is rewritten.  They go through spirv_to_nir and the same early passes as
 * vk_spirv_to_nir / anv_shader_preprocess_nir, then brw_compile_vs / brw_compile_fs for ADL-P.
 * Where anv applies its pipeline layout, this tool applies the executor's:
 *   - push constants: one block, byte 0 of the block = byte 0 of the push buffer (load_push_data_intel);
 *   - the one combined image sampler: binding table entry 1 (entry 0 is the render target), sampler 0.
 *
 * Output (stdout): a C include with the kernels, the words of every packet that depends on prog_data
 * (3DSTATE_VS, 3DSTATE_PS, 3DSTATE_PS_EXTRA, 3DSTATE_WM, 3DSTATE_SBE, 3DSTATE_SBE_SWIZ, 3DSTATE_CLIP bits) and
 * the vertex-element order.  stderr: the human-readable manifest.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GFX_VERx10 120
#include "genxml/gen_macros.h"
#include "dev/intel_device_info.h"
bool intel_get_device_info_for_build(int, struct intel_device_info *);
#include "compiler/brw/brw_compiler.h"
#include "compiler/brw/brw_nir.h"
#include "compiler/brw/brw_private.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/spirv/nir_spirv.h"
#include "compiler/spirv/spirv_info.h"
#include "compiler/glsl_types.h"
#include "util/ralloc.h"
#include "dev/intel_debug.h"

#define __gen_address_type uint64_t
#define __gen_user_data void
static uint64_t
__gen_combine_address(void *data, void *location, uint64_t address, uint32_t delta)
{
   (void)data; (void)location;
   return address + delta;
}
#include "genxml/genX_pack.h"
#include "common/intel_genX_state_brw.h"

/* Where the executor puts the two kernels in its instruction heap (shared with gfx-draw.c). */
#define VS_KERNEL_OFFSET 0u
#define PS_KERNEL_OFFSET 4096u

static void reflog(void *d, unsigned *id, const char *fmt, ...) { (void)d;(void)id;(void)fmt; }

static uint32_t *
read_words(const char *path, size_t *count)
{
   FILE *f = fopen(path, "rb");
   if (f == NULL) { perror(path); exit(1); }
   fseek(f, 0, SEEK_END);
   long bytes = ftell(f);
   fseek(f, 0, SEEK_SET);
   uint32_t *words = malloc((size_t)bytes);
   if (fread(words, 1, (size_t)bytes, f) != (size_t)bytes) { perror(path); exit(1); }
   fclose(f);
   *count = (size_t)bytes / 4;
   return words;
}

/* load_push_constant -> load_push_data_intel, byte 0 of the block at byte 0 of the push buffer. */
static bool
lower_push(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   unsigned *max_byte = data;
   if (intrin->intrinsic != nir_intrinsic_load_push_constant)
      return false;
   b->cursor = nir_before_instr(&intrin->instr);
   nir_def *v = nir_load_push_data_intel(b, intrin->def.num_components, intrin->def.bit_size,
                                         intrin->src[0].ssa,
                                         .base = nir_intrinsic_base(intrin),
                                         .range = nir_intrinsic_range(intrin));
   unsigned end = nir_intrinsic_base(intrin) + nir_intrinsic_range(intrin);
   if (end > *max_byte)
      *max_byte = end;
   nir_def_replace(&intrin->def, v);
   return true;
}

/* The one combined image sampler: binding table entry 1, sampler 0. */
static bool
lower_tex(nir_builder *b, nir_instr *instr, void *data)
{
   (void)b; (void)data;
   if (instr->type != nir_instr_type_tex)
      return false;
   nir_tex_instr *tex = nir_instr_as_tex(instr);
   int i;
   while ((i = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref)) >= 0)
      nir_tex_instr_remove_src(tex, i);
   while ((i = nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref)) >= 0)
      nir_tex_instr_remove_src(tex, i);
   tex->texture_index = 1;
   tex->sampler_index = 0;
   return true;
}

static nir_shader *
load(const struct brw_compiler *compiler, void *mem_ctx, const char *path, mesa_shader_stage stage,
     unsigned *push_bytes)
{
   size_t count;
   uint32_t *words = read_words(path, &count);

   static struct spirv_capabilities caps;
   memset(&caps, 0, sizeof(caps));
   caps.Shader = true;
   caps.Matrix = true;
   caps.Sampled1D = true;
   caps.ClipDistance = true;
   caps.CullDistance = true;

   struct spirv_to_nir_options opt;
   memset(&opt, 0, sizeof(opt));
   opt.capabilities = &caps;
   opt.ubo_addr_format = nir_address_format_32bit_index_offset;
   opt.ssbo_addr_format = nir_address_format_32bit_index_offset;
   opt.phys_ssbo_addr_format = nir_address_format_64bit_global;
   opt.push_const_addr_format = nir_address_format_logical;
   opt.shared_addr_format = nir_address_format_32bit_offset;

   nir_shader *nir = spirv_to_nir(words, count, NULL, stage, "main", &opt, &compiler->nir_options[stage]);
   if (nir == NULL) { fprintf(stderr, "refvk: spirv_to_nir failed for %s\n", path); exit(1); }
   ralloc_steal(mem_ctx, nir);

   /* vk_spirv_to_nir() */
   NIR_PASS(_, nir, nir_lower_variable_initializers, nir_var_function_temp);
   NIR_PASS(_, nir, nir_lower_returns);
   NIR_PASS(_, nir, nir_inline_functions);
   NIR_PASS(_, nir, nir_opt_copy_prop);
   NIR_PASS(_, nir, nir_opt_constant_folding);
   NIR_PASS(_, nir, nir_opt_deref);
   nir_remove_non_cmat_call_entrypoints(nir);
   NIR_PASS(_, nir, nir_lower_variable_initializers, ~0);
   NIR_PASS(_, nir, nir_split_var_copies);
   NIR_PASS(_, nir, nir_split_per_member_structs);
   NIR_PASS(_, nir, nir_remove_dead_variables,
            nir_var_shader_in | nir_var_shader_out | nir_var_system_value, NULL);
   nir_gather_clip_cull_distance_sizes_from_vars(nir);
   NIR_PASS(_, nir, nir_merge_clip_cull_distance_vars);
   NIR_PASS(_, nir, nir_propagate_invariant, false);

   /* anv_shader_preprocess_nir() */
   NIR_PASS(_, nir, nir_lower_io_vars_to_temporaries, nir_shader_get_entrypoint(nir), nir_var_shader_out);
   struct brw_nir_compiler_opts opts;
   memset(&opts, 0, sizeof(opts));
   brw_preprocess_nir(compiler, nir, &opts);

   /* the executor's "pipeline layout" */
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_push_const, nir_address_format_32bit_offset);
   *push_bytes = 0;
   NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_push, nir_metadata_control_flow, push_bytes);
   NIR_PASS(_, nir, nir_shader_instructions_pass, lower_tex, nir_metadata_control_flow, NULL);
   if (stage == MESA_SHADER_FRAGMENT) {
      nir->info.num_textures = 2;
      BITSET_SET(nir->info.textures_used, 1);
      BITSET_SET(nir->info.samplers_used, 0);
   }
   NIR_PASS(_, nir, nir_opt_dce);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   free(words);
   return nir;
}

static void
print_words(const char *name, const uint32_t *w, unsigned n)
{
   printf("static const uint32_t %s[%u] = {", name, n);
   for (unsigned i = 0; i < n; i++)
      printf("%s0x%08xU,", i % 4 == 0 ? "\n\t" : " ", w[i]);
   printf("\n};\n");
}

static void
print_sha256(const char *macro, const char *path)
{
   /* identity of the input, as a list of its words' count and an FNV-1a of its bytes (the executor checks both) */
   size_t count;
   uint32_t *words = read_words(path, &count);
   uint32_t h = 2166136261u;
   const unsigned char *p = (const unsigned char *)words;
   for (size_t i = 0; i < count * 4; i++) { h ^= p[i]; h *= 16777619u; }
   printf("#define %s_WORDS %zuU\n#define %s_FNV1A 0x%08xU\n", macro, count, macro, h);
   free(words);
}

int
main(int argc, char **argv)
{
   if (argc != 3) { fprintf(stderr, "usage: refvk vertex.spv fragment.spv\n"); return 1; }
   process_intel_debug_variable();
   glsl_type_singleton_init_or_ref();
   void *mem_ctx = ralloc_context(NULL);
   struct intel_device_info devinfo;
   if (!intel_get_device_info_for_build(0x46a8, &devinfo)) { fprintf(stderr, "refvk: devinfo failed\n"); return 1; }
   struct brw_compiler *compiler = brw_compiler_create(mem_ctx, &devinfo);
   compiler->shader_debug_log = reflog;
   compiler->shader_perf_log = reflog;

   /* ---------------- vertex ---------------- */
   unsigned vs_push = 0;
   nir_shader *vs_nir = load(compiler, mem_ctx, argv[1], MESA_SHADER_VERTEX, &vs_push);
   uint64_t vs_inputs = vs_nir->info.inputs_read;

   struct brw_vs_prog_key vs_key;
   memset(&vs_key, 0, sizeof(vs_key));
   vs_key.base.vue_layout = INTEL_VUE_LAYOUT_FIXED;
   vs_key.max_payload_percent = 90;   /* iris; the attributes of this shader are pushed in the payload */
   struct brw_vs_prog_data *vs_pd = rzalloc(mem_ctx, struct brw_vs_prog_data);
   vs_pd->base.base.push_sizes[0] = (vs_push + 31u) & ~31u;
   struct brw_compile_vs_params vs_params;
   memset(&vs_params, 0, sizeof(vs_params));
   vs_params.base.mem_ctx = mem_ctx;
   vs_params.base.nir = vs_nir;
   vs_params.base.key = &vs_key.base;
   vs_params.base.prog_data = &vs_pd->base.base;
   const unsigned *vs_program = brw_compile_vs(compiler, &vs_params);
   if (vs_program == NULL) { fprintf(stderr, "refvk: brw_compile_vs: %s\n", vs_params.base.error_str); return 1; }
   vs_pd->base.base.push_sizes[0] = (vs_push + 31u) & ~31u;

   const struct intel_vue_map *vue = &vs_pd->base.vue_map;
   fprintf(stderr, "refvk: VS size=%u grf_start=%u urb_read_length=%u inputs_read=0x%llx push=%u bytes "
      "vue slots=%d (entry size %u x 64 B) dispatch_mode=%d\n",
      vs_pd->base.base.program_size, vs_pd->base.base.dispatch_grf_start_reg, vs_pd->base.urb_read_length,
      (unsigned long long)vs_inputs, vs_push, vue->num_slots,
      (unsigned)((vue->num_slots * 16u + 63u) / 64u), (int)vs_pd->base.dispatch_mode);
   for (int s = 0; s < vue->num_slots; s++)
      fprintf(stderr, "refvk:   vue slot %d = varying %d\n", s, vue->slot_to_varying[s]);

   /* ---------------- fragment ---------------- */
   unsigned fs_push = 0;
   nir_shader *fs_nir = load(compiler, mem_ctx, argv[2], MESA_SHADER_FRAGMENT, &fs_push);

   struct brw_fs_prog_key fs_key;
   memset(&fs_key, 0, sizeof(fs_key));
   fs_key.base.vue_layout = INTEL_VUE_LAYOUT_FIXED;
   fs_key.nr_color_regions = 1;
   struct brw_fs_prog_data *fs_pd = rzalloc(mem_ctx, struct brw_fs_prog_data);
   fs_pd->base.push_sizes[0] = (fs_push + 31u) & ~31u;
   struct brw_compile_fs_params fs_params;
   memset(&fs_params, 0, sizeof(fs_params));
   fs_params.base.mem_ctx = mem_ctx;
   fs_params.base.nir = fs_nir;
   fs_params.base.key = &fs_key.base;
   fs_params.base.prog_data = &fs_pd->base;
   fs_params.max_polygons = 1;
   const unsigned *fs_program = brw_compile_fs(compiler, &fs_params);
   if (fs_program == NULL) { fprintf(stderr, "refvk: brw_compile_fs: %s\n", fs_params.base.error_str); return 1; }
   fs_pd->base.push_sizes[0] = (fs_push + 31u) & ~31u;

   fprintf(stderr, "refvk: PS size=%u d8=%d d16=%d d32=%d multi=%d off16=%u off32=%u grf8=%u grf16=%u grf32=%u "
      "num_varying=%u uses_src_depth=%d uses_src_w=%d bary_modes=0x%x push=%u early_tests=%d side_effects=%d\n",
      fs_pd->base.program_size, fs_pd->dispatch_8, fs_pd->dispatch_16, fs_pd->dispatch_32, fs_pd->dispatch_multi,
      fs_pd->prog_offset_16, fs_pd->prog_offset_32, fs_pd->base.dispatch_grf_start_reg,
      fs_pd->dispatch_grf_start_reg_16, fs_pd->dispatch_grf_start_reg_32, fs_pd->num_varying_inputs,
      fs_pd->uses_src_depth, fs_pd->uses_src_w, (unsigned)fs_pd->barycentric_interp_modes, fs_push,
      fs_pd->early_fragment_tests, fs_pd->has_side_effects);

   /* ---------------- packets that depend on prog_data (anv: genX_shader.c, genX_gfx_state.c) ---------------- */
   uint32_t vs_pkt[GENX(3DSTATE_VS_length)];
   struct GENX(3DSTATE_VS) vs = { GENX(3DSTATE_VS_header) };
   vs.Enable = true;
   vs.StatisticsEnable = true;
   vs.KernelStartPointer = VS_KERNEL_OFFSET;
   vs.SIMD8DispatchEnable = vs_pd->base.dispatch_mode == DISPATCH_MODE_SIMD8;
   vs.VectorMaskEnable = false;
   vs.SamplerCount = 0;
   vs.BindingTableEntryCount = 0;
   vs.FloatingPointMode = IEEE754;
   vs.MaximumNumberofThreads = devinfo.max_vs_threads - 1;
   vs.VertexURBEntryReadLength = vs_pd->base.urb_read_length;
   vs.VertexURBEntryReadOffset = 0;
   vs.DispatchGRFStartRegisterForURBData = vs_pd->base.base.dispatch_grf_start_reg;
   vs.UserClipDistanceClipTestEnableBitmask = vs_pd->base.clip_distance_mask;
   vs.UserClipDistanceCullTestEnableBitmask = vs_pd->base.cull_distance_mask;
   GENX(3DSTATE_VS_pack)(NULL, vs_pkt, &vs);

   uint32_t ps_pkt[GENX(3DSTATE_PS_length)];
   struct GENX(3DSTATE_PS) ps = { GENX(3DSTATE_PS_header) };
   intel_set_ps_dispatch_state(&ps, &devinfo, fs_pd, 1, 0);
   ps.KernelStartPointer0 = PS_KERNEL_OFFSET + brw_fs_prog_data_prog_offset(fs_pd, ps, 0);
   ps.KernelStartPointer1 = PS_KERNEL_OFFSET + brw_fs_prog_data_prog_offset(fs_pd, ps, 1);
   ps.KernelStartPointer2 = PS_KERNEL_OFFSET + brw_fs_prog_data_prog_offset(fs_pd, ps, 2);
   ps.DispatchGRFStartRegisterForConstantSetupData0 = brw_fs_prog_data_dispatch_grf_start_reg(fs_pd, ps, 0);
   ps.DispatchGRFStartRegisterForConstantSetupData1 = brw_fs_prog_data_dispatch_grf_start_reg(fs_pd, ps, 1);
   ps.DispatchGRFStartRegisterForConstantSetupData2 = brw_fs_prog_data_dispatch_grf_start_reg(fs_pd, ps, 2);
   ps.PositionXYOffsetSelect = !fs_pd->uses_pos_offset ? POSOFFSET_NONE :
      fs_pd->persample_dispatch ? POSOFFSET_SAMPLE : POSOFFSET_CENTROID;
   ps.DualSIMD8DispatchEnable = fs_pd->dispatch_multi;
   ps.VectorMaskEnable = fs_pd->uses_vmask;
   ps.SamplerCount = 1;               /* "up to 4" */
   ps.BindingTableEntryCount = 2;     /* render target + texture */
   ps.PushConstantEnable = fs_pd->base.push_sizes[0] > 0;
   ps.MaximumNumberofThreadsPerPSD = devinfo.max_threads_per_psd - 1;
   GENX(3DSTATE_PS_pack)(NULL, ps_pkt, &ps);

   uint32_t psx_pkt[GENX(3DSTATE_PS_EXTRA_length)];
   struct GENX(3DSTATE_PS_EXTRA) psx = { GENX(3DSTATE_PS_EXTRA_header) };
   psx.PixelShaderValid = true;
   psx.AttributeEnable = fs_pd->num_varying_inputs > 0;
   psx.oMaskPresenttoRenderTarget = fs_pd->uses_omask;
   psx.PixelShaderComputedDepthMode = fs_pd->computed_depth_mode;
   psx.PixelShaderUsesSourceDepth = fs_pd->uses_src_depth;
   psx.PixelShaderUsesSourceW = fs_pd->uses_src_w;
   psx.PixelShaderComputesStencil = fs_pd->computed_stencil;
   psx.PixelShaderPullsBary = fs_pd->pulls_bary;
   psx.PixelShaderRequiresSubpixelSampleOffsets = fs_pd->uses_sample_offsets;
   psx.PixelShaderRequiresNonPerspectiveBaryPlaneCoefficients = fs_pd->uses_npc_bary_coefficients;
   psx.PixelShaderRequiresPerspectiveBaryPlaneCoefficients = fs_pd->uses_pc_bary_coefficients;
   psx.PixelShaderRequiresSourceDepthandorWPlaneCoefficients = fs_pd->uses_depth_w_coefficients;
   psx.PixelShaderKillsPixel = fs_pd->uses_kill;
   psx.PixelShaderIsPerSample = fs_pd->persample_dispatch;
   GENX(3DSTATE_PS_EXTRA_pack)(NULL, psx_pkt, &psx);

   uint32_t wm_pkt[GENX(3DSTATE_WM_length)];
   struct GENX(3DSTATE_WM) wm = { GENX(3DSTATE_WM_header) };
   wm.StatisticsEnable = true;
   wm.LineEndCapAntialiasingRegionWidth = _05pixels;
   wm.LineAntialiasingRegionWidth = _10pixels;
   wm.PointRasterizationRule = RASTRULE_UPPER_LEFT;
   wm.EarlyDepthStencilControl = fs_pd->early_fragment_tests ? EDSC_PREPS :
      fs_pd->has_side_effects ? EDSC_PSEXEC : EDSC_NORMAL;
   wm.BarycentricInterpolationMode = fs_pd->barycentric_interp_modes;
   GENX(3DSTATE_WM_pack)(NULL, wm_pkt, &wm);

   uint32_t read_offset, read_length, varyings, prim_id, flat;
   brw_compute_sbe_per_vertex_urb_read(vue, false, false, fs_pd, &read_offset, &read_length, &varyings, &prim_id, &flat);
   fprintf(stderr, "refvk: SBE read_offset=%u read_length=%u varyings=%u flat=0x%x\n", read_offset, read_length, varyings, flat);

   uint32_t sbe_pkt[GENX(3DSTATE_SBE_length)];
   struct GENX(3DSTATE_SBE) sbe = { GENX(3DSTATE_SBE_header) };
   sbe.AttributeSwizzleEnable = true;
   sbe.PointSpriteTextureCoordinateOrigin = UPPERLEFT;
   sbe.NumberofSFOutputAttributes = varyings;
   sbe.ConstantInterpolationEnable = flat;
   sbe.VertexURBEntryReadOffset = read_offset;
   sbe.VertexURBEntryReadLength = read_length;
   sbe.ForceVertexURBEntryReadOffset = true;
   sbe.ForceVertexURBEntryReadLength = true;
   for (unsigned i = 0; i < 32; i++)
      sbe.AttributeActiveComponentFormat[i] = ACF_XYZW;
   GENX(3DSTATE_SBE_pack)(NULL, sbe_pkt, &sbe);

   uint32_t swiz_pkt[GENX(3DSTATE_SBE_SWIZ_length)];
   struct GENX(3DSTATE_SBE_SWIZ) swiz = { GENX(3DSTATE_SBE_SWIZ_header) };
   for (unsigned idx = 0; idx < fs_pd->urb_setup_attribs_count; idx++) {
      int attr = fs_pd->urb_setup_attribs[idx];
      int input_index = fs_pd->urb_setup[attr];
      int slot = vue->varying_to_slot[attr];
      if (slot < 0 || input_index < 0 || input_index >= 16)
         continue;
      swiz.Attribute[input_index].SourceAttribute = slot - 2 * (int)read_offset;
      fprintf(stderr, "refvk:   fs input %d = varying %d = vue slot %d -> source attribute %d\n",
         input_index, attr, slot, slot - 2 * (int)read_offset);
   }
   GENX(3DSTATE_SBE_SWIZ_pack)(NULL, swiz_pkt, &swiz);

   /* ---------------- the C include ---------------- */
   printf("/*\n * SPDX-License-Identifier: MIT\n *\n * GENERATED FILE - WS031 E-127 reference kernels.  Do not edit by hand.\n *\n"
          " * Generator : plan/ws031/handover/tools/refvk.c (built inside the Mesa tree as\n"
          " *             src/intel/compiler/brw/refvk.c; zedBSD project code)\n"
          " * Inputs    : Mesa main @ ab691a1cc7bcd264bec8f735deb2127861ad15ef (MIT): spirv_to_nir, brw_compile_vs,\n"
          " *             brw_compile_fs, genxml gen120; device ADL-P 0x46a8.\n"
          " *             userland/base/vkdemo/shaders/cuboid.vert.spv, cuboid.frag.spv (unchanged).\n */\n");
   print_sha256("VKREF_VS_SPIRV", argv[1]);
   print_sha256("VKREF_FS_SPIRV", argv[2]);
   printf("#define VKREF_VS_KERNEL_OFFSET %uU\n#define VKREF_PS_KERNEL_OFFSET %uU\n", VS_KERNEL_OFFSET, PS_KERNEL_OFFSET);
   printf("#define VKREF_VS_BYTES %uU\n#define VKREF_PS_BYTES %uU\n", vs_pd->base.base.program_size, fs_pd->base.program_size);
   printf("#define VKREF_VS_PUSH_BYTES %uU\n#define VKREF_FS_PUSH_BYTES %uU\n", vs_push, fs_push);
   printf("#define VKREF_VS_PUSH_REGS %uU\n#define VKREF_FS_PUSH_REGS %uU\n",
      vs_pd->base.base.push_sizes[0] / 32u, fs_pd->base.push_sizes[0] / 32u);
   printf("#define VKREF_VS_URB_READ_LENGTH %uU\n", vs_pd->base.urb_read_length);
   printf("#define VKREF_VS_URB_ENTRY_SIZE %uU\n", (unsigned)((vue->num_slots * 16u + 63u) / 64u));
   printf("#define VKREF_VS_INPUTS_READ 0x%llxULL   /* VERT_ATTRIB_GENERIC0 = bit %d: location n = bit %d + n */\n",
      (unsigned long long)vs_inputs, VERT_ATTRIB_GENERIC0, VERT_ATTRIB_GENERIC0);
   printf("#define VKREF_VERT_ATTRIB_GENERIC0 %dU\n", VERT_ATTRIB_GENERIC0);
   print_words("vkref_vs_kernel", (const uint32_t *)vs_program, (vs_pd->base.base.program_size + 3) / 4);
   print_words("vkref_ps_kernel", (const uint32_t *)fs_program, (fs_pd->base.program_size + 3) / 4);
   print_words("vkref_3dstate_vs", vs_pkt, GENX(3DSTATE_VS_length));
   print_words("vkref_3dstate_ps", ps_pkt, GENX(3DSTATE_PS_length));
   print_words("vkref_3dstate_ps_extra", psx_pkt, GENX(3DSTATE_PS_EXTRA_length));
   print_words("vkref_3dstate_wm", wm_pkt, GENX(3DSTATE_WM_length));
   print_words("vkref_3dstate_sbe", sbe_pkt, GENX(3DSTATE_SBE_length));
   print_words("vkref_3dstate_sbe_swiz", swiz_pkt, GENX(3DSTATE_SBE_SWIZ_length));

   ralloc_free(mem_ctx);
   return 0;
}
