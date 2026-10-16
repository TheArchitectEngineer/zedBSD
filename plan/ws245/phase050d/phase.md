<!-- awesome-plan project=zedbsd record=ws245-p050d -->

# ws245-p050d: 口 xd.h と own の file の付け替え、旧 file と旧 host 試験の削除

Status: 分けた（2026-10-17 Q1 の判断、q1229-i08: [p050d1](../phase050d1/phase.md)〜[p050d4](../phase050d4/phase.md) の 4 段。この file は範囲と着手の手がかりの記録）
Parent: [WS245](../ws.md)
Queue: q1229-i06（P1）
設計: [design.md](../design.md) §6.2 の p050 の行、§3.3、§4.2

## 範囲

own の present.c・head.c・output.c・control.c・backlight.c・interrupts.c・display.c・device.c の呼び先を xd.h に（design §4.2）。旧 file（modeset.c・modeset-internal.h・state.c・ddi.c・dp-sink.c・dp-internal.h・pipe.c・plane.c・aux.c・panel.c・panel-backlight.c・vblank.c・takeover.c の残り・takeover-internal.h と internal.h の Linux の型）の中身を空にして vmunix.mk から外す（file の削除は Q1）。旧 host 試験（host-dp-test・host-lcd-modeset-test）を消し、その oracle の check を xd の試験へ移す。

## 受け入れ

- host 試験（run-xd-host-tests.sh、新しい check は xd の試験へ）、build（既定と I915_TESTS=y の 5 組）warning 0、段の前後の比べ（旧 host 試験が残る間は refcmp.sh）、R11 の数。
- 5330 の回帰は全部の段の後に 1 回（ユーザーの決定 2026-10-16）。

## 着手の手がかり（2026-10-17 P1、p050c の後の引き継ぎ）

- 今の形: light・dark の step は 3 種とも `xd-output.c`（環境 `struct i915_xd_output_env`、Type-C は `struct i915_xd_output_tc` の hook）。殻 `output-shell.c` が modeset object から記述を作り、backend の hook を環境にし、旧の写しを更新する（`drv_i915_lcd_ms_xo_*`）。
- 残る旧 file の大きさ（81332cf8e）: modeset.c 5054、modeset-internal.h 3718、internal.h 3315、dp-sink.c 3120、power.c 2701、clock.c 2473、watermark.c 2062、takeover.c 1661、state.c 1648、panel.c 1501、dp-internal.h 1500、ddi.c 1253、pipe.c 931、vblank.c 811、output-shell.c 717、aux.c 661、plane.c 660、dp.c 427、panel-backlight.c 318、color.c 282（約 3.5 万行）。
- own の file が呼ぶ旧の口（xd.h に置き換える物）: `drv_i915_lcd_world_create/_destroy`、`_set_xd_platform`・`_set_display_ver`、`_modeset_select/_prepare/_status/_retained/_commit_enable/_commit_disable/_plane_disable/_plane_released/_flip_nowait/_flip_poll/_flip_wait_screen/_flip_settle/_brightness/_brightness_get/_backlight`、`_show_retained`、`_resident_buffer/_back/_flip`、`_output_params/_output_cfg/_output_link_fallback`、`_kernel_resident_run/_preflight/_locks_init/_gpu_retained/_fill_cfg/_bind_ops/_abandoned`、`_last_resort_stop`、`_debug_set/_enabled`、`_compute_hdmi/_dp_ext`、`drv_i915_edp_device_prepare/_init_connector/_fini`。
- 旧 file を空にする前に xd に要る物（どれも今は旧の code が持つ）:
  1. 内蔵 panel の sink の取得（dp-sink.c の `drv_i915_edp_begin/_init_late`・`edp_device_*`: DPCD の caps、EDID、PPS の初期化と VDD）→ xd-aux（`drv_i915_xd_dpcd_read/_write`）・xd-panel・xd-edid。
  2. kernel の panel の hook（panel.c の `drv_i915_edp_emit_panel` → `drv_i915_edp_panel_op`: PPS の on/off/VDD/backlight bit）→ xd-output の環境の panel を xd-panel の直の呼びに（`drv_i915_xd_pps_*`、待ちの時刻の記録）。
  3. DPCD の accessor（dp.c の `drv_i915_dp_sink_bind` → aux.c の transfer）→ xd-aux の native read/write と DP-ext の own の channel。
  4. prepare の check（state.c の compute、modeset.c の `i915_modeset_prepare_state`、watermark.c の DDB/WM、clock.c の CDCLK の check）→ xd-screen の plan（p038 の影）を本物に、xd-dbuf の allocate と WM の書き込み。
  5. plane の update と flip・vblank の event と evasion（plane.c・vblank.c・modeset.c の flip）→ xd-pipe の plane と xd-screen の flip（p049 の判断）。
  6. status・show run・resident run・kernel の binding（modeset.c）→ xd-display。
  7. ddi.c に残る Type-C の物（DKL の levels・DP mode・WA、buf trans の表、`voltage_max`）は p040 まで要る: 小さな殻 file に移すか、p040 を先に行う（Q1 の判断）。clock.c の TC PLL の pool も p040・p052 まで。
- 案（既定）: p050d は 1 段では大きい（p050b・c の 2〜3 倍）。1→6 の順に 4 段程度に分けるのが安全（p050d1 sink と panel の op、p050d2 prepare と WM/DBUF、p050d3 plane・flip・vblank、p050d4 xd.h の口・own の file の付け替え・旧 file と旧 host 試験の削除）。旧 host 試験（host-lcd-modeset-test・host-dp-test）の oracle は p050d4 で xd の試験へ。各段の比べは `build/tmp/trnorm.py`（lcd 試験）と harness `temp/harness/run-out50c.sh`（HDMI・DP-TC・DP-alt）。
