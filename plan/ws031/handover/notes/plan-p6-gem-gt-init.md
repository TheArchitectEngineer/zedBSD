# WS031 P6 計画：`i915_gem_init()` → `intel_gt_init()` — execlists 先行（判断②）

計画のみ。実装は未着手です。正本（v6.8.12）の `i915_gem.c` / `gt/intel_gt.c` / `intel_gt_pm.c` / `intel_rc6.c` / `intel_rps.c` / `intel_engine_cs.c` / `intel_execlists_submission.c` / `intel_lrc.c` / `intel_workarounds.c` / `intel_mocs.c` / `intel_sseu.c` / `intel_gt_irq.c` / `uc/intel_uc.c` / `intel_clock_gating.c` を読んだ上で書いています。

## 0. 結論と、先に申し上げるべき3点

- P6 は **P6-0（前提の穴埋め）＋ P6-a / b / c の4段**に分けます。**P6-c が「parity 経路での初回 GPU 実行」**（`__engines_record_defaults`：描画ではない null request）で、ここがこの工程の山です。
- **規模の上方修正**：工程表では P6 を「3〜4 DMC」としましたが、精読の結果 **≈7〜8 DMC** です（前提の穴埋め 1.8、engine/WA/LRC 2.5、init_hw/RC6/RPS 1.5、request 経路＋初回実行 1.5〜2）。P5 に続き見積もりが甘く、お詫びします。
- **設計上の分岐が1つ**（§6 判断1）：既存 big-bang 資産は **Linux 7.1 転記**で正本（6.8.12）と別です。「機構は再利用、表は 6.8.12 から再導出」を推奨します。

## 1. 正本の呼び出し列と、ADL-P（execlists, enable_guc=0）での実効

`i915_gem_init()`：
```
i915_gem_init_userptr            -- userptr 基盤。無い → 記録
intel_uc_fetch_firmwares         -- uc_ops_off → no-op（enable_guc=0 で ops=off。確認済）
intel_wopcm_init                 -- GuC/HuC 無し分岐 → 実質 no-op（要確認）
setup_private_pat -> tgl_setup_private_ppat   -- GEN12_PAT_INDEX(0..7) = WB,WC,WT,UC,WB,WB,WB,WB  ★HW書
i915_init_ggtt                   -- GGTT 初期化（P2 の GGTT/WC 資産を parity へ正式移管）
intel_clock_gating_init          -- ★ADL-P は hooks 選択に該当なし → nop_clock_gating_funcs = no-op（確認済）
for_each_gt: intel_gt_init       -- 本体（下）
intel_engines_driver_register    -- sysfs。記録
```

`intel_gt_init()`：
```
intel_gt_init_workarounds        -- gt_tuning_settings(ADL-P なし) + gen12_gt_workarounds_init
forcewake_get(ALL)
intel_gt_init_scratch(4K)
intel_gt_pm_init -> intel_rc6_init(rpm_get, rc6_supported, __intel_rc6_disable) + intel_rps_init(gen6_rps_init: PCODE ★)
gt->vm = kernel_vm -> i915_ppgtt_create (48bit 4-level)
intel_set_mocs_index             -- gen12_mocs_table: uc_index=3
intel_engines_init               -- engine_setup_common → intel_execlists_submission_setup → engine_init_common(kernel context)
intel_uc_init                    -- off → 0
intel_gt_resume                  -- gt_sanitize(force) → intel_gt_init_hw → rps_enable → llc_enable → engine_resume ×N → rc6_enable
intel_gt_init_hwconfig           -- GuC 経由 → 不可。記録
__engines_record_defaults        -- ★初回 submission（null request + ctx WA LRI）→ wait_for_idle → default_state 採取
__engines_verify_workarounds     -- CONFIG_DRM_I915_DEBUG_GEM のみ → 正本既定では skip（§6 判断2）
intel_uc_init_late / intel_migrate_init  -- nop / 記録
forcewake_put(ALL)
```

`intel_gt_init_hw()`（gt_resume 内）：
```
forcewake_get(ALL)
intel_gt_apply_workarounds + verify   -- GT WA 表を MMIO へ ★
intel_gt_init_swizzling / init_unused_rings  -- gen12 では no-op
i915_ppgtt_init_hw -> gtt_write_workarounds
intel_uc_init_hw                      -- off → __uc_check_hw → 0
intel_mocs_init                       -- global MOCS 64 entries + RCS L3CC(LNCFCMOCS) ★
forcewake_put(ALL)
```

