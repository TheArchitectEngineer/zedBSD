<!-- awesome-plan project=zedbsd record=ws118-p006 -->
# ws118-p006: Tiger Lake の DPLL と firmware の表示の引き継ぎ（5320 で Keiland を表示する）、世代に依らない fallback

Status: in-progress（q846-i01、P3、2026-10-07）
Disposition: normal
Parent: [WS118](../ws.md)
Queue: q846
依存: なし（実機 5320 は zedBSD で稼働中、`ssh kei@10.0.30.5`（password kei）、2026-10-07 ユーザー）
目安: 4〜8h（実機の確認はユーザーの再起動・書き込みが要る時は Q1 経由）

## 由来

2026-10-07 ユーザー:「サブエージェントP3を立てて、5320のDPLLに対応してほしいです。できれば世代やバリエーションにかかわらず表示できるようにフォールバックも実装してほしいですが、難しければいいです。」

## Q1 の調査（2026-10-07、実機の log、`tests/k5320-20261007-*.log`）

- Keiland（greeter・kei の session）は 1366x768 の output を開き、最初の frame の `acquire`/`submit` が `VK_ERROR_DEVICE_LOST`（-4）→ `KWL FAILED site=compose_draw errno=5`。sessiond が 6 回で console に戻す。
- kernel: firmware は eDP（DDI A、pipe A）を **640x480（PIPESRC 0x027f01df、panel fitter で 1366x768）** で点けている。`P5a objects: ... dplls=0(mgr=0)`（TGL の DPLL の管理が空）。
  takeover の readout `pipe 0 transcoder 0 DPLL-1 port 0, mode 1366x768 0 kHz`。停止で `pipe_off wait timed out`・`Timeout waiting for DDI BUF A to get idle`、
  その後 TRANSCONF=0x40000024（enable は落ち、状態の bit 30 が残る）、DPCLKA_CFGCR0 0x01e17800→0x01e17c00、DPLL0_ENABLE 0xcc000000→0。
  takeover は rc=0 を返すが preflight が `the display is not idle` → `resident display: not started` → 以降 `presentation fails from now on`。
- 未移植の step: `skl_scaler_get_config`（pfit の readout）など（LCD-B UNRESOLVED の列）。

## 範囲

1. TGL（display version 12）の DPLL の管理（combo PHY の DPLL0/1、必要なら TBT/TC は対象外で可）を Linux の `intel_dpll_mgr.c`（tgl_pll_mgr）に倣って埋め、readout が firmware の PLL を正しく結ぶ。
2. takeover の停止（`crtc_disable_noatomic` 相当）を TGL で正しい順にする: plane → pfit/scaler → pipe off の待ち → DDI・transcoder → clock gate → PLL。pfit の readout（scaler）を移植する。
3. 5320 で Keiland が 1366x768 で表示される（実機の証拠: SSH で log、目視はユーザー）。
4. （できれば）**fallback**: 世代・variation に依らず、modeset が使えない・失敗した時に firmware が点けた pipe をそのまま使い、plane だけを自分の buffer に向ける（PLANE_SURF/STRIDE/CTL、pipe の解像度のまま、pfit も保持）など、表示が出る経路。既定の経路が成功する機械（5330）の挙動を変えない。難しければ設計と見積もりを残して Q1 に返す。

## 受け入れ

- build（warning 0）、変えた所の host 試験（短い物）。
- 5320 実機: greeter/desktop が eDP に出る（kernel.log・greeter.log で present が進む、目視はユーザー）。
- 5330 で回帰しない（T1 に依頼。5330 の起動はユーザー）。
- fallback を実装したなら、その経路を強制する option か試験で 1 回は通す。

## 規則

- HAL の API は変えない。toolchain は変えない。rm は打たない（Q1 に path を送る）。security の判定で止まったら止まって Q1 に返す。
- 実機 5320 への image・kernel の入れ替えと再起動は、方法を決めたら Q1 に送る（ユーザーの立会いが要るなら Q1 が聞く）。

## q846-i01 の結果（P3、2026-10-07、base main `cee1699d2`）

### 原因（source と実機の log から）

- `display/modeset-internal.h` の `I915_LCD_IS_DISPLAY_VER()` が固定の 13 を答えていた。TGL（12）で `drv_i915_phy_is_combo()`（`ALDERLAKE_P || IS_DISPLAY_VER(11, 12)`）が偽になり、
  takeover の readout が `icl_ddi_combo_get_config` を結ばず、crtc の state に PLL が無い（log の `DPLL-1 ... 0 kHz`）。N1 の sanitize が「誰も使わない」DPLL0 を
  firmware の pipe の下で止め（`DPLL0_ENABLE 0xcc000000→0`）、clock の無い pipe は止まれない（`pipe_off wait timed out`・`DDI BUF idle` の timeout、TRANSCONF の状態 bit が残る）。
  ws084 で ADL-P に起きたのと同じ仕組み。同じ述語で TGL の combo PHY の lane の power-up（`i915_ddi_power_up_lanes`）も飛ばされていた。
