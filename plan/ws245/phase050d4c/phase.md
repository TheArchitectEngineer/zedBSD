<!-- awesome-plan project=zedbsd record=ws245-p050d4c -->

# ws245-p050d4c: 始まりと終わり（eDP の device・nogem・setup_outputs・VGA・world）

Status: in-progress（2026-10-17 q1229-i12 で実装・host 試験・build 済み（43c3ed47c・f491cfcad）、5330 は全部の段の後に 1 回。world の削除は d4d へ）
Parent: [WS245](../ws.md)
Queue: q1229-i11（P1）
設計: [design.md](../design.md) §6.2 の p050 の行、§3.6。分け方は [phase050d4](../phase050d4/phase.md)

## 範囲

- eDP の device（dp-sink.c の `edp_device_prepare/_init_connector/_fini`、`i915_edp_device_lcd_a` の compute）を xd-edp に。
- `drv_i915_display_nogem_front/_fini`・`drv_i915_setup_outputs`・VGA の client の登録・takeover の world を xd-display/xd-takeover に。display.c の `drv_i915_lcd_world_create/_destroy`・`_set_display_ver/_set_xd_platform` を消す。
- dp-ext-kern.c が呼ぶ dp-sink.c の DPCD・binding は殻 file（p052 で消える）に移す。
- edp-ktest・edp-sync-ktest・aux（ktest）・host-dp-test の oracle を xd-edp・xd-aux の試験へ。

## 受け入れ

- 消した関数の本番の呼び手が 0、build warning 0、`I915_XD_STRICT`、R11。5330 は全部の段の後に 1 回。

## 調べと段取り（2026-10-17 q1229-i12、P1、p050d4b の後。base 2ce3f4de3）

p050d4b の後に own の file（present・head・output・interrupts・display・resident・device）から旧 file への呼びを数えた（`/tmp/…/xref.py` の結果、関数名だけ）: display.c 69（power 13・hotplug 13・state 8・takeover 7・vbt-shell 5・watermark 6・dp-sink 5・modeset 4・dmc-shell 4・tc-kern 3・clock 1・diagnostics 2・dp-ext-kern 1）、resident.c 50（diagnostics 17・power 6・tc-kern 8・vblank 6・aux 3・panel 1・pipe 1・dp-sink 1・dp-ext-kern 3・modeset 1・clock 1・watermark 1・vbt-shell 2）、output.c 13（hotplug・dp-ext-kern・state の panel_mode/size・tc-kern）、interrupts.c 3、present.c 1、device.c 1。p039（hotplug）・p052（tc・dp-ext）・p042（diagnostics・display.c の段）・p041（power の suspend）の物は残す。

nogem の記録の読み手: hotplug.c の `i915_hpd_make_objects`（encoders）、tc-kern.c の `tc_kern_declare_ports`（encoders の is_tc・port）、display.c の driver_probe（encoders の hpd pin）、resident.c（wm の latency・SAGV の block time）。crtc・plane・DPLL の記録の読み手は 0（readout は p050a で消えた）。