`intel_engine_resume()` = `intel_engine_apply_workarounds`(engine WA → MMIO ★) + `intel_engine_apply_whitelist`(RING_FORCE_TO_NONPRIV ×12 ★) + `execlists_resume`(`intel_mocs_init_engine` / breadcrumbs reset / **`enable_execlists`**: HWSTAM, RING_MODE_GEN7 DISABLE_LEGACY_MODE, MI_MODE STOP_RING clear, HWS_PGA, `enable_error_interrupt` EMR/EIR/ESR ★)。

`gen11_rc6_enable()`：RC6 閾値群 → **`GEN9_PG_ENABLE = RENDER_PG | MEDIA_PG | MEDIA_SAMPLER_PG | VDN_HCP/MFX(VCS0,VCS2)`** → `RC_CTL = RC6_ENABLE`（GuCRC 無し）。**render power gating が ON になる**段です（§5）。

## 2. ADL-P で実際に入る WA / 表（正本から確定、hang 候補の本体）

| 区分 | 内容 |
|---|---|
| GT WA (`gen12_gt_workarounds_init`) | `icl_wa_init_mcr`、Wa_14011060649、Wa_14011059788（GEN10_DFR_RATIO_EN_AND_CHICKEN DFR_DISABLE, MCR）、Wa_14015795083（GEN7_MISCCPCTL DOP_CLOCK_GATE_RENDER_ENABLE、**readback 検証しない**） |
| RCS engine WA (`rcs_engine_wa_init`) | FF_DOP_CLOCK_GATE_DISABLE(CS_DEBUG_MODE1)、GEN12_DISABLE_EARLY_READ(ROW_CHICKEN2, MCR)、FF_TESSELATION_DOP_GATE_DISABLE(FF_THREAD_MODE)、ENABLE_SMALLPL(SAMPLER_MODE, MCR)、PUSH_CONST_DEREF_HOLD_DIS(ROW_CHICKEN2)、DISABLE_TDL_PUSH(ROW_CHICKEN4)、**PSMI_CTL WAIT_FOR_EVENT_POWER_DOWN_DISABLE \| RC_SEMA_IDLE_MSG_DISABLE**（Wa_1607297627）。`general_render_compute_wa_init` は ADL-P 該当なし（DG2/PVC/MTL のみ、要再確認） |
| engine fake WA | RING_CMD_CCTL の MOCS override（uc_index=3 の r/w） |
| ctx WA (`gen12_ctx_workarounds_init`, LRI で context に) | COMMON_SLICE_CHICKEN3 DISABLE_CPS_AWARE_COLOR_PIPE、CS_CHICKEN1 GPGPU thread-group preempt、**FF_MODE2 TDS_128 \| GS_224（Wa_1608008084: CPU 読戻し不可、~0 clear）**、HIZ_CHICKEN HZ_DEPTH_TEST_LE_GE_OPT_DISABLE、COMMON_SLICE_CHICKEN4 DISABLE_TDC_LOAD_BALANCING_CALC、＋`gen12_ctx_gt_mocs_init` |
| whitelist (`tgl_whitelist_build`) | ctx timestamp 読出し許可、PS_INVOCATION_COUNT×4 RD、GEN7_COMMON_SLICE_CHICKEN1、HIZ_CHICKEN、GEN11_COMMON_SLICE_CHICKEN3 |
| indirect ctx BB (`gen12_emit_indirect_ctx_rcs`) | timestamp WA（LRM/LRR）、cmd_buf WA、restore scratch、aux table inv（ADL-P は flat CCS 無し→`gen12_needs_ccs_aux_inv` 要確認）、**Wa_18022495364 state cache invalidate（IP 12.0..12.10 → ADL-P 該当）** |
| MOCS | `gen12_mocs_table`（TGL/RKL 用 `tgl_mocs_table` ではない）、uc_index=3、unused=2、global(0x4000〜) + L3CC |
| SSEU | `gen12_sseu_info_init`：GT_SLICE_ENABLE(=1 想定)、GEOMETRY_DSS_ENABLE、EU_DISABLE(2EU/bit) → `gen11_compute_sseu_info`、has_slice_pg |
| PAT | tgl: 8 entries |
| RC6 / PG | 上記 §1 |
| LRC image | `gen12_rcs_offsets`（REG/LRI 表）、MI_MODE idx 0x60、BB 0x70、GPR0 0x74、CMD_BUF_CCTL 0xb6、wa_bb_per_ctx 0x12、render ctx size = **14 page**、other = 2 page |

これらを**「レジスタ・マニフェスト」**（reg, 期待値, 由来 WA 名, 適用先=MMIO/ctx LRI/whitelist）として1ファイルに落とし、GPU-free で fake 書込み列と照合し、実機で読戻し可能なものは読戻し比較します。EU 試験前に Linux 側ダンプと diff できる形にするのが P6 の実質的な成果物です。

