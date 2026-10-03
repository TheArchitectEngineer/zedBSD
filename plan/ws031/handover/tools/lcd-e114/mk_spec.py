#!/usr/bin/env python3
"""WS031 E-114: writes tools/port_lcd_modeset.json (the table the LCD generator reads).
The macro roots already collected in an existing file are kept.  usage: mk_spec.py <out.json>"""
import json, os, sys
NL, TAB = chr(10), chr(9)
END = NL + "};" + NL
def table(name):
    return entries(name) + table_shared(name)
def table_shared(name):
    return [["static const struct intel_ddi_buf_trans " + name + " = {", END, "table " + name]]
def entries(name):
    return [["static const union intel_ddi_buf_trans_entry _" + name + "[] = {", END, "entries _" + name]]
def fn_range(sig, label):
    return [sig, NL + "}" + NL, label]

COMPAT_MACROS = ["DISPLAY_VER", "IS_DISPLAY_VER", "DISPLAY_INFO", "DISPLAY_RUNTIME_INFO", "HAS_VRR", "HAS_DP20", "HAS_GMCH",
                 "_MMIO", "_PICK_EVEN", "_PICK", "REG_BIT", "REG_GENMASK", "REG_FIELD_PREP", "REG_FIELD_GET",
                 "_MMIO_TRANS", "_MMIO_TRANS2", "_MMIO_PIPE2", "_MMIO_PORT", "_TRANS", "_PORT"]
