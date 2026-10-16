<!-- awesome-plan project=zedbsd record=ws245-p050d2 -->

# ws245-p050d2: prepare の check と WM/DBUF を xd に

Status: planned（2026-10-17 Q1 の判断で p050d を 4 段に分けた 2 段目。p050d1 の後）
Parent: [WS245](../ws.md)
Queue: q1229-i08（P1）
設計: [design.md](../design.md) §6.2 の p038・p050 の行、§2.6、§2.10、§3.3。着手の手がかりは [phase050d](../phase050d/phase.md) の 4

## 範囲

- prepare の判断を xd-screen の plan（p038 の影）を本物にする: `state.c` の compute（mode・link・M/N・PLL の語・CDCLK の下限・voltage）と `modeset.c` の `i915_modeset_prepare_state` の拒む条件、`clock.c` の CDCLK の check を xd-screen の plan と xd-clock に。旧の check との比べの 1 行（p038）を外す。
- WM/DBUF: `watermark.c` の DDB の割り当てと WM の計算・書き込みを xd-dbuf の allocate と level の書き込みに（p034 の影を本物に）。xd-output の記述（PLL の語、M/N、WM、DBUF）は plan から作る（殻の記述の写しを減らす）。
- 残す物: plane・flip・vblank（p050d3）、own の file の口（p050d4）。

## 受け入れ

- xd の host 試験（plan の値が旧の check の値と同じ: 5330 の panel、HDMI、DP-TC。WM/DBUF の語）、build 6 組 warning 0、旧 host 試験の前後の比べ（trnorm）、R11 の数。5330 は全部の段の後に 1 回。

## 着手の手がかり（2026-10-17 P1、p050d1 の後の引き継ぎ）

- 今の形（3604ddfa1）: `modeset.c` の `i915_modeset_prepare_state`（:2787 付近）が crtc state を作り、`i915_modeset_plan_check`（plan の影）→ `drv_i915_lcd_ms_wm_compute`（watermark.c:892、WM と DDB と新しい DBUF の状態を `ms->wm` に）→ `drv_i915_ddi_compute_min_voltage_level`（ddi.c:124）・`drv_i915_lcd_ms_plane_min_cdclk`（plane.c:372）・`drv_i915_lcd_ms_cdclk_check`（clock.c:479）→ `drv_i915_lcd_ms_bw_data_rate`（watermark.c:831）の順。判断は旧、plan との違いは `i915_modeset_plan_compare` が log 1 行。
- 案（既定）: (1) 拒む判断を plan の `refused` に（CDCLK・VOLTAGE は旧の文言「the mode requires a CDCLK state other than the current one: CDCLK programming is not connected」、QGV は「the memory bandwidth of the allowed QGV point is unknown or below what the plane needs」を保つ: lcd 試験の出力が同じになるように）。`ms->cdclk.*`（crtc_min・bw_min・min_cdclk・cdclk・vco・voltage_level・change_needed）、`ms->cdclk_rc`、`ms->bw_data_rate` は plan からの写しに（status の `cdclk_crtc_min`・`cdclk_bw_min` を host-lcd 試験が印字するので、plan に crtc の下限と帯域の下限を別の field で持たせる）。消せる旧: clock.c の CDCLK の check と static、plane.c の `drv_i915_lcd_ms_plane_min_cdclk`、ddi.c の `drv_i915_ddi_compute_min_voltage_level`、watermark.c の `drv_i915_lcd_ms_bw_min_cdclk/_bw_data_rate` と `i915_intel_bw_crtc_*`（`build/tmp/killdead.sh`）。(2) WM/DDB: watermark.c は既に xd-dbuf の殻（`i915_wm_xd_pipe`・`i915_wm_plane_to_xd/_from_xd`、`i915_skl_build_pipe_wm` は `drv_i915_xd_wm_plane` を呼ぶ）。残る Linux の手順は DBUF の slice と join（`i915_skl_compute_dbuf_slices`・`i915_adlp_check_mbus_joined`・`i915_intel_dbuf_enabled_slices`、xd-dbuf の `drv_i915_xd_dbuf_slices/_joined` がある）、pipe の DDB の範囲（`i915_skl_crtc_allocate_ddb`・`i915_intel_crtc_dbuf_weights`、xd の `drv_i915_xd_dbuf_pipe_range`）、plane の DDB（`i915_skl_crtc_allocate_plane_ddb`、xd の `drv_i915_xd_wm_allocate`）。plan の check に WM と DDB を足し（入力に `struct i915_xd_wm_pipe`・`i915_xd_wm_surface` と running の pipe の weight）、`drv_i915_lcd_ms_wm_compute` は plan の結果を `ms->wm`（plane.c の `skl_write_plane_wm` と xd-output の DBUF の記述が読む写し）に写すだけにする。`drv_i915_lcd_ms_wm_compute_off`（dark の DBUF）も同じ形に。
- 比べ: lcd 試験の D の行（crtc min 70400、bw min 11000 → 179200 kHz vco 537600 level 0、564 MB/s、slice 0xf joined）と WM・DDB の書き込み（trnorm の差 0 を目標）。`sh build/tmp/stagecmp.sh <前の段> <名前>`（旧 dp/lcd と harness out50c を trnorm で比べる、P1 の build/tmp）。
