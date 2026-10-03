# WS029 i915 ネイティブ GPU driver: 設計資料（q314 計画）

この資料は WS029 p001–p007 が共有する事実、決定、ファイル構成、関数一覧、シーケンス、試験設計を固定する。各 Phase はここを参照し、Phase 本文には差分と受入条件だけを書く。数値（register offset、bit、opcode）は照合用であり、実装時は必ず Linux の MIT ファイル（下記「参照元」）から転記して出典行を残す。

## 1. 確定した事実（2026-09-14 調査）

| 項目 | 値 |
| --- | --- |
| テスト機 | Dell Latitude 5330（BIOS 1.31.1）、`awe@10.0.10.25`、Debian 13、kernel 6.19.13、12 thread、RAM 8 GiB、`sudo -n` 可 |
| GPU | Intel Alder Lake-P GT2 Iris Xe、PCI `0000:00:02.0` `8086:46a8` rev 0c、subsystem `1028:0b02`。i915: platform ALDERLAKE_P、graphics/media version 12、stepping C0 |
| 能力 | has_logical_ring_contexts=yes、has_logical_ring_elsq=yes、ppgtt-type 2（full）、ppgtt-size 48、has_llc=yes、has_reset_engine=yes、has_64bit_reloc=yes、has_flat_ccs=no。engine: rcs0、bcs0、vcs0、vcs1、vecs0 |
| BAR | BAR0 16 MiB MMIO（非prefetch、GTTMMADR: 下位 8 MiB = register、上位 8 MiB = GGTT PTE）、BAR2 256 MiB prefetch（GMADR aperture）、BAR4 I/O 64 byte。MSI 1 vector（maskable、32bit）。PASID/ATS/PRI/SR-IOV capability あり（使わない） |
| IOMMU/VFIO | DMAR あり、iommu group 0 に `00:02.0` 単独。`vfio`, `vfio_iommu_type1`, `vfio-pci` module 存在、`CONFIG_VFIO_PCI_IGD=y`。RMRR `0x6c000000–0x707fffff`（IGD は relaxable） |
| host 表示 | i915 が bound、GDM active、seat0/tty2 に awe のセッション。`xe` module は load 済みだが未 bind |
| QEMU | `/usr/bin/qemu-system-x86_64` 10.0.11（Debian）。`vfio-pci`、`vfio-pci-igd-lpc-bridge` device あり |
| 既存 harness | `plan/ws014/tests/run-venus-remote.py` + `venus-qemu.py`: image 複製→scp→QEMU（`-machine pc,accel=kvm`、OVMF、`-debugcon file:`、QMP `pmemsave` で `vt_history` を回収）。console 文字列と guest RAM の物理読出しが既にある |

Linux 6.x i915 の参照元ファイルのライセンス（raw.githubusercontent.com torvalds/linux master、2026-09-14 確認）:

| ファイル | 表記 |
| --- | --- |
| `i915_reg.h` | MIT/X11（Tungsten Graphics 2003、Permission is hereby granted…） |
| `intel_uncore.c` | MIT/X11（Intel 2013） |
| `gt/intel_gt_regs.h`, `gt/intel_engine_regs.h`, `gt/intel_lrc_reg.h` | `SPDX-License-Identifier: MIT` |
| `gt/intel_gpu_commands.h` | `SPDX-License-Identifier: MIT` |
| `gt/intel_lrc.c`, `gt/intel_execlists_submission.c`, `gt/intel_reset.c`, `gt/gen8_ppgtt.c`, `gt/intel_ggtt.c` | `SPDX-License-Identifier: MIT` |

p001 でこれ以外の参照ファイル（`i915_pci.c`, `gt/intel_gt_irq.c`, `gt/intel_engine_cs.c`, `gt/gen8_engine_cs.c`, `gt/intel_mocs.c`, `gt/intel_gtt.h`, `i915_reg_defs.h`, `intel_pci_config.h`, `gt/intel_engine_types.h`, `gt/intel_context_types.h`）も同じ方法で確認し、GPL-2.0 のファイルは参照元から外す。`gt/uc/`（GuC/HuC）は使わない。

## 2. 決定

