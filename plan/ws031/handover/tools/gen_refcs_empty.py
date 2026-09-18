import os
M = os.path.expanduser("~/zedBSD/plan/ws031/mesa-refs/mesa")
src = os.path.join(M, "src/intel/compiler/brw/refcs.c")
dst = os.path.join(M, "src/intel/compiler/brw/refcs_empty.c")
s = open(src).read()

# Remove the nir_store_global block (store-less compute: proper EOT only).
store = ('   /* One A64 data-cache store of the tag to the compute marker VA. */\n'
         '   nir_store_global(&b, nir_imm_int(&b, 0xc0ffee02),\n'
         '                    nir_imm_int64(&b, 0x100400c20ull), .align_mul = 4);\n')
assert store in s, "store block not found in refcs.c"
s = s.replace(store, '   /* Store-less: empty compute body, proper EOT only (C2). */\n', 1)
s = s.replace('"refcs:', '"refcs_empty:')
s = s.replace('"marker_cs"', '"empty_cs"')
open(dst, "w").write(s)
print("wrote", dst)

mb = os.path.join(M, "src/intel/compiler/brw/meson.build")
m = open(mb).read()
if "refcs_empty" not in m:
    ref_block = ("refcs = executable(\n  'refcs',\n  'refcs.c',\n"
                 "  include_directories : [inc_include, inc_src, inc_intel, inc_intel_compiler],\n"
                 "  link_with : libisl,\n  c_args : ['-fsanitize=address', '-g', '-O0'],\n"
                 "  link_args : ['-fsanitize=address'],\n"
                 "  dependencies : [idep_nir, idep_mesautil, idep_intel_dev, idep_intel_compiler_brw],\n"
                 "  install : false,\n)\n")
    assert ref_block in m, "refcs meson block not found"
    empty_block = ref_block.replace("refcs = executable(\n  'refcs',\n  'refcs.c',",
                                    "refcs_empty = executable(\n  'refcs_empty',\n  'refcs_empty.c',")
    m = m.replace(ref_block, ref_block + "\n" + empty_block, 1)
    open(mb, "w").write(m)
    print("registered refcs_empty in meson")
else:
    print("refcs_empty already in meson")
