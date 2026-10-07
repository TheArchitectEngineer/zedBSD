<!-- awesome-plan project=zedbsd record=ws051-p002b -->

# ws051-p002b: TC の port の核

Phase ID: `ws051-p002b`
Parent: [WS051](../ws.md)
Status: in-progress（2026-10-07 P2: 途中。UAT の ws181-p005 を先にするため区切った）
Phase disposition: normal
Queue: q834 の続き（P2、Q1 の ACK 2026-10-07「範囲 1〜6 で ACK、display/ の下だけ」。Linux の intel_tc.c・intel_dkl_phy.c は値と手順の事実の確認だけに使い、code・注・名前の並びは写さない）

## 範囲（[design.md](../design.md) §12・§14.7）

1. H1: TC の AUX の power domain を表に。2. H2: AUX_USBC・TBT の well の TBT_IO の routing と DKL の UC_HEALTH の待ち、DKL の index の読み書き。
3. H5: GEN11_DE_HPD_IIR を hotplug へ（long・short の判定）。4. tc.c（ADL-P の TC の核）、hotplug の TC の connected の step と TC の disable（put_link）をつなぐ。
5. 診断の log（向きは FIA の lane mask の記録だけ）。6. host の fixture、kernel と `I915_TESTS=y` の build の warning 0。

## 実装（2026-10-07、commit 3b5009699・7899f4e47）

- H1: `power.c` の `drv_i915_aux_legacy_power_domain`・`drv_i915_aux_tbt_power_domain`・`drv_i915_aux_io_power_domain`（display 12・13 の port-domain の表）。
  `modeset-internal.h` の 3 つの macro、`dp-internal.h` の `i915_aux_power_domain`、`pipe.c` の注がこれを使う。`internal.h` に `I915_AUX_CH_*`。
- H2: `power.c` の `drv_i915_power_well_enable` で TC の AUX の well（index で判定: AUX_USBC1〜4、AUX_TBT1〜4）は request の前に `DP_AUX_CH_CTL` の TBT_IO を
  設定・消去、AUX_USBC は ack の後に DKL の `CMN_UC_DW27` の UC_HEALTH を 1 ms 待つ（timeout は log と `pwc->tc_uc_health_timeouts`）。`dkl-phy.c`・`.h`（新、
  window と bank の index、spinlock）。`display.c` が `display->dkl` を作り `pwc.dkl` に。
- `tc.c`・`tc.h`（新、kernel に依存しない核、`struct i915_tc_env` で register・power・lock・delay・log を受ける）: live status（DE の HPD の ISR と SDEISR）、
  ready（TCSS_DDI_STATUS）、ownership（DDI_BUF_CTL bit 6）、FIA の lane・pin（modular FIA、2 port ずつ）、mode（NONE・TBT・DP_ALT・LEGACY）の読み出し、
  connect（ownership → ready → TC cold の block → DP-alt の再確認と lane 数、失敗は巻き戻し、default の mode へ fallback）、disconnect、lock・unlock（link が 0 の
  unlock は同期で PHY を返す、M2）、get_link・put_link、readout（firmware が DDI buffer を有効にした port は link 1、他は返す）、状態の log。
- `tc-kern.c`・`.h`（新）: kernel の env（raw MMIO、power domain: CORE・DDI_LANES_TCn・AUX の legacy の domain、port ごとの mutex、`drv_i915_udelay`、`kern_logf`）、
  VBT の legacy の flag と AUX channel で port を宣言、readout。`display.c` が `drv_i915_display_driver_probe` の後、hotplug の start の前に呼ぶ。
  `rlcd->tc`、lcd の emit の `tc_put_link` hook（`modeset.c` で bind、`modeset-internal.h` の `I915_LCD_INTEL_TC_PORT_PUT_LINK` が使う。model では STEP のまま）。
- `vmunix.mk` に dkl-phy.c・tc.c・tc-kern.c（別の行）。

## 再開の情報（残り）

1. H5: `hotplug.c` に `drv_i915_hpd_de_irq(display, iir)`（`drv_i915_hpd_pch_irq` と同じ入口の gate、GEN11_TC/TBT_HOTPLUG_CTL を rmw(0,0) で ack、long は
   `2 << (pin − TC1) × 4`、`i915_get_hpd_pins` → `i915_hpd_intel_hpd_irq_handler`）、`interrupts.c` の GEN11_DE_HPD_IIR の ack の後に呼ぶ。
2. hotplug の world に `struct i915_tc *tc`（start で `drv_i915_tc_kern_ports(display)`、model は NULL）、`i915_hpd_tc_connected_step` を `drv_i915_tc_connected` に。
3. host の試験（`plan/ws051/tests/`）: tc.c を fake の env で（live status、readout、connect の順と巻き戻し、lane 数）、AUX の domain の表（sed で power.c から）、DKL の window。
4. `I915_TESTS=y I915_TEST_SET=execution`（build/p2-i915t）の build の warning 0 を確かめる（7899f4e47 で起動、結果は build/p2-ws051b-t.log）。
5. 積み残し（plan/ws177/backlog-p2.md へ）: legacy port の 500 ms の busy wait、TBT の TC_COLD_OFF を取らない（ADL-P に well が無い）、driver の stop で PHY を返さない。
