#!/usr/bin/env python3
"""WS031: generate the eDP AUX / panel-power-sequencer port from the fixed references.

  i915 (Linux 6.8.12 fixed reference, display/):
    <out>/intel_dp_aux_port.c   intel_dp_aux.c   with whole functions removed + recorded edits
    <out>/intel_pps_port.c      intel_pps.c      with whole functions removed + recorded edits
    <out>/intel_dp_aux_regs.h, intel_pps_regs.h, intel_pps.h, intel_dp_aux.h   copies (includes replaced)
    <out>/dp_ref_types.h        struct intel_pps, extracted textually from intel_display_types.h
  DRM core (upstream stable v6.8.12, plan/ws031/linux-parity/linux-reference/drm-v6.8.12/):
    <out>/drm_dp_helper_port.c  the DPCD access + I2C-over-AUX functions of drm_dp_helper.c
    <out>/drm_edid_port.c       the DDC EDID block read + header/checksum helpers of drm_edid.c
    <out>/drm_dp.h              include/drm/display/drm_dp.h copy (include replaced)

Function bodies are NOT retyped: this script copies the reference text.  Every removal and
substitution is recorded in the generated file's header.

usage: port_dp_aux_pps.py <i915 reference display dir> <drm reference dir> <output dir>
"""
import hashlib, os, re, sys

ref, drmref, out = sys.argv[1], sys.argv[2], sys.argv[3]
NL, TAB = chr(10), chr(9)

def rd(d, name):
    return open(os.path.join(d, name)).read()

def sha(t):
    return hashlib.sha256(t.encode()).hexdigest()

def wr(name, text):
    open(os.path.join(out, name), "w").write(text)
    print("generated", name, len(text.split(NL)), "lines")

def func_span(text, name):
    """(start, end) of the DEFINITION of `name`, with a directly preceding comment block."""
    pat = re.compile(r"^(?:static |const |struct |enum |bool |int |void |u8 |u32 |ssize_t )[^\n;{]*\b" +
                     re.escape(name) + r"\(", re.M)
    m = None
    for cand in pat.finditer(text):
        # a prototype ends in ';' before any '{'
        brace, semi = text.find("{", cand.end()), text.find(";", cand.end())
        if brace != -1 and (semi == -1 or brace < semi):
            m = cand
            break
    if m is None:
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
        if seg.count(NL) < 60 and (NL + NL) not in seg.rstrip(NL):
            start = c
    return start, end

def after_first_comment(text):
    return text.index("*/") + 2

class Port:
    def __init__(self, text):
        self.body = text
        self.log = []
    def remove(self, names):
        for n in names:
            s, e = func_span(self.body, n)
            self.body = self.body[:s] + self.body[e:]
    def sub(self, old, new, count=1):
        assert self.body.count(old) == count, (old[:70], self.body.count(old))
        self.body = self.body.replace(old, new)

def header_note(src_path, digest, lines):
    t = NL + NL + "/*" + NL + " * zedBSD WS031: generated from " + src_path + NL
    t += " * (sha256 " + digest + ") by plan/ws031/handover/tools/port_dp_aux_pps.py." + NL
    t += " * The function bodies are the reference text.  Changes:" + NL
    for l in lines:
        t += " *" + (" " + l if l else "") + NL
    return t + " */"

def wrap_names(names, indent=" *    "):
    rows, cur = [], ""
    for n in names:
        piece = n + ", "
        if len(cur) + len(piece) > 84:
            rows.append(cur.rstrip())
            cur = ""
        cur += piece
    rows.append(cur.rstrip().rstrip(",") + ";")
    return [("   " + r) for r in rows]

