<!-- awesome-plan project=zedbsd record=ws245-p050d4d -->

# ws245-p050d4d: 旧 file の削除

Status: in-progress（2026-10-17 q1229-i13 で実装・host 試験・build 済み（a702fb741・8f2d1f564・f25557317）、5330 は全部の段の後に 1 回。QEMU の boot-test と 5320・SV7 の節目は T1・ユーザーの一覧）
Parent: [WS245](../ws.md)
Queue: q1229-i11・q1229-i12・q1229-i13（P1）
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

## 実装（2026-10-17 q1229-i13、P1。base d5fe3c20a、commit a702fb741・8f2d1f564・f25557317）

再開の手順 1〜6 を行った。

- 1: `drv_i915_display_panel_mode/_size_mm` を output.c の static（`d->edp->mode` を読む）に移した。resident の preflight の register は xd の define（`I915_XD_TRANS_CONF(0)`・`I915_XD_PLANE_CTL(0)`・`I915_XD_DPLL0_ENABLE`・`I915_XD_DDI_BUF_CTL(0)`・pipe A の IMR・`I915_XD_MBUS_CTL`）で直に読む。`drv_i915_lcd_log_regs` の 3 つの呼び（preflight・picture-up・after-disable、`i915: LCD-B reg[...]` の行）は消した（読む道具・試験は無い: grep は bug の log だけ。picture up の register は p047 の survey が取る）。表 `drv_i915_lcd_reg_table`・`_reg_by_name` も消した。
- 2: 旧 host 試験と model（host-lcd-modeset-test・host-lcd-test・host-dp-test・lcd-fake-hw・dp-fake-hw）を空にした。oracle は xd の試験にある事を値で確かめた: DDI_BUF_CTL 0x80000002 と 2×270000（host-xd-ddi-test・-output-test）、WM0 0x80004010 と PIPE の 0x01038806（host-xd-dbuf-test）、0x70188（host-xd-pipe-test）、bandwidth 564（host-xd-screen-test）、dither（host-xd-pipe・-output-test）、0x164280・0x70030・0x7003c（host-xd-output-test）、PWM 0xc8250（host-xd-panel・-output-test）、訓練の失敗（host-xd-output-test の「a link that does not train」、host-xd-dp-test）、eDP の sink（host-xd-edp-test・-aux-test）。harness の oldtests.sh・stagecmp.sh の比べはここで終わり（以後は run-xd-host-tests.sh と build）。tests/display/README.md を今の試験に書き直した。
- 3: modeset.c/.h・modeset-internal.h・state.c/.h・ddi.c/.h・dp.c/.h・pipe.c/.h・plane.c/.h・panel.c・takeover-internal.h・watermark-internal.h・edid.c/.h（drm_modes の殻、読み手は pipe.c だけだった）・intel/clock.h（CDCLK の表、読み手 0）を空にし、.c を vmunix.mk から外した。modeset の world（create・destroy・set_display_ver・set_xd_platform・`drv_i915_lcd_display_ver`）は display.c の呼びを消した: hotplug の `i915_hpd_display_ver` は hdmi.c の「version 11 以上なら live を先に」だけが使っていたので、この道（display 11〜13）では常に live を見る形にした、resident の preflight は `device->gt.display_ver`。clock.c は CD clock の殻だけ（旧 PLL の pool・DKL の route・`drv_i915_icl_hdmi_wrpll/_dp_combo_pll`・呼び手 0 の `bxt_calc_cdclk*`・`bxt_set_cdclk` を消し、intel_dpll_mgr.c の表示も消した）。watermark.c は bandwidth と SAGV だけ（wm world・global object・pmdemand の object（display 14 だけの WA）を消した）。dp-sink.c の eDP の道（`drv_i915_edp_begin/_init_late/_work_run/_snapshot/_end/_dpcd_*/_read_dpcd_caps/_panel_op` と static）を消した。aux.c（DP-ext の AUX、p052 まで残る）は PPS の呼びを、eDP でない port での Linux の振る舞い（display core の電源の参照と PPS の mutex だけ、VDD は eDP だけ）に直した。aux.c と dp-sink.c の DP world は dp-ext が使うので p052 で消す（`drv_i915_dp_aux_init` は移さなかった: p052 で xd-aux に替わる）。新しく死んだ export（diagnostics の `trace_find`・`kernel_note_sink`・`observer_pipe_active`、power の `enabled_dbuf_slices_mask`・`power_domain_hw_state_on`）と古い宣言を消した。diagnostics の observer の register は xd の define に（`I915_XD_PIPE_STATUS_UNDERRUN_SOFT/_HARD/_PORT` を display-regs-xd.h に足した、出典の comment に名前を追記）。
- 4: 始まりの段（state.c）: mode_config・CDCLK と DBUF の global object・colour・FBC・quirk は読み手が log だけだったので消した（quirk_mask を読む code は無い）。bw の global state は `struct i915_display_state` を bandwidth だけ（`bw_obj_state.qgv_points_mask` と SAGV の結果）にして watermark.c の `drv_i915_bw_init` が直に消去する。display.c の noirq の後半は bw だけ（`i915: intel_bw_init:` の行は obj の数を除いて残す）。`drv_i915_has_sagv` の legacy platform の引数を消した。
- 5: internal.h: global state・mode_config・FBC・quirk・legacy platform の型、`struct i915_display_nogem` とその型（encoder・plane・crtc・dpll・gmbus pin、`I915_NOGEM_*`、port と phy の define は使う `I915_PORT_A/B/D/TC1` だけ残す）、lcd_state・lcd_words・regwrite・link・pll・dkl_words・named_reg・test_summary、edp_stage・display_step、読み手 0 の define（LCD_MS・LCD_FLIP・OUTPUT・OPREGION_POLICY・COMBO_PHY・VGA_RSRC の一部・HPD の一部）、`struct i915_display` の `nogem`・`lcd_world`・`wm_world`・`dstate_inited`（`nogem_inited` は `front_done` に）を消した（3006 → 約 2370 行）。`i915_lcd_mode` の別名は own の file では `i915_display_mode` に直し、別名は他の WS の host 試験（plan/ws075/tests/hdmi/host-output-test.c、80 checks PASS）のために残す。`struct i915_display` を display-internal.h へ移すのは、残る世界（power・hotplug・tc・dp-ext・vbt-shell・edp-device の kernel backend）が internal.h の型を持つ間はできない: p042・p052 へ。
- Q1 の承認（2026-10-17）で plan/ws084/tests/display-host-lib.sh の既定に mmio-record.c を足した（xd-io の記録を全ての display host 試験が link するため。runner が I915_HOST_EXTRA_PRODUCTION で同じ file を渡しても 1 回だけ compile）。
- WS237 の host-gen9-mode-test.c の `struct i915_lcd_mode`（internal.h の別名、この試験は internal.h を読まない）を `struct i915_display_mode` に直した。