1. **submission は execlists（ELSQ）**。GuC/HuC firmware は使わない（firmware 配布条件と DMA 検証コストを避ける）。ADL-P は execlists をサポートする（has_logical_ring_elsq）。
2. **表示は今回対象外**。VFIO の IGD passthrough は UPT（legacy VGA なし）で guest に画面出力が無い。scanout/mode set/DMC は WS029 の後続 Phase で扱う。
3. **CPU/GPU coherency は LLC に依存**。ADL は has_llc=yes。全 GEM object を LLC cache（PAT index 0 / WB）で map し、CPU も WB で map する。clflush 用の HAL primitive は存在しないため追加しない（非 LLC 機は対象外と明記）。
4. **移植方針（ライセンス境界）**: 設計方針（`plan/master-design-policy.md` 2.1「External implementations are not incorporated into the base system」）と RTL8822B の前例（`src/drivers/wifi/rtl8822b/rtl8822b-tables.inc`、BSD-3 header を保持した分離 `.inc`）を踏まえ、次を採る。
   - Linux の **MIT ファイルから転記するのは定義とテーブル**（register offset/bit、command opcode、context image の offset 列、device ID、MOCS 表）に限り、`src/drivers/gpu/i915/linux/*.inc` へ元の copyright/permission notice を verbatim で置き、先頭に「Modified for zedBSD: 抜粋・改名・整形。出典 Linux <path>@<commit>」を追記する。ファイルごとに出典 commit hash を `plan/ws029/i915-license-audit.md` に記録する。
   - **driver の論理（初期化、GGTT/PPGTT 管理、submission、割込み、reset、drv_gpu 統合）は zedBSD の規約で新規に書く**（Zlib）。Linux の関数を写さず、参照した関数名を purpose comment に「reference: intel_ggtt.c gen8_ggtt_insert_entries」の形で残す。
   - ユーザーは「著作権表示をしつつ修正した旨をヘッダに入れて中身を直す」案を提示した。上記は同じ帰結（MIT notice + 修正表示）を定義/テーブルに適用し、論理はゼロから書くことで規約適合と設計方針の両立を図る。**Queue 開始時にこの方針をユーザーへ確認し、承認を journal に残す**（p001 の最初の項目）。
5. **HAL は変更しない**。BAR は `drv_pci_device_map_bar_region`（NOCACHE）で map し、aperture（BAR2）は最小範囲だけ map する。必要になれば個別承認。
6. **UAPI は変更しない**。既存 `/dev/gpuN` の ioctl（GET_INFO、RESOURCE_CREATE/DESTROY、TRANSFER、COMMAND_SUBMIT/WAIT、JOB_*、FENCE_*）で試験する。native command stream の中身は backend 定義（§7）。
7. **v1 の GEM backing は物理連続**（`kern_pmem_alloc_limited`、39-bit 制約、最大 16 MiB/object）。scatter は後続。
8. **root 専用 ABI（既存）**。batch の検証は行わず、command stream の header と relocation だけ検証する。

## 3. ファイル構成

```
src/drivers/gpu/i915/
  internal.h            構造体・定数・関数プロトタイプ（Zlib）
  i915.c                PCI driver、drv_gpu_ops、session/resource/submit の glue（Zlib）
  uncore.c              MMIO read/write、forcewake domain、register wait（Zlib）
  ggtt.c                GGTT: probe、scratch page、insert/clear、aperture window（Zlib）
  ppgtt.c               48-bit 4-level PPGTT: PML4/PDP/PD/PT 割当、insert/clear、scratch（Zlib）
  gem.c                 GEM object: 物理連続 backing、CPU view、GGTT/PPGTT bind、reloc patch（Zlib）
  engine.c              engine 記述子（RCS0/BCS0）、HWSP、ring、init、engine reset（Zlib）
  lrc.c                 context image 構築、context descriptor、ELSQ 投入、CSB 消費（Zlib）
  irq.c                 Gen11 割込み: master/GT enable、identity/IIR、handler（Zlib）
  request.c             request ring、seqno、完了判定、drv_gpu_complete 呼出し（Zlib）
  selftest.c            CONFIG_DRIVER_PCI_I915_SELFTEST 時の attach 後 smoke（Zlib）
  linux/i915-regs.inc          register offset/bit 定義（MIT 転記、出典付き）
  linux/i915-commands.inc      MI_*/BLT/PIPE_CONTROL opcode（MIT 転記）
  linux/i915-lrc-offsets.inc   gen12 rcs/xcs context register offset 列（MIT 転記）
  linux/i915-ids.inc           ADL-P device ID 表の抜粋（MIT 転記）
  linux/i915-mocs.inc          gen12 MOCS 表の抜粋（MIT 転記）
include/drivers/i915.h  drv_i915_pci_driver_register() だけ公開
config/drivers/pci.drivers  CONFIG_DRIVER_PCI_I915|bool|Intel i915 GPU (ADL-P, experimental)|amd64|n|
Makefile                CONFIG_DRIVER_PCI_I915、KERN_GPU_BACKENDS に追加、-D、source list、CONFIG_DRIVER_PCI_I915_SELFTEST
plan/ws029/tests/       host fixture、guest 試験、remote harness、host 手順 script
userland/base/tests/gpu-i915/main.c  guest 試験プログラム /bin/gpu-i915-test
```