- 5320 の firmware の mode は起動で違う: `k5320-20261007-greeter-fail.log` は PIPESRC 1366x768（scaler 無し）、2026-10-07 稼働中の起動は PIPESRC `0x027f01df`（640x480、pipe scaler で 1366x768）。
  scaler の readout（`skl_scaler_get_config`）と停止（`skl_scaler_disable`）は未移植だった。
- P5 の nogem の DPLL の管理は ADL-P の表だけ（`dplls=0(mgr=0)`）。CDCLK の hook は TGL にも crawl を付けていた（TGL の gen12 は crawl しない）。

### 修正（commit `13573574f`）

- `I915_LCD_IS_DISPLAY_VER()` を device の版で答える（ADL-P の答えは同じ: (12,13)・(5,7)・(8,10)・(10,12) の 4 か所とも 13 で不変）。
- `drv_i915_skl_scaler_get_config()`・`drv_i915_skl_scaler_disable()`（`pipe.c`、Linux v6.8.12 の skl_scaler.c の通り: 有効で plane に結ばれない scaler を pfit として読み、停止は pipe の 2 つの scaler の CTRL・WIN_POS・WIN_SZ を 0）。crtc の state に `scaler_state.scalers[].in_use`・`scaler_users`。
- `drv_i915_shared_dpll_init()` に tgl_plls（DPLL0/1、TBT、TC1..6 は MG_PLL_ENABLE `0x46030+4n`）、`I915_NOGEM_MAX_DPLLS` 8→10。ktest（`ktest-display-probe.c`）に TGL の表の確認を足した（kernel の試験の build は未実行）。
- `drv_i915_init_cdclk_hooks()`: TGL は tgl の関数、crawl は ADL-P だけ。

### 確認

- `make -j16 ZEDBSD_CONFIG=plan/ws118/tests/config-remote-log.mk BUILD=build/p3-ws118 vmunix`: rc=0、warning 0、kernel include check・amd64 vmunix check PASS（vmunix sha256 `1b7ae5df…`）。
- host 試験 `sh plan/ws118/tests/run-tgl-display-host-test.sh`: 13 checks 0 failures（12 と 13 の両方で combo PHY・DPLL の表・CDCLK の hook・pipe A/B の scaler の readout と停止）。
  `plan/ws084/tests/run-native-decide-host-test.sh`: 14 checks 0 failures。
- 未実施: 5320 の実機（kernel の入れ替えの方法を Q1 に提案、返事待ち）、5330 の回帰（T1）、QEMU。

### 残り・既知の未対応

- `IS_TIGERLAKE_UY` は常に 0（9a49 は Linux では UY）。HBR2 以上の DP/eDP の buf trans の表だけが変わる。5320 の panel は HBR x1 なので影響なし。
- VBT と DP の環境（`vbt.h` の `i915_vbt_display_ver()` が 13 固定）: TGL の Type-C/HDMI の port の対応（xelpd の表）と DPLS の WA（TGL でも掛かる、無害）。eDP の AUX の power domain は 12 と 13 で同じ。範囲外（TC/TBT）として残す。

### 範囲 4（世代に依らない fallback）の設計と見積もり（未実装）

firmware の pipe を止めずに使う「adopt」の経路。takeover（N1）の readout の後、`crtc_disable_noatomic` と full modeset の代わりに:

1. 条件: N1 の readout が 1 本の pipe を active と読み、encoder（eDP）・PLL・transcoder が結ばれ、**PIPESRC が panel の mode と同じで pfit が無い**こと。
   pfit がある（5320 の 640x480 の起動）なら PIPESRC と scaler を変える fastset（Linux の `intel_update_pipe_config` 相当: PIPESRC、`skl_pfit_enable`/`skl_detach_scalers`、plane の DDB・WM の再計算）が要り、規模が倍になる。
2. modeset の object（`struct i915_lcd_modeset`）を readout の crtc の state で埋める: `crtc.active=1`、`prepared=1`、crtc の power domain（readout の `hw_readout_power_domains`）と PLL の参照（pool の pipe_mask）を引き継ぐ、encoder の `ddi_io_wakeref`/`aux_wakeref` を取る、backlight は今の duty から。
3. `drv_i915_lcd_modeset_commit_enable` の crtc の enable を飛ばし、DBUF の pre-plane（旧の DBUF の state = readout の値）→ plane の update（自分の buffer の PLANE_SURF/STRIDE/SIZE/CTL と WM・BUF_CFG）→ post-plane。以降の flip（`i915_modeset_flip_arm`）と停止（`commit_disable`）は今の経路のまま。
4. 起動の option（例 `i915.display=adopt`）で強制し、既定は takeover が失敗した時（`i915_resident_takeover` の EIO、または preflight の not idle）だけ adopt に落ちる。5330 の既定の経路（takeover が成功）は通らない。
5. 確認: option で 5330 と 5320 の両方で 1 回ずつ（T1・実機）。

見積もり: pfit 無しの adopt で 1〜1.5 日（実機の往復 3〜4 回）、pfit の fastset まで含めて 2.5〜3 日。危険: readout の state と自分の計算の差（link rate・bpp・WM）で underrun、firmware の DDB を引き継ぐ時の DBUF の状態の不整合。
判断: 今回の修正（readout が PLL を結ぶ）で takeover が通るなら fallback は急がない。実機の結果を見てから Q1 が Queue にするか決める（Future Work の候補）。
