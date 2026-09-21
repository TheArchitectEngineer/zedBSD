# S4 agent reports (condensed)

## P5 hotplug/gmbus/hdmi (done)
- display/{hotplug,gmbus,hdmi}.c/.h, env hotplug-internal.h. i915-cc ok.
- neutral hotplug entry points take struct i915_display * first. Extra exports: drv_i915_hpd_world_create/destroy(display), drv_i915_ddi_hpd_pin, hpd_irq_setup, hpd_init, hpd_poll_disable (for P9 display.c), hpd_pch_irq (interrupts.c), hpd_events (old rd_events, for P9 display ops table), event_bits/pin_count (tests).
- XXX: flush_reenable rebuilt (requeue at tail); HPD pin fixed ver 13; mutex_is_locked -> mutex_owned.
- deps: drv_i915_drm_edid_read (P6) returns block count or negative Linux errno; drv_i915_lcd_display_ver(void) (P9).

## P6 edp/aux/pps/edid-read (done)
- display/{dp-sink,aux,panel}.c/.h, edid-read.c. i915-cc ok. Needs P2 drv_i915_vbt_emit/fmtcheck to link.
- drv_i915_edp_* take struct i915_dp_world * first. dp-env-typed decls appended to dp-internal.h (aux_init, drm_dp_aux_init, drm_edid_read, pps_*).
- new exports: dp_world_create/destroy(display), edp_world_of, dp_kernel_bind, edp_device_prepare/init_connector/fini; emit hooks for P9 bind_ops: edp_emit_dpcd_read/_write/_read_dpcd_caps (aux.h), edp_emit_panel (panel.h).
- DECISION (integrator): eDP bring-up/DPCD/panel entry points keep negative Linux errno (-I915_EDP_E*) — observable/compared (plan §1.2). Accepted.
- file-scope i915_dp_current_world pointer (helpers lack device arg) — XXX for later.
- XXX: dp env display ver 13 (WA 22019252566, AUX power request bit).

## P2 vbt/opregion (done)
- display/vbt.c + vbt-parse.h, opregion.c + opregion.h; data/display-vbt-tables.inc, display-opregion-mailbox.inc, display-acpi-display.inc (GPL-2.0!).
- opregion glue/ACPI chain take struct i915_display * first. notify_encoder(port,type,enable) unchanged. old parity_bios_is_valid_vbt -> drv_i915_bios_is_valid_vbt_header.
- exports for P9: vbt_world_create/destroy, opregion_world_create/destroy, bios_init_ex, bios_init, bios_driver_remove, bios_set_opregion_vbt, vbt_explicit_pin, bios_process_vbt, bios_init_vbt_missing_defaults, vbt_init/fini, vbt_set_log_level.
- file-scope pointers i915_vbt_bound_world / i915_opregion_bound_display (hooks lack world arg) — XXX; second world create -> EBUSY.
- opregion-internal.h: old Linux-named prototypes (intel_opregion_setup etc.) now undefined -> clean at integration.
- LICENSE (report to user): ACPI functions + display-acpi-display.inc are GPL-2.0 derived (intel_acpi.c). Pre-existing in old tree (parity/lcd/intel_acpi_port.c). User decision.
- XXX kept: OpRegion panel-type source answers -ENODEV; VBT parser ver 13.

## P3 state/watermark (done)
- display/state.c/.h, watermark.c/.h, data/display-wm-dbuf-slices.inc. i915-cc ok. Needs drv_i915_lcd_display_ver() from P9 modeset.c.
- A09 args: lcd_compute/emit_*/error_bind take struct i915_lcd_world * first; dbuf_pre/post_plane_update, mbus_dbox_update(wm ctx, state); lcd_ms_wm_compute(_off)(wm_world, ms).
- exports: wm_world_create/destroy(display), alloc_global_state, atomic_global_obj_init.
- shared headers: watermark-internal.h + display field in i915_wm_world; modeset-internal.h ms_wm_compute prototypes changed (ACCEPTED, no caller yet).
- stale prototypes to clean: modeset-internal.h intel_dbuf_pre/post_plane_update, intel_mbus_dbox_update; watermark-internal.h intel_wm_plane_visible, intel_usecs_to_scanlines, drm_mode_get_hv_timing; takeover-internal.h intel_bw_crtc_update.
- file-scope i915_state_sink_world (note/error lack world arg) XXX.

## P4 takeover/diagnostics (done; needs P9 headers scanout.h, modeset.h)
- display/takeover.c (6072 lines) + takeover.h, diagnostics.c/.h. Compile blocked only on P9 headers (stubs used privately).
- exports as appendix A; added takeover_world_create/destroy, drv_i915_display_survey(display) (old P5-0 block).
- deviations: n1_* take display first; lcd_reg_by_name/_reg_table take lcd world; abandoned/gpu_retained take display; n0_pipe_powered ctx = display (P9 must set); n1_mirror_console(so) 0/ENOENT; nogem_front, vga_disable, vga_io_test_set, native_precheck/_log/_log_again, opregion_read_data take display; old parity_intel_dpll_sanitize_state -> drv_i915_nogem_dpll_sanitize_state; kernel_summary test-only.
- k_note_sink/k_step/k_error/k_debug exported as drv_i915_lcd_kernel_* for P9 hook table.
- assumed P9 signature: drv_i915_scanout_publish (check at integration).
- header issues: takeover-internal.h I915_TAKEOVER_FOR_EACH_ENCODER_ON_CRTC macro broken (param crtc replaces .crtc member) -> fix/remove; stale Linux-named readout prototypes; P1 changed crtc_compute_min_cdclk / n1_dpll_readout_hw_state to take takeover.
- observer errno positive (P9 callers compare != 0); eDP connector hook still `< 0` (P6 convention kept).

