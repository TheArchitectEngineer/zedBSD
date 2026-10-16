<!-- awesome-plan project=zedbsd record=ws245-p050b -->

# ws245-p050b: eDP の step の本体を xd の部品の直の呼びに

Status: planned（2026-10-16 Q1 の判断で p050 を p050a〜d に分けた。design の順のまま p039 の前に行う）
Parent: [WS245](../ws.md)
Queue: q1229-i06（P1）
設計: [design.md](../design.md) §6.2 の p050 の行、§3.3、§4.2

## 範囲

xd-screen の light・dark の step（power・dbuf・setup・pipe dmc・link power・pll・port・pipe・output・check・plane・settle と dark の各 step）の中身を、eDP（内蔵 panel）について xd の部品（xd-panel の PPS と backlight、xd-aux の DPCD、xd-power の DDI IO と AUX の well の参照、xd-dp の訓練、xd-dbuf の WM/DBUF の書き込み、xd-pipe、xd-clock、xd-ddi、vblank の event）を直に呼ぶ形にする。旧の modeset.c・state.c・ddi.c・dp-sink.c・pipe.c・plane.c・aux.c・panel.c・panel-backlight.c・vblank.c の eDP の道の state（Linux の型の PPS・backlight・wakeref・link）は使わなくなる。

## 受け入れ

- host 試験（run-xd-host-tests.sh、新しい check は xd の試験へ）、build（既定と I915_TESTS=y の 5 組）warning 0、段の前後の比べ（旧 host 試験が残る間は refcmp.sh）、R11 の数。
- 5330 の回帰は全部の段の後に 1 回（ユーザーの決定 2026-10-16）。

## 着手の手がかり（2026-10-16 P1、p050a の後の引き継ぎ）

- 今の step の中身: `modeset.c` の `i915_modeset_step_*`（light 12・dark 9、`i915_modeset_light_hooks`/`_dark_hooks`）→ `pipe.c` の `drv_i915_lcd_ms_crtc_enable_part/_disable_part` → `i915_hsw_crtc_enable_part/_disable_part`（DMC・LINK・PLL・PORT・PIPE・OUTPUT）→ `ddi.c` の encoder の段（eDP は `i915_tgl_ddi_pre_enable_dp`（ddi.c:894、pps_on・enable_clock・DDI IO の wakeref・mg dp mode・transcoder clock・func config・signal levels・lanes・MSO・D0・link train の start/stop・FEC）、`i915_enable_ddi_dp`、`i915_disable_ddi_dp`、`i915_ddi_post_disable_dp`、`i915_ddi_post_pll_disable`）と `pipe.c` の source size・misc・transcoder・colour・linetime・chicken、`clock.c` の shared DPLL の enable/disable、`watermark.c` の dbuf pre/post plane update と MBUS DBOX、`dmc.c` の pipe DMC、`modeset.c` の crtc の power domain の get/put。
- 旧の host 試験 `host-lcd-modeset-test`（120 check）の詳細の出力（`build/p050a/old-new/lcd.txt`、`oldtests.sh` が作る）は、commit ごとの全部の trace（register の read/write/rmw、`pw-get`（backend の power の get、domain の番号）、`panel`（backend の PPS の op）、`dbuf`、DPCD、`decide`、observer の点）を持つ。refcmp の SAME はこの trace の一致。新しい step の中身が backend の hook（read32/write32/rmw/usleep、power_get/put/put_async、panel、dpcd_read/write、dbuf_slices_update、step）を同じ順で呼べば SAME を保てる（p028 の形: backend の read32/write32 を hook にした xd の window）。
- 案（既定）: 新しい xd の file（例 `xd-output.c`: 1 つの出力の step）に、eDP の step の中身を xd の部品で書く。環境は hook の構造体（register の window、power の get/put（機能の名前で）、panel の PPS の op、DPCD の accessor、vblank の event の待ち）。殻（modeset.c）は今の backend の hook をその環境に渡し、plan（p038 の `struct i915_xd_plan`。数は旧の check と一致を確かめ済み）・PPS と backlight の設定・plane の語から step を呼ぶ。殻が更新する旧の写し（`ms->crtc.active`・`ms->dig_port.dp.link_rate/lane_count/train_set/link_trained`・`ms->pll.on`・`ddi_io_wakeref`・`aux_wakeref`・backlight の level・`stop_unconfirmed`・`dc_off_held`）は status・confirm・backlight.c が読む間（p050d まで）保つ。
- 先に読む物: `ddi.c` の DP の段（上の関数と `i915_ddi_enable_transcoder_clock`・`i915_ddi_config_transcoder_func`・`i915_ddi_power_up_lanes`・`i915_ddi_init_dp_buf_reg`・`i915_icl_ddi_combo_enable_clock`・`i915_ddi_set_dp_msa`・`i915_disable_ddi_buf`・`i915_ddi_disable_transcoder_func`）、`panel.c` の pps_on/off/vdd、`panel-backlight.c` の enable/disable、`dp.c` の link train の glue、`pipe.c` の PIPE の part、`watermark.c` の `drv_i915_dbuf_pre/post_plane_update`・`drv_i915_mbus_dbox_update`、`clock.c` の `drv_i915_enable/disable_shared_dpll`、`modeset.c` の `drv_i915_modeset_get/put_crtc_power_domains`。
