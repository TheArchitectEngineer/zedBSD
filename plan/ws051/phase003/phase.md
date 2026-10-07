<!-- awesome-plan project=zedbsd record=ws051-p003 -->

# ws051-p003: DKL PHY と TC PLL

Phase ID: `ws051-p003`
Parent: [WS051](../ws.md)
Status: in-progress（2026-10-07 P1、q847-i01: 実装済み、host PASS、build warning 0。ktest（P5C-DPLL-TC ほか）は 5330 の T1 待ち。TC の port の実際の出力は p004b）
Phase disposition: normal
Queue: q847（P1）。承認: ユーザー 2026-10-07「UCSIとDP alt modeってもう動いてるんですか？シェーダコンパイラより優先してほしいです」（Q1 経由、担当 P1、正解値は q848 で 5330 から採取）

## 範囲（[design.md](../design.md) §5・§12・§14.2-3・§14.7）

1. DKL PHY と TC PLL: ADL-P の TC PLL の enable の番地の分岐（`intel_tc_pll_enable_reg`: ADL-P は `ADLP_PORTTC_PLL_ENABLE`、他は `MG_PLL_ENABLE`）、
   DKL PLL の値の計算（`icl_mg_pll_find_divisors`・`icl_calc_mg_pll_state` の DKL の分岐、`icl_ddi_mg_pll_get_freq`）、DKL PLL の enable・disable・
   readout（`dkl_pll_write`・`mg_pll_enable`・`mg_pll_disable`・`dkl_pll_get_hw_state`）、TBT PLL（`icl_calc_tbt_pll`、`tbt_pll_*`。TBT-alt は範囲外だが、
   Linux の TC の PLL の予約は TBT PLL と TC PLL の 2 つを取るので、その形を保つ）。
2. DDI の TC の clock（`icl_ddi_tc_enable_clock`・`disable_clock`・`is_clock_enabled`・`icl_ddi_tc_get_pll`・`icl_ddi_tc_get_config`）。
3. ADL-P の DKL の buffer translation（`adlp_get_dkl_buf_trans`、`adlp_dkl_phy_trans_dp_hbr`・`_dp_hbr2_hbr3`、HDMI は `tgl_dkl_phy_trans_hdmi`）と
   `tgl_dkl_phy_set_signal_levels`。
4. DP_MODE（`icl_program_mg_dp_mode`）、FIA の lane 数（`intel_tc_port_set_fia_lane_count`）、`intel_tc_port_get_link`、`intel_ddi_update_active_dpll`、
   `adlp_tbt_to_dp_alt_switch_wa` を modeset の環境（modeset-internal.h）の STEP・GUARD から本物に。TC の port の mode は tc.c から。
5. readout（判定は変えない、§14.2-3）: nogem の P5c の `icl_ddi_tc_get_pll`（DDI_CLK_SEL から TC PLL・TBT PLL）、P5d の sanitize の TC PLL の disable
   （Linux の `icl_pll_disable` の順: PLL_ENABLE、lock の解除の待ち、PLL_POWER_ENABLE、power state の待ち）。tc.c の init_mode・sanitize の readout は
   p002b の `tc_readout_port` のまま（Linux の `intel_tc_port_init_mode`・`sanitize_mode` と同じ結果: firmware の DDI buffer が有効な port は link 1）。
6. 照合: q848 の 5330 の正解値（[tests/m3-5330-20261007](../tests/m3-5330-20261007/README.md)）、Linux の計算との一致（host）。

範囲の外（決めたこと）: lcd の world の DPLL の pool に TC PLL・TBT PLL を足すのは **p004b**（TC の encoder を N1 の registry に入れる時）。今足すと N1 の
sanitize が、GOP が panel と USB-C を clone した時の TC の pipe の PLL を「使われていない」と見て止める（TC の encoder が registry に無いので）。

## 受け入れ

- PLL の値の計算が Linux と同じ（host、DP の 4 rate と HDMI の clock の範囲）、q848 の 5330 の TC PLL 2 の値（RBR 162000、ref 38.4 MHz）と一致。
- DKL PLL の enable・readout の register の順と値（host、fake の register で）、buffer translation の表の選び方（host）。
- kernel の build warning 0、`I915_TESTS=y` の build warning 0。
- PLL の lock の bit の読み戻しの診断（M7）は enable の後の log（`PLL %d not locked`）。実機の TC の出力は p004b（ここでは TC の port は modeset に入らない）。