# =============================================================== copied i915 headers
def copy_header(name, subs, note):
    t = rd(ref, name)
    digest = sha(t)
    for old, new in subs:
        assert t.count(old) == 1, (name, old)
        t = t.replace(old, new)
    ne = after_first_comment(t)
    # SPDX line + copyright comment: put the note after the copyright comment (the second comment)
    if t.startswith("/* SPDX"):
        ne = t.index("*/", ne) + 2
    t = (t[:ne] + NL + NL + "/*" + NL +
         " * zedBSD WS031: copied from Linux v6.8.12 drivers/gpu/drm/i915/display/" + name + NL +
         " * (sha256 " + digest + ") by tools/port_dp_aux_pps.py." + NL +
         " * Change: " + note + NL + " */" + t[ne:])
    wr(name, t)

copy_header("intel_dp_aux_regs.h",
            [('#include "intel_display_reg_defs.h"', "/* zedBSD: register helper macros come from dp_compat.h */")],
            "the include of intel_display_reg_defs.h is removed (dp_compat.h supplies the macros).")
copy_header("intel_pps_regs.h",
            [('#include "intel_display_reg_defs.h"', "/* zedBSD: register helper macros come from dp_compat.h */")],
            "the include of intel_display_reg_defs.h is removed (dp_compat.h supplies the macros).")
copy_header("intel_pps.h",
            [("#include <linux/types.h>" + NL + NL + '#include "intel_wakeref.h"',
              "/* zedBSD: types and intel_wakeref_t come from dp_compat.h, which includes this file */")],
            "the <linux/types.h> and intel_wakeref.h includes are removed.")
copy_header("intel_dp_aux.h",
            [("#include <linux/types.h>", "/* zedBSD: types come from dp_compat.h, which includes this file */")],
            "the <linux/types.h> include is removed.")

# =============================================================== dp_ref_types.h
t = rd(ref, "intel_display_types.h")
m = re.search(r"^struct intel_pps \{", t, re.M)
e = t.index(NL + "};" + NL, m.start()) + 4
wr("dp_ref_types.h",
   "/*" + NL +
   " * zedBSD WS031: `struct intel_pps` extracted textually from the Linux v6.8.12 i915 reference" + NL +
   " * display/intel_display_types.h (MIT permission notice, Copyright Intel Corporation; the full" + NL +
   " * notice is kept in intel_pps_port.c's source header) by tools/port_dp_aux_pps.py." + NL +
   " * Do not edit by hand." + NL + " */" + NL +
   "#ifndef PARITY_DP_REF_TYPES_H" + NL + "#define PARITY_DP_REF_TYPES_H" + NL + NL +
   "/* from intel_display_types.h */" + NL + t[m.start():e] + NL +
   "#endif /* PARITY_DP_REF_TYPES_H */" + NL)

# =============================================================== intel_dp_aux.c
src = rd(ref, "intel_dp_aux.c")
p = Port(src)
AUX_REMOVED = [
    # other platforms' clock dividers / send-control words / register selectors
    "g4x_get_aux_clock_divider", "ilk_get_aux_clock_divider", "hsw_get_aux_clock_divider",
    "g4x_dp_aux_precharge_len", "g4x_get_aux_send_ctl",
    "vlv_aux_ctl_reg", "vlv_aux_data_reg", "g4x_aux_ctl_reg", "g4x_aux_data_reg",
    "ilk_aux_ctl_reg", "ilk_aux_data_reg", "skl_aux_ctl_reg", "skl_aux_data_reg",
    "xelpdp_aux_ctl_reg", "xelpdp_aux_data_reg",
    # object setup: replaced by the zedBSD glue (the ADL-P selections of intel_dp_aux_init)
    "aux_ch_name", "intel_dp_aux_fini", "intel_dp_aux_init",
    # AUX channel choice across encoders: the zedBSD side takes the VBT's aux channel directly
    "default_aux_ch", "get_encoder_by_aux_ch", "intel_dp_aux_ch",
    # the AUX-done interrupt only wakes a waiter; the kept wait polls the register
    "intel_dp_aux_irq_handler",
]
p.remove(AUX_REMOVED)
inc = ('#include "i915_drv.h"' + NL + '#include "i915_reg.h"' + NL + '#include "i915_trace.h"' + NL +
       '#include "intel_bios.h"' + NL + '#include "intel_de.h"' + NL + '#include "intel_display_types.h"' + NL +
       '#include "intel_dp_aux.h"' + NL + '#include "intel_dp_aux_regs.h"' + NL + '#include "intel_pps.h"' + NL +
       '#include "intel_quirks.h"' + NL + '#include "intel_tc.h"' + NL)
