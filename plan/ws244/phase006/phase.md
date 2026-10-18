<!-- awesome-plan project=zedbsd record=ws244-p006 -->

# ws244-p006 GT の電源と WA: gt-power・workarounds・verify-workarounds の書き直し

Status: cleared（2026-10-19 Q1 判定: gt-power の RC6・RPS と workarounds の rule の表への書き直し、verify-workarounds の整理。前の版との比べ gt-power 608 行・workarounds 1559 行一致、rps contract 74/0、build 4 組 warning 0（P1 の報告）。aea136585・f480a5cbe、merge 09e2996c4）（旧: in-progress（2026-10-19 q1260 P1。範囲は済み、cleared は Q1））

- 親: [WS244](../ws.md)。範囲: gt-power.c・gt-power.h・workarounds.c・workarounds.h・verify-workarounds.c・verify-workarounds.h。動作は変えない。

## 実行の記録（2026-10-19）

### 1. gt-power.c・gt-power.h（RC6・RPS）

- 前の版は「Follows Linux intel_rc6_init() …」など 21 関数が Linux の intel_rc6.c・intel_rps.c の関数（rps_set_power・gen6_rps_set_thresholds・rps_set・intel_rps_set・rps_work・rps_enable_interrupts・gen6_gt_pm_update_irq ほか）と 1 対 1 の名前と説明だった。
- 書き直し: RC6 の閾値の書き込みを 2 つの表（engine の idle の前と後、`struct i915_rc6_write`）にし、`Values referenced: Linux v6.19 …/intel_rc6.c (gen11_rc6_enable)`（v6.19 の値と照合）。RPS の power mode の選び方を 1 つの関数（`i915_rps_mode_for`: 範囲の両端、上の縁・下の縁を越えた時だけ 1 段）にし、`Procedure referenced: … (gen6_rps_set_thresholds)`。内部の関数を自前の名前に（program_mode・request・move_to・adjust・open_events・mask_events_locked）。周波数の判断は `Procedure referenced: … (rps_work)`、evaluation interval の値は `Values referenced: … (rps_set_power・BUSY_MAX_EI)`。
- 公開の名前: `drv_i915_gen11_rc6_enable` → `drv_i915_rc6_enable`、`drv_i915_rc6_sanitize` → `drv_i915_rc6_disable`、`drv_i915_rps_sanitize` → `drv_i915_rps_mask_all`（呼び手 engine.c・ktest-gt.c）。header の Linux の説明を外した。
- 確かめ: `plan/ws244/tests/gt-power-equiv/run.sh <前の gt-power.c> <前の gt-power.h> <今の gt-power.c> <今の gt-power.h> <新しい出力の dir>`。RC6（Gen12 の video engine 2 つ、Gen9、無し）、RPS の Gen12 の busy-time の評価（busy・idle・boost・park・unpark・telemetry・stop）、割り込みの道（up・down・timeout の event、従わない event、boost、stop）、work queue・timer の作成の失敗、Gen9 の固定、幅の無い範囲、next_freq・pm_mask・limits・pm_interval の掃引で、register の書き込み・log・状態の全部が一致（`gt-power-equiv: PASS (608 lines)`、power mode の 3 つとも通る）。rps の contract 試験（tests/contracts の rps、run.sh は host の rm を含むので同じ compile を build/ws244-rps で手で）: `== 74 checks, 0 failures ==`。

### 2. workarounds.c・workarounds.h（WA・whitelist・MOCS・PAT）

- 前の版は Linux の wa_write_or・wa_write_clr_set・wa_masked_en・wa_masked_field_set の 1 つずつの呼び出しで list を組む、intel_workarounds.c と同じ形だった。
- 書き直し: list を rule の表（`struct i915_wa_rule`: 種類・register（engine の base からの相対か）・bit・MCR・名前・platform と stepping の条件の関数）から組む形にした。表は 11 個（Gen12 の GT・render engine・render の context・render の whitelist・timestamp の whitelist、Gen9 の GT・render・その他の engine・render の context・render と その他の whitelist）で、各表に `Values referenced: Linux v6.19 …/intel_workarounds.c (<関数名>)`。条件は 8 つの関数（skl/kbl、kbl の C0 前、in-place decompress、kbl の semaphore poll、push const、skl 以外、cml、skl/kbl 以外）。GT の状態に依る物（steering、video engine の IECP、MOCS の index の CMD_CCTL・BLIT_CCTL）は表の前後の code。PAT と GTT の WA の値に `Values referenced: … /intel_gtt.c`。
- 公開の名前: `drv_i915_wa_write_or` → `drv_i915_wa_add_or`、`_write_clr_set` → `_add_field`、`_masked_en` → `_add_masked_bits`、`_masked_field_set` → `_add_masked_field`、`drv_i915_gt_init_workarounds_adlp/_gen9` → `drv_i915_wa_build_gt_gen12/_gen9`、`drv_i915_engine_init_workarounds/_ctx_wa/_whitelist` → `drv_i915_wa_build_engine/_context/_whitelist`、`drv_i915_get_mocs_settings` → `drv_i915_mocs_build`、`drv_i915_mocs_init` → `drv_i915_mocs_program`、`drv_i915_init_l3cc_table` → `drv_i915_mocs_program_l3cc`、`drv_i915_mocs_init_engine` → `drv_i915_mocs_program_engine`、`drv_i915_tgl/bdw_setup_private_ppat` → `drv_i915_pat_program_gen12/_gen9`、`drv_i915_gt_init_hw_core` → `drv_i915_gt_program`（呼び手 engine.c・ktest-gt.c）。WA の manifest の文言・WA の名前の文字列は変えていない。
- 確かめ: `plan/ws244/tests/workarounds-equiv/run.sh <前の workarounds.c> <前の workarounds.h> <今の workarounds.c> <今の workarounds.h> <新しい出力の dir>`。Gen12（video engine の instance 0・2）と Gen9 の 9 つ（Skylake rev 0・7、Kaby Lake rev 0・2・4・5・6、Coffee Lake、Comet Lake）で、全 list の manifest、MOCS の表、GT の programming と全 engine の resume の register の読み書き・log が一致（`workarounds-equiv: PASS (1559 lines)`）。

### 3. verify-workarounds.c・verify-workarounds.h

- multicast の範囲の照合を 1 つの関数（`i915_mcr_in_ranges`）にまとめ、Gen8 の範囲に `Values referenced: Linux v6.19 …/intel_workarounds.c (mcr_ranges_gen8)`（v6.19 の値と照合、旧の SHA-256 の行は外した）。検証の手順に `Procedure referenced: … (engine_wa_list_verify)`。header の Linux の関数の対応の説明を自前の説明に書き直した。
- 公開の名前: `drv_i915_gen12_mcr_range` → `drv_i915_wa_multicast_gen12`、`drv_i915_wa_list_srm` → `drv_i915_wa_list_store_regs`。
- 失敗の段の名前の文字列（"__vm_create_scratch_for_read"・"i915_request_create"・"wa_list_srm"・"switch_to_kernel_context" ほか）は log の文言として残した（p009 で直す、Q1 の判断 2026-10-19）。

### build（2026-10-19）

- amd64 の既定・I915_TESTS=y・I915_TEST_SET=vke2・I915_TEST_SET=execution の 4 つとも rc=0、warning 0、`amd64 vmunix check: PASS`。

## 残り

- 範囲は済み。log の文言は p009。
- gt-power-equiv・workarounds-equiv の試験は開発の確かめ用（前の版を入力に取る）。p006 が cleared になったら消してよい（path を Q1 へ）。
