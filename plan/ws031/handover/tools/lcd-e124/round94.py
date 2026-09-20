#!/usr/bin/env python3
"""WS031 E-124 round 94 (N1): the reference's hardware readout, its sanitize, and the takeover of a pipe the
firmware left running -- as reference text.

The generator gains:
 - intel_display.c: the pipe-config readout (hsw_get_pipe_config and its callees), the plane's noatomic disable and
   the bookkeeping the readout leaves behind;
 - intel_ddi.c: the encoder readout (which pipes a DDI drives, TRANS_DDI_FUNC_CTL decode, the clock / PLL it uses)
   and intel_ddi_sync_state;
 - intel_dpll_mgr.c: the PLL readout (combo_pll_get_hw_state / icl_pll_get_hw_state) and its sanitize;
 - skl_universal_plane.c: skl_plane_get_hw_state;
 - skl_watermark.c: the watermark / DDB readout and its sanitize;
 - a new unit from intel_modeset_setup.c: intel_modeset_readout_hw_state, the sanitize functions and
   intel_crtc_disable_noatomic (the takeover itself).
usage: round94.py <repo root>"""
import sys, json
root = sys.argv[1].rstrip("/") + "/"
NL = chr(10)

spec = root + "plan/ws031/handover/tools/port_lcd_modeset.json"
j = json.load(open(spec))

extra = {
    "intel_display.c": [
        "hsw_panel_transcoders", "hsw_enabled_transcoders", "hsw_get_transcoder_state",
        "intel_get_transcoder_timings", "intel_get_pipe_src_size", "bdw_get_pipe_misc_output_format",
        "hsw_get_pipe_config", "intel_crtc_get_pipe_config", "intel_crtc_readout_derived_state",
        "intel_encoder_get_config",
        "intel_set_plane_visible", "intel_plane_fixup_bitmasks", "intel_plane_disable_noatomic",
        "transcoder_ddi_func_is_enabled", "intel_crtc_dotclock",
    ],
    "intel_ddi.c": [
        "intel_ddi_get_encoder_pipes", "intel_ddi_get_hw_state", "intel_ddi_read_func_ctl", "ddi_dotclock_get",
        "intel_ddi_get_config", "intel_ddi_get_clock", "_icl_ddi_get_pll", "icl_ddi_combo_get_pll",
        "icl_ddi_combo_get_config", "intel_ddi_sync_state", "intel_ddi_get_power_domains",
    ],
    "intel_dpll_mgr.c": [
        "intel_dpll_get_hw_state", "combo_pll_get_hw_state", "icl_pll_get_hw_state",
        "readout_dpll_hw_state", "intel_dpll_readout_hw_state", "sanitize_dpll_state", "intel_dpll_sanitize_state",
    ],
}
for key, add in extra.items():
    have = j["extra"].setdefault(key, [])
    j["extra"][key] = have + [n for n in add if n not in have]

# the readout halves of the plane and the watermarks join their existing units
for out, names in (("skl_plane_port.c", ["skl_plane_get_hw_state"]),
                   ("intel_vblank_port.c", ["intel_crtc_update_active_timings"]),
                   ("intel_crtc_port.c", ["intel_crtc_wait_for_next_vblank"]),
                   ("skl_watermark_port.c", ["skl_wm_level_from_reg_val", "skl_pipe_wm_get_hw_state",
                                             "skl_pipe_ddb_get_hw_state", "skl_ddb_get_hw_plane_state", "skl_ddb_entry_union", "skl_wm_get_hw_state",
                                             "skl_dbuf_is_misconfigured", "skl_wm_sanitize",
                                             "skl_wm_get_hw_state_and_sanitize"])):
    for e in j["new_files"]:
        if e["out"] == out:
            e["functions"] = e["functions"] + [n for n in names if n not in e["functions"]]

setup = {"dir": "i915", "source": "display/intel_modeset_setup.c",
         "path": "drivers/gpu/drm/i915/display/intel_modeset_setup.c",
         "out": "intel_modeset_setup_port.c",
         "includes": ["lcd_compat.h", "lcd_ddi_regs.h", "lcd_seq_compat.h", "lcd_modeset_compat.h",
                      "lcd_plane_compat.h", "lcd_wm_compat.h", "n1_compat.h"],
         "glue": "parity_modeset_setup_glue.inc",
         "functions": [
             "intel_crtc_disable_noatomic_begin", "intel_crtc_disable_noatomic_complete", "intel_crtc_disable_noatomic",
             "set_encoder_for_connector", "reset_encoder_connector_state", "reset_crtc_encoder_state",
             "intel_modeset_update_connector_atomic_state", "intel_crtc_copy_hw_to_uapi_state",
             "intel_crtc_has_encoders", "intel_encoder_find_connector", "intel_sanitize_fifo_underrun_reporting",
             "has_bogus_dpll_config", "intel_sanitize_crtc", "intel_sanitize_all_crtcs", "intel_sanitize_encoder",
             "readout_plane_state", "intel_modeset_readout_hw_state", "get_encoder_power_domains",
             "intel_modeset_setup_hw_state",
         ]}
j["new_files"] = [e for e in j["new_files"] if e["out"] != setup["out"]] + [setup]
open(spec, "w").write(json.dumps(j, indent=1) + NL)
print("spec updated: extras + intel_modeset_setup_port.c")