p.sub(inc, '#include "dp_compat.h"' + TAB + "/* zedBSD: replaces the drm/i915 includes */" + NL +
      '#include "intel_dp_aux_regs.h"' + NL)
p.sub("#define AUX_CH_NAME_BUFSIZE" + TAB + "6" + NL + NL, "")
body = p.body.rstrip(NL) + NL + NL + '#include "parity_dp_aux_glue.inc"' + TAB + "/* zedBSD: object setup */" + NL
note = header_note("Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dp_aux.c", sha(src), [
    " - the drm/i915 includes are replaced by dp_compat.h (register access, waits, power",
    "   references, the PPS entry points) and the copied intel_dp_aux_regs.h;",
    " - whole functions removed (other platforms, or replaced by the zedBSD object setup):"] +
    wrap_names(AUX_REMOVED) + [
    " - the AUX_CH_NAME_BUFSIZE define (only used by the removed aux_ch_name) is removed;",
    " - parity_dp_aux_glue.inc (zedBSD code) is included at the end of the file.",
    " intel_dp_aux_xfer(), intel_dp_aux_transfer(), the pack/unpack/header helpers, the SKL+",
    " send-control word and the TGL+ register selectors are unmodified."])
# the file starts with a // SPDX line then a comment: insert after that comment
first = body.index("*/") + 2
body = body[:first] + note + body[first:]
wr("intel_dp_aux_port.c", body)

# =============================================================== intel_pps.c
src = rd(ref, "intel_pps.c")
p = Port(src)
p.sub("static void vlv_steal_power_sequencer(struct drm_i915_private *dev_priv," + NL +
      TAB + TAB + TAB + TAB + "      enum pipe pipe);" + NL + NL, "")
PPS_REMOVED = [
    # VLV / CHV per-pipe power sequencers
    "vlv_power_sequencer_kick", "vlv_find_free_pps", "vlv_power_sequencer_pipe", "vlv_initial_pps_pipe",
    "vlv_initial_power_sequencer_setup", "vlv_detach_power_sequencer", "vlv_steal_power_sequencer",
    "vlv_pps_init",
    # walks every encoder of the device (power-well reset notification; BXT/GLK and VLV only)
    "intel_pps_reset_all",
    # connector -> encoder lookups through DRM objects this port does not have
    "intel_pps_backlight_power",
    # pre-DDI register unlock, device-wide setup (the glue sets mmio_base), pre-DDI assert
    "intel_pps_unlock_regs_wa", "intel_pps_setup", "assert_pps_unlocked",
]
p.remove(PPS_REMOVED)
inc = ('#include "g4x_dp.h"' + NL + '#include "i915_drv.h"' + NL + '#include "i915_reg.h"' + NL +
       '#include "intel_de.h"' + NL + '#include "intel_display_power_well.h"' + NL +
       '#include "intel_display_types.h"' + NL + '#include "intel_dp.h"' + NL + '#include "intel_dpio_phy.h"' + NL +
       '#include "intel_dpll.h"' + NL + '#include "intel_lvds.h"' + NL + '#include "intel_lvds_regs.h"' + NL +
       '#include "intel_pps.h"' + NL + '#include "intel_pps_regs.h"' + NL + '#include "intel_quirks.h"' + NL)
p.sub(inc, '#include "dp_compat.h"' + TAB + "/* zedBSD: replaces the drm/i915 includes */" + NL +
      '#include "intel_pps_regs.h"' + NL)
