import os
M = os.path.expanduser("~/zedBSD/plan/ws031/mesa-refs/mesa")
src = os.path.join(M, "src/intel/compiler/brw/refps.c")
dst = os.path.join(M, "src/intel/compiler/brw/refps_marker.c")
s = open(src).read()

anchor = ('   nir_builder b = nir_builder_init_simple_shader(\n'
          '      MESA_SHADER_FRAGMENT,\n'
          '      &compiler->nir_options[MESA_SHADER_FRAGMENT],\n'
          '      "const_color");\n')
assert anchor in s, "builder-init anchor not found"
# Entry marker: an A64 global store of 0xC0FFEE01 to the marker page's fixed GPU VA
# (objects[0] deterministically binds at 0x100400000; marker slot at +0xC10).
inject = anchor + ('\n'
   '   /* Entry marker: A64 data-cache store, emitted before the RT write, so a\n'
   '    * landed value proves the PS thread fetched and executed to this point. */\n'
   '   nir_store_global(&b, nir_imm_int(&b, 0xc0ffee01),\n'
   '                    nir_imm_int64(&b, 0x100400c10ull), .align_mul = 4);\n')
s = s.replace(anchor, inject, 1)
s = s.replace('"refps:', '"refps_marker:')
open(dst, "w").write(s)
print("wrote", dst)

# Register in meson.
mb = os.path.join(M, "src/intel/compiler/brw/meson.build")
m = open(mb).read()
if "refps_marker" not in m:
    ref_block = ("refps = executable(\n  'refps',\n  'refps.c',\n"
                 "  include_directories : [inc_include, inc_src, inc_intel, inc_intel_compiler],\n"
                 "  link_with : libisl,\n  c_args : ['-fsanitize=address', '-g', '-O0'],\n"
                 "  link_args : ['-fsanitize=address'],\n"
                 "  dependencies : [idep_nir, idep_mesautil, idep_intel_dev, idep_intel_compiler_brw],\n"
                 "  install : false,\n)\n")
    assert ref_block in m, "refps meson block not found verbatim"
    marker_block = ref_block.replace("refps = executable(\n  'refps',\n  'refps.c',",
                                     "refps_marker = executable(\n  'refps_marker',\n  'refps_marker.c',")
    m = m.replace(ref_block, ref_block + "\n" + marker_block, 1)
    open(mb, "w").write(m)
    print("registered refps_marker in meson")
else:
    print("refps_marker already in meson")
