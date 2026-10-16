<!-- awesome-plan project=zedbsd record=ws245-p029 -->

# ws245-p029: combo PHY（xd-ddi: init・lane・swing）

Status: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない。`phy.c`・`phy.h` の削除は merge の時に Q1）
Parent: [WS245](../ws.md)
Queue: q1229-i03（P1、ws245-p028 から残りの段を design の順に実装して branch に積む）
設計: [design.md](../design.md) §6.2 の p029 の行、§2.7

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-ddi.c/.h`（新、Zlib） | combo PHY: `drv_i915_xd_combo_phy_ready`（PHY_MISC と COMP_INIT、display 12 以降の ODCC と DCC、procmon の 3 語、master の IREFGEN、CL_DW5 の power down enable を全部読む）・`_init`（platform の `combo_phys` の各 PHY で ready でない物だけ: PHY_MISC → ODCC（LN0 を読み GRP に）→ DCC → procmon → IREFGEN（`comp_masters`）→ COMP_INIT → CL_DW5、書いた PHY の mask を返す）・`_uninit`（後ろから: PHY_MISC の power down → COMP_INIT を消す、PHY A が ready でなかったかを返す）・`drv_i915_xd_combo_lanes`（CL_DW10 の lane の power down）・`_swing_table`（HDMI、eDP は 5.4 GHz 超 → HBR3、HOBL、low swing、他は DP の rate）・`_swing_is_hobl`・`_signal_levels`（cmnkeeper → lane ごとの loadgen → SUS clock → training を消す → eDP の 4K2K の override（HOBL の時だけ立てる）→ DW5 の scaling/rterm/tap → lane ごとの DW2/DW4/DW7 → training を立てる）。`Procedure referenced:`（intel_combo_phy.c の icl_combo_phy_verify_state・icl_combo_phy_enabled・icl_verify_procmon_ref_values・icl_combo_phys_init・icl_get/set_procmon_ref_values・icl_combo_phys_uninit・intel_combo_phy_power_up_lanes・phy_is_master・has_phy_misc、intel_ddi.c の icl_combo_phy_set_signal_levels・icl_ddi_combo_vswing_program・icl_combo_phy_loadgen_select、intel_ddi_buf_trans.c の tgl/adlp_get_combo_buf_trans* ・use_edp_hobl・use_edp_low_vswing）、固定の値（scaling 2、rterm 6、rcomp 0x98、loadgen の 6 GHz、lane の power down の code、PHY の番地）は `Values referenced:` |
| `display/ddi.c`（殻） | combo の set_signal_levels を xd に（lane ごとの level は旧の `i915_ddi_level` が決め、`struct i915_xd_combo_levels` にして渡す。eDP の `hobl_active` は xd の is_hobl から）。lane の power up を xd に。combo の get_buf_trans は xd の表の長さと HDMI の既定だけの view（`i915_combo_trans_view`: DP の voltage max と HDMI の level の選びが読む）。Type-C（Dekel）の get_buf_trans と表の選びは phy.c から移した（p040 まで）。`i915_ddi_bind_buf_trans`（旧 `drv_i915_lcd_ms_bind_buf_trans`） |
| `display/power.c`（殻） | core init と DC_off の combo PHY の立て直しは `i915_combo_phys_up`（xd の init、trace の記録、`i915: combo PHYs: N initialised (mask 0x…)` の log）、uninit は xd の uninit（PHY A が ready でなければ log）。R11: 旧の log の `P3 intel_combo_phy_init` を消した |
| `display/modeset.c`・`modeset-internal.h` | lcd world の xd の window を `drv_i915_lcd_xd_io(i915)` として modeset.c に移した（p028 の clock.c の window を一般にした。clock.c・ddi.c が使う） |
| `display/phy.c`・`phy.h` | 中身を消した（vmunix.mk から外した）。**merge の時に Q1 が消す** |
| `intel/phy.h` | combo の表（Linux の型）を削除、Dekel の表は p040 まで残る。R11: `WS031`・`ws051-p003` を消した |
| `intel/display-regs-xd.h`・`tests/display/xd-regs-map.h` | CL_DW10 の EDP4K2K の 2 bit（`Values referenced:`）と 2 つの対 |
| `tests/display/host-xd-ddi-test.c`（新）・`plan/ws245/tests/run-xd-host-tests.sh` | 26 の check（下） |
| `platform/amd64/vmunix.mk` | `xd-ddi.c` を足し `phy.c` を外した |

段の phase.md の必須の項目:
- 消す file（merge の時に Q1）: `src/drivers/gpu/i915/display/phy.c`、`src/drivers/gpu/i915/display/phy.h`（中身は消した。include する file は 0）。
- 消した関数（本番の呼び手 0）: phy.c の公開の 6（`drv_i915_combo_phy_init`・`_verify_state`・`_init_one`・`_uninit`・`_power_up_lanes`・`drv_i915_is_hobl_buf_trans`）と `drv_i915_lcd_ms_bind_buf_trans`（ddi.c の static に）、static の 28（register の番地の helper 8、procmon 4、enabled、hobl/low vswing、combo の表の選び 6、Dekel の表の選び 4 は ddi.c に移した）、ddi.c の `i915_icl_ddi_combo_vswing_program`・`i915_icl_combo_phy_loadgen_select`。
- 殻が更新する旧の写し: `intel_dp->hobl_active`（xd の表が HOBL か）、get_buf_trans の view の `num_entries`・`hdmi_default_entry`。
- 表 R: 新しい共有の register は無い（combo PHY の語の書き手は xd-ddi だけ。CL_DW10 の lane と 4K2K は同じ xd-ddi の rmw）。
- hook と lock: core init・DC_off は旧の power の lock の下で `pwc->xd->io`（本物の MMIO）。signal levels と lane は modeset の lock の下で `drv_i915_lcd_xd_io` の window。
- R11: 触った file は全部 0（power.c は p025 で 0、phy.h の `WS031` を消した）。

## 旧と同じに保った物と違い

- 確かめ方 1: 旧の host 試験 `host-dp-test`（72）は段の前（a8cb64e8b の `git archive`）と出力が同じ、`host-lcd-modeset-test`（123）は rmw を書いた値に直した書き込みの列が**全部一致**（signal levels・vswing・lane の power の全部の語を含む）。
- 確かめ方 2（scratch の harness、git に入れない: `plan/ws245/temp/harness/wells29.c`・`run-wells29.sh`、GRP の書き込みを 4 lane に写す model）: 冷えた PHY A・B の init、2 回目（何も書かない）、PHY B の DW9 だけ違う時の init を ADL-P と TGL で段の前後に流し、**init の書き込みの列は全部一致**。
- 違い（Linux に合わせた）: uninit は Linux の順（後ろの PHY から: B → A）。旧は A → B。uninit の前に PHY A が ready でなければ log。
- 違い: TGL の UY の SKU（platform の表の 2 行目）は DP の HBR2 の表が `tgl_uy`（旧は `IS_TIGERLAKE_UY` が常に 0 で非 UY の表）。
- 違い: RKL・ADL-S は platform の表の PHY の数・master・PHY_MISC で動く（旧は A・B と A の master 固定）。RKL の eDP の表の選び（Linux の `rkl_get_combo_buf_trans` は 5.4 GHz 超でも HBR3 の表を使わない）は TGL・ADL-P の規則のまま: p044（platform の網羅）で直す（RKL・ADL-S は attach しない: §7 #18）。
- 違い: log の文言 `i915: P3 intel_combo_phy_init: combo PHYs A/B (N initialised)` → `i915: combo PHYs: N initialised (mask 0x…)`（R11。読む試験・道具は無い: plan/ws118 の古い log だけ）。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- `i915: combo PHYs: 0 initialised (mask 0x0)` か、PHY B だけ（GOP は PHY A を立てる）。訓練が同じ swing（level）で通る（`link training`・CR/EQ の行）。survey に差が無い。
- 節目（ユーザーの一覧）: 5320 の起動（TGL の PHY と swing）。外部出力（HDMI = combo PHY B の swing）: 未確認（p031 の節目で）。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p029/xd-host` → regs 502・io 50・platform 89・vbt 137・survey 57・aux 43・gmbus 24・panel 47・power 65・dmc 32・clock 18・dpll 30・ddi 26、失敗 0。`I915_XD_STRICT` の検査は通る。
- ddi の check: 5330 の PHY A（m3 の Linux の語）が ready、PHY_MISC・procmon・IREFGEN・ODCC の 1 つが違うと ready でない。init: PHY A は触らず PHY B だけを Linux の順で書く（8 語、IREFGEN 無し）、2 回目は何も書かない、RKL は A〜D（IREFGEN は C に、D に無い）、ADL-S は A〜E で PHY_MISC は A だけ。uninit: B → A、PHY A が落ちていれば報告。lane: 2 lane = 0x000300c0（5330 の dump）、1 lane 逆、4 lane、3 lane は全部。表: 5330 の eDP（HOBL・low 無し、2.7 GHz）は DP の HBR の表の row 4 = N scalar 0x4c、HOBL が low に勝つ、5.4 GHz 超の eDP は HBR3、DP は VBT を見ない、HDMI の既定は最後の行、TGL は別の表。levels: 5330 の eDP（row 4、2 lane）で DW2 0x9098・DW4 0x0028003f（lane 1・2 は loadgen）・DW5 0xa0290030・DW7 0x4c000000 が m3 の dump と一致、HOBL でないので 4K2K を消す、SUS clock と cmnkeeper、HDMI は cmnkeeper を消す、6 GHz 超は loadgen 無し。
- `sh plan/ws245/tests/check-xd-regs-linux.sh build/ws229-p001/linux/linux-6.8.12 build/p029/linuxcheck` → 1023 対が一致、差 0。master の Tools の `plan/ws051/tests/host-tc.sh` は 112・34 で失敗 0。
- build（2026-10-16）: `sh build/tmp/kbuild.sh p029` で既定の vmunix と `I915_TESTS=y` の 5 組が rc 0・warning 0。`git diff --check` は空。
- 5330: 未実施（全部の段の後に 1 回）。