body = p.body
first = body.index("*/") + 2
note = header_note("Linux v6.8.12 drivers/gpu/drm/i915/display/intel_pps.c", sha(src), [
    " - the drm/i915 includes are replaced by dp_compat.h and the copied intel_pps_regs.h;",
    " - the forward declaration of vlv_steal_power_sequencer is removed;",
    " - whole functions removed (VLV/CHV, device-wide walks, pre-DDI paths):"] +
    wrap_names(PPS_REMOVED) + [
    " The PPS lock, the VDD on/off paths and their delayed worker, the panel-status waits, the",
    " delay selection (BIOS / VBT / eDP-spec fallback), the register programming, panel power",
    " on/off and the backlight-enable bit handling are unmodified."])
body = body[:first] + note + body[first:]
wr("intel_pps_port.c", body)

# =============================================================== drm_dp.h
t = rd(drmref, "drm_dp.h")
digest = sha(t)
assert t.count("#include <linux/types.h>") == 1
t = t.replace("#include <linux/types.h>", "/* zedBSD: types come from dp_compat.h, which includes this file */")
ne = after_first_comment(t)
t = (t[:ne] + NL + NL + "/*" + NL +
     " * zedBSD WS031: copied from Linux v6.8.12 include/drm/display/drm_dp.h" + NL +
     " * (sha256 " + digest + ") by tools/port_dp_aux_pps.py." + NL +
     " * Change: the <linux/types.h> include is removed." + NL + " */" + t[ne:])
wr("drm_dp.h", t)

# =============================================================== drm_dp_helper.c (keep-list)
src = rd(drmref, "drm_dp_helper.c")
def take(text, name):
    s, e = func_span(text, name)
    return text[s:e]
def take_between(text, start_marker, end_marker):
    s = text.index(start_marker)
    e = text.index(end_marker, s) + len(end_marker)
    return text[s:e]

lic_end = after_first_comment(src)
parts = []
parts.append("#define AUX_RETRY_INTERVAL 500 /* us */" + NL)
for n in ("drm_dp_dump_access", "drm_dp_dpcd_access", "drm_dp_dpcd_probe", "drm_dp_dpcd_read",
          "drm_dp_dpcd_write", "drm_dp_read_extended_dpcd_caps", "drm_dp_read_dpcd_caps"):
    parts.append(take(src, n))
i2c = take_between(src, "static u32 drm_dp_i2c_functionality(struct i2c_adapter *adapter)",
                   "static const struct i2c_algorithm drm_dp_i2c_algo = {" + NL +
                   TAB + ".functionality = drm_dp_i2c_functionality," + NL +
                   TAB + ".master_xfer = drm_dp_i2c_xfer," + NL + "};" + NL)
for old in ("module_param_unsafe(dp_aux_i2c_speed_khz, int, 0644);" + NL +
            "MODULE_PARM_DESC(dp_aux_i2c_speed_khz," + NL +
            TAB + TAB + ' "Assumed speed of the i2c bus in kHz, (1-400, default 10)");' + NL,
            "module_param_unsafe(dp_aux_i2c_transfer_size, int, 0644);" + NL +
            "MODULE_PARM_DESC(dp_aux_i2c_transfer_size," + NL +
            TAB + TAB + ' "Number of bytes to transfer in a single I2C over DP AUX CH message, (1-16, default 16)");' + NL):
    assert i2c.count(old) == 1, old[:50]
    i2c = i2c.replace(old, "")