`.inc` の header 形式（RTL8822B に倣う）:

```
/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2003-2018 Intel Corporation
 * (元の permission notice を verbatim)
 *
 * Modified for zedBSD: excerpted and renamed from Linux
 * drivers/gpu/drm/i915/gt/intel_gpu_commands.h at commit <hash>.
 * Values are unchanged; only the subset used by src/drivers/gpu/i915 remains.
 */
```

## 4. 関数一覧（概要設計）

命名は `i915_<file>_<verb>` の static、公開は `drv_i915_*`。エラーは errno 正値。全 callback は core spinlock 外で呼ばれる（drv_gpu 契約）。

### internal.h（主要構造体）

- `struct i915_device`: `struct drv_pci_device *pci`、`struct drv_pci_mapping regs`（BAR0 0..8 MiB）、`gtt`（BAR0 8..16 MiB）、`aperture`（BAR2 先頭 16 MiB）、`struct drv_dma_device *dma`、`struct drv_pci_irq irq`、forcewake state、`struct i915_ggtt ggtt`、`struct i915_engine engines[2]`、`struct mutex mutex`（controller 直列化）、`struct spinlock irq_lock`、`struct drv_gpu_device *gpu`、`failed`、`stage`。
- `struct i915_ggtt`: PTE 数、scratch page、bitmap allocator（4 KiB 単位、先頭 1 MiB は予約）。
- `struct i915_ppgtt`: PML4 物理、各 level の page 配列（物理連続 4 KiB）、scratch 3 段。
- `struct i915_gem_object`: `struct kern_pmem run`、bytes、CPU address、GGTT offset（0=未bind）、PPGTT VA、owner session、`quarantined`。
- `struct i915_engine`: class/instance、mmio base（RCS0 `0x2000`、BCS0 `0x22000`）、HWSP object、ring object（64 KiB）、tail、default context image、`struct i915_request` ring[32]、seqno、CSB 読出し位置、`reset_count`。
- `struct i915_context`（session ごと）: `struct i915_ppgtt vm`、engine ごとの LRC object（`lrc[2]`）、descriptor、`stopping`、`quarantined`。
- `struct i915_request`: seqno、engine、`struct drv_gpu_completion *completion`、batch object、submitted tick、`supervised`。
- `struct i915_session`: context、resource list、`stop_request`、`quiesced`。

### uncore.c

| 関数 | 役割・参照 |
| --- | --- |
| `i915_mmio_read32/write32(device, offset)` | BAR0 volatile access + `kern_io_*_barrier`。offset < 8 MiB を assert |
| `i915_forcewake_get(device, domains)` / `_put` | `FORCEWAKE_GT_GEN9`/`FORCEWAKE_RENDER_GEN9`/`FORCEWAKE_MEDIA_GEN9`（mask 書込み `(bit<<16)|bit`）と ACK register の poll（reference: intel_uncore.c `fw_domain_get`/`fw_domain_wait_ack_set`）。timeout 50 ms → ETIMEDOUT |
| `i915_uncore_wait(device, offset, mask, value, timeout_ms)` | 共通 register poll（`sched_ticks` ベース） |
| `i915_uncore_init(device)` | forcewake domain の初期 put（`FORCEWAKE_*` に 0xffff0000）、`GEN6_MBCTL` 等は触らない。`GDRST` 読出しで 0 確認 |

### ggtt.c

| 関数 | 役割・参照 |
| --- | --- |
| `i915_ggtt_probe(device)` | PCI config `GGC (0x50)` から GGTT size（reference: intel_ggtt.c `gen8_get_total_gtt_size`）、PTE 数 = size/8、BAR0 上位 8 MiB を PTE window として map |
| `i915_ggtt_scratch_init(device)` | scratch page（4 KiB、zero）を割当て全 PTE を scratch で埋める（`gen8_ggtt_pte_encode`: addr \| PRESENT(1) \| RW(2)、ADL は LM bit なし） |
| `i915_ggtt_alloc(device, pages, *offset)` / `_free` | bitmap allocator |
| `i915_ggtt_insert(device, offset, phys, pages)` / `_clear` | PTE 書込み後 `GFX_FLSH_CNTL_GEN6 (0x101008)` に `GFX_FLSH_CNTL_EN` を書く（reference: `gen8_ggtt_invalidate`） |
| `i915_ggtt_aperture_read/write(device, offset, buffer, bytes)` | BAR2 window 経由の diagnostic（v1 は CPU 直接 map を主に使うため任意） |

