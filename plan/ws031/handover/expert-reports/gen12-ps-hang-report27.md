# Gen12 PS/compute ハング 第27報 — Work1(DMA/scratch)完了・実機実証、Work2(実 kernel 並行試験)で文脈制約を確認、指示を仰ぐ

ご指示の作業順(Work1 DMA/scratch → Work2 実 kernel 並行試験 → Work3 MSI 分離 → Work4 P2 実機)を進めています。
**Work1 完了**(実機実証)、**Work2 は core 機構を検証したが実行文脈の制約に到達**しました。ご指定 4 点の成果物と、
Work2 の文脈についての質問を提出します。

## 1. DMA 設定の実装差分(成果物1)
### 32 の出所と修正
- **32 の出所 = `pci-pcat.c` の共有バス DMA 制約ハードコード**(`address_bits=32, max_segment_size=16MB, coherent=1`)。
  型/HW 制限ではありません(drv_dma の演算は uint64_t 一貫、範囲検査も 64bit)。ご分類の「汎用初期 mask」に該当。
- **`drv_dma_map` は identity**(mapping->segment.address = allocation->paddr。現 env は vIOMMU 無しで dma≈guest-phys)。
  **ただし mask 超過ページには bounce buffer 経路**あり。i915 は `I915_DMA_MAX_ADDRESS=(1<<39)-1` で 39bit 確保するため、
  32bit のままだと 39bit ページ(>4GB)が bounce される(parity は bounce sync 未実装)。→ 39bit 要求が必要。
### 実装(要求は backend 現在値から逆算しない)
- parity P2.1 は Linux 準拠に **39bit 要求**: `requested streaming/coherent = 0x7fffffffff, max_seg = UINT_MAX`。
- 共有 32bit バスを使わず、**i915 自身の 39bit DMA device を `drv_dma_device_create` で宣言**(dma_set_mask 相当)。
  → 39bit ページが identity map され bounce されない。teardown で `drv_dma_device_destroy`。
- 実機ログ:
```
P2 dma_request: streaming_mask=0x7fffffffff coherent_mask=0x7fffffffff max_seg=0xffffffff
P2 dma_backend:  bus_bits=32  i915_bits=39  i915_max_seg=0xffffffff  coherent=1
```
  bus(32)と i915 device(39)を分離記録。要求は accept され 32 へ縮退・切詰めなし。

## 2. GGTT probe + scratch の実装差分・ログ(成果物2)
scratch は **ggtt_probe 内**(参照どおり)に実装。
- ggtt_probe: `SNB_GMCH_CTRL(0x50)` config 読み→GGMS→table size, BAR upper half に GTT window map, entries=table/8。
- **scratch = internal-object 経路**: `drv_dma_map` は既存 allocation 内しか map 不可と判明したため、zedBSD の
  internal-object/SG 相当 = **`drv_dma_vector`**(pages 確保+CPU addr+DMA segment 公開)を使用(**alloc_coherent 決め打ち
  せず**、ご指示どおり pages→SG→DMA mapping 経路)。CPU addr で内容 zero 初期化→segment の DMA アドレス取得。
- **GGTT/PPGTT encoder を別実装**(parity/pte.c): GGTT PTE=`dma|GEN8_PAGE_PRESENT`(**LM bit1 clear** for SMEM),
  PPGTT=`dma|PRESENT|RW`(**bit1=RW**)。**overflow-safe 範囲検査**(`length-1 <= mask-addr`)+ page align 検査。
  範囲外/misalign は encode 拒否(**AND 丸めなし、32bit 切詰めなし、64bit PTE**)。
- **table は fill しない**(clear_range/scratch_range は別 callback。1M entry を独自 fill しない)。
- 実機ログ:
```
P2 ggtt_probe: ggms=3 entries=1048576 (4096 MiB GPU VA window)
P2 scratch: cpu=0xffff80003fe5c000 dma=0x3fe5c000 len=0x1000 ggtt_pte=0x3fe5c001
```
  scratch DMA=0x3fe5c000(この run は 1GB RAM で low), PTE=dma|PRESENT(LM clear)。encode は full 64bit 保持
  (mock で >4GB 非切詰めを検証)。
- **所有権/解放**: teardown で `drv_dma_vector_free`(CPU/DMA mapping+backing pages を一括解放)。逆順:
  scratch→dma device→gtt window→regs window→BAR→PCI_COMMAND。table を実 fill していないため GPU 側参照は未発生。

## 3. 契約試験・実 kernel 試験の結果(成果物3、mock と実機を区別)
- **mock 契約試験(GPU 無し, host gcc)= 計 191 checks 全 pass**(DMA 36 / MMIO 43 / PCI 43 / sync 31 / rpm 20 /
  **pte 18**[GGTT≠PPGTT, >4GB 非切詰め, 範囲/整列拒否])。**mock-VERIFIED であり HW-VERIFIED でない**。
