<!-- awesome-plan project=zedbsd record=ws029-p003 -->

# WS029 p003: メモリ: GEM object、48-bit PPGTT、CPU view

<!-- awesome-plan-current:start -->
Status: cleared
Phase disposition: normal
Parent: [WS029](https://github.com/awemorris/zedBSD/issues/386)
Queue: q314 active / q314-i03 cleared (whole Phase)
Dependencies: ws029-p002
Next: ws029-p004
<!-- awesome-plan-current:end -->

Combined ID: `ws029-p003`
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2, O4
Estimate: 180 active minutes（q314 の合計 1440 の一部）

共通の設計・事実・関数構成は [i915-design.md](../i915-design.md) を正本とする。本文は差分と受入条件だけを書く。実装前に [Guardrail](https://github.com/awemorris/zedBSD/issues/363) と `plan/coding-style.md` 全文を読む。HAL（`include/hal/hal.h`）と UAPI layout は変更しない。git add/commit/push はユーザーが行う。

## 目標

GEM object（物理連続 backing）と 48-bit 4-level PPGTT を実装し、drv_gpu の `resource_create/destroy/read/write` を接続する。

## 手順

1. **`gem.c`**: `i915_gem_create`（`kern_pmem_alloc_limited(bytes, 4096, (1ULL<<39)-1, 0)`、CPU address は `include/kern/pmem.h` の物理→kernel 変換、zero fill）、`i915_gem_destroy`（quarantined なら controller list に保持して reset で解放）、`i915_gem_bind_ggtt/unbind_ggtt`、`i915_gem_bind_vm/unbind_vm`、`i915_gem_read/write`（`memcpy`、境界検査は core が済ませた前提でも再検査）。
2. **`ppgtt.c`**: `i915_ppgtt_create`（PML4 + scratch PT/PD/PDP）、`i915_ppgtt_insert/clear`（level ごとに 4 KiB page を `kern_pmem_alloc_limited`、PTE/PDE/PDPE/PML4E の encode 値は `.inc` の `GEN8_PAGE_PRESENT|GEN8_PAGE_RW`、gen12 PAT index 0）、`i915_ppgtt_destroy`、`i915_ppgtt_va_alloc`（bump、開始 `0x1_0000_0000`、2 MiB 揃え）。
3. **drv_gpu 接続**（`i915.c`）: `capabilities |= GPU_CAP_RESOURCE | GPU_CAP_TRANSFER`、`resource_create`（`usage` は STORAGE のみ受理、`i915_gem_create` + session VM へ bind、`kern_logf("i915: resource session=%u slot=%u bytes=%llu phys=0x%llx va=0x%llx\n")`）、`resource_destroy`、`resource_read/write`。`open` で `i915_ppgtt_create`、`close` で destroy。
4. **fixture**: `i915-gtt-test.c` に PPGTT 部分を追加（4-level walk が正しい index を使う、scratch が未 map 領域に残る、encode 値が Linux 定義と一致、VA allocator、2 つの VM が独立）。`i915-backend-test.c`（新規、偽 hardware）で open→create→write→read→destroy→close と 16 MiB 上限、EINVAL/ENOMEM の巻戻し。
5. **build**: p002 と同じ 2 構成。

## 完了条件

fixture PASS、build PASS、`resource` ログ行の書式固定（harness が p006 で解析する）。記録同期。

## q314 p003 完了: メモリ（GEM object、48-bit PPGTT、CPU view）（2026-09-14）

[WS029 p003](https://github.com/awemorris/zedBSD/issues/400)（q314-i03）を cleared にし、[p004](https://github.com/awemorris/zedBSD/issues/401)（q314-i04、実行）を in-progress にする。実機は未使用。HAL/UAPI 変更なし。git add/commit/push はユーザー担当。

新規・変更 source（すべて local、Zlib）:
- `src/drivers/gpu/i915/ppgtt.c`: `drv_i915_ppgtt_create`（scratch page → scratch PT/PD/PDP の連鎖、PML4 は scratch PDP で充填）、`destroy`、`va_alloc`（bump、開始 0x1_0000_0000、2 MiB 揃え、再利用なし）、`insert`（4-level walk、欠けた table を 4 KiB page で確保し下位 scratch entry で充填）、`clear`（leaf を scratch に戻す）、`lookup`（試験用）。encode: page/table = `GEN8_PAGE_PRESENT|GEN8_PAGE_RW`（PAT index 0）、scratch table は Linux の `gen8_pde_encode(I915_CACHE_NONE)` と同じ `PAT0|PAT1`。
- `src/drivers/gpu/i915/gem.c`: `drv_i915_gem_create`（`kern_pmem_alloc_limited` 4 KiB 揃え・39-bit 上限・zero fill、device list に登録）、`destroy`（bound/quarantined なら保持）、`bind/unbind_ggtt`、`bind/unbind_vm`、`read/write`（範囲再検査、barrier）。
- `src/drivers/gpu/i915/internal.h`: `struct i915_ppgtt`/`i915_ppgtt_page`/`i915_gem_object`、session は `vm` を別 allocation で保持（quarantine 時に close で device の `quarantined_vms` に移し、reset/stop で解放）、device の object list と counter。
- `src/drivers/gpu/i915/i915.c`: capabilities = `GPU_CAP_RESOURCE|GPU_CAP_TRANSFER`、`open`（session 番号、PPGTT 作成）、`close`（PPGTT 破棄または quarantine 保持）、`resource_create`（`GPU_RESOURCE_USAGE_STORAGE` のみ、1..16 MiB、VM へ bind、log `i915: resource session=%u slot=%u bytes=%llu phys=0x%llx va=0x%llx`）、`resource_destroy`（quarantine 時は保持）、`resource_read/write`（mutex 下で copy）、`i915_stop` が残存 object と quarantined VM を解放。
- build: `platform/amd64/vmunix.mk` に `ppgtt.c`/`gem.c`。監査に `gt/intel_gtt.c`（MIT）を追加（29→30 ファイル）。
- fixture: `plan/ws029/tests/i915-gtt-test.c` に PPGTT 3 試験（scratch 連鎖と encode 値、index bit ごとの walk と table 確保数、VA allocator）、`i915-backend-test.c`（新規: 登録→attach→publish→open/get_info→create/write/read/destroy→close→unpublish→detach、6000 B が 2 page に丸められ VA 0x1_0000_0000、16 MiB 上限、EINVAL/ENOMEM 巻戻し、quarantine 保持と detach での回収、lease 全解放）、`i915-fixture.inc` に mutex/PCI attach/drv_gpu 登録の stub と 32 MiB の偽 page pool。

検証（agent-1）:
- `make BUILD=build/i915-amd64 ZEDBSD_CONFIG=plan/ws029/tests/config-i915-amd64.mk vmunix`: PASS（warning 0、`amd64 vmunix check: PASS`、`drv_i915_*` 28 symbol）。I915 := n: PASS、symbol 0。
- `sh plan/ws029/tests/run-i915-host-tests.sh`（uncore/gtt/irq/backend、通常＋ASan/UBSan）: PASS。
- `python3 plan/ws029/tests/run-i915-build-selection-test.py`: 6 platform × Venus/i915 各 y/n PASS（i915 object 6 個は amd64 かつ y のときだけ）。
- `git diff --check`: PASS。

判明した事項: quarantined session の close で page table が漏れる設計穴を fixture が検出し、VM を別 allocation にして device 側 list へ移す形に直した（close は失敗できない契約のため close 時に allocation しない）。

制限: 実機未接続。`GPU_CAP_COMMAND`/JOB は p004–p005。resource 上限は 1 object 16 MiB、物理連続。