### ppgtt.c

| 関数 | 役割・参照 |
| --- | --- |
| `i915_ppgtt_create(device, *vm)` | PML4 1 page + scratch PT/PD/PDP（reference: gen8_ppgtt.c `gen8_ppgtt_create`/`gen8_init_scratch`）。PTE encode: addr \| PRESENT \| RW \| PAT index 0（gen12 `GEN12_PPGTT_PTE_PAT0..2` は 0） |
| `i915_ppgtt_insert(vm, va, phys, pages)` / `_clear` | 4-level walk、必要 level を割当（4 KiB page、物理連続、`kern_pmem_alloc_limited` 39-bit）。PDE/PDPE/PML4E encode: addr \| PRESENT \| RW |
| `i915_ppgtt_destroy(vm)` | 全 level 解放 |
| `i915_ppgtt_va_alloc(vm, bytes, *va)` | 単純 bump allocator（開始 `0x0000_0001_0000_0000`、2 MiB 揃え） |

TLB: context 切替時に無効化される。同一 context 内で PTE を変更した場合は次の submit の前に `PIPE_CONTROL`/`MI_FLUSH_DW` の TLB invalidate を batch 先頭に入れる（p004）。

### gem.c

| 関数 | 役割 |
| --- | --- |
| `i915_gem_create(device, bytes, *object)` | 4 KiB 切上げ、`kern_pmem_alloc_limited(bytes, 4096, (1<<39)-1, 0)`、CPU address は `kern_pmem` → kernel address 変換（`include/kern/pmem.h` の translate）。zero fill |
| `i915_gem_destroy(device, object)` | GGTT/PPGTT unbind 後に解放。quarantined なら controller list に保持 |
| `i915_gem_bind_ggtt(device, object)` / `_unbind_ggtt` | HWSP/ring/LRC 用 |
| `i915_gem_bind_vm(vm, object)` / `_unbind_vm` | session context の PPGTT へ |
| `i915_gem_read(object, offset, buffer, bytes)` / `_write` | drv_gpu resource_read/write 用 CPU copy。GPU 実行中は core が排他する（同一 session 1 ioctl）|

### engine.c

| 関数 | 役割・参照 |
| --- | --- |
| `i915_engine_init(device, engine, class, mmio_base)` | HWSP object（4 KiB、GGTT bind、`RING_HWS_PGA(base)`）、ring object（64 KiB、GGTT bind）。`RING_MODE_GEN7`: `GFX_RUN_LIST_ENABLE`、`RING_MI_MODE`: stop ring 解除。reference: intel_engine_cs.c `intel_engine_init_common`、intel_execlists_submission.c `execlists_reset_prepare`/`enable_execlists` |
| `i915_engine_reset(device, engine)` | `GEN11 engine reset`: `RING_RESET_CTL(base)` に `RESET_CTL_REQUEST_RESET` → `RESET_CTL_READY_TO_RESET` を待ち → `GDRST (0x941c)` に engine domain bit（`GEN11_GRDOM_RENDER`/`GEN11_GRDOM_BLT`）→ 0 になるまで wait → `RESET_CTL` の request を落とす。reference: intel_reset.c `gen11_reset_engines`、`gen8_engine_reset_prepare` |
| `i915_gt_reset(device)` | `GDRST` に `GEN11_GRDOM_FULL`、全 engine 再 init（recovery->reset 用） |
| `i915_engine_idle(engine)` | `RING_EXECLIST_STATUS` / CSB と seqno から idle 判定 |

engine 2 本（RCS0、BCS0）。VCS/VECS は init しない（forcewake media も取らない）。

### lrc.c

