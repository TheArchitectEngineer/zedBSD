<!-- awesome-plan project=zedbsd record=ws029-p004 -->

# WS029 p004: 実行: engine/LRC/execlists、request/seqno、engine reset

<!-- awesome-plan-current:start -->
Status: cleared
Phase disposition: normal
Parent: [WS029](https://github.com/awemorris/zedBSD/issues/386)
Queue: q314 active / q314-i04 cleared (whole Phase)
Dependencies: ws029-p003
Next: ws029-p005
<!-- awesome-plan-current:end -->

Combined ID: `ws029-p004`
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2, O4
Estimate: 300 active minutes（q314 の合計 1440 の一部）

共通の設計・事実・関数構成は [i915-design.md](../i915-design.md) を正本とする。本文は差分と受入条件だけを書く。実装前に [Guardrail](https://github.com/awemorris/zedBSD/issues/363) と `plan/coding-style.md` 全文を読む。HAL（`include/hal/hal.h`）と UAPI layout は変更しない。git add/commit/push はユーザーが行う。

## 目標

RCS0/BCS0 の engine 初期化、LRC（context image）、ELSQ 投入、CSB 消費、request/seqno、engine reset を実装し、attach 時 selftest（有効時）で user interrupt まで到達する。

## 手順

1. **転記 `.inc`**: `linux/i915-commands.inc`（p001 symbols の command 分）、`linux/i915-lrc-offsets.inc`（`gen12_xcs_offsets`、`gen12_rcs_offsets` と `NOP/LRI/REG/REG16/END` の意味を注記）、`linux/i915-mocs.inc`（gen12 global MOCS table）。
2. **`engine.c`**: `i915_engine_init`（HWSP object 4 KiB を GGTT に bind して `RING_HWS_PGA`、ring object 64 KiB、`RING_MODE_GEN7` の `GFX_RUN_LIST_ENABLE`、`RING_MI_MODE` の stop 解除、global MOCS を `GEN12_GLOBAL_MOCS(i)` へ書込み）、`i915_engine_reset`（`RING_RESET_CTL` request→ready→`GDRST` engine bit→完了→request 解除、失敗は EIO）、`i915_gt_reset`、`i915_engine_idle`。
3. **`lrc.c`**: `i915_lrc_create`（PPHWSP 1 page + state、offset 列を `MI_LOAD_REGISTER_IMM` 形式に展開、`RING_START/CTL/HEAD/TAIL`、`CTX_CONTEXT_CONTROL`、PDP0 = PML4 物理、`CTX_BB_PER_CTX_PTR = 0`）、`i915_lrc_descriptor`、`i915_lrc_submit`（`RING_EXECLIST_SQ_CONTENTS` 2 entry、`RING_EXECLIST_CONTROL` の load）、`i915_lrc_csb_consume`（HWSP の CSB write pointer と entry を読み、complete/switched を判定）、`i915_lrc_ring_emit`。
4. **`request.c`**: `i915_request_alloc`（slot 32、満杯 EAGAIN）、`i915_request_emit`（BCS: `MI_BATCH_BUFFER_START`(48-bit, PPGTT) → `MI_FLUSH_DW` post-sync store seqno to HWSP (GGTT) → `MI_USER_INTERRUPT` → padding。RCS: `PIPE_CONTROL` post-sync。batch 無しの marker request（job 用）も作れる）、`i915_request_submit`、`i915_request_retire`（HWSP seqno と CSB から完了 request を回収し `drv_gpu_complete(completion, 0)`、`drv_gpu_capacity_changed`）、`i915_request_fail`。
5. **`irq.c`**: handler から `i915_request_retire` を呼ぶ（irq_lock 内で slot 取得、callback は lock 外）。
6. **`selftest.c`**（`CONFIG_DRIVER_PCI_I915_SELFTEST=y` の時だけ link）: attach 内で BCS0 に `MI_STORE_DWORD_IMM`（GGTT の HWSP+0x80 へ 0xdeadbeef）+ post-sync + user interrupt の request を投入し、100 ms 以内に値と `user_count` の増加を確認。失敗は attach 失敗（publish しない）。`kern_logf("i915: selftest bcs0 store=%s irq=%u\n")`。
7. **fixture**: `i915-lrc-test.c`（offset 列の展開結果と期待 register 列、descriptor 値、CSB 判定）、`i915-irq-test.c` に user interrupt→retire→`drv_gpu_complete` を追加、`i915-backend-test.c` に submit→interrupt→完了、slot 枯渇 EAGAIN、engine reset 手順の register 列を追加。
8. **build** 2 構成 + selftest 構成（`config-i915-selftest-amd64.mk`）。

## 関数構成

設計資料 §4 の `engine.c`、`lrc.c`、`request.c`、`selftest.c`。ELSQ は 1 entry、preemption なし、engine ごとに直列。

## 完了条件

fixture PASS、build 3 構成 PASS、`.inc` 出典一致。実機は未使用（selftest は p006 で初めて実行）。記録同期。

## q314 p004 完了: 実行（engine/LRC/execlists、request/seqno、engine reset、selftest）（2026-09-14）

[WS029 p004](https://github.com/awemorris/zedBSD/issues/401)（q314-i04）を cleared にし、[p005](https://github.com/awemorris/zedBSD/issues/402)（q314-i05、drv_gpu 統合）を in-progress にする。実機は未使用（selftest の実行は p006）。HAL/UAPI 変更なし。git add/commit/push はユーザー担当。

新規・変更 source（すべて local、Zlib。転記 `.inc` は MIT 表示付き）:
- `linux/i915-commands.inc`（63 定義: MI_*/XY_*/PIPE_CONTROL）、`linux/i915-lrc-offsets.inc`（`gen12_xcs_offsets`/`gen12_rcs_offsets` と NOP/LRI/REG/REG16/END の encode macro を `I915_LRC_*` に改名して verbatim 転記）、`linux/i915-mocs.inc`（LE_/L3_/L4_ macro、`MOCS_ENTRY`、`GEN11_MOCS_ENTRIES`、`gen12_mocs_table`）。generator `plan/ws029/tests/gen-inc.py` に verbatim block 転記と `BUILD_BUG_ON_ZERO` 除去を追加。`i915-regs.inc` に `GEN11_GRDOM_RENDER`、`GEN9_LNCFCMOCS`、`BLIT_CCTL_*_MOCS_MASK`、`GEN12_GFX_PREFETCH_DISABLE` を追加（168 定義）。
- `engine.c`: `drv_i915_engines_start`（forcewake GT+RENDER を device 寿命で保持、global MOCS 64 entry と LNCFCMOCS 32 pair を書込み、RCS0/BCS0 の HWSP object と kernel context を作成し `i915_engine_program`: HWSTAM、`GEN11_GFX_DISABLE_LEGACY_MODE`、STOP_RING 解除、HWS_PGA、EMR/EIR/ESR、BCS の `BLIT_CCTL` を uncached MOCS index 3、CSB pointer reset）、`drv_i915_engine_reset`（STOP_RING+PREFETCH_DISABLE→MODE_IDLE 待ち→`RESET_CTL` request/ready→`GDRST` engine domain（2 回書き）→cancel→再 program）、`drv_i915_engine_interrupt`（irq_lock 内で CSB 消費→seqno retire→次 request 投入、lock 外で完了 callback）、`drv_i915_engine_idle`。
- `lrc.c`: `drv_i915_lrc_create`（RCS 14 page / BCS 2 page の image を GGTT に bind、context ごとに 64 KiB ring、offset 列を `MI_LOAD_REGISTER_IMM|LRM_CS_MMIO(|FORCE_POSTED)` に展開、CONTEXT_CONTROL の inhibit、PDP0=PML4、MI_MODE pair の STOP_RING 解除、RING_START/HEAD/TAIL/CTL、末尾 `MI_BATCH_BUFFER_END|1`、descriptor low=64B addressing|VALID|PRIVILEGE|GGTT、high=sw_id<<5|class<<29|instance<<16）、`submit`（image tail 更新→ELSQ port1=0/port0=desc|FORCE_RESTORE→`EL_CTRL_LOAD`）、`reset_csb`、`csb_consume`（HWSP write pointer、entry -1 なら mmio mirror、gen12 parse: away 無効または new queue で promotion、それ以外 completion）、`ring_space`/`ring_emit`（末尾 NOOP 詰めで wrap）。
- `request.c`: slot 32、FIFO queue、`kick`（engine idle 時のみ emit+submit: preparser disable→TLB invalidate flush(BCS: MI_FLUSH_DW、RCS: PIPE_CONTROL)→extra dwords→`MI_BATCH_BUFFER_START_GEN8|NON_SECURE`(48-bit PPGTT)→breadcrumb（BCS: flush + `MI_FLUSH_DW` post-sync store to HWSP seqno via GGTT、RCS: PIPE_CONTROL flush + QW_WRITE）→`MI_USER_INTERRUPT`→ARB enable→ARB_CHECK/NOOP）、`retire`（HWSP seqno 一致）、`fail`（queue/active を error で回収）、`complete_list`（`drv_gpu_complete` を lock 外で呼び slot 解放、`drv_gpu_capacity_changed`）。
- `selftest.c`（`CONFIG_DRIVER_PCI_I915_SELFTEST=y` のみ link）: kernel context で BCS0 に `MI_STORE_DWORD_IMM|USE_GGTT`（HWSP scratch dword に 0xdeadbeef）を含む request を投入し 100 ms 以内に値・seqno・user interrupt 増加を確認、`i915: selftest bcs0 store=%s irq=%u seqno=%u/%u`。失敗は attach 失敗。
- `irq.c` が engine へ転送、`i915.c` は attach で engines start（+selftest）、open で engine ごとの context 作成、close で破棄（quarantine 時は保持）、stop で GT reset→engines stop→object 回収。`uncore.c` に `drv_i915_domain_reset`。`platform/amd64/vmunix.mk` に engine/lrc/request と条件付き selftest、`plan/ws029/tests/config-i915-selftest-amd64.mk`。
- fixture: `i915-lrc-test.c`（新規: image layout が Linux の CTX_* index と一致、descriptor、ELSQ 書込み、CSB 判定と mirror fallback、ring wrap/space）、`i915-fixture.inc` に execlists emulator（ELSQ load を記録し `fixture_run_engines()` が image→ring を parse: MI_STORE_DWORD_IMM(GGTT/PPGTT)、MI_FLUSH_DW store、PIPE_CONTROL QW write、MI_BATCH_BUFFER_START を PPGTT 経由で追跡、MI_SEMAPHORE_WAIT で hang、CSB event 2 件と割込み identity を作り handler を呼ぶ; MI_MODE/RESET_CTL/GDRST の応答）、`i915-backend-test.c` に engine 初期化 register 値、selftest 成功、batch による resource 書込みと完了 callback、hang→`request_fail(EIO)`→engine reset→再実行を追加、`i915-irq-test.c` に engine 転送の確認。

検証（agent-1）:
- 3 構成 build PASS（i915、i915+selftest、GPU なし: `drv_i915_` symbol 0）、warning 0、`amd64 vmunix check: PASS`。
- `sh plan/ws029/tests/run-i915-host-tests.sh`（uncore/gtt/irq/lrc/backend、通常＋ASan/UBSan）: PASS。
- `python3 plan/ws029/tests/run-i915-build-selection-test.py`: PASS。`git diff --check`: PASS。

設計との差分: ring は engine 共有ではなく context ごと（execlists は context save で RING_HEAD を image に書き戻すため、共有 ring では head/tail が食い違う。Linux と同じ構成）。forcewake は attach 後に恒久保持（IRQ 文脈からの ELSQ 書込みで ACK 待ちを避けるため）。`CTX_R_PWR_CLK_STATE` は 0（RCS の 3D 利用は WS029 後続）。

制限: 実機での CSB/割込み挙動は未証明（fixture の model は Linux の parse 規則に基づく）。1 engine 1 request 直列。`GPU_CAP_COMMAND`/JOB/recovery ops は p005。
