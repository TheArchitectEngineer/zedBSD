<!-- awesome-plan project=zedbsd record=ws245-p050d4c -->

# ws245-p050d4c: 始まりと終わり（eDP の device・nogem・setup_outputs・VGA・world）

Status: planned（2026-10-17 Q1 の判断で p050d4 を 4 段に分けた 3 段目。p050d4b の後）
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