| 関数 | 役割・参照 |
| --- | --- |
| `i915_lrc_create(device, engine, context, *object)` | LRC object = PPHWSP 1 page + register state（gen12: 約 20 page、`LRC_STATE_OFFSET = 1 page`）。`linux/i915-lrc-offsets.inc` の `gen12_xcs_offsets`/`gen12_rcs_offsets` 列を解釈して MI_LOAD_REGISTER_IMM 形式の state を書く（reference: intel_lrc.c `set_offsets`、`init_common_regs`、`init_ppgtt_regs`）。RING_START/CTL/HEAD/TAIL、`CTX_CONTEXT_CONTROL`（inhibit sync/RS 不使用）、PDP0 に PML4 物理（48-bit）、`CTX_BB_PER_CTX_PTR` 0 |
| `i915_lrc_descriptor(context, engine)` | gen12: `LRCA(GGTT offset) \| GEN8_CTX_VALID \| GEN8_CTX_ADDRESSING_MODE(48b) \| (ctx_id << GEN11_SW_CTX_ID_SHIFT)`（reference: intel_lrc.c `lrc_descriptor`、intel_lrc_reg.h） |
| `i915_lrc_submit(engine, descriptor)` | `RING_EXECLIST_SQ_CONTENTS(base)` へ 2 entry（2 個目は 0）を書き、`RING_EXECLIST_CONTROL(base)` に `EL_CTRL_LOAD`。reference: `execlists_submit_ports`（ELSQ 経路） |
| `i915_lrc_csb_consume(engine)` | HWSP 内 CSB（gen12: `GEN12_CSB_*`、write pointer は HWSP の `CSB_WRITE` dword）を読み、`GEN12_CTX_STATUS_SWITCHED_TO_UNPINNED`/complete を識別。reference: `process_csb`、`gen12_csb_parse` |
| `i915_lrc_ring_emit(engine, dwords, count)` | ring への dword 書込みと wrap、`RING_TAIL` は LRC の state 経由（execlists では context image の tail を更新して再投入） |

v1 は engine ごとに同時 1 context だけ投入（ELSQ 1 entry）。preemption なし。context 切替は完了後に次を投入する単純 scheduler（`request.c`）。

### irq.c

| 関数 | 役割・参照 |
| --- | --- |
| `i915_irq_init(device)` | MSI 1 本を `drv_pci_device_allocate_irqs(DRV_PCI_IRQ_ALLOW_MSI)` + `establish_irq`。`GEN11_GFX_MSTR_IRQ (0x190010)` master enable、`GEN11_RENDER_COPY_INTR_ENABLE (0x190030)` に RCS0/BCS0 の `GT_RENDER_USER_INTERRUPT (1<<0)` と `GT_CONTEXT_SWITCH_INTERRUPT (1<<8)`、`GEN11_RCS0_RSVD_INTR_MASK (0x190090)`/`GEN11_BCS_RSVD_INTR_MASK (0x1900a0)` で unmask。reference: gt/intel_gt_irq.c `gen11_gt_irq_postinstall` |
| `i915_irq_handler(argument)` | `GEN11_GFX_MSTR_IRQ` 読出し（master disable→…→enable の順、reference: `gen11_gt_irq_handler`）、`GEN11_GT_INTR_DW0 (0x190018)` の bank/bit、`GEN11_IIR_REG0_SELECTOR (0x190070)` に bit を書いて `GEN11_INTR_IDENTITY_REG0 (0x190060)` の valid を待ち、engine/instance/intr を取り出す。RCS0/BCS0 の user interrupt と context switch を `request.c` へ通知。IIR ack は identity register に書き戻す |
| `i915_irq_reset(device)` | 全 enable を 0、identity を clear |

### request.c

| 関数 | 役割 |
| --- | --- |
| `i915_request_alloc(engine, session, completion, *request)` | ring slot（32）から 1 つ。満杯なら EAGAIN（drv_gpu の容量契約） |
| `i915_request_emit(engine, request, batch_va, batch_dwords)` | ring に `MI_BATCH_BUFFER_START`（48-bit、PPGTT）、`MI_FLUSH_DW`（post-sync: HWSP の seqno slot に seqno 書込み、gen12 では `MI_FLUSH_DW_OP_STOREDW`＋GGTT address）、`MI_USER_INTERRUPT`、`MI_NOOP` padding。RCS は `PIPE_CONTROL`（post-sync write）を使う |
| `i915_request_submit(engine, request)` | LRC の ring tail を更新し `i915_lrc_submit` |
| `i915_request_retire(engine)` | HWSP の seqno を読み、完了 request に `drv_gpu_complete(completion, 0)`、slot 解放、`drv_gpu_capacity_changed` |
| `i915_request_fail(engine, error)` | reset/隔離後に未完了 request を error で公開 |

### i915.c（PCI + drv_gpu）