## 3. 前提の穴埋め（P6-0）— 正本では P1/P4 に属するが未実施のもの

- **P6-0a `intel_gt_init_mmio` の残り**（P1 相当、≈1 DMC）：`intel_gt_init_clock_frequency`（CTC_MODE / RPM_CONFIG0 → タイムスタンプ周波数）、**`gen12_sseu_info_init` 完全版**（P1 は slice/DSS fuse の読出しのみ。EU_DISABLE と `gen11_compute_sseu_info` が未）、**`intel_gt_mcr_init`**（L3BANK steering 表 + GEN10_MIRROR_FUSE3、P1 は multicast selector のみ）、**`intel_engines_init_mmio`**（engine オブジェクト生成：`intel_engines[]` 表から RCS0/BCS0/VCS0/VCS2/VECS0、`engine_mask_apply_media_fuses`(GEN11_GT_VEBOX_VDBOX_DISABLE) で VCS/VECS の fuse 反映、mmio_base、`intel_engine_context_size`、`intel_setup_engine_capabilities`、`intel_uncore_prune_engine_fw_domains`）、`intel_gt_check_and_clear_faults`（GEN12_RING_FAULT_REG）。
- **P6-0b GT 側 IRQ ack 経路**（P4 相当、≈0.5 DMC、HAL 非変更）：`gen11_gt_irq_handler` → bank(0/1) `GEN11_GT_INTR_DW` → bit 毎に `gen11_gt_engine_identity`（**IIR_REG_SELECTOR 書込 → INTR_IDENTITY_REG を ≤100µs poll → DATA_VALID で ack**）→ class/instance → engine の `irq_handler`（`execlists_irq_handler`：CS_MASTER_ERROR → EIR/EMR、WAIT_SEMAPHORE → yield、CONTEXT_SWITCH → tasklet、RENDER_USER → breadcrumbs）→ `GEN11_GT_INTR_DW` を write-back で clear。OTHER_CLASS(GTPM/GuC) は計数。tasklet は既存 kworkqueue、100µs poll は既存時間 backend。**E-89 の display ack と同じ「ack しないとストーム」問題の GT 版**で、P6-c の前提。
- **P6-0c gen12 forcewake 範囲表**（≈0.3 DMC）：`__gen12_fw_ranges`（RENDER/GT/VDBOX0/VDBOX2 のアドレス範囲）と `intel_uncore_forcewake_for_reg`。`wa_list_apply` の `wal_get_fw_for_rmw` と `engine->fw_domain`（ELSQ 書込み）が要求します。現在の parity は "forcewake ALL" のみ。

## 4. 増分分割（各増分 = GPU-free → 実機1回 → 報告）

### P6-a：`i915_gem_init` 前段 ＋ `intel_gt_init` の静的部分（submission なし、≈2.5 DMC）
PAT 8本、GGTT 移管、clock_gating(no-op 確認)、GT WA 表構築、scratch、rc6_init（`__intel_rc6_disable` まで）、rps_init（**PCODE 読: RP_STATE_CAP / DYNAMIC_DUTY_CYCLE**）、kernel ppgtt、mocs index、**engine 毎**：status page(HWSP, GGTT)、breadcrumbs、execlists 構造、engine WA / whitelist / ctx WA **表の構築**（適用は P6-b）、`lrc_init_wa_ctx`（indirect ctx BB を GGTT に置く）、ELSQ/CSB 設定、**kernel context**（LRC image：`gen12_rcs_offsets` で `set_offsets` → `init_common_regs`(CTX_CTRL) → `init_ppgtt_regs`(PML4) → `init_wa_bb_regs` → `__reset_stop_ring`）、`measure_breadcrumb_dw`。
試験：表の写像（WA 名 → reg/値/masked/MCR）、LRC image の各 index の値、PAT 列、fw range 引き。

### P6-b：`intel_gt_resume` → `intel_gt_init_hw`（HW プログラム、submission なし、≈1.5 DMC）★実 HW に大量書込み
`gt_sanitize(force)`（`reset_csb_pointers` / HWSP 清掃 / **全 engine reset**（既存 E-7x reset 資産）/ `intel_rps_sanitize`）→ `intel_rc6_sanitize` → **GT WA 適用＋readback 検証** → `gtt_write_workarounds` → **MOCS 96 entries** → `intel_rps_enable`（gen9: RP_IDLE_HYSTERSIS, rps_reset→RPNSWREQ）→ `intel_llc_enable`（**PCODE WRITE_MIN_FREQ_TABLE の周波数ループ**）→ engine 毎 **engine WA 適用 / whitelist 12 slot / enable_execlists / error interrupt** → **`intel_rc6_enable`（GEN9_PG_ENABLE, RC_CTL）**。
試験：fake への書込み列を §2 マニフェストと全件照合、順序（WA → MOCS → engine → RC6）、readback verify の不一致検出。実機：マニフェスト読戻し比較（FF_MODE2 のような読戻し不可は除外を明示）。

