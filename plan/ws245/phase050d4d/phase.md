<!-- awesome-plan project=zedbsd record=ws245-p050d4d -->

# ws245-p050d4d: 旧 file の削除

Status: in-progress（2026-10-17 q1229-i12 で一部（854bd85fb: resident の vblank・電源の hook を own に、vblank.c を空に）。残りは下の「再開の手順」）
Parent: [WS245](../ws.md)
Queue: q1229-i11（P1）
設計: [design.md](../design.md) §6.2 の p050 の行。分け方は [phase050d4](../phase050d4/phase.md)

## 範囲

- 呼び手 0 になった旧 file（modeset.c・modeset-internal.h・state.c・ddi.c・dp-sink.c の eDP の部分・dp-internal.h・pipe.c・plane.c・aux.c・panel.c・vblank.c・takeover.c の残り・takeover-internal.h・internal.h の Linux の型・watermark.c の modeset の半分・color.c）の中身を空にして vmunix.mk から外す（削除は Q1）。
- 残す物（hotplug.c・tc*.c・dp-ext*.c・hdmi.c・clock.c の TC PLL の pool・diagnostics.c・vbt-shell.c・power.c の殻）に xd.h の型を渡す。
- host-lcd-modeset-test・host-lcd-test・host-dp-test・lcd-modeset-ktest・lcd-fake-hw・dp-fake-hw と harness の oldtests.sh・stagecmp.sh の比べを終える（path を Q1 へ）。oracle は xd の host 試験に移っている事。

## 受け入れ

- 付け替えの後の host 試験の全部（run-xd-host-tests.sh）、`I915_XD_STRICT`、build warning 0、試験の組の image が 16 MiB に入る（size）、R11。QEMU の boot-test と 5320・SV7 の節目は T1・ユーザーの一覧。5330 は全部の段の後に 1 回。

## 一部の実装（2026-10-17 q1229-i12、P1。base 0bb843718、commit 854bd85fb）

- resident.c が旧 file を呼ばないように: vblank の hook（get・put・sleep・arm・wait・cancel、frame は `I915_XD_PIPE_FRMCNT`）と電源の domain の hook（`I915_PW_DOMAIN_*`、AUX の domain は display の版で）を resident.c の static に、backend の fault は `drv_i915_xd_pictures_fault(&k->display->xd.pictures, …)` を直に（power.c の run の hook も）。hook の負の errno は resident.c の own の定義（`I915_RESIDENT_EINVAL/ETIMEDOUT/EIO`、値は旧と同じ）。
- vblank.c・vblank.h は中身を空にし vmunix.mk から外した（control.c・pipe.c・host-lcd-modeset-test の include も外した）。削除は Q1。
- 確かめた事: build 3 組 warning 0、stagecmp（0bb843718）dp SAME・lcd/out50c 差 0、run-xd-host-tests 26 本 all passed、`git diff --check` 0。新しく死んだ export は vblank.c の 6 つだけ（空にした）。

## 再開の手順（2026-10-17、P1 の調べ。次の世代へ）