- **実 kernel 並行試験(Work2)= 構築したが実行文脈の制約に到達**:
  - `parity/ktest.c` に**実 waitq+spinlock completion**(counting 契約準拠)+ kthread worker を実装
    (K1 complete-before-wait / K2 counting / K3 blocking-wait-woken-by-thread / K4 timeout / K5 complete_all)。
  - **K1(sleep 無)= PASS**(実 waitq completion の counting+wake 機構は実機で動作)。
  - **K4(waitq_sleep+deadline timeout)= hang**。原因確定: **i915 attach は boot device-probe 文脈**で、
    貴 driver の NVMe 実装が明記する通り「idle/早期 boot 文脈では timer 駆動の waitq wakeup が stall しうる」
    (NVMe はこの文脈で sleep せず poll、regular threads 成立後に waitq を通常使用)。attach 中の blocking-wait は不適。
  - 対処: parity attach からの inline ktest 呼出を**無効化**(P0-P2 経路 intact)。deadline 単位は sched_ticks で
    正当(kern_deadline_after=now+delta, scheduler_ticks 比較)と確認済。ktest.c は image に含むが未呼出。

## 4. P2 の実行結果(成果物4)
```
parity attach begin (stop_after=P2)                          [legacy 不実行, parity 単一選択]
P0 pci_enable_device ok (command=0x0007)                     [PM cap@0xd0→D0, resource-aware IO|MEM]
P0 rpm_init_early / driver_create / early_probe / vgpu:physical / gt_probe_all
P1 uncore BAR mapped (8388608 bytes)
P1 device_info: slice_fuse=0x1(1) dss_fuse=0x1f(5 DSS)        [forcewaked MMIO 読み]
P1 sanitize_gpu gt_reset done (polls=39)                      [GEN6_GRDOM_FULL 単発 write+poll]
P2 dma_request/dma_backend (上記)
P2 ggtt_probe: ggms=3 entries=1048576 (4096 MiB)
P2 scratch: cpu=.. dma=0x3fe5c000 len=0x1000 ggtt_pte=0x3fe5c001
BLOCKED where=i915_ggtt_init_hw                              [scratch まで完了、init_hw 以降未実装]
teardown → device 非公開
```
診断: reached=P2 outcome=BLOCKED。sanitize 実行列は現状**単発 GDRST**(下記 Q3)。MSI 資源構成は未到達
(ggtt_init_hw で BLOCKED、MSI は P2 後段)。STOPPED/BLOCKED/FAILED を固定、driver 非公開、teardown は取得資源のみ逆順。

## 5. 伺いたいこと
### Q1(最重要): Work2 実 kernel 並行試験の実行文脈
attach 文脈は blocking-wait に不適と判明しました。GPU-free で「regular threads/timer が steady state」の文脈で
ktest を走らせる方法として、どれを採るべきでしょうか。
1. **kern init の boot 完了後 hook**(boot_start 末尾等)に parity_sync_ktest を追加(config gate)。core kernel を触る。
2. **独立 detached kthread** を attach で spawn し、system 起動完了を待って実行(core kernel 非改変だが待機条件が曖昧)。
3. **userspace test binary** から driver の device node/ioctl 経由で起動(device node/ioctl 追加が要る)。
4. 別の推奨機構。
zedBSD の慣行として最も適切な入口をご指示ください(NVMe の poll 回避策との整合も含め)。
### Q2: DMA device の per-device 化
共有バス(32bit)を変えず **i915 が自前の 39bit drv_dma_device を宣言**(dma_set_mask 相当)、で合っていますか。
それとも pci-pcat の platform 能力値自体(guest phys 幅)を正すべきでしょうか(全 PCI device 影響)。
### Q3: sanitize_gpu の reset 手順
現状は **GEN6_GRDOM_FULL の単発 write+poll**(polls≈40 完了)です。ご指示の `__intel_gt_reset(ALL_ENGINES)` 関数列
(Gen11+ は **GEN11_GRDOM_FULL**、gen6_hw_domain_reset は **2 回 write/poll + 50µs 待機 + 外側 retry**、forcewake 取得/解放)を
今 P1 で完全移植すべきでしょうか。それとも P2 到達を優先し reset 手順の完全移植は後続でよいでしょうか。
### Q4: 次の進め方
Work2 の文脈解決(Q1)を先に片付けてから Work3(MSI 分離)→Work4(P2 続行: ggtt_init_hw→memory region→GGTT enable→
bus master→MSI)へ、というご指示の順で合っていますか。ggtt_init_hw(address-space 管理/mappable aperture/fence)は
DMA scratch とは別の実装単位と理解しています。

（成果物: `src/drivers/gpu/i915/parity/`{osdep, backend_{pci,mmio,dma}.c, pte.{h,c}, probe.c, ktest.{h,c}, parity.h,
 backend.h}, `plan/ws031/linux-parity/`{ledger.md, M1-test-results.txt}。git 操作なし。開示済み: Makefile/vmunix.mk/i915.c。
 GPU vfio-pci 維持。build 0 error/warning。累積修正[PAT/AUX/WA/golden]は legacy 側に保持。）