段取り（この順で WIP commit を分ける）:
1. **port の表**（xd）: `struct i915_xd` に VBT の child と platform の port から作る port の表（DDI、Type-C か、DP・HDMI・eDP、AUX、device type）。`setup_outputs` の encoder の判断（DVO port → DDI、platform に無い port、同じ port の 2 つ目、DSI、DVI/HDMI/DP でない）を xd で。hotplug.c・tc-kern.c・display.c の driver_probe は nogem の encoders の代わりに xd の port の表を読む（殻に xd の型を渡す）。eDP の connector の初期化は表の eDP の port で display.c が直に呼ぶ。
2. **eDP の device**（own の新しい file `edp-device.c/.h` と xd-edp）: dp-sink.c の `edp_device_prepare/_start/_init_connector/_fini`・late・result・xe の環境の hook、`i915_dp_kernel`（lock・thread・遅延の work・電源）を own に。panel の mode と link は `drv_i915_lcd_compute` の代わりに xd-edid の first timing と xd-screen の plan_link で own の `struct i915_display_mode`・`struct i915_display_link` に（state.c の compute・`i915_state_from_plan`・word の記録器 `drv_i915_lcd_emit_*` と「LCD-A」の log を消す）。resident の hook の `drv_i915_edp_emit_dpcd_*`（aux.c）・`_emit_panel`（panel.c）は edp-device の own の hook に。dp-ext-kern.c が使う `drv_i915_dp_kernel_bind`・`drv_i915_drm_dp_dpcd_*`・AUX の I2C は殻 file `dp-ext-shell.c`（p052 で消える）へ。
3. **始まりの前半**（xd-display と own）: `drv_i915_display_nogem_front` の段（wm の latency の PCODE の読み、SAGV、GMBUS の reset、CD clock の読み、WA、VGA の plane の停止）を xd の部品（`drv_i915_xd_cdclk_read`・`_gmbus_reset`・`_vga_off` と新しい latency の読み）で。nogem の crtc・plane・DPLL の記録と `nogem_fini` を消す。VGA の client の登録（arbiter）は own の display.c へ。takeover.c と takeover-internal.h・takeover.h を空にする（削除は Q1）。
4. **world**: `drv_i915_lcd_world_create/_destroy`・`_set_display_ver`・`_set_xd_platform` は旧 file（pipe・plane・clock・ddi・state・diagnostics の reg_by_name・log_regs）が呼ぶ間は消せない。d4d の旧 file の削除と同じ commit で消す（phase.md の範囲から d4d へ移す: 既定の案、Q1 に報告）。
5. 試験: host-dp-test の oracle は host-xd-edp-test・host-xd-aux-test に既にある物を確かめ、足りない物を足す。edp-ktest 等の ktest は d4a で組ごと外した。

比べ: 段ごとに `sh build/tmp/stagecmp.sh <前> <名前> out50c-d4b.c out50c-d4b.c`（旧 host 試験が通る間）と run-xd-host-tests.sh。

## 結果（2026-10-17 q1229-i12、P1。base 7211a06ef、段の commit 43c3ed47c（port の表と eDP の device）・f491cfcad（始まりの前半と VGA、takeover.c を空に））

