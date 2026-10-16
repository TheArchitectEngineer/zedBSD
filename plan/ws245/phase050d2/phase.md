<!-- awesome-plan project=zedbsd record=ws245-p050d2 -->

# ws245-p050d2: prepare の check と WM/DBUF を xd に

Status: planned（2026-10-17 Q1 の判断で p050d を 4 段に分けた 2 段目。p050d1 の後。q1229-i09 で P1 が着手し、source を変える前にユーザーの指示「いったんi915の作業をラップアップします。」（Q1 経由、2026-10-17 01:4x）でラップアップ: 調べと形の案だけを下の「再開の手順」に残した。source の変更は無い）
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

## 再開の手順（2026-10-17 01:41 P1、q1229-i09 のラップアップ。source は未変更、base 3a974d882）

調べた事（3a974d882 の code で確かめた）:
- 旧 host 試験の基準: `sh plan/ws245/temp/harness/oldtests.sh <tree> build/d2/old-base` → dp 72 check・lcd 120 check とも 0 failures、lcd の出力に `plan's check differs` の行は 0（p038 の plan の check は旧の判断と今も一致）。
- 旧の判断の順と文言（`modeset.c` の `i915_modeset_prepare_state` の末尾）: (1) WM/DDB（`drv_i915_lcd_ms_wm_compute`、失敗は `ms->wm_rc` を返すだけで文言なし）→ (2) `drv_i915_lcd_ms_cdclk_check`（`clock.c:479`。要る CD clock が最大を超える・step が無い時は `I915_LCD_EINVAL`（22）を `ms->cdclk_rc` に返すだけで文言なし、`ms->cdclk.crtc_min`・`bw_min` だけ記録）→ (3) `ms->cdclk.change_needed` なら文言「the mode requires a CDCLK state other than the current one: CDCLK programming is not connected」で EINVAL → (4) QGV は文言「the memory bandwidth of the allowed QGV point is unknown or below what the plane needs」で EINVAL。plan の `I915_XD_PLAN_CDCLK` は (2) と (3) の両方を含むので、殻は `plan->cdclk_khz == 0`（step を選ぶ前に拒まれた）で (2) と (3) を分ける。
- 状態の写しの読み手: status（`modeset.c:705-723`: `ddb_start/end`・`wm0_*`・`dbuf_slices_wanted`・`mbus_joined`・`cdclk_*`・`bw_data_rate`）、`output-shell.c:268-273`（DBUF の記述: old/new の enabled_slices・joined・active_pipes）、`skl_write_plane_wm`（`watermark.c:1101`: `crtc_state->wm.skl.optimal` と `plane_ddb`）、commit の publish（`modeset.c:4774`・`:5004`: `ms->wm.new_dbuf`）。`wm.skl.raw`・`wm.skl.ddb`（絶対の番地）・`new_dbuf.ddb` は watermark.c の中でしか読まれない。
- plane は拡縮なし（`plane.c:308-311` src = dst）・linear XRGB8888 だけ（`drv_i915_xd_plane_check`）なので、plane の pixel rate = mode の clock、data rate = clock × 4、相対の data rate = fb の幅 × 高さ × 4（async flip なし）。

形の案（未実装）:
1. `xd-screen.h` の `struct i915_xd_plan_input` に足す: WM の pipe（`struct i915_xd_wm_pipe`: levels・latency・ipc・SAGV の block time。pixel rate と htotal は plan が mode から埋める）、fb の幅と高さ、frame start delay、running の他の pipe の slices と weight（`ms->wm.old_dbuf` の `slices[4]`・`weight[4]`）。`struct i915_xd_plan` に足す: `crtc_min_cdclk_khz`・`bw_min_cdclk_khz`（status の `cdclk_crtc_min`・`cdclk_bw_min`）、`struct i915_xd_plane_wm wm`（cut の後と allocate の後）、`wm_cut`（旧の `wm_level_disabled`）、`weight`、`enabled_slices`（S1 と各 pipe の slices の和: xd-dbuf に `drv_i915_xd_dbuf_enabled` を足す）、pipe の DDB の範囲、plane と cursor の DDB、`wm_error`。拒みに `I915_XD_PLAN_WM`（WM/DDB が満たせない、`wm_error` を status の `wm_rc` に）を足し、`refusal_name` にも。
2. `drv_i915_xd_screen_plan_check` の順を旧と同じに: slices と joined → WM（`drv_i915_xd_wm_plane` → `drv_i915_xd_wm_vblank_level`（vblank の行 = vtotal − vdisplay）→ display 12 未満は SAGV の短さを無視 → `drv_i915_xd_wm_cut`）→ weight（hdisplay）と同じ slices の pipe の weight の和 → `drv_i915_xd_dbuf_pipe_range` → `drv_i915_xd_wm_cursor_blocks`（run_pipes の数）→ `drv_i915_xd_wm_allocate`（相対の data rate）→ CD clock（crtc の下限 = pixel/2 と TGL の pixel rate の規則、帯域の下限 = data × 10 / 512、下限 = 大きい方）→ voltage → QGV。`input.cpp` は残す（host-xd-screen-test・host-xd-cover-test が埋める）。
3. 殻: `drv_i915_lcd_ms_wm_compute(wm_world, ms, plan)` は plan の結果を `ms->wm.new_dbuf`（active_pipes・joined・slices[pipe]・enabled_slices・weight[pipe]・ddb[pipe]・base.changed）、`crtc_state->wm.skl.optimal.planes[PRIMARY]`・`plane_ddb[PRIMARY/CURSOR]`・`wm_level_disabled` に写すだけ。`_compute_off` は xd-dbuf の joined・slices・enabled を直に。`i915_modeset_prepare_state` は plan を作って（`i915_modeset_plan_check` を本物にし `_plan_compare` を消す）、拒みを上の文言のまま旧の rc に写す。`ms->cdclk.*`・`ms->cdclk_rc`・`ms->bw_data_rate` は plan の写し。
4. 消せる旧（呼び手 0 を `python3 -I build/tmp/deadexp2.py` と `build/tmp/killdead.sh` で）: `clock.c` の `drv_i915_lcd_ms_cdclk_check` と static、`plane.c` の `drv_i915_lcd_ms_plane_min_cdclk`・`drv_i915_lcd_ms_plane_data_rates`（相対の data rate を plan が持つなら）、`ddi.c` の `drv_i915_ddi_compute_min_voltage_level` と 3 つの static、`watermark.c` の `drv_i915_lcd_ms_bw_min_cdclk/_bw_data_rate`・`i915_intel_bw_crtc_*`・`i915_skl_build_pipe_wm`・`i915_skl_crtc_allocate_ddb/_plane_ddb`・`i915_intel_crtc_dbuf_weights`・`i915_skl_cursor_allocation`・`i915_intel_crtc_ddb_weight`・`i915_skl_compute_dbuf_slices`・`i915_adlp_check_mbus_joined`・`i915_intel_dbuf_enabled_slices`・`i915_mbus_ddb_offset`。
5. 試験: host-xd-screen-test に 5330 の panel の WM・DDB の語（lcd 試験の D の行: crtc min 70400、bw min 11000 → 179200 kHz vco 537600 level 0、564 MB/s、slice 0xf joined と DDB・wm0）、`run_pipes` = A+B の weight の分け方、WM の拒み。比べは `sh build/tmp/stagecmp.sh 3a974d882 d2cmp`（dp SAME、lcd と out50c の trnorm の差 0 が目標）、build は `sh build/tmp/kbuild.sh d2`（6 組 warning 0）。
