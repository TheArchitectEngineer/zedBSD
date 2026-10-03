#!/usr/bin/env python3
"""WS031 E-118: make tools/reftex.c take the render-target size from argv (default 32 32 = the T1 output, unchanged) and,
with a third argument "rt", also emit the render target's RENDER_SURFACE_STATE from isl (B8G8R8A8_UNORM, linear,
render-target usage).  usage: reftex_param.py <reftex.c>"""
import sys
NL = chr(10)
p = sys.argv[1]
s = open(p).read()
def rep(s, a, b):
    assert s.count(a) == 1, a[:80]
    return s.replace(a, b)
s = rep(s, " *   PS:  uv = frag_coord.xy / (32, 32);  colour = txl(texture BTI 1, sampler 0, uv, lod 0);",
        " *   PS:  uv = frag_coord.xy / (RT_W, RT_H);  colour = txl(texture BTI 1, sampler 0, uv, lod 0);" + NL +
        " *        RT_W / RT_H: argv[1] / argv[2], default 32 / 32 (the T1 fixture; its output is unchanged).  E-118: with" + NL +
        " *        argv[3] == \"rt\" the render target's RENDER_SURFACE_STATE is emitted too (isl, B8G8R8A8_UNORM, linear).")
s = rep(s, "#define RT_W 32" + NL + "#define RT_H 32" + NL, "#define RT_VA_PLACEHOLDER 0x100800000ull" + NL)
s = rep(s, "int" + NL + "main(void)" + NL + "{", "int" + NL + "main(int argc, char **argv)" + NL + "{" + NL +
        "   unsigned rt_w = argc > 2 ? (unsigned)atoi(argv[1]) : 32u;" + NL +
        "   unsigned rt_h = argc > 2 ? (unsigned)atoi(argv[2]) : 32u;" + NL +
        "   int emit_rt = argc > 3 && strcmp(argv[3], \"rt\") == 0;")
s = rep(s, "nir_imm_vec2(&b, 1.0f / RT_W, 1.0f / RT_H)", "nir_imm_vec2(&b, 1.0f / (float)rt_w, 1.0f / (float)rt_h)")
s = rep(s, "   /* raw PS for the disassembler */", """   if (emit_rt) {
      /* E-118: the render target as the draw names it -- layout and RENDER_SURFACE_STATE from isl */
      struct isl_surf rt;
      if (!isl_surf_init(&dev, &rt, .dim = ISL_SURF_DIM_2D, .format = ISL_FORMAT_B8G8R8A8_UNORM,
            .width = rt_w, .height = rt_h, .depth = 1, .levels = 1, .array_len = 1, .samples = 1,
            .usage = ISL_SURF_USAGE_RENDER_TARGET_BIT, .tiling_flags = ISL_TILING_LINEAR_BIT)) {
         fprintf(stderr, "reftex: isl_surf_init(render target) failed\\n");
         return 1;
      }
      struct isl_view rtv = {
         .usage = ISL_SURF_USAGE_RENDER_TARGET_BIT, .format = ISL_FORMAT_B8G8R8A8_UNORM,
         .base_level = 0, .levels = 1, .base_array_layer = 0, .array_len = 1,
         .swizzle = ISL_SWIZZLE_IDENTITY,
      };
      uint32_t rt_rss[16];
      memset(rt_rss, 0, sizeof(rt_rss));
      isl_surf_fill_state(&dev, rt_rss, .surf = &rt, .view = &rtv, .address = RT_VA_PLACEHOLDER, .mocs = MOCS);
      fprintf(stderr, "reftex: render target %ux%u row_pitch_B=%u size_B=%llu alignment_B=%u\\n", rt_w, rt_h,
         rt.row_pitch_B, (unsigned long long)rt.size_B, rt.alignment_B);
      printf("#define TEXFIX_RT_WIDTH %uU\\n#define TEXFIX_RT_HEIGHT %uU\\n#define TEXFIX_RT_ROW_PITCH %uU\\n"
             "#define TEXFIX_RT_SIZE %lluU\\n#define TEXFIX_RT_ALIGNMENT %uU\\n#define TEXFIX_RT_VA_PLACEHOLDER 0x%llxULL\\n",
         rt_w, rt_h, rt.row_pitch_B, (unsigned long long)rt.size_B, rt.alignment_B, (unsigned long long)RT_VA_PLACEHOLDER);
      printf("/* RENDER_SURFACE_STATE of the render target (isl_surf_fill_state, render-target usage, address = placeholder, mocs %d). */\\n", MOCS);
      printf("static const uint32_t texfix_rt_rss[16] = {");
      for (unsigned i = 0; i < 16; i++)
         printf("%s0x%08xU,", (i % 4) == 0 ? "\\n\\t" : " ", rt_rss[i]);
      printf("\\n};\\n");
   }

   /* raw PS for the disassembler */""")
open(p, "w").write(s)
print("patched", p)