形:
- **port の表**（xd-display.c、xd.h）: `drv_i915_xd_ports(platform, vbt, ddi_mask, &ports)` が xd-vbt の child から `struct i915_xd_ports`（DDI・Type-C・DP・HDMI・eDP・AUX・device type）を作る（platform に無い DDI・mask の外・同じ DDI の 2 つ目・MIPI・DP/HDMI/DVI でない child は数えて捨てる）。`struct i915_xd` の `ports`。display.c の `i915_display_setup_outputs` が表を作り、DP の port で eDP の connector を作る（取れない panel の port は落とす）。hotplug.c の `drv_i915_hpd_start`・`i915_hpd_make_objects`、tc-kern.c の `tc_kern_declare_ports`、display.c の hpd pin は表を読む（nogem の encoders は読まない）。mask は今の hotplug・TC の道が扱う DDI（A・B・TC1〜4）のまま（p051 で platform の規則に）。
- **eDP の device**（新しい own の `edp-device.c/.h`）: dp-sink.c から device（prepare・start・init_connector・fini・late・result・xe の環境）と kernel の backend（`i915_dp_kernel`: lock・thread・遅延の work・電源・時計）を移し、aux.c・panel.c の resident 用の hook（`drv_i915_edp_emit_dpcd_read/_write/_read_dpcd_caps`・`_emit_panel`）も移した（aux.h・panel.h は空）。panel の mode と link は `drv_i915_lcd_compute` の代わりに xd-edid の first timing と decode の bpc と xd-screen の plan_link で `struct i915_edp_device` の `mode`・`link`・`link_rc` に（`lcd`・word の記録器の field を消し、「LCD-A」の log を `i915: edp link: …` の 1 行に）。dp-sink.c に残るのは DP の world、旧 host 試験の eDP の道（edp_begin ほか）と dp-ext の DPCD・I2C（p052 まで）、新しい `drv_i915_dp_ext_stop_of`。
- **始まりの前半**（display.c の `i915_display_nogem_front` を書き直し）: xd-dbuf の新しい `drv_i915_xd_memory_read`（SAGV の block time、latency の 2 語、0 us の level の切り、read latency の足し、DIMM の調整: Procedure referenced: skl_watermark.c）、`drv_i915_xd_gmbus_reset`、旧 clock.c の `drv_i915_update_cdclk`（CD clock の dev は旧のまま: d4d 以降）、xd-core の新しい `drv_i915_xd_display_wa`（display 13・12・11 の WA、値の変わらない register は書かない）、VGA の plane の停止。結果は `struct i915_xd` の `memory`、resident の config が読む（deps の nogem を消した）。nogem の crtc・plane・DPLL・GMBUS の記録と `nogem_fini` は使われなくなった（`struct i915_display_nogem` の型と field は internal.h とともに d4d）。
- **VGA**（新しい own の `vga.c/.h`）: arbiter の client の登録・解除、legacy I/O の reset、VGA の plane の停止。呼び手 0 の `drv_i915_gmch_vga_set_state/_set_decode`・`drv_i915_vga_io_test_set` は消した。log の `intel_vga_register` は `vga register` に。
- **takeover.c・takeover.h** は中身を空にし vmunix.mk から外した（takeover world も消した）。takeover-internal.h は旧 file（vblank・clock・ddi・pipe・watermark）が include するので d4d。
- 範囲の変更（既定の案、Q1 に報告）: modeset world（`drv_i915_lcd_world_create/_destroy`・`_set_display_ver`・`_set_xd_platform`）は旧 file（pipe・plane・clock・ddi・state・diagnostics の reg_by_name・log_regs）が呼ぶ間は消せないので、d4d の旧 file の削除と同じ段で消す。

確かめた事:
- 前後の比べ（`sh build/tmp/stagecmp.sh 7211a06ef d4c/cmp2 out50c-d4b.c out50c-d4b.c`）: dp SAME、lcd・out50c の差 0（旧 host 試験は旧の eDP の道を使うので、新しい device の道はこの比べに入らない）。
- `run-xd-host-tests.sh` 26 本 all passed。足した check: host-xd-vbt-test に port の表（一つの output に一つの port、panel が先、mask の外、同じ DDI の 2 つ目、記述の無い device）、host-xd-dbuf-test に memory の読み（5330 の語で 3 us を 6 level に足す・level 6/7 は読んだまま、block time 35 us の後に latency の 2 語、0 us の level の切りと DIMM、SAGV を制御しない、拒まれた mailbox）、host-xd-core-test に WA（display 13・同じ値で書かない・display 12）。
- build（kbuild.sh d4c、既定・vke2・execution）3 組 rc=0 warning 0。`git diff --check` 0。新しく死んだ export は 0（dead の基準は 7211a06ef）、古い宣言 0。R11: 触った file 全部 0。

実機で確かめる事（5330 は全部の段の後に 1 回）: 起動の log の `i915: setup_outputs: … ports=` と `port[n]`、`i915: edp link: rc=0 …`（旧の LCD-A と同じ mode・link・CFGCR）、`i915: wm: levels=6 latency=…`（旧と同じ値）、VGA の plane の停止、HDMI・USB-C の hotplug（port の表から）、TC の declare。Tiger Lake（5320）は display 12 の WA。

Q1 に頼む削除（中身を空にした file）: `src/drivers/gpu/i915/display/takeover.c`、`takeover.h`、`aux.h`、`panel.h`。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- 始まりと終わり: 起動の始まりの log（core started・takeover・sanitize・serving）

満たしていない・未確認の基準と残る物:

- 終わり（reboot の止める道 `stop: display power released`）は i20 で log が届かず未確認

P1 の見立て（Status は変えない。判定は Q1）: 保留（止める道が未確認）。止める道を T1 の次の 5330 で確かめれば cleared。