| 関数 | 役割 |
| --- | --- |
| `drv_i915_pci_driver_register(void)` | `drv_pci_driver` を登録（ids: `linux/i915-ids.inc` の ADL-P 抜粋、少なくとも `8086:46a8`） |
| `i915_attach(device, id)` | `kern_calloc`、mutex、`drv_pci_device_set_driver_data`、`i915_start` |
| `i915_start(device)` | 順に: enable state save、memory enable、bus master、BAR0 map（region 0..8 MiB NOCACHE）、GTT window map、BAR2 先頭 16 MiB map（NOCACHE）、DMA device（`drv_pci_device_dma`）、`i915_uncore_init`、`i915_gt_reset`（初期状態を揃える）、`i915_ggtt_probe/scratch`、`i915_engine_init`×2、`i915_irq_init`、selftest（有効時）、`drv_pci_device_set_service(publish/unpublish)`。各段階で `device->stage` を更新し失敗時に `kern_logf("i915: attach stopped at %s: %d")` |
| `i915_publish/unpublish` | `drv_gpu_register(&i915_gpu_ops, device, &device->gpu)` / unregister |
| `i915_detach` | gpu NULL 確認、irq reset/free、engine 停止（`GDRST` full）、object 解放、mapping unmap、enable state restore |
| `i915_open(device, **session)` | `i915_context` 作成: PPGTT、RCS0/BCS0 の LRC、descriptor。`i915_close` は resource_destroy 後に LRC/PPGTT 解放 |
| `i915_get_info` | driver_name "i915"、capabilities = `GPU_CAP_RESOURCE \| GPU_CAP_TRANSFER \| GPU_CAP_COMMAND \| GPU_CAP_NOTIFICATION \| GPU_CAP_JOB \| GPU_CAP_JOB_CAPACITY`、max_resource_bytes 16 MiB |
| `i915_resource_create/destroy` | `i915_gem_create` + `i915_gem_bind_vm`。作成時に `kern_logf("i915: resource session=%u slot=%u bytes=%llu phys=0x%llx va=0x%llx")`（harness の独立検証に使う） |
| `i915_resource_read/write` | `i915_gem_read/write` |
| `i915_command(device, session, buffer, bytes)` | 同期 command: §7 の stream を検証し、reloc を patch した batch object を作り request を投入して **完了を待たない**（受理のみ）。v1 では `GPU_COMMAND` は使わず `GPU_COMMAND_SUBMIT` のみを推奨 |
| `i915_command_submit(..., completion)` | 非同期: 上と同じ + `completion` を request に結び付け、完了で `drv_gpu_complete` |
| `i915_command_drain(device, session)` | session の全 request の retire を待つ（`waitq` + irq） |
| `i915_job_reserve/commit/cancel/capacity` | request slot を reserve（callback 保持）、commit で `MI_BATCH_BUFFER_START` なしの marker request（seqno のみ）を投入、cancel(0) は slot 解放、cancel(fault) は保持。capacity = 空き slot 数 |
| `i915_stop_begin/stop_poll` | begin: context を `stopping`、新規投入拒否（core も拒否する）。poll: その context の全 request が retire（seqno 到達）していれば 0、未完なら EAGAIN。**engine が hang していれば永遠に EAGAIN → core の停止期限で escalation** |
| `i915_isolate(device, session)` | context の未完 request を `i915_request_fail(EIO)`、context を `quarantined`（LRC/PPGTT/object を reset まで保持）。engine が hang している場合はその **engine を `i915_engine_reset`** して他 context を継続させる（execlists は engine 単位で復帰できる） |
| `i915_fault(device, error)` | 全 engine 停止、全 request fail、`failed=1` |
| `i915_reset_device(device)` | `i915_gt_reset`、engine 再 init、quarantined object 解放、`failed=0` |

## 5. 初期化シーケンス（attach）

1. PCI: enable state save → memory space/bus master enable。
2. BAR0 region 0..8 MiB を NOCACHE で map（`regs`）。BAR0 region 8..16 MiB を map（`gtt`）。BAR2 先頭 16 MiB を map（`aperture`、任意）。
3. `i915_uncore_init`: forcewake を全 domain で "put"（0xffff0000）。`GDRST` が 0 か確認。
4. `i915_gt_reset`: `GDRST |= GEN11_GRDOM_FULL` → 0 まで wait（500 µs 単位、最大 1 s）。
5. `i915_ggtt_probe` → `i915_ggtt_scratch_init`（全 PTE を scratch）→ `GFX_FLSH_CNTL`。
6. forcewake GT+RENDER を get し、RCS0/BCS0 の engine init（HWSP、ring、MODE/MI_MODE）、forcewake put。
7. `i915_irq_init`（MSI）。
8. selftest（有効時）: BCS0 で `MI_STORE_DWORD_IMM`（GGTT の HWSP+0x80 へ 0xdeadbeef）を含む request を投入し 100 ms 以内に user interrupt と値を確認。失敗は attach 失敗（driver を publish しない）。
9. service 登録 → PCI が publish → `/dev/gpu0`。

## 6. 実行シーケンス（submit）