## 実装（2026-10-07 P1）

- `intel/dkl.h`（新、Linux の intel_dkl_phy_regs.h・intel_mg_phy_regs.h・i915_reg.h の値）: `enum tc_port`、DKL の PHY address（bank は bit 15..12、
  dkl-phy.c が窓に直す）と field、MG の clktop・refclkin・DP_MODE の field、`TBT_PLL_ENABLE`、`ADLP_PORTTC_PLL_ENABLE`、`DDI_CLK_SEL`、
  `ICL_DPCLKA_CFGCR0_TC_CLK_OFF`。`intel/clock.h` に `tgl_tbt_pll_*_values`、`intel/phy.h` に TGL・ADL-P の DKL の buffer translation の表。
- `clock.c`: `i915_intel_tc_pll_enable_reg`（ADL-P は PORTTC、他は MG_PLL_ENABLE）、`icl_mg_pll_find_divisors`・`icl_calc_mg_pll_state`（DKL の分岐。
  display 11 の MG は error で拒む）、`icl_ddi_mg_pll_get_freq`、`dkl_pll_get_hw_state`・`dkl_pll_write`・`mg_pll_enable`・`disable`、TBT PLL の
  calc・get_hw_state・enable・disable・get_freq、表 `i915_dkl_pll_funcs`・`i915_tbt_pll_funcs`。公開: `drv_i915_dkl_pll_describe`・`drv_i915_tbt_pll_describe`
  （p004b の pool が使う）、`drv_i915_dkl_pll_calc`・`drv_i915_dkl_pll_freq`、`drv_i915_icl_compute_tc_phy_dplls`、`drv_i915_icl_set_active_port_dpll`、
  `drv_i915_icl_update_active_dpll`。
- `ddi.c`: `icl_ddi_tc_enable_clock`・`disable_clock`・`is_clock_enabled`・`icl_pll_to_ddi_clk_sel`・`icl_ddi_tc_get_pll`・`icl_calc_tbt_pll_link`・
  `icl_ddi_tc_get_clock`・`icl_ddi_tc_get_config`、`tgl_dkl_phy_set_signal_levels`、`icl_program_mg_dp_mode`（GUARD を本物に）、
  `intel_ddi_update_active_dpll`（STEP を本物に）、`adlp_tbt_to_dp_alt_switch_wa`（STEP を本物に）。`drv_i915_lcd_ms_bind_encoder` と
  `drv_i915_lcd_ms_bind_readout` が TC の PHY に TC の hook を付ける（Linux の intel_ddi_init の display 12 以上の分岐。`port_pll_type` の hook は
  zedBSD の encoder に無いので付けない）。`phy.c` の `drv_i915_lcd_ms_bind_buf_trans` に TC の分岐（ADL-P は adlp_get_dkl_buf_trans、TGL は tgl_*）。
- `modeset-internal.h`: `i915_lcd_intel_port_to_tc`、`i915_lcd_tc_port_mode`・`in_mode`・`in_tbt_alt_mode`（今までの「常に false」を tc.c の mode に）、
  `intel_tc_port_in_dp_alt_mode`・`in_legacy_mode`（今までの (0)）、`intel_tc_port_get_link`・`set_fia_lane_count`・`get_pin_assignment_mask`、
  `intel_tc_port_link_cancel_reset_work`（GUARD から STEP に: reset の work は p005）、DKL の read・write・rmw・posting_read。combo の port では今までと同じ答え
  （hook を呼ばない）。
- `internal.h` の `struct i915_lcd_emit` に hook: `tc_get_link`・`tc_mode`・`tc_set_fia_lane_count`・`tc_pin_assignment`・`dkl_read`・`dkl_write`・`dkl_rmw`
  （model は NULL: TC は step、DKL は bank の index と窓を write32・read32 で）。実機の hook は `tc-kern.c`（tc.c と display の DKL の spinlock）、`modeset.c` が
  付ける。`dkl-phy.c` に `drv_i915_dkl_phy_rmw`（read と write を 1 回の lock の中で）、`tc.c` に `drv_i915_tc_set_fia_lane_count`（DFLEXDPMLE1）。