## P7 ddi/dp/edid/hdmi-mode (done)
- display/{ddi,dp,edid,hdmi-mode}.c/.h (decls under #ifdef I915_DISPLAY_WORLD_MODESET). i915-cc ok. ddi.c also includes takeover-internal.h.
- current device: ms->world->i915_lcd_cur_i915; ddi_ms / i915_lcd_only_encoder are world fields set by drv_i915_lcd_ms_bind_encoder(ms).
- drv_i915_encoders_*(state, crtc) find world via NEW field state->world (intel_atomic_state) set by bind_encoder.
- ddi_emit(world,...), edid_preferred_mode(world,...), ddi_sanitize_encoder_pll_mapping(takeover, encoder).
- extra exports: dp_link_symbol_clock, dp_effective_data_rate, ddi_connector_get_hw_state, lcd_ms_bound_port(world), lcd_hdmi_set_infoframes, lcd_hdmi_tmds_output.
- INTEGRATION TODO: takeover.c drv_i915_n1_atomic_state(): add `takeover->n1.state.world = takeover->display->lcd_world;` (test path).
- INTEGRATION TODO: internal.h link_status_rc comment: now 0/positive errno.
- orphan decls in modeset-internal.h to clean (intel_encoders_*, intel_dp_*, drm_dp_*, drm_mode_set_crtcinfo, drv_i915_lcd_drm_mode_create + #define drm_mode_create, i915_lcd_hdmi_set_infoframes).
- XXX: HDMI scrambling conn_state->connector type confusion (old); big joiner walk mask 0; dead functions kept __maybe_unused.

## P8 pipe/plane/vblank/color/panel-backlight (done)
- display/{pipe,plane,vblank,color,panel-backlight}.c/.h. i915-cc ok.
- modeset-internal.h: added `world` to struct i915_lcd_modeset; P9 prepare must set ms->world (sent to P9).
- run hooks drv_i915_lcd_kernel_{vblank_get,vblank_put,vblank_sleep,arm_event,wait_event,cancel_event} for lk.ops.
- XXX: frame counter via reg_by_name table (pipe A); big joiner walk dropped (mask 0).
- LICENSE TODO (integrator, end of S4): P8 dropped Intel MIT notices of Linux-derived files — audit all display/*.c derived from Linux ports and restore the MIT notice under the Zlib header.

## P1 power/clock/phy/dmc (done)
- display/{power,clock,phy,dmc}.c/.h; data/display-clock-tables.inc, display-phy-buf-trans.inc. i915-cc ok.
- k_power_* -> drv_i915_lcd_power_get/_get_if_enabled/_put/_put_async (for P9 hook table).
- prototype changes ACCEPTED: lcd_ms_alloc_pll(world,ms,hw) (neg on refusal), lcd_ms_release_pll(world,ms), lcd_ms_cdclk_check(takeover,ms), n1_dpll_readout_hw_state(takeover,i915), crtc_compute_min_cdclk(takeover,cs), lcd_dplls_reset(world), lcd_ms_release_pipe(world,pipe).
- display_power_get returns EIO (log "power get ... FAILED rc=5" instead of -1).
- XXX: icl_calc_wrpll abs() on unsigned (old); TGL TC cold-off well not built.
- test hooks parity_dmc_test_pause/fault_at dropped.

## P9 modeset/scanout/present/display/interrupts + integration (done)
- display/{modeset,scanout,present,display,interrupts}.c/.h; device.c display stages in old probe order; i915.c binds display/scanout ops + CAP_DISPLAY|DISPLAY_EVENTS when panel; worker sync kinds batch/present/present_blob/release, serving-loop inversion kept; interrupts via i915_irq_display_ops (interim hooks only without display).
- N0 STOP -> display absent, node published without display ops (ACCEPTED: plan §7, safer than old whole-probe stop).
- active firmware crtc at display probe -> start fails ENOTSUP (old BLOCKED).
- ~124 stale prototypes removed; takeover n1.state.world set; link_status_rc comment fixed; broken macro param renamed.
- file-scope bound-world pointer for lcd_display_ver/backend_fault/debug (XXX).

## Integrator after P9
- CRLF -> LF in 9 files (display vblank/panel-backlight/pipe/color/plane, render blit/draw/state).
- HW: static frame shared GPU copy, rgb 9461546…19b1 = E-130, LCD-B reg dump 17/17, ended PASS (photo s4-lcd-static.jpg). Live 40 s: 695 frames, ended PASS stop confirmed buffers released (photo s4-lcd-live2.jpg).
- LICENSE: restored Linux notices in 17 display .c (from old port file headers) and 5 display headers (from Linux 7.2 source headers; same MIT text).
