<!-- awesome-plan project=zedbsd record=ws029-p002 -->

# WS029 p002: driver骨格: PCI attach、MMIO/forcewake、GGTT、割込み

<!-- awesome-plan-current:start -->
Status: cleared
Phase disposition: normal
Parent: [WS029](https://github.com/awemorris/zedBSD/issues/386)
Queue: q314 active / q314-i02 cleared (whole Phase)
Dependencies: ws029-p001
Next: ws029-p003
<!-- awesome-plan-current:end -->

Combined ID: `ws029-p002`
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2, O4
Estimate: 240 active minutes（q314 の合計 1440 の一部）

共通の設計・事実・関数構成は [i915-design.md](../i915-design.md) を正本とする。本文は差分と受入条件だけを書く。実装前に [Guardrail](https://github.com/awemorris/zedBSD/issues/363) と `plan/coding-style.md` 全文を読む。HAL（`include/hal/hal.h`）と UAPI layout は変更しない。git add/commit/push はユーザーが行う。

## 目標

i915 backend の骨格を作り、PCI attach → BAR map → forcewake → GT reset → GGTT → MSI 割込み → `drv_gpu_register` までを host fixture と amd64 build で固定する。実機はまだ使わない。

## 手順

1. **転記 `.inc`**: `src/drivers/gpu/i915/linux/i915-regs.inc`（p001 の symbols のうち register 分）と `linux/i915-ids.inc`（`8086:46a8` を含む ADL-P の ID。出典が `include/drm/intel/pciids.h` の場合はそのライセンスも audit に追加）。header は設計資料 §3 の形式、出典 path と tag/commit を必ず書く。
2. **公開 header と内部 header**: `include/drivers/i915.h`（`int drv_i915_pci_driver_register(void);` だけ）、`src/drivers/gpu/i915/internal.h`（設計資料 §4 の構造体、`I915_ENGINE_RCS0=0`/`I915_ENGINE_BCS0=1`、`I915_RCS0_BASE 0x2000`/`I915_BCS0_BASE 0x22000` は `.inc` の値を参照）。
3. **`i915.c`**: `drv_i915_pci_driver_register`、`i915_match`（ids 表）、`i915_attach`/`i915_start`/`i915_detach`/`i915_publish`/`i915_unpublish`。attach 手順は設計資料 §5 の 1–7 と 9（8 の selftest は p004）。`drv_gpu_ops` は `version=DRV_GPU_INTERFACE_VERSION`、`capabilities=0`、`open/close/get_info` だけを暫定実装（open は `kern_calloc` の session、get_info は `driver_name="i915"`、`max_resource_bytes=16 MiB`）。Venus と同じく `stage` 文字列と `kern_logf("i915: attach stopped at %s: %d\n")`。
4. **`uncore.c`**: `i915_mmio_read32/write32`、`i915_uncore_wait`、`i915_forcewake_get/put`（GT/RENDER の 2 domain。書込み値は `(mask<<16)|bits`、ACK は対応 register の bit を `i915_uncore_wait` で 50 ms）、`i915_uncore_init`。
5. **`ggtt.c`**: `i915_ggtt_probe`（PCI config 0x50 の GGMS から size、8 MiB 窓 map）、`i915_ggtt_scratch_init`、`i915_ggtt_alloc/free`（bitmap、先頭 1 MiB 予約）、`i915_ggtt_insert/clear`（PTE 書込み後 `GFX_FLSH_CNTL`）。
6. **`irq.c`**: `i915_irq_init`（MSI 1、`drv_pci_device_allocate_irqs(DRV_PCI_IRQ_ALLOW_MSI, 1, 1)`、`establish_irq`）、`i915_irq_handler`（設計資料 §4 の identity/IIR 手順、今は `user_count`/`csb_count` を数えるだけ）、`i915_irq_reset`。
7. **build 配線**: `config/drivers/pci.drivers` に `CONFIG_DRIVER_PCI_I915|bool|Intel i915 GPU (Alder Lake-P, experimental)|amd64|n|`、`Makefile` に `CONFIG_DRIVER_PCI_I915 ?= n`、`-DCONFIG_DRIVER_PCI_I915=`、`KERN_GPU_BACKENDS := $(CONFIG_DRIVER_PCI_VENUS) $(CONFIG_DRIVER_PCI_I915)`（y を含めば GPU core を link）、i915 source list、`CONFIG_DRIVER_PCI_I915_SELFTEST ?= n`。`src/drivers/pci/pci.c` の Venus 登録と同じ場所に `#if CONFIG_DRIVER_PCI_I915` で登録。`plan/ws029/tests/config-i915-amd64.mk`（`config-wayland-amd64.mk` を基に `CONFIG_DRIVER_PCI_VENUS := n`、`CONFIG_DRIVER_PCI_I915 := y`）。
8. **host fixture**（`plan/ws029/tests/`、production source を include、通常＋ASan/UBSan、`run-*.sh` 形式は `plan/ws014/tests/run-gpu-supervision-test.sh` に倣う）: `i915-uncore-test.c`（偽 MMIO 配列。forcewake get で `FORCEWAKE_*` に期待値が書かれ ACK を poll し、ACK が立たなければ ETIMEDOUT）、`i915-gtt-test.c`（GGTT 部分: PTE encode、scratch fill、allocator、insert/clear と flush 書込み）、`i915-irq-test.c`（enable 列と handler の identity/IIR 手順）、`run-i915-build-selection-test.py`（`plan/ws014/tests/run-gpu-build-selection-test.py` を複製し I915 y/n を追加）。
9. **build**: `make -j16 BUILD=build/i915-amd64 ZEDBSD_CONFIG=plan/ws029/tests/config-i915-amd64.mk vmunix` と GPU なし amd64 build で `drv_i915_` symbol 不在（`nm`）。`git diff --check`。

## 関数構成

設計資料 §4 の `uncore.c`、`ggtt.c`、`irq.c`、`i915.c`（attach/start/detach/publish/unpublish/open/close/get_info）。forward declaration、purpose comment、`Succeeded:` return の規約を最初から守る。

## 完了条件

fixture 4 種 PASS、I915 y/n の amd64 build PASS、`.inc` の出典と audit の一致、規約の自己確認。記録を GitHub へ同期。実機未使用と明記。

## q314 p002 完了: driver 骨格（PCI attach、MMIO/forcewake、GGTT、割込み）（2026-09-14）

[WS029 p002](https://github.com/awemorris/zedBSD/issues/399)（q314-i02）を cleared にし、[p003](https://github.com/awemorris/zedBSD/issues/400)（q314-i03、メモリ）を in-progress にする。実機は未使用。HAL/UAPI 変更なし。git add/commit/push はユーザー担当。

新規 source（すべて local、Zlib）:
- `include/drivers/i915.h`: `drv_i915_pci_driver_register()` のみ公開。
- `src/drivers/gpu/i915/internal.h`: `struct i915_device`/`i915_ggtt`/`i915_session`、定数（engine slot、forcewake domain、timeout、予約 GGTT page、IRQ bank）、cross-file prototype。
- `src/drivers/gpu/i915/i915.c`: ID 表（ADL-P/ADL-N/RPL-U/RPL-P、`8086:46a8` を含む 54 ID）、attach → start（stage: dma-provider、save-pci-state、enable-pci-memory、bar0、map-registers、uncore、gt-reset、ggtt-probe/scratch/bitmap/fill、enable-bus-master、irq、gpu-publication）→ publish（`drv_gpu_ops` v9、capabilities=0、open/close/get_info のみ）、stop/detach の逆順解放。失敗時 `i915: attach stopped at <stage>: <errno>`。
- `src/drivers/gpu/i915/uncore.c`: `drv_i915_read32/write32`（BAR0 窓の範囲検査）、`drv_i915_wait32`（`sched_ticks` 上限）、`drv_i915_forcewake_get/put`（GT/RENDER の 2 domain、masked write、ACK poll 50 ms、参照計数）、`drv_i915_uncore_init`（全 domain 解放、GDRST 進行中なら待つ）、`drv_i915_gt_reset`（GRDOM_FULL、1 s）。
- `src/drivers/gpu/i915/ggtt.c`: `drv_i915_ggtt_start`（GMCH 0x50 の GGMS から entry 数、BAR0 上半分を map、scratch page、bitmap、全 PTE を scratch で埋めて flush）、`alloc/free`（first-fit、先頭 1 MiB 予約）、`insert/clear`（PTE=phys|PRESENT、`GFX_FLSH_CNTL`）、`stop`。
- `src/drivers/gpu/i915/irq.c`: `drv_i915_irq_start/stop/reset`（MSI 1 本、RENDER_COPY enable に user/CS error/context switch/semaphore、RCS0/BCS0 mask、他 class は disable/mask、master enable）、`drv_i915_irq_handler`（master disable→bank→selector→identity valid 待ち→class/instance/intr→counter→ack→master enable）。今は counter（user/context switch/error/unknown/identity timeout）だけを更新し、p004 で request 処理へ接続する。
- `src/drivers/gpu/i915/linux/i915-regs.inc`（163 定義）、`linux/i915-ids.inc`（4 表）: `plan/ws029/tests/gen-inc.py v6.19` が Linux v6.19 の MIT ファイルから `#define` だけを機械転記（`_MMIO` 除去、`REG_BIT`→`I915_INC_BIT` 等、U suffix）。header に各出典の copyright 行、MIT permission notice、出典 path と SHA-256、変換規則を記載。generator は出典 SHA-256 が `i915-license-audit.md` の値と一致し判定が MIT であることを検査し、転記本文が未転記 symbol を参照していれば失敗する。
- build: `Makefile`（`CONFIG_DRIVER_PCI_I915`/`_SELFTEST` 既定 n、-D、`KERN_GPU_BACKENDS` に追加）、`platform/amd64/vmunix.mk`（`AMD64_I915_SOURCES`）、`src/kern/platform/pcat.c`（登録）、`config/drivers/pci.drivers`、`config/kernel-options.list`（selftest bool）、`plan/ws029/tests/config-i915-amd64.mk`。

検証（agent-1）:
- `make BUILD=build/i915-amd64 ZEDBSD_CONFIG=plan/ws029/tests/config-i915-amd64.mk vmunix`: PASS（warning 0、`amd64 vmunix check: PASS`、`drv_i915_*` 14 symbol link）。
- 同 config で I915 := n: PASS、`drv_i915_`/`drv_gpu_register` symbol 0。
- `sh plan/ws029/tests/run-i915-host-tests.sh`（uncore/gtt/irq、通常＋ASan/UBSan）: PASS。forcewake 参照計数と timeout 時の巻き戻し、GDRST self-clear/stuck、範囲外 MMIO 拒否、GGTT 1M entry の scratch fill・first-fit・insert/clear/free・不正引数、IRQ enable/mask 値、bank/identity decode（RCS0/BCS0/未知 class）、CS error の EIR 記録、identity timeout。
- `python3 plan/ws029/tests/run-i915-build-selection-test.py`: 6 platform × Venus/i915 各 y/n で PASS（GPU core はどちらかの backend が y のときだけ、i915 object は amd64 かつ y のときだけ）。
- `git diff --check`: PASS。規約 checklist（forward declaration、purpose comment、`Succeeded:` return、条件分割、for 初期化子なし）を自己確認。

制限: 実機未接続のため hardware 動作は未証明。engine/LRC/submission は p004。`GPU_CAP_*` は 0 のため `/dev/gpu0` は open/get_info しかできない。