1. U: `GPU_RESOURCE_CREATE`（src/dst）、`GPU_TRANSFER` write で src に pattern。
2. U: `GPU_COMMAND_SUBMIT`（stream = header + reloc + batch dwords、§7）。
3. K: stream 検証 → batch object（session VM に bind）へ dwords copy、reloc entry の VA patch → `i915_request_alloc` → ring に `MI_BATCH_BUFFER_START` + post-sync + `MI_USER_INTERRUPT` → LRC tail 更新 → ELSQ 投入。
4. IRQ: user interrupt → `i915_request_retire` → HWSP seqno 確認 → `drv_gpu_complete`。
5. U: `GPU_COMMAND_WAIT` → `GPU_TRANSFER` read → 検証。
6. host: guest.log の `i915: resource ... phys=` を読み、QMP `pmemsave` で dst の物理範囲を dump し、期待 pattern と独立に照合。

## 7. native command stream（backend 定義、root 専用）

```
struct i915_stream_header {      /* 32 byte、little endian */
	uint32_t magic;              /* 0x31394958 ('XI91') */
	uint32_t version;            /* 1 */
	uint32_t engine;             /* 0 = RCS0, 1 = BCS0 */
	uint32_t relocation_count;   /* 0..64 */
	uint32_t batch_dwords;       /* 1..16384、末尾は MI_BATCH_BUFFER_END */
	uint32_t flags;              /* 0 */
	uint64_t reserved;
};
struct i915_stream_relocation {  /* 16 byte × relocation_count */
	uint32_t dword_offset;       /* batch 内の書換え位置（64-bit VA を 2 dword に書く） */
	uint32_t reserved;
	uint64_t handle;             /* 同 session の resource handle */
	/* VA = resource VA + delta は v1 では delta=0 固定 */
};
/* 続いて batch dwords */
```

K は handle を session の resource に解決し（他 session/無効は EINVAL）、VA を patch した **copy** を batch object へ書く。U の buffer は変更しない。

試験で使う batch（BCS0）:
- copy: `XY_SRC_COPY_BLT_CMD`（gen12: 48-bit address、dword length は `intel_gpu_commands.h` の定義から）、`MI_FLUSH_DW`、`MI_BATCH_BUFFER_END`。
- fill: `XY_COLOR_BLT_CMD`。
- store: `MI_STORE_DWORD_IMM_GEN4`（`MI_MEM_VIRTUAL` 付き、PPGTT VA）。

RCS0 は p007 で `MI_STORE_DWORD_IMM` と `PIPE_CONTROL` だけ確認する（3D pipeline の state 設定は WS029 後続）。

## 8. 試験設計

### host fixture（`plan/ws029/tests/`、実 production source を include、通常＋ASan/UBSan）

| runner | 対象 |
| --- | --- |
| `run-i915-uncore-test.sh` | 偽 MMIO 配列に対する forcewake get/put の書込み列と ACK wait、timeout |
| `run-i915-gtt-test.sh` | GGTT/PPGTT の PTE encode（Linux 定義との値比較）、4-level walk、scratch、bitmap allocator |
| `run-i915-lrc-test.sh` | context image の offset 列展開（`gen12_xcs_offsets` の解釈結果と期待 register 列）、descriptor 値 |
| `run-i915-irq-test.sh` | identity/IIR 手順の register 書込み列、user interrupt→retire |
| `run-i915-stream-test.sh` | §7 の検証（bounds、reloc、他 session handle 拒否） |
| `run-i915-backend-test.sh` | drv_gpu ops を偽 hardware で: open/close、resource、submit→irq→complete、job reserve/commit/cancel/capacity、stop_begin/poll、isolate（engine reset 呼出し）、fault/reset |
| `run-i915-build-selection-test.py` | 6 platform × I915 y/n の make 入力、amd64 実 build（`config-i915-amd64.mk`） |

### guest 試験（`userland/base/tests/gpu-i915/main.c`、`/bin/gpu-i915-test`）

`GPUI915 START` → GET_INFO（driver_name=i915）→ copy 試験 → fill 試験 → store 試験 → job 経路（RESERVE/COMMIT + marker）→ `GPUI915 PASS copy=1 fill=1 store=1 job=1 dst_slot=<n>`。失敗は `GPUI915 FAIL stage=… errno=…`。各 resource の handle→slot を出力し、K の `i915: resource ... phys=` 行と突合できるようにする。

### remote harness（`plan/ws029/tests/run-i915-remote.py`、`i915-qemu.py`、`host-igd.sh`）