### P6-c：初回 submission — `__engines_record_defaults`（≈1.5〜2 DMC）★parity 経路での初 GPU 実行
request 経路（`intel_context_create`/pin、`i915_request_create`、`intel_engine_emit_ctx_wa`（ctx WA を **LRI で ring に**）、`i915_request_add` → `execlists_submit_request` → dequeue → **ELSQ 書込**）→ CS 割込 → `process_csb` → breadcrumb → `intel_gt_wait_for_idle` → 各 engine の context image を `default_state` として保存。`intel_renderstate_init` は gen12 で rodata 無し（null）。
- **これは描画ではありません**（batch 無し、LRI のみの null request）。正本の通常初期化の一部です。ただし **parity 経路で GPU が初めて命令を実行する**ので、§6 判断3 で明示のご確認をいただきます。
- 試験：fake CSB/ELSQ モデルで submit→CSB promote→complete の1往復、ctx WA LRI の dword 列、default_state のサイズ。実機1回：5 engine の record 完走、`fence.error=0`、CS 割込み受信数（**E-88 で未達だった「実際に割込みを受信した」実証がここで取れます**）。
- `__engines_verify_workarounds`：正本既定は skip だが §6 判断2。

### P6 末尾（記録のみ）
`intel_gt_init_hwconfig`(GuC)、`intel_migrate_init`、`intel_engines_driver_register`、`intel_pxp_init`(HuC 無し→非対応)。

## 5. リスク

1. **GT 割込みストーム**（P6-0b 未実施のまま P6-b で engine の割込み enable が入ると発生）→ P6-0b を P6-b より前に必ず。
2. **RC6 / render power gating**：正本どおり ON にします（faithful）。同時に、保留中の EU hang（EU per-thread state 読出し待ち）の**候補機構そのもの**でもあります。→ 既定は正本どおり、EU 試験の A/B 用に「RC6/PG を適用しない」診断スイッチを用意（既定を変えない）。
3. **big-bang 資産の版ズレ**（Linux 7.1 転記 vs 正本 6.8.12）：WA/MOCS/LRC offsets を流用すると、Ubuntu 正本との差分が EU 試験の解釈に混入します。§6 判断1。
4. **PCODE 依存が増える**（RPS caps、DDCC、LLC ring freq table）：既存 PCODE 基盤で可。失敗は正本どおり許容（RPS 無効化等）だが、静かに成功にしない。
5. **P6-c の実機はタイムアウト予算**：record は 5 engine × request、`intel_gt_wait_for_idle` は I915_GEM_IDLE_TIMEOUT。QEMU 120/240s 枠に収まる想定だが、hang した場合は wedged 相当で **停止・報告**（深追いしない）。
6. **HAL**：P6 全体で HAL は触りません。tasklet→既存 kworkqueue、µs poll→既存時間 backend、IRQ→既存 MSI attach。

## 6. ご判断いただきたい点

1. **big-bang 資産の扱い**：(a) 機構（ELSQ 書込・CSB 解析・PPGTT walker・forcewake 握手）は 6.8.12 と diff した上で再利用、**表（WA/MOCS/LRC offsets/regs）は 6.8.12 から再導出**し、7.1 との差分を台帳に記録【推奨】／(b) 全面再移植／(c) 7.1 転記をそのまま使用。
2. **`__engines_verify_workarounds`**：正本既定（DEBUG_GEM のみ）では走りません。P6 の目的が「Linux と同じレジスタ状態か」の確認なので、**読戻し検証を診断として常時実行**（HW 変更なし）することを推奨します。
3. **P6-c の実機実行**：null request（描画なし）ですが parity 経路での初 GPU 実行です。「描画再試験・hang 探索に戻らない」規約の範囲内（正本の通常初期化）と解釈していますが、**実行前に明示のご承認**をいただきたいです。
4. **RC6/PG の診断スイッチ**（既定は正本どおり ON、EU 試験 A/B 用に OFF 可）を用意してよいか。

## 7. 見積り（1 DMC = 5増分）
P6-0 1.8 ／ P6-a 2.5 ／ P6-b 1.5 ／ P6-c 1.5〜2 → **≈7〜8 DMC**（工程表の 3〜4 から上方修正）。P6 完了時点で「Linux 6.8.12 と同じ GT 初期化状態」のレジスタ・マニフェストが揃い、P7 前半（power_domains_enable / rpm 整理）を経て EU 試験へ進めます。