### 確かめた事（6）

- build 3 組（既定・`I915_TESTS=y` の vke2・execution）warning 0（f25557317）。
- `sh plan/ws245/tests/run-xd-host-tests.sh` 26 本 all passed（`I915_XD_STRICT` の check を含む）。
- 他の WS の display host 試験: WS237 の gen9 の 5 本（flip 48・backlight 21・mode 71・modeset 80・outputs 29 checks、0 failures）、WS229 の bootfb（20 checks）、WS051 の host-tc.sh（112 checks）、WS075 の hdmi/host-output-test.sh（80 checks）がすべて 0 failures。base（d5fe3c20a）では gen9 と bootfb は mmio-record の link error と host-gen9-mode-test の型で落ちていた（この段で直した）。
- size: `size build/d4d/k-*/vmunix` の合計 14499040 byte（16 MiB = 16777216 に入る）。
- R11: 触った file の数（`grep -cE 'ws[0-9]{3}|WS[0-9]{3}|BUG-[0-9]+|review [A-Z][0-9]|\bP[0-9]+[a-z]? '`）は全部 0（aux.c・clock.c/h・diagnostics.c/h・display.c・dmc-shell.c・dp-internal.h・dp-sink.c/h・hdmi.c・head.c・hotplug.c・hotplug-internal.h・internal.h・output.c/h・power.c/h・present.c・resident.c・watermark.c/h・display-regs-xd.h・host-kernel.c・host-xd-vbt-test.c・host-gen9-mode-test.c（2 → 0）・tests/display/README.md（3 → 0、plan/ への参照も消した））。
- `git diff --check` 0。

### 他の WS の試験（判断は Q1）

- plan/ws051/tests/host-n1-tc.c・host-n1-tc.sh: 空にした modeset-internal.h・takeover-internal.h・ddi.h を読む（この段で壊れる）。master の一覧・未完の Phase から参照なし（phase004b の過去の記録だけ）。案: 削除（N1 の TC の walk は xd-takeover・xd-tc の試験）。
- plan/ws051/tests/host-power-map.c・host-power-map.sh: base で既に壊れている（空の phy.h・dc9.h・dkl-phy.h と modeset-internal.h を読む）。案: 削除（well の表は host-xd-power-test）。
- plan/ws051/tests/host-dpext.sh: base で既に link error（`drv_i915_xd_dp_fallback`）。p052 で dp-ext の試験を xd に移す時に直すか消す。

### 実機で確かめる事

- 5330: 起動・panel・backlight・外部出力（HDMI・USB-C DP の AUX: aux.c の PPS の lock を外した道）は全部の段の後の T1 の 1 回で。preflight の log の文言は `i915: resident display: preflight: …` のまま（`LCD-B reg[...]` の行は無くなる）。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- size 14499040 byte（16 MiB に入る、phase.md の記録）

満たしていない・未確認の基準と残る物:

- QEMU の boot-test は T1 の依頼に無く未実施
- 5320・SV7 の節目: ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: 保留（QEMU の boot-test が未実施。T1 に頼めば足りる）。
