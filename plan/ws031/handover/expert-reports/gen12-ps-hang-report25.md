# Gen12 PS/compute ハング 第25報 — M2 中間: 適合層を実 backend へ接続、parity attach が P0→P1→P2 を実機走破

ご指示の M2(①接続契約補強 → ②runtime PM 基盤 → ③kernel backend → ④単一 parity attach で P0→P2)を実装し、
**実機(8086:46a8, 同一 VFIO)で Linux-parity attach が P0→P1→P2 を走破**しました。ご要望の 3 点(適合層実装/
試験結果/P0-P2 実行記録)を提出します。C2 改善は本段では求められていない通り、扱いません。

## 1. 適合層の実装(差分)
`src/drivers/gpu/i915/parity/`(portable osdep + 実 backend + probe.c、計 ~1700 LOC)。kernel build に統合済
(vmunix.mk)。全て kernel 厳格フラグ(-ffreestanding -mcmodel=kernel -Wall -Wextra -Werror)で 0 error/warning。
- **osdep/**(portable, backend-vtable): dma / mmio / pci / sync / runtime_pm / trace。§1 接続契約を反映
  (dma_map_sg=count/0, map_sgtable=0/-errno, pin 中 unmap=-EBUSY / forcewake refcount+ACK + auto/held/raw 3 層 +
  Intel masked write / PCI resource-aware enable+refcount+MSI setup / completion count + PENDING-RUNNING + cancel_sync /
  rpm init_early≠enable + get_sync≠resume_and_get 失敗時 usage)。
- **backend_*.c**(zedBSD 境界のみ、legacy を呼ばない):
  - backend_pci.c: drv_pci_device_config_* wrap。
  - backend_mmio.c: kern_mmio_* + Gen9+ forcewake(render 0xa278/0xd84, GT 0xa188/0x130044, KERNEL bit0 masked)。
  - backend_dma.c: **set_info は drv_dma_device_address_bits で device 実 DMA 幅を検証**(host-phys-bits を代入しない)。
    map ops は GGTT 未実装のため NULL(呼ばれれば UNIMPL 記録、DMA アドレス捏造なし)。
- **probe.c**: P0..P2 診断 walk。stop_after と「未実装 dep で BLOCKED」frontier、trace dump、逆順 teardown。
- **選択**: Makefile に CONFIG_DRIVER_PCI_I915_PARITY 追加、i915_start 冒頭で **GPU 操作前に一回だけ選択**、parity ON 時は
  legacy 不実行→attach→ENODEV(非公開)。parity→legacy fallback なし。parity OFF 既定は legacy 不変(OFF ビルドも 0 warning)。

## 2. 試験結果(mock / 実機を区別)
- **mock 契約試験(GPU 無し, host gcc)**: DMA 36 + MMIO 43 + PCI 43 + sync 31 + rpm 20 = **173 checks 全 pass**。
  §1 の親 API 挙動(resource-aware enable, enable 参照数, MSI setup+途中失敗解放, completion count, self-requeue,
  cancel_sync, get_sync≠resume_and_get)を含む。これは **mock-VERIFIED であり HW-VERIFIED ではありません**。
- **kernel in-build コンパイル**: 6 osdep + 4 backend + probe を vmunix へ link 成功。
- **実機 backend 試験**: 下記 P0-P2 walk が実 backend の HW 動作を実証(config/MMIO/forcewake/reset/DMA-info)。
  ※ 実 zedBSD lock/waitq/thread を使う **in-kernel 並行試験(IRQ complete, cross-CPU queue, 実行中 sync-cancel)は未実施**
  (作業1 残)。completion/workqueue の並行契約は現状 host 単スレッド mock まで。

## 3. P0-P2 実行記録(実機, 8086:46a8 VFIO)
```
parity attach begin (stop_after=P2)
P0 pci_enable_device ok (command=0x0007)         [PM cap@0xd0→D0, resource-aware IO|MEM decode]
P0 rpm_init_early / driver_create / early_probe / vgpu_detect:physical / gt_probe_all:single_gt
P1 uncore BAR mapped (8388608 bytes)             [claim+map register half of BAR0]
P1 device_info: slice_fuse=0x1(1) dss_fuse=0x1f(5 DSS)   [forcewake GT get→read→put, ACK 実機成立]
P1 intel_gt_init_mmio:mcr_multicast              [GEN8_MCR_SELECTOR=MULTICAST]
P1 sanitize_gpu gt_reset done (polls=39)         [GEN6_GDRST=FULL, HW が clear 確認=実 GPU reset]
P2 set_dma_info: dma_bits=32 max_seg=0x1000000 coherent=1   [drv_dma_device の実 DMA 幅]
BLOCKED at i915_ggtt_probe_hw                     [P2 最初の未実装 dep]
teardown: BAR unmap → PCI restore(command 0x7)   [逆順、安全]
end: reached=P2 outcome=BLOCKED where=i915_ggtt_probe_hw err=0  [device 非公開]
```
- 選択は legacy を通らず parity のみ実行(実機実証)。診断結果は STOPPED/BLOCKED/FAILED に固定、いずれも
  driver-ready/公開へ進めず。teardown は取得資源のみ逆順解放(PCI_COMMAND 復元だけを合格条件にせず、BAR unmap も実施)。

## 4. 気づき・確認したい点
1. **DMA 幅 = 32bit**: `drv_dma_device_address_bits(device->dma)` が **32** を返しました(host phys-bits=39, GPU VA=48bit,
   IOMMU MGAW=39 とは別概念の「zedBSD DMA 層が report する device DMA 幅」)。動作 Linux では i915 は 39-bit DMA mask を
   設定します。**この 32bit が GGTT/PPGTT の DMA アドレス(実 env では guest-phys 直, ~ 数 GB)を表現できるか**が P2 GGTT
   実装時の焦点になります。現 env は guest vIOMMU 無し=dma≈guest-phys で、guest RAM は 4GB(32bit で表現可)なので当面
   問題ないと見ますが、drv_dma の 32bit 制約が softpin VA(0x1_0000_0000 超)と衝突しないか、GGTT 実装時に確認します。
   → この drv_dma の 32bit 報告は、動作 Linux(39bit)との差として記録すべきでしょうか。それとも zedBSD DMA 層の
   report 定義の差として扱い、実 mapping が guest-phys 直である限り無害と判断してよいでしょうか。
2. **sanitize_gpu の GT full reset**: P1 で実 GPU reset を実行しました(polls=39 で完了)。動作 Linux の sanitize と同じく
   attach 中に一度 reset します。この位置・範囲(GEN6_GRDOM_FULL)で問題ないか、engine 個別 reset との差をご確認ください。
3. **MSI の P2/P4 分離**: zedBSD は kern_irq_register_msi が vector+handler を結合しており、P2(vector/message)/P4(handler)の
   クリーンな分離に IRQ 基盤側の対応が要ります。当面 backend_pci の alloc_msi_vector=NULL(setup_msi は UNIMPL/-ENODEV)。
   P2 到達時に MSI を BLOCKED として扱い、handler 分離は後続で実装、という段取りでよいでしょうか。

## 5. 次段
P2 残(**ggtt_probe/init**[GTT サイズ probe・map・scratch/PTE, ここで DMA backend の map 経路と 3 アドレス分離を実接続]→
memory region → GGTT enable → bus master[osdep_pci_set_bus_master, 実装済]→ MSI[上記段取り]→ opregion)を実装し
BLOCKED frontier を前進。並行して Work1 の in-kernel 並行試験(実 zedBSD スレッドで completion/workqueue)を追加。
その後 P3-P5(display power/IRQ handler)→ M4(GEM/VM/context/request の依存一式)→ M5(client+復旧)。

（成果物: `src/drivers/gpu/i915/parity/{osdep,backend_*.c,probe.c,parity.h,backend.h}`, `plan/ws031/linux-parity/`
 {ledger.md, M1-test-results.txt}, `P0-P2-hardware-log.txt`。git 操作なし(10 files, +2417/-81 は user がコミット)。
 開示済み変更: Makefile/vmunix.mk/i915.c(gated 選択, legacy 既定不変)。/tmp に .bak。GPU vfio-pci 維持。）