- `takeover.c`（nogem の P5c・P5d）: TC の encoder の PLL を DDI_CLK_SEL から読む（MG → TC PLL n、TBT の rate → TBT PLL、NONE → 無し、他 → 不明で
  readout incomplete）。P5d の sanitize の TC PLL を Linux の `icl_pll_disable` の順（ENABLE、lock の解除の待ち 1 ms、POWER_ENABLE、power の待ち 1 ms）で
  止める（p002 で外していた分）。ktest の P5C-DPLL-TC を新しい readout に直し、P5C-DPLL-TC-UNKNOWN（不明な select → 何も止めない）と
  P5D-DPLL-TC-UNUSED（使われない TC PLL 2 が ENABLE・POWER_ENABLE とも消える）を足した。
- 範囲の外のまま: lcd の world の pool に TC PLL・TBT PLL を足すこと（p004b、上の「範囲の外」）。

## 確かめ（2026-10-07 P1）

- 正解値（q848、[m3-5330-20261007](../tests/m3-5330-20261007/README.md)、Linux 6.19.13）: DP-2 = TC2、RBR 162000、4 lanes、pin C、ref 38.4 MHz、TC PLL 2。
  `drv_i915_dkl_pll_calc(162000, DP, 38400)` が debugfs の 8 語（refclkin 0x100、coreclkctl1 0xa00、hsclkctl 0x6200、div0 0x84269、div1 0x1c0027、
  ssc 0x40002000、bias 0x5e000000、tdc 0x52）と一致。dump の窓の生の値（bank 2: 0x169200 = 0x7e284269 ほか）から readout の mask が同じ 8 語を出し、
  周波数は 162000。PORTTC2_PLL_ENABLE（0x46040）= 0xcc000000、DDI_CLK_SEL(E) = 0x80000000（MG）、DPCLKA_CFGCR0 の TC2 の gate が開、FIA1 の
  DFLEXDPMLE1 = 0xf0（TC2 の 4 lanes）、DFLEXPA1 = 0x30（pin C）も host の試験で照合。
- host: `sh plan/ws051/tests/host-dkl.sh` → host-dkl 40 checks 0 failures（上の照合、Linux 6.8.12 の text（参照の source から抜いて compile）との sweep:
  DP 8 rate と HDMI 25〜600 MHz の 250 kHz 刻みを 3 つの ref で 6927 件（両方が拒む 556 件）すべて一致、enable が PORTTC2 と bank 2 の窓に書き readout で
  戻る、disable で ENABLE・POWER_ENABLE が消える、TBT PLL の記述、ADL-P の DKL の表の選び方）。`sh plan/ws051/tests/host-tc.sh` → host-tc 79/0
  （FIA の lane 数 8 件を追加）、host-tc-tables 34/0。回帰: host-vbt-pll PASS、ws118 の host-tgl-display 13/0、ws084 の native-decide 14/0。
- kernel: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p1-ci vmunix` warning 0。`I915_TESTS=y I915_TEST_SET=execution`・`display`・
  `display_ktest`・`display_ktest2` warning 0（display・display_ktest2 は途中の source で、最後の差分の後は execution・display_ktest を再 build）。
- style-check: 新しい finding は critical section の本体の段落だけ（規約の例の形、p002b と同じ）。
- 未実施: ktest（P5C-DPLL-TC・-UNKNOWN、P5D-DPLL-TC-UNUSED、display_ktest の既存の modeset）は 5330 で T1。DP_MODE・TC の clock の enable は TC の
  encoder が modeset に入る p004b まで実機で通らない（host でも static で直接は試していない、Linux の text と照らしてレビュー）。

## 再開の情報

- T1 への依頼（Q1 経由）: execution の ktest（P5 の 3 件）と display_ktest の既存の modeset の回帰を 5330 で。
- 次: p004a（TC の AUX・DPCD・EDID、外部 DP の object）。
