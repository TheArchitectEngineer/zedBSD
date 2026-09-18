/* WS031 T1: the first textured-draw fixture, generated from the fixed Mesa tree.
 *
 *   PS:  uv = frag_coord.xy / (32, 32);  colour = txl(texture BTI 1, sampler 0, uv, lod 0);
 *        render target 0 (BTI 0) = colour.  It keeps the A64 entry marker of the
 *        single-colour PS (0xc0ffee01 -> 0x100400c10) so "the PS ran" and "the
 *        sample was right" stay separate observations.
 *   Texture: 8x8 2D R8G8B8A8_UNORM, 1 level, linear; layout from isl.
 *   Sampler: nearest, clamp-to-edge, LOD 0, normalised coordinates; packed with genxml gen120.
 *
 * Output (stdout) is a C include consumed by the zedBSD fixture; the numbers on
 * stderr are the human-readable manifest.
 */
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
#include "isl/isl.h"

#define __gen_address_type uint64_t
#define __gen_user_data void
static uint64_t
__gen_combine_address(void *data, void *location, uint64_t address, uint32_t delta)
{
   (void)data; (void)location;
   return address + delta;
}
#include "genxml/genX_helpers.h"
#include "genxml/gen120_pack.h"

#define TEX_W 8
#define TEX_H 8
#define RT_W 32
#define RT_H 32
#define TEX_VA_PLACEHOLDER 0x100404000ull
#define MOCS 6

static void reflog(void *d, unsigned *id, const char *fmt, ...) { (void)d;(void)id;(void)fmt; }