- `run-venus-remote.py` の build/transfer/QMP/pmemsave/evidence を再利用（module として import）。profile `i915`: config `plan/ws029/tests/config-i915-amd64.mk`（`CONFIG_DRIVER_PCI_I915=y`、`CONFIG_DRIVER_PCI_VENUS=n`、`ZEDBSD_USER_PROGRAMS` に `gpu-i915-test`）。
- host 手順 `host-igd.sh attach|restore|status`（`sudo -n`）:
  - attach: `systemctl stop gdm`（awe の graphical session も終了する）→ `/dev/dri/*` の利用者無し確認（`fuser`）→ `modprobe vfio-pci` → `echo 0000:00:02.0 > /sys/bus/pci/drivers/i915/unbind` → `echo vfio-pci > /sys/bus/pci/devices/0000:00:02.0/driver_override` → `echo 0000:00:02.0 > /sys/bus/pci/drivers_probe` → `/dev/vfio/0` と `driver` symlink を確認。
  - restore: QEMU 終了確認 → `unbind` → `driver_override` を空に → `drivers_probe` で i915 へ戻す → `/sys/class/drm/card0` の再出現を待つ → `systemctl start gdm` → status。
  - status: bound driver、GDM 状態、`/dev/vfio` を JSON で出力。harness は各 attempt の前後で status を保存し、**restore 失敗時は attempt を fail にして host 状態を報告する**（reboot はしない。reboot が必要なら別途ユーザー承認）。
- QEMU 引数（`sudo -n` で起動、memlock 制限回避）: `-machine pc,accel=kvm -cpu host -m 1024 -smp 2 -vga none -device vfio-pci,host=0000:00:02.0 -debugcon file:… -qmp unix:… -monitor none -serial none -nic none -no-reboot`。egl-headless/VNC/virtio-vga は使わない。生成物の owner を `awe` に戻す。
- 検証: guest の `GPUI915 PASS` と各 stage の値、guest.log の `i915: resource` 行、QMP `pmemsave` で dst 物理範囲を dump し pattern（copy: src pattern、fill: 定数）を harness が独立計算して照合。attach 時の `i915:` 段階ログ、割込み回数（`i915: irq user=… csb=…` の close 時 diagnostic）も記録。

## 9. 受入（「動く」の定義）

1. host fixture 全 PASS、amd64 実 build（I915 y/n）、GPU なし build で symbol 不在。
2. 実機 VFIO 起動で `/dev/gpu0` が i915 として公開され、selftest の user interrupt が届く。
3. `gpu-i915-test` の copy/fill/store/job が guest 内検証で PASS。
4. harness の `pmemsave` 照合が PASS（GPU が実際に guest RAM へ書いた証拠）。
5. hang 注入（`MI_SEMAPHORE_WAIT` で待ち続ける batch を BCS0 に投入）で core の期限→isolate→engine reset が走り、別 session の copy が継続する（p007 の stretch、成立しなければ制限として記録）。
6. host の i915 と GDM が restore で復帰する（各 attempt 後）。

## 10. 参照元と転記ルール

| 用途 | Linux ファイル（drivers/gpu/drm/i915/） | 転記先 |
| --- | --- | --- |
| MMIO offset/bit（GDRST、FORCEWAKE、GFX_FLSH_CNTL、GGC、GEN11 IRQ、RING_*、EXECLIST_*） | `i915_reg.h`、`gt/intel_gt_regs.h`、`gt/intel_engine_regs.h`、`intel_pci_config.h` | `linux/i915-regs.inc` |
| command opcode/flag（MI_*, XY_*_BLT, PIPE_CONTROL, MI_FLUSH_DW） | `gt/intel_gpu_commands.h` | `linux/i915-commands.inc` |
| context descriptor bit、LRC register offset 列、CSB 定義 | `gt/intel_lrc_reg.h`、`gt/intel_lrc.c`（`gen12_xcs_offsets`、`gen12_rcs_offsets`、`gen12_rcs_offsets` の `NOP/LRI/REG/END` macro） | `linux/i915-lrc-offsets.inc` |
| device ID | `i915_pci.c` / `include/drm/intel/pciids.h`（ライセンス確認必須） | `linux/i915-ids.inc` |
| MOCS | `gt/intel_mocs.c`（gen12 table） | `linux/i915-mocs.inc` |
| 手順の参照（転記しない） | `intel_uncore.c`、`gt/intel_ggtt.c`、`gt/gen8_ppgtt.c`、`gt/intel_execlists_submission.c`、`gt/intel_reset.c`、`gt/intel_gt_irq.c`、`gt/intel_engine_cs.c`、`gt/gen8_engine_cs.c` | purpose comment に関数名を引用 |

転記の手順: `plan/ws029/tests/fetch-linux-refs.sh` が固定 commit（p001 で決める tag、例 `v6.19`）の raw ファイルを `plan/ws029/temp/linux/` に取得し SHA256 を `i915-license-audit.md` に記録、SPDX/permission notice を機械確認（`MIT` 以外は停止）。`.inc` には出典 path と commit を書く。