own の file から旧 file への呼び（854bd85fb、関数名の xref。power・diagnostics・hotplug・tc*・dp-ext*・vbt-shell・dmc-shell は残す物なので除く）:
- output.c: state.c の `drv_i915_display_panel_mode/_size_mm`（2）→ output.c に移す（`d->edp->mode` を読むだけ）。
- display.c: modeset の world（create/destroy・set_display_ver・set_xd_platform）、dp-sink の dp world（dp-ext が使うので残す）、state.c の始まりの段（`drv_i915_mode_config_init`・`_cdclk_init`・`_color_init`・`_dbuf_init`・`_fbc_init`・`_init_quirks`・`_display_state_fini`）、watermark.c（`drv_i915_bw_init/_bw_init_hw/_dram_detect/_pmdemand_init`・wm world）、clock.c（`drv_i915_init_cdclk_hooks`・`_update_cdclk`）。
- resident.c: clock.c の `drv_i915_max_cdclk_freq`、watermark.c の `drv_i915_icl_qgv_bw`、diagnostics の `drv_i915_lcd_log_regs`・`_reg_by_name`（lcd world を読む: preflight の register を xd の define で直に読む形に）。
- 残す旧 file から消す旧 file への呼び: dp-ext-kern.c → aux.c の `drv_i915_dp_aux_init`（dp-sink.c か dp-ext-kern.c へ移す）、power.c → 無し（854bd85fb で fault を直した）、clock.c → world（`drv_i915_lcd_xd_io`・`_xd_platform`・`_lcd_error`: 旧の PLL の code（hdmi/dp の語・dkl describe・enable/disable・shared_dpll_state）は旧 host 試験だけが使う: 試験と一緒に消す）、watermark.c → state.c の `drv_i915_alloc_global_state`・`_atomic_global_obj_init`（bw の global state: watermark.c へ移すか、bw の判断を xd に）、dp-sink.c → panel.c の pps（旧 host 試験の eDP の道 `drv_i915_edp_begin/_init_late/_end/_snapshot/_dpcd_*/_panel_op/_work_run` だけ: 試験と一緒に消す）。

順:
1. output.c に panel_mode/size を移す。resident の preflight の register の読み（TRANSCONF・PLANE_CTL_1・ICL_DPLL_ENABLE・DDI_BUF_CTL・GEN8_DE_PIPE_IMR・MBUS_CTL）を xd の define で、`drv_i915_lcd_log_regs` の呼び（preflight・picture-up・after-disable）は消すか xd の survey の読みに（log の文言の変更は R11 の基準で）。
2. 旧 host 試験と model を消す: host-lcd-modeset-test・host-lcd-test・host-dp-test・lcd-fake-hw・dp-fake-hw（中身を空に、path を Q1 へ）。その oracle は xd の試験にある事を確かめる（host-xd-edp-test・host-xd-aux-test・host-xd-picture-test・host-xd-output-test。足りない物を足す）。harness の oldtests.sh・stagecmp.sh の比べはここで終わる（以後は run-xd-host-tests.sh と build だけ）。
3. 呼び手 0 になった旧 file を空にして vmunix.mk から外す: modeset.c（world: display.c の呼びを消す）・modeset-internal.h・modeset.h・state.c/.h（始まりの段: 下の 4）・ddi.c/.h・dp.c/.h・pipe.c/.h・plane.c/.h・panel.c・aux.c（`drv_i915_dp_aux_init` を移した後）・dp-sink.c の eDP の道・takeover-internal.h・watermark.c の modeset の半分と wm world・clock.c の旧の PLL。`killdead.sh`・`staledecl.py`・`rmstaticdecl.py` で呼び手 0 を辿る。
4. 始まりの段（state.c）: mode_config・color・dbuf・fbc の記録は読み手を数えて 0 なら消す。quirks（quirk_mask の読み手）と bw の global state（QGV の mask: resident の config が読む）は残す先を決める（既定の案: watermark.c に bw の global state の確保を移す。QGV・SAGV の xd 化は p042 の段の縮約で）。
5. internal.h: 消した file だけが使う Linux の型（crtc・plane・encoder・nogem・lcd_state・lcd_words・dp_env の eDP の道・edp_result の旧の field など）を消す。`struct i915_display` を display-internal.h へ移すのは残る世界（power・hotplug・tc・dp-ext）が internal.h の型を持つ間はできない: p042・p052 へ（Q1 に報告）。
6. 受け入れ: run-xd-host-tests.sh、`I915_XD_STRICT`、build 3 組 warning 0、試験の組の image の size（`size build/d4d/k-execution/vmunix` が 16 MiB に入る）、R11。QEMU の boot-test と 5320・SV7 の節目は T1・ユーザーの一覧。
