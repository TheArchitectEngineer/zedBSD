#!/usr/bin/env python3
"""WS031: generate the VBT parser port from the fixed Linux 6.8.12 reference.

  <out>/intel_bios_port.c   display/intel_bios.c with whole functions removed and the listed edits
  <out>/intel_vbt_defs.h    display/intel_vbt_defs.h, verbatim except the include / guard message
  <out>/intel_bios.h        display/intel_bios.h, verbatim except the <linux/types.h> include
  <out>/vbt_ref_types.h     enums / structs extracted textually from other reference headers

The function bodies are NOT retyped: this script copies the reference text.  Every removal and
substitution is recorded in the generated file's header.

usage: port_intel_bios.py <reference display dir> <output dir>
"""
import hashlib, os, re, sys

ref, out = sys.argv[1], sys.argv[2]
src = open(os.path.join(ref, "intel_bios.c")).read()
defs = open(os.path.join(ref, "intel_vbt_defs.h")).read()
sha_src = hashlib.sha256(src.encode()).hexdigest()
sha_defs = hashlib.sha256(defs.encode()).hexdigest()
NL = chr(10)
TAB = chr(9)

# ---------------------------------------------------------------- intel_vbt_defs.h
d = defs
a = '#error "intel_vbt_defs.h is private to intel_bios.c"'
assert d.count(a) == 1
d = d.replace(a, '#error "intel_vbt_defs.h is private to intel_bios_port.c"')
assert d.count('#include "intel_bios.h"') == 1
d = d.replace('#include "intel_bios.h"', '#include "vbt_compat.h"' + TAB + '/* zedBSD: replaces intel_bios.h */')
ne = d.index("*/") + 2
d = (d[:ne] + NL + NL + "/*" + NL +
     " * zedBSD WS031: copied from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_vbt_defs.h" + NL +
     " * (sha256 %s) by tools/port_intel_bios.py." % sha_defs + NL +
     " * Changes: the include of intel_bios.h is replaced by vbt_compat.h; the private-include" + NL +
     " * guard message names the zedBSD file.  Nothing else is modified." + NL + " */" + d[ne:])
open(os.path.join(out, "intel_vbt_defs.h"), "w").write(d)

# ---------------------------------------------------------------- intel_bios.h
bh = open(os.path.join(ref, "intel_bios.h")).read()
sha_bh = hashlib.sha256(bh.encode()).hexdigest()
a = "#include <linux/types.h>" + NL
assert bh.count(a) == 1
bh = bh.replace(a, "/* zedBSD: <linux/types.h> is provided by vbt_compat.h, which includes this file */" + NL)
ne = bh.index("*/") + 2
bh = (bh[:ne] + NL + NL + "/*" + NL +
      " * zedBSD WS031: copied from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_bios.h" + NL +
      " * (sha256 %s) by tools/port_intel_bios.py.  Change: the <linux/types.h> include is removed." % sha_bh + NL +
      " */" + bh[ne:])
open(os.path.join(out, "intel_bios.h"), "w").write(bh)

# ---------------------------------------------------------------- vbt_ref_types.h
def block(path, start_re):
    t = open(os.path.join(ref, path)).read()
    m = re.search(start_re, t, re.M)
    assert m, (path, start_re)
    e = t.index(NL + "};" + NL, m.start()) + 4
    return "/* from %s */" % path + NL + t[m.start():e] + NL

parts = [
    block("intel_display_limits.h", r"^enum port \{"),
    block("intel_display.h", r"^enum aux_ch \{"),
    block("intel_display.h", r"^enum phy \{"),
    block("../soc/intel_pch.h", r"^enum intel_pch \{"),
    block("intel_display_types.h", r"^enum drrs_type \{"),
    block("intel_display_types.h", r"^struct intel_vbt_panel_data \{"),
    block("intel_display_core.h", r"^struct intel_vbt_data \{"),
]
open(os.path.join(out, "vbt_ref_types.h"), "w").write(
    "/*" + NL +
    " * zedBSD WS031: enums and structures extracted textually from the Linux v6.8.12 i915" + NL +
    " * reference (MIT, Copyright Intel Corporation; see the notice in intel_bios_port.c) by" + NL +
    " * tools/port_intel_bios.py.  Do not edit by hand." + NL + " */" + NL +
    "#ifndef PARITY_VBT_REF_TYPES_H" + NL + "#define PARITY_VBT_REF_TYPES_H" + NL + NL +
    NL.join(parts) + NL + "#endif /* PARITY_VBT_REF_TYPES_H */" + NL)

# ---------------------------------------------------------------- intel_bios.c
def func_span(text, name):
    """(start, end) of the definition of `name`, with a directly preceding comment block."""
    m = re.search(r"^(?:static |const |struct |enum |bool |int |void |u8 |u32 )[^\n;{]*\b" + re.escape(name) + r"\(", text, re.M)
    if not m:
        m = re.search(r"^" + re.escape(name) + r"\(", text, re.M)   # return type on the previous line
        assert m, name
        start = text.rfind(NL, 0, m.start() - 1) + 1
    else:
        start = m.start()
    end = text.index(NL + "}" + NL, m.start()) + 3
    pre = text[:start].rstrip(NL)
    if pre.endswith("*/"):
        c = pre.rfind("/*")
        seg = text[c:start]
        if seg.count(NL) < 40 and (NL + NL) not in seg.rstrip(NL):
            start = c
    return start, end

