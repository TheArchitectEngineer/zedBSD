<!-- awesome-plan project=zedbsd record=ws051-p002 -->

# ws051-p002: TC PLL の enable の番地と VBT の DVO の code

Phase ID: `ws051-p002`
Parent: [WS051](../ws.md)
Status: cleared（2026-10-07 Q1 の判定: T1-334 の 5330 の VFIO の execution の ktest（290 checks）に P5A・P5B が含まれ、FAIL の行も「display_probe not linked」も無い（ktest は PASS を数だけで出す）。1 件の FAIL P6C0-WINDOW と display_ktest の SCANOUT-SETUP は ws051 と無関係の古い fixture（ktest-gt.c・scanout-ktest.c の GGTT の stand-in が小さい、10-03 から不変）、P2 が直す）（旧: in-progress（2026-10-07 P2: 実装、新しい host の試験 PASS、kernel の build と `I915_TESTS=y I915_TEST_SET=execution` の build の warning 0。ktest）
（P5A・P5B）を流すのは 5330 が戻った後の T1。依存の ws113-p002 の clearance も待つ）
Phase disposition: normal
Queue: q834 の続き（P2、Q1 の指示 2026-10-07「WS051 は p001 → p002 で可」）

## 範囲（[design.md](../design.md) §14.2）

1. TC PLL の enable の番地（H8）、TC の PLL を sanitize で止めない。2. DVO の code の値（L1）。3. GOP が USB-C の時の判定は WS113 の host-gop.c に既にある
（足さない）。4. clone は log だけ。新しい host の試験と ktest の期待値。

## 実装（2026-10-07）

- `takeover.c`: `I915_MG_PLL1_ENABLE`（0x46030、ICL/TGL の MG_PLL_ENABLE）をやめ、`I915_PORTTC1_PLL_ENABLE` 0x46038 と間隔 8（Linux v6.8.12 の
  `PORTTC1_PLL_ENABLE`・`PORTTC2_PLL_ENABLE`、`intel_tc_pll_enable_reg` の ADL-P の分岐）で `adlp_plls` の TC PLL 1〜4 を 0x46038・0x46040・0x46048・
  0x46050 に。nogem の sanitize（`drv_i915_nogem_dpll_sanitize_state`）は DKL の funcs の PLL を「NOT disabled（the Type-C PLL's disable is not ported）」
  と log して止めない（Linux の `icl_pll_disable` の手順が無いため、p003 まで）。`I915_DVO_PORT_*` の port G 以降を Linux の値（DPG 15、HDMIG 16、DPH 17、
  HDMIH 18、DPI 19、HDMII 20）に、XXX の注を由来の注に。
- `diagnostics.c`: `i915_survey_pll_regs` の TC1〜4 を同じ番地に。
- `ktest-display-probe.c`: P5A-DPLL の期待値（TC PLL 1〜4 の番地）、P5B-PORTMAP の HDMII を code 20 に、DPH（17）= TC3 を足した。HDMID の NONE は残す
  （ADL-P に D_XELPD は無い）。
- clone: WS113 の `i915: N0: the firmware's output: … (lit pipes 0x…)` の log が既に lit な pipe の mask を出すので、code は足さない（TC の pipe の有無は
  mask で分かる）。

## 確かめ（2026-10-07）

- host: `sh plan/ws051/tests/host-vbt-pll.sh`（takeover.c から sed で写像の関数・定数・`adlp_plls` の行を取り出す）→ PASS 3（code 0〜24 が Linux の
  xelpd の写像の ADL-P の port と一致、display 12 の HDMID は port D、PLL の番地）。直す前の takeover.c に当てると dvo-17〜21 と pll-tc1〜4 が FAIL
  （効き目を確かめた）。
- kernel: config/ci の vmunix の build warning 0。`I915_TESTS=y I915_TEST_SET=execution`（`build/p2-i915t`、ktest-display-probe.c を含む）の build warning 0。
- style-check: 変えた 3 つの source は前と同じ数（takeover.c 59・diagnostics.c 41・ktest 60、どれも既存の物）、新しい試験は 0。
- ktest（5330 の VFIO）・QEMU: 未実施（ktest は 5330 が戻った後の T1。QEMU の boot test は takeover.c を通らないので不要、design §14.2）。

## 残り

ws113-p002 の clearance、5330 での ktest（P5A・P5B）。新しい host の試験を Master の試験の一覧に（Q1 へ）。