parts.append(i2c)
body = NL.join(x.rstrip(NL) + NL for x in parts)
body = body.replace("EXPORT_SYMBOL(drm_dp_dpcd_probe);" + NL, "").replace("EXPORT_SYMBOL(drm_dp_dpcd_read);" + NL, "")
body = body.replace("EXPORT_SYMBOL(drm_dp_dpcd_write);" + NL, "").replace("EXPORT_SYMBOL(drm_dp_read_dpcd_caps);" + NL, "")
note = header_note("Linux v6.8.12 drivers/gpu/drm/display/drm_dp_helper.c", sha(src), [
    " - only these parts of the 4102-line file are kept, in the reference order:",
    "     AUX_RETRY_INTERVAL, drm_dp_dump_access, drm_dp_dpcd_access, drm_dp_dpcd_probe,",
    "     drm_dp_dpcd_read, drm_dp_dpcd_write, drm_dp_read_extended_dpcd_caps,",
    "     drm_dp_read_dpcd_caps, and the contiguous I2C-over-AUX section from",
    "     drm_dp_i2c_functionality to the drm_dp_i2c_algo table;",
    " - the two module_param_unsafe / MODULE_PARM_DESC pairs (dp_aux_i2c_speed_khz,",
    "   dp_aux_i2c_transfer_size) are removed: the defaults 10 kHz / 16 bytes are fixed;",
    " - the includes are replaced by dp_compat.h; EXPORT_SYMBOL lines are dropped;",
    " - parity_drm_dp_glue.inc (zedBSD code) is included at the end of the file."])
wr("drm_dp_helper_port.c", src[:lic_end] + note + NL + NL + '#include "dp_compat.h"' + TAB +
   "/* zedBSD: replaces the linux/ and drm/ includes */" + NL + NL + body + NL +
   '#include "parity_drm_dp_glue.inc"' + TAB + "/* zedBSD: drm_dp_aux_init() equivalent */" + NL)

# =============================================================== drm_edid.c (keep-list)
src = rd(drmref, "drm_edid.c")
lic_end = after_first_comment(src)
hdr = take_between(src, "static const u8 edid_header[] = {", "};" + NL)
parts = [hdr, take(src, "drm_edid_header_is_valid"), take(src, "edid_block_compute_checksum"),
         take(src, "edid_block_get_checksum"),
         "#define DDC_SEGMENT_ADDR 0x30" + NL + take(src, "drm_do_probe_ddc_edid")]
body = NL.join(x.rstrip(NL) + NL for x in parts).replace("EXPORT_SYMBOL(drm_edid_header_is_valid);" + NL, "")
# take() pulled the "#define DDC_SEGMENT_ADDR" line only if it sits inside the comment span: make it single
assert body.count("#define DDC_SEGMENT_ADDR 0x30") >= 1
while body.count("#define DDC_SEGMENT_ADDR 0x30") > 1:
    i = body.rindex("#define DDC_SEGMENT_ADDR 0x30")
    body = body[:i] + body[i + len("#define DDC_SEGMENT_ADDR 0x30" + NL):]
note = header_note("Linux v6.8.12 drivers/gpu/drm/drm_edid.c", sha(src), [
    " - only these parts of the 7386-line file are kept: edid_header[], drm_edid_header_is_valid,",
    "   edid_block_compute_checksum, edid_block_get_checksum, DDC_SEGMENT_ADDR and",
    "   drm_do_probe_ddc_edid;",
    " - the includes are replaced by dp_compat.h plus the two drm_edid.h constants EDID_LENGTH and",
    "   DDC_ADDR; EXPORT_SYMBOL lines are dropped;",
    " - parity_drm_edid_glue.inc (zedBSD code) is included at the end of the file."])
wr("drm_edid_port.c", src[:lic_end] + note + NL + NL + '#include "dp_compat.h"' + TAB +
   "/* zedBSD: replaces the linux/ and drm/ includes */" + NL +
   "#define EDID_LENGTH 128" + TAB + "/* drm_edid.h */" + NL +
   "#define DDC_ADDR 0x50" + TAB + "/* drm_edid.h */" + NL + NL + body + NL +
   '#include "parity_drm_edid_glue.inc"' + TAB + "/* zedBSD: base + extension block read */" + NL)