REMOVED = [
    # SDVO: not reachable on ADL-P (DISPLAY_VER 13)
    "parse_sdvo_panel_data", "parse_sdvo_device_mapping",
    # PSR: the target sink reports no PSR; not needed for the first panel
    "parse_psr",
    # MIPI DSI
    "parse_dsi_backlight_ports", "parse_mipi_config", "find_panel_sequence_block",
    "goto_next_sequence", "goto_next_sequence_v3", "get_init_otp_deassert_fragment_len",
    "vlv_fixup_mipi_sequences", "icl_fixup_mipi_sequences", "fixup_mipi_sequences", "parse_mipi_sequence",
    "intel_bios_is_dsi_present",
    # DSC compression parameters
    "parse_compression_parameters", "fill_dsc", "intel_bios_get_dsc_params",
    # VBT acquisition: done by the zedBSD provider (../bios.c)
    "intel_spi_read", "spi_oprom_get_vbt", "oprom_get_vbt",
    # legacy presence queries
    "intel_bios_is_tv_present", "intel_bios_is_lvds_present",
]
body = src
for name in REMOVED:
    s, e = func_span(body, name)
    body = body[:s] + body[e:]

def sub(old, new):
    global body
    assert body.count(old) == 1, old[:70]
    body = body.replace(old, new)

def comment_out_call(call):
    sub(TAB + call + NL, TAB + "/* zedBSD: " + call + " -- not ported (see header) */" + NL)

for call in ("parse_sdvo_panel_data(i915, panel);", "parse_psr(i915, panel);",
             "parse_mipi_config(i915, panel);", "parse_mipi_sequence(i915, panel);"):
    comment_out_call(call)

sub('#include <drm/display/drm_dp_helper.h>' + NL + '#include <drm/display/drm_dsc_helper.h>' + NL +
    '#include <drm/drm_edid.h>' + NL + NL + '#include "i915_drv.h"' + NL + '#include "i915_reg.h"' + NL +
    '#include "intel_display.h"' + NL + '#include "intel_display_types.h"' + NL + '#include "intel_gmbus.h"' + NL,
    '#include "vbt_compat.h"' + TAB + '/* zedBSD: replaces the drm/i915 includes */' + NL)

# intel_bios_init(): the bytes come from the zedBSD provider
sub(TAB + "const struct vbt_header *vbt = i915->display.opregion.vbt;" + NL +
    TAB + "struct vbt_header *oprom_vbt = NULL;" + NL,
    TAB + "const struct vbt_header *vbt = parity_vbt_provider_get(i915);" + TAB +
    "/* zedBSD: was i915->display.opregion.vbt */" + NL)
sub(TAB + "/*" + NL + TAB + " * If the OpRegion does not have VBT, look in SPI flash through MMIO or" + NL +
    TAB + " * PCI mapping" + NL + TAB + " */" + NL +
    TAB + "if (!vbt && IS_DGFX(i915)) {" + NL + TAB + TAB + "oprom_vbt = spi_oprom_get_vbt(i915);" + NL +
    TAB + TAB + "vbt = oprom_vbt;" + NL + TAB + "}" + NL + NL +
    TAB + "if (!vbt) {" + NL + TAB + TAB + "oprom_vbt = oprom_get_vbt(i915);" + NL +
    TAB + TAB + "vbt = oprom_vbt;" + NL + TAB + "}" + NL + NL,
    TAB + "/* zedBSD: the SPI / PCI ROM lookups belong to parity_vbt_provider_get() */" + NL + NL)
sub(TAB + "/* Depends on child device list */" + NL + TAB + "parse_compression_parameters(i915);" + NL,
    TAB + "/* zedBSD: parse_compression_parameters(i915); -- not ported (see header) */" + NL)
sub(TAB + "parse_sdvo_device_mapping(i915);" + NL + TAB + "parse_ddi_ports(i915);" + NL + NL +
    TAB + "kfree(oprom_vbt);" + NL,
    TAB + "/* zedBSD: parse_sdvo_device_mapping(i915); -- not ported (see header) */" + NL +
    TAB + "parse_ddi_ports(i915);" + NL)

names = ", ".join(REMOVED)
wrapped, line = [], " *    "
for w in names.split(" "):
    if len(line) + len(w) > 96:
        wrapped.append(line.rstrip())
        line = " *    "
    line += w + " "
wrapped.append(line.rstrip() + ";")

ne = body.index("*/") + 2
header = (NL + NL + "/*" + NL +
          " * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_bios.c" + NL +
          " * (sha256 %s) by plan/ws031/handover/tools/port_intel_bios.py." % sha_src + NL +
          " * The function bodies are the reference text.  Changes:" + NL +
          " *  - the drm/i915 includes are replaced by vbt_compat.h (types, logging, allocation, lists);" + NL +
          " *  - whole functions removed (not reachable for ADL-P eDP, or replaced by the zedBSD VBT" + NL +
          " *    provider):" + NL + NL.join(wrapped) + NL +
          " *  - the calls to the removed parse_* functions are commented out;" + NL +
          " *  - intel_bios_init(): the VBT pointer comes from parity_vbt_provider_get() instead of" + NL +
          " *    i915->display.opregion.vbt plus the SPI / PCI ROM fallbacks;" + NL +
          " *  - parity_vbt_glue.inc (zedBSD code) is included at the end of the file." + NL +
          " */")
body = body[:ne] + header + body[ne:]
body = (body.rstrip(NL) + NL + NL +
        "/* zedBSD: the device-owned state, arena and accessors (separate file, zedBSD project code). */" + NL +
        '#include "parity_vbt_glue.inc"' + NL)
open(os.path.join(out, "intel_bios_port.c"), "w").write(body)
print("generated: intel_bios_port.c %d lines (reference %d), removed %d functions" % (
    body.count(NL), src.count(NL), len(REMOVED)))
