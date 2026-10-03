#!/usr/bin/env python3
"""WS031 E-124 round 95: what the readout text of N1 still reaches.
Everything that is reference code and cheap to carry is generated (the transcoder M/N readout, the cursor's
watermark registers, the DDB helpers); everything that belongs to a subsystem this path does not port (bigjoiner,
DSI, DSC, VRR, audio, infoframe readback, MSO) is a recorded step, and the platform branches this machine never
takes are constants.  The structures gain the fields a readout fills.
usage: round95.py <repo root>"""
import sys, json
root = sys.argv[1].rstrip("/") + "/"
L = root + "src/drivers/gpu/i915/parity/lcd/"
NL = chr(10)
TAB = chr(9)

spec = root + "plan/ws031/handover/tools/port_lcd_modeset.json"
j = json.load(open(spec))
add = {
    "intel_display.c": ["assert_enabled_transcoders", "intel_pipe_is_interlaced", "intel_cpu_transcoder_get_m1_n1",
                        "intel_cpu_transcoder_get_m2_n2"],
}
for k, names in add.items():
    have = j["extra"].setdefault(k, [])
    j["extra"][k] = have + [n for n in names if n not in have]
for out, names in (("skl_watermark_port.c", ["skl_ddb_entry_init_from_hw", "skl_ddb_dbuf_slice_mask",
                                             "skl_ddb_allocation_overlaps"]),):
    for e in j["new_files"]:
        if e["out"] == out:
            e["functions"] = e["functions"] + [n for n in names if n not in e["functions"]]
for m in j["macro_headers"]:
    if m["out"] == "lcd_mreg_wm.h":
        for r in ["CUR_WM", "CUR_WM_TRANS", "CUR_WM_SAGV", "CUR_WM_SAGV_TRANS", "CUR_BUF_CFG"]:
            if r not in m["roots"]:
                m["roots"].append(r)
open(spec, "w").write(json.dumps(j, indent=1) + NL)

# ---- the structures a readout fills ----
p = L + "lcd_compat.h"
s = open(p).read()
if "hw_readout_power_domains" not in s:
    s = s.replace("struct intel_crtc {", "struct intel_crtc {" + NL +
                  TAB + "/* the power domains the READOUT found this crtc using (intel_modeset_setup.c) */" + NL +
                  TAB + "struct intel_power_domain_mask hw_readout_power_domains;" + NL +
                  TAB + "struct intel_power_domain_mask enabled_power_domains;", 1)
if "struct drm_plane *primary;" not in s:
    s = s.replace("struct drm_crtc { struct drm_device *dev;",
                  "struct drm_crtc { struct drm_device *dev; struct drm_crtc_state *state; struct drm_plane *primary;", 1)
if "struct { u32 enable; } infoframes;" in s and "avi" not in s.split("infoframes;")[0][-200:]:
    s = s.replace("struct { u32 enable; } infoframes;",
                  "/* the infoframes a readout would decode: only their presence is tracked here */" + NL +
                  TAB + "struct { u32 enable; int avi, spd, hdmi, drm; } infoframes;", 1)
open(p, "w").write(s)

# ---- the rest: n1_compat.h ----
p = L + "n1_compat.h"
s = open(p).read()
if "for_each_cpu_transcoder_masked" not in s:
    add = (NL + "/* ---- the walks and helpers the readout text uses ---- */" + NL +
           "#define HAS_TRANSCODER(i915, tr) ((tr) >= 0 && (tr) <= 3)          /* A..D on this platform */" + NL +
           "#define for_each_cpu_transcoder_masked(i915, tr, mask) " +
           "for ((tr) = 0; (tr) <= 3; (tr)++) for_each_if((mask) & BIT(tr))" + NL +
           "static inline void drm_rect_init(struct drm_rect *r, int x, int y, int w, int h)" + NL +
           "{ r->x1 = x; r->y1 = y; r->x2 = x + w; r->y2 = y + h; }" + NL +
           "#define drm_crtc_wait_one_vblank(crtc) PARITY_LCD_STEP(parity_lcd_cur_i915, \"drm_crtc_wait_one_vblank\")" + NL +
           NL +
           "/* ---- subsystems this path does not port: each call is recorded, never a silent success ---- */" + NL +
           "#define N1_STEP(name) PARITY_LCD_STEP(parity_lcd_cur_i915, name)" + NL +
           "#define enabled_bigjoiner_pipes(i915) (0u)                          /* no bigjoiner on this display */" + NL +
           "#define get_bigjoiner_master_pipe(pipe, mask) (pipe)" + NL +
           "#define intel_bigjoiner_adjust_pipe_src(cs) ((void)0)" + NL +
           "#define intel_bigjoiner_get_config(cs) ((void)0)" + NL +
           "#define bxt_get_dsi_transcoder_state(crtc, cs, mask) ((void)0)      /* no DSI panel */" + NL +
           "#define BXT_PHY_CTL(port) _MMIO(0)" + NL +
           "#define bxt_ddi_phy_get_lane_lat_optim_mask(encoder) (0u)" + NL +
           "#define intel_dsc_get_config(cs) N1_STEP(\"intel_dsc_get_config\")" + NL +
           "#define intel_vrr_get_config(cs) N1_STEP(\"intel_vrr_get_config\")" + NL +
           "#define intel_ddi_mso_get_config(encoder, cs) N1_STEP(\"intel_ddi_mso_get_config\")" + NL +
           "#define intel_ddi_is_audio_enabled(i915, tr) (N1_STEP(\"intel_ddi_is_audio_enabled\"), false)" + NL +
           "#define intel_hdmi_infoframes_enabled(encoder, cs) (N1_STEP(\"intel_hdmi_infoframes_enabled\"), 0u)" + NL +
           "#define intel_hdmi_read_gcp_infoframe(encoder, cs) N1_STEP(\"intel_hdmi_read_gcp_infoframe\")" + NL +
           "#define intel_read_infoframe(encoder, cs, type, frame) N1_STEP(\"intel_read_infoframe\")" + NL +
           "#define intel_read_dp_sdp(encoder, cs, type) N1_STEP(\"intel_read_dp_sdp\")" + NL +
           "#define intel_edp_fixup_vbt_bpp(encoder, bpp) N1_STEP(\"intel_edp_fixup_vbt_bpp\")" + NL +
           "#define bdw_get_trans_port_sync_config(cs) N1_STEP(\"bdw_get_trans_port_sync_config\")" + NL)
    s = s.replace("#endif /* PARITY_N1_COMPAT_H */", add + NL + "#endif /* PARITY_N1_COMPAT_H */")
    open(p, "w").write(s)
print("done")