spec = {
 "extra": {
  "intel_dpll_mgr.c": ["intel_combo_pll_enable_reg", "icl_pll_power_enable", "icl_dpll_write", "icl_pll_enable",
                       "adlp_cmtg_clock_gating_wa", "combo_pll_enable", "icl_pll_disable", "combo_pll_disable",
                       "_intel_enable_shared_dpll", "intel_enable_shared_dpll", "_intel_disable_shared_dpll",
                       "intel_disable_shared_dpll"],
  "intel_ddi.c": ["intel_ddi_enable_clock", "intel_ddi_disable_clock", "_icl_ddi_enable_clock", "_icl_ddi_disable_clock",
                  "icl_ddi_combo_enable_clock", "icl_ddi_combo_disable_clock", "intel_ddi_main_link_aux_domain",
                  "main_link_aux_power_domain_get", "main_link_aux_power_domain_put",
                  "intel_ddi_enable_transcoder_clock", "intel_ddi_disable_transcoder_clock",
                  "intel_ddi_dp_level", "intel_ddi_level", "icl_combo_phy_loadgen_select", "icl_ddi_combo_vswing_program",
                  "icl_combo_phy_set_signal_levels", "translate_signal_level",
                  "intel_ddi_power_up_lanes", "intel_ddi_mso_configure", "tgl_dp_tp_transcoder", "dp_tp_ctl_reg", "dp_tp_status_reg",
                  "intel_wait_ddi_buf_idle", "intel_wait_ddi_buf_active", "intel_ddi_prepare_link_retrain",
                  "intel_ddi_set_link_train", "intel_ddi_set_idle_link_train", "intel_ddi_disable_fec", "disable_ddi_buf",
                  "intel_disable_ddi_buf", "intel_ddi_disable_transcoder_func", "intel_disable_ddi_dp", "intel_disable_ddi",
                  "intel_ddi_post_disable_dp", "intel_ddi_post_disable", "intel_ddi_post_pll_disable"],
  "intel_display.c": ["is_hdr_mode", "intel_phy_is_combo", "intel_aux_power_domain", "intel_wait_for_pipe_off",
                      "intel_enable_transcoder", "intel_disable_transcoder", "bdw_set_pipe_misc", "icl_set_pipe_chicken",
                      "hsw_set_linetime_wm", "hsw_crtc_disable"],
  "intel_dp.c": ["intel_dp_set_link_params", "intel_dp_set_power", "intel_edp_backlight_on", "intel_edp_backlight_off"]
 },
 "extra_ranges": {
  "intel_ddi.c": [["static const u8 index_to_dp_signal_levels[] = {", END, "index_to_dp_signal_levels[]"]]
 },
 "new_files": [
  {"out": "intel_ddi_buf_trans_port.c", "dir": "i915", "source": "display/intel_ddi_buf_trans.c",
   "path": "drivers/gpu/drm/i915/display/intel_ddi_buf_trans.c",
   "ranges": table("icl_combo_phy_trans_hdmi") + table("tgl_combo_phy_trans_edp_hbr2_hobl") + table("adlp_combo_phy_trans_dp_hbr") +
             entries("adlp_combo_phy_trans_dp_hbr2_hbr3") + entries("adlp_combo_phy_trans_edp_hbr2") +
             entries("adlp_combo_phy_trans_dp_hbr2_edp_hbr3") + table_shared("adlp_combo_phy_trans_dp_hbr2_hbr3") +
             table_shared("adlp_combo_phy_trans_edp_hbr3") + table_shared("adlp_combo_phy_trans_edp_up_to_hbr2"),
   "functions": ["is_hobl_buf_trans", "use_edp_hobl", "use_edp_low_vswing", "intel_get_buf_trans",
                 "adlp_get_combo_buf_trans_dp", "adlp_get_combo_buf_trans_edp", "adlp_get_combo_buf_trans"],
   "includes": ["lcd_compat.h", "lcd_modeset_compat.h"], "glue": "parity_buf_trans_glue.inc"},
  {"out": "intel_combo_phy_port.c", "dir": "i915", "source": "display/intel_combo_phy.c",
   "path": "drivers/gpu/drm/i915/display/intel_combo_phy.c", "ranges": [],
   "functions": ["intel_combo_phy_power_up_lanes"],
   "includes": ["lcd_compat.h", "lcd_modeset_compat.h"]},
  {"out": "intel_dp_link_training_port.c", "dir": "i915", "source": "display/intel_dp_link_training.c",
   "path": "drivers/gpu/drm/i915/display/intel_dp_link_training.c", "ranges": [],
   "functions": ["intel_dp_reset_lttpr_common_caps", "intel_dp_reset_lttpr_count", "intel_dp_lttpr_phy_caps", "intel_dp_read_lttpr_phy_caps", "intel_dp_read_lttpr_common_caps", "intel_dp_set_lttpr_transparent_mode", "intel_dp_lttpr_transparent_mode_enabled", "intel_dp_init_lttpr_phys", "intel_dp_init_lttpr", "intel_dp_init_lttpr_and_dprx_caps", "dp_voltage_max", "intel_dp_lttpr_voltage_max", "intel_dp_lttpr_preemph_max", "intel_dp_phy_is_downstream_of_source", "intel_dp_phy_voltage_max", "intel_dp_phy_preemph_max", "has_per_lane_signal_levels", "intel_dp_get_lane_adjust_tx_ffe_preset", "intel_dp_get_lane_adjust_vswing_preemph", "intel_dp_get_lane_adjust_train", "intel_dp_get_adjust_train", "intel_dp_training_pattern_set_reg", "intel_dp_set_link_train", "dp_training_pattern_name", "intel_dp_program_link_training_pattern", "intel_dp_set_signal_levels", "intel_dp_reset_link_train", "intel_dp_update_link_train", "intel_dp_lane_max_tx_ffe_reached", "intel_dp_lane_max_vswing_reached", "intel_dp_link_max_vswing_reached", "intel_dp_update_downspread_ctrl", "intel_dp_update_link_bw_set", "intel_dp_prepare_link_train", "intel_dp_adjust_request_changed", "intel_dp_dump_link_status", "intel_dp_link_training_clock_recovery", "intel_dp_training_pattern", "intel_dp_link_training_channel_equalization", "intel_dp_disable_dpcd_training_pattern", "intel_dp_stop_link_train", "intel_dp_link_train_phy", "intel_dp_schedule_fallback_link_training", "intel_dp_link_train_all_phys", "intel_dp_start_link_train"],
   "includes": ["lcd_compat.h", "lcd_modeset_compat.h", "lcd_dp_compat.h"]},
  {"out": "intel_vblank_port.c", "dir": "i915", "source": "display/intel_vblank.c",
   "path": "drivers/gpu/drm/i915/display/intel_vblank.c", "ranges": [],
   "functions": ["pipe_scanline_is_moving", "wait_for_pipe_scanline_moving", "intel_wait_for_pipe_scanline_stopped",
                 "intel_wait_for_pipe_scanline_moving"],
   "includes": ["lcd_compat.h", "lcd_modeset_compat.h"]}
 ],
 "range_headers": [
  {"out": "lcd_power_domain_enum.h", "dir": "i915", "source": "display/intel_display_power.h",
   "path": "drivers/gpu/drm/i915/display/intel_display_power.h",
   "ranges": [["enum intel_display_power_domain {", END, "enum intel_display_power_domain"]]},
  {"out": "lcd_buf_trans_types.h", "dir": "i915", "source": "display/intel_ddi_buf_trans.h",
   "path": "drivers/gpu/drm/i915/display/intel_ddi_buf_trans.h",
   "ranges": [["struct hsw_ddi_buf_trans {", TAB + "u8 hdmi_default_entry;" + END, "buffer translation entry types .. struct intel_ddi_buf_trans"]]},
  {"out": "lcd_dpll_id_enum.h", "dir": "i915", "source": "display/intel_dpll_mgr.h",
   "path": "drivers/gpu/drm/i915/display/intel_dpll_mgr.h",
   "ranges": [["enum intel_dpll_id {", END, "enum intel_dpll_id"]]},
  {"out": "lcd_link_training_inlines.h", "dir": "i915", "source": "display/intel_dp_link_training.h",
   "path": "drivers/gpu/drm/i915/display/intel_dp_link_training.h",
   "ranges": [fn_range("static inline u8 intel_dp_training_pattern_symbol(u8 pattern)", "intel_dp_training_pattern_symbol")]}
 ],
 "macro_headers": [
  {"out": "lcd_mreg_i915_reg.h", "dir": "i915", "source": "i915_reg.h", "path": "drivers/gpu/drm/i915/i915_reg.h"},
  {"out": "lcd_mreg_combo_phy.h", "dir": "i915", "source": "display/intel_combo_phy_regs.h",
   "path": "drivers/gpu/drm/i915/display/intel_combo_phy_regs.h"},
  {"out": "lcd_mreg_vdsc.h", "dir": "i915", "source": "display/intel_vdsc_regs.h",
   "path": "drivers/gpu/drm/i915/display/intel_vdsc_regs.h"},
  {"out": "lcd_mreg_cx0.h", "dir": "i915", "source": "display/intel_cx0_phy_regs.h",
   "path": "drivers/gpu/drm/i915/display/intel_cx0_phy_regs.h"},
  {"out": "lcd_mreg_display_device.h", "dir": "i915", "source": "display/intel_display_device.h",
   "path": "drivers/gpu/drm/i915/display/intel_display_device.h"},
  {"out": "lcd_mreg_display.h", "dir": "i915", "source": "display/intel_display.h",
   "path": "drivers/gpu/drm/i915/display/intel_display.h"},
  {"out": "lcd_mreg_reg_defs.h", "dir": "i915", "source": "i915_reg_defs.h", "path": "drivers/gpu/drm/i915/i915_reg_defs.h"},
  {"out": "lcd_mreg_display_reg_defs.h", "dir": "i915", "source": "display/intel_display_reg_defs.h",
   "path": "drivers/gpu/drm/i915/display/intel_display_reg_defs.h"},
  {"out": "lcd_mreg_drm_dp.h", "dir": "drm", "source": "drm_dp.h", "path": "include/drm/display/drm_dp.h"}
 ]
}
old = json.load(open(sys.argv[1])) if os.path.exists(sys.argv[1]) else {"macro_headers": []}
kept = {h["out"]: h.get("roots", []) for h in old.get("macro_headers", [])}
for h in spec["macro_headers"]:
    h["roots"] = [r for r in kept.get(h["out"], []) if r not in COMPAT_MACROS]
    h["exclude"] = COMPAT_MACROS
json.dump(spec, open(sys.argv[1], "w"), indent=1, sort_keys=True)
print("wrote", sys.argv[1])