int
main(void)
{
   process_intel_debug_variable();
   void *mem_ctx = ralloc_context(NULL);
   struct intel_device_info devinfo;

   if (!intel_get_device_info_for_build(0x46a8, &devinfo)) {
      fprintf(stderr, "reftex: devinfo failed\n");
      return 1;
   }
   struct brw_compiler *compiler = brw_compiler_create(mem_ctx, &devinfo);
   compiler->shader_debug_log = reflog;
   compiler->shader_perf_log = reflog;

   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT,
      &compiler->nir_options[MESA_SHADER_FRAGMENT],
      "tex_rect");

   nir_store_global(&b, nir_imm_int(&b, 0xc0ffee01),
                    nir_imm_int64(&b, 0x100400c10ull), .align_mul = 4);

   /* Upper-left origin, half-integer pixel centres (the hardware's native payload). */
   /*
    * The front-end form: gl_FragCoord.  floor(gl_FragCoord.xy) is the integer
    * pixel; the compiler's own nir_opt_frag_coord_to_pixel_coord turns it into
    * the payload pixel position (no source depth / W needed), and because the
    * shader reads SYSTEM_VALUE_FRAG_COORD the compiler emits the code that
    * derives per-pixel X/Y from the subspan coordinates in r1.
    *
    * E-103 first attempt: this generator emitted load_pixel_coord directly.
    * brw only emits that X/Y derivation when FRAG_COORD (or an input) is read,
    * so the kernel read an unwritten register and every pixel sampled texel (0,0).
    */
   nir_def *fc = nir_load_frag_coord(&b);
   nir_def *pixel = nir_ffloor(&b, nir_trim_vector(&b, fc, 2));
   nir_def *uv = nir_fmul(&b, nir_fadd(&b, pixel, nir_imm_vec2(&b, 0.5f, 0.5f)),
                          nir_imm_vec2(&b, 1.0f / RT_W, 1.0f / RT_H));

   nir_tex_instr *tex = nir_tex_instr_create(b.shader, 2);
   tex->op = nir_texop_txl;
   tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
   tex->dest_type = nir_type_float32;
   tex->is_array = false;
   tex->coord_components = 2;
   tex->texture_index = 1;      /* binding table entry 1 (entry 0 is the render target) */
   tex->sampler_index = 0;
   tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_coord, uv);
   tex->src[1] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_imm_float(&b, 0.0f));
   nir_def_init(&tex->instr, &tex->def, 4, 32);
   nir_builder_instr_insert(&b, &tex->instr);

   nir_variable *frag_color = nir_variable_create(
      b.shader, nir_var_shader_out, glsl_vec4_type(), "gl_FragColor");
   frag_color->data.location = FRAG_RESULT_DATA0;
   nir_store_var(&b, frag_color, &tex->def, 0xf);

   b.shader->info.outputs_written |= (1ull << FRAG_RESULT_DATA0);
   b.shader->info.num_textures = 2;
   BITSET_SET(b.shader->info.textures_used, 1);
   BITSET_SET(b.shader->info.samplers_used, 0);
   nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
   nir_lower_io_vars_to_temporaries(b.shader, nir_shader_get_entrypoint(b.shader), nir_var_shader_out);

   struct brw_nir_compiler_opts opts;
   memset(&opts, 0, sizeof(opts));
   brw_preprocess_nir(compiler, b.shader, &opts);
   nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));

   struct brw_fs_prog_key wm_key;
   memset(&wm_key, 0, sizeof(wm_key));
   wm_key.nr_color_regions = 1;

   struct brw_fs_prog_data *pd = rzalloc(mem_ctx, struct brw_fs_prog_data);
   struct brw_compile_fs_params params;
   memset(&params, 0, sizeof(params));
   params.base.mem_ctx = mem_ctx;
   params.base.nir = b.shader;
   params.base.key = &wm_key.base;
   params.base.prog_data = (struct brw_stage_prog_data *)pd;

   const unsigned *program = brw_compile_fs(compiler, &params);
   if (program == NULL) {
      fprintf(stderr, "reftex: brw_compile_fs NULL: %s\n", params.base.error_str);
      return 1;
   }

   fprintf(stderr,
      "reftex: PS size=%u d8=%d d16=%d d32=%d off16=%u off32=%u grf_start8=%u grf_start16=%u "
      "grf_start32=%u num_varying=%u persample=%d uses_kill=%d uses_src_depth=%d uses_src_w=%d "
      "uses_pos_offset=%d uses_omask=%d computed_depth=%d has_side_effects=%d total_scratch=%u "
      "push_sizes=%u/%u/%u/%u barycentric_interp_modes=0x%x\n",
      pd->base.program_size, pd->dispatch_8, pd->dispatch_16, pd->dispatch_32,
      pd->prog_offset_16, pd->prog_offset_32,
      pd->base.dispatch_grf_start_reg, pd->dispatch_grf_start_reg_16,
      pd->dispatch_grf_start_reg_32, pd->num_varying_inputs, pd->persample_dispatch,
      pd->uses_kill, pd->uses_src_depth, pd->uses_src_w, pd->uses_pos_offset,
      pd->uses_omask, pd->computed_depth_mode, pd->has_side_effects,
      pd->base.total_scratch, pd->base.push_sizes[0], pd->base.push_sizes[1],
      pd->base.push_sizes[2], pd->base.push_sizes[3], (unsigned)pd->barycentric_interp_modes);

   /* ---- texture layout (isl) and its RENDER_SURFACE_STATE ---- */
   struct isl_device dev;
   isl_device_init(&dev, &devinfo);
   struct isl_surf surf;
   bool ok = isl_surf_init(&dev, &surf,
         .dim = ISL_SURF_DIM_2D, .format = ISL_FORMAT_R8G8B8A8_UNORM,
         .width = TEX_W, .height = TEX_H, .depth = 1,
         .levels = 1, .array_len = 1, .samples = 1,
         .usage = ISL_SURF_USAGE_TEXTURE_BIT,
         .tiling_flags = ISL_TILING_LINEAR_BIT);
   if (!ok) {
      fprintf(stderr, "reftex: isl_surf_init(linear) failed\n");
      return 1;
   }
   fprintf(stderr, "reftex: texture tiling=%d row_pitch_B=%u size_B=%llu alignment_B=%u "
      "image_alignment=%ux%u ss.size=%u ss.align=%u\n",
      surf.tiling, surf.row_pitch_B, (unsigned long long)surf.size_B, surf.alignment_B,
      surf.image_alignment_el.w, surf.image_alignment_el.h, dev.ss.size, dev.ss.align);

   struct isl_view view = {
      .usage = ISL_SURF_USAGE_TEXTURE_BIT,
      .format = ISL_FORMAT_R8G8B8A8_UNORM,
      .base_level = 0, .levels = 1,
      .base_array_layer = 0, .array_len = 1,
      .swizzle = ISL_SWIZZLE_IDENTITY,
   };
   uint32_t rss[16];
   memset(rss, 0, sizeof(rss));
   isl_surf_fill_state(&dev, rss, .surf = &surf, .view = &view,
      .address = TEX_VA_PLACEHOLDER, .mocs = MOCS);

   /* ---- SAMPLER_STATE: nearest, clamp, LOD 0, normalised coordinates ---- */
   uint32_t smp[GFX12_SAMPLER_STATE_length];
   struct GFX12_SAMPLER_STATE ss = { 0 };
   ss.LODPreClampMode = CLAMP_MODE_OGL;     /* what anv and iris program */
   ss.MipModeFilter = MIPFILTER_NONE;
   ss.MagModeFilter = MAPFILTER_NEAREST;
   ss.MinModeFilter = MAPFILTER_NEAREST;
   ss.TCXAddressControlMode = TCM_CLAMP;
   ss.TCYAddressControlMode = TCM_CLAMP;
   ss.TCZAddressControlMode = TCM_CLAMP;
   ss.MinLOD = 0;
   ss.MaxLOD = 0;
   ss.NonnormalizedCoordinateEnable = false;
   ss.MaximumAnisotropy = RATIO21;
   GFX12_SAMPLER_STATE_pack(NULL, smp, &ss);

   /*
    * The bilinear variant (E-105): only the filter changes.  As anv and blorp do
    * for any non-nearest filter, address rounding is enabled for min and mag.
    */
   uint32_t smp_lin[GFX12_SAMPLER_STATE_length];
   struct GFX12_SAMPLER_STATE sl = ss;
   sl.MagModeFilter = MAPFILTER_LINEAR;
   sl.MinModeFilter = MAPFILTER_LINEAR;
   sl.UAddressMinFilterRoundingEnable = true;
   sl.UAddressMagFilterRoundingEnable = true;
   sl.VAddressMinFilterRoundingEnable = true;
   sl.VAddressMagFilterRoundingEnable = true;
   sl.RAddressMinFilterRoundingEnable = true;
   sl.RAddressMagFilterRoundingEnable = true;
   GFX12_SAMPLER_STATE_pack(NULL, smp_lin, &sl);

   /* ---- packet fields that differ from the single-colour draw, packed by genxml ---- */
   uint32_t ps_pkt[GFX12_3DSTATE_PS_length];
   struct GFX12_3DSTATE_PS ps = { GFX12_3DSTATE_PS_header };
   ps.SamplerCount = 1;               /* blorp: "Up to 4 samplers" */
   ps.BindingTableEntryCount = 2;     /* RT + source texture */
   /* The SIMD8 kernel is the one dispatched (KSP0); its payload layout comes from prog_data. */
   ps.DispatchGRFStartRegisterForConstantSetupData0 = pd->base.dispatch_grf_start_reg;
   GFX12_3DSTATE_PS_pack(NULL, ps_pkt, &ps);

   uint32_t psx_pkt[GFX12_3DSTATE_PS_EXTRA_length];
   struct GFX12_3DSTATE_PS_EXTRA psx = { GFX12_3DSTATE_PS_EXTRA_header };
   psx.PixelShaderValid = true;
   psx.PixelShaderHasUAV = true;      /* the A64 entry-marker store (as in the single-colour PS) */
   psx.PixelShaderUsesSourceDepth = pd->uses_src_depth;
   psx.PixelShaderUsesSourceW = pd->uses_src_w;
   psx.PixelShaderKillsPixel = pd->uses_kill;
   psx.oMaskPresenttoRenderTarget = pd->uses_omask;
   psx.PixelShaderIsPerSample = pd->persample_dispatch;
   GFX12_3DSTATE_PS_EXTRA_pack(NULL, psx_pkt, &psx);
   fprintf(stderr, "reftex: 3DSTATE_PS DW7(grf start %u)=0x%08x | 3DSTATE_PS_EXTRA = 0x%08x 0x%08x\n",
      pd->base.dispatch_grf_start_reg, ps_pkt[7], psx_pkt[0], psx_pkt[1]);

   uint32_t ssp_pkt[GFX12_3DSTATE_SAMPLER_STATE_POINTERS_PS_length];
   struct GFX12_3DSTATE_SAMPLER_STATE_POINTERS_PS ssp = {
      GFX12_3DSTATE_SAMPLER_STATE_POINTERS_PS_header
   };
   ssp.PointertoPSSamplerState = 896;  /* TEXFIX_SAMPLER_OFFSET in the dynamic heap */
   GFX12_3DSTATE_SAMPLER_STATE_POINTERS_PS_pack(NULL, ssp_pkt, &ssp);
   fprintf(stderr, "reftex: 3DSTATE_PS header=0x%08x DW3(sampler count 1, bt count 2)=0x%08x | "
      "3DSTATE_SAMPLER_STATE_POINTERS_PS = 0x%08x 0x%08x\n",
      ps_pkt[0], ps_pkt[3], ssp_pkt[0], ssp_pkt[1]);

   /* ---- the C include ---- */
   printf("#define TEXFIX_3DSTATE_PS_DW3 0x%08xU\n", ps_pkt[3]);
   printf("#define TEXFIX_3DSTATE_PS_DW7 0x%08xU\n", ps_pkt[7]);
   printf("#define TEXFIX_3DSTATE_PS_EXTRA_DW0 0x%08xU\n#define TEXFIX_3DSTATE_PS_EXTRA_DW1 0x%08xU\n",
      psx_pkt[0], psx_pkt[1]);
   printf("#define TEXFIX_SAMPLER_OFFSET 896U\n");
   printf("#define TEXFIX_SAMPLER_POINTERS_PS_DW0 0x%08xU\n#define TEXFIX_SAMPLER_POINTERS_PS_DW1 0x%08xU\n",
      ssp_pkt[0], ssp_pkt[1]);
   const unsigned char *bytes = (const unsigned char *)program;
   unsigned nd = (pd->base.program_size + 3) / 4;
   printf("/* Generated by plan/ws031/mesa-refs reftex.c (brw_compile_fs + isl + genxml gen120, ADL-P 0x46a8). Do not edit. */\n");
   printf("#define TEXFIX_PS_BYTES %uU\n", pd->base.program_size);
   printf("#define TEXFIX_PS_DISPATCH_8 %dU\n#define TEXFIX_PS_DISPATCH_16 %dU\n#define TEXFIX_PS_DISPATCH_32 %dU\n",
      pd->dispatch_8, pd->dispatch_16, pd->dispatch_32);
   printf("#define TEXFIX_PS_OFFSET_16 %uU\n", pd->prog_offset_16);
   printf("#define TEXFIX_PS_GRF_START_8 %uU\n#define TEXFIX_PS_GRF_START_16 %uU\n",
      pd->base.dispatch_grf_start_reg, pd->dispatch_grf_start_reg_16);
   printf("#define TEXFIX_PS_NUM_VARYING %uU\n#define TEXFIX_PS_USES_SRC_DEPTH %dU\n#define TEXFIX_PS_USES_SRC_W %dU\n"
          "#define TEXFIX_PS_USES_POS_OFFSET %dU\n#define TEXFIX_PS_TOTAL_SCRATCH %uU\n#define TEXFIX_PS_PUSH_SIZE0 %uU\n",
      pd->num_varying_inputs, pd->uses_src_depth, pd->uses_src_w, pd->uses_pos_offset,
      pd->base.total_scratch, pd->base.push_sizes[0]);
   printf("#define TEXFIX_TEX_WIDTH %dU\n#define TEXFIX_TEX_HEIGHT %dU\n#define TEXFIX_TEX_ROW_PITCH %uU\n"
          "#define TEXFIX_TEX_SIZE %lluU\n#define TEXFIX_TEX_ALIGNMENT %uU\n#define TEXFIX_TEX_VA_PLACEHOLDER 0x%llxULL\n",
      TEX_W, TEX_H, surf.row_pitch_B, (unsigned long long)surf.size_B, surf.alignment_B,
      (unsigned long long)TEX_VA_PLACEHOLDER);
   printf("static const uint32_t texfix_ps[%u] = {", nd);
   for (unsigned i = 0; i < nd; i++) {
      uint32_t w = 0;
      for (unsigned k = 0; k < 4 && i * 4 + k < pd->base.program_size; k++)
         w |= (uint32_t)bytes[i * 4 + k] << (8 * k);
      printf("%s0x%08xU,", (i % 4) == 0 ? "\n\t" : " ", w);
   }
   printf("\n};\n");
   printf("/* RENDER_SURFACE_STATE of the texture (isl_surf_fill_state, address = placeholder, mocs %d). */\n", MOCS);
   printf("static const uint32_t texfix_tex_rss[16] = {");
   for (unsigned i = 0; i < 16; i++)
      printf("%s0x%08xU,", (i % 4) == 0 ? "\n\t" : " ", rss[i]);
   printf("\n};\n");
   printf("/* SAMPLER_STATE: nearest/nearest, no mip, clamp, LOD [0,0], normalised. */\n");
   printf("static const uint32_t texfix_sampler[%u] = {\n\t", (unsigned)GFX12_SAMPLER_STATE_length);
   for (unsigned i = 0; i < GFX12_SAMPLER_STATE_length; i++)
      printf("0x%08xU, ", smp[i]);
   printf("\n};\n");

   printf("/* SAMPLER_STATE, bilinear: linear/linear, address rounding on (anv/blorp), otherwise identical. */\n");
   printf("static const uint32_t texfix_sampler_linear[%u] = {\n\t", (unsigned)GFX12_SAMPLER_STATE_length);
   for (unsigned i = 0; i < GFX12_SAMPLER_STATE_length; i++)
      printf("0x%08xU, ", smp_lin[i]);
   printf("\n};\n");

   /* raw PS for the disassembler */
   FILE *f = fopen("reftex_ps.bin", "wb");
   if (f) { fwrite(bytes, 1, pd->base.program_size, f); fclose(f); }

   ralloc_free(mem_ctx);
   return 0;
}
