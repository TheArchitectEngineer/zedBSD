# Gen12 PS/compute ハング 第26報 — P2 は GGTT probe まで実機走破。ggtt_init_hw の DMA 符号化で判断を仰ぎたい

第25報(P0→P2 走破)後、P2 をもう一段進め、**ggtt_probe_hw を実機実行**しました。次の ggtt_init_hw
(scratch+PTE)は「PTE に何のアドレスを書くか」= DMA アドレスの扱いに直結し、ここが以前ご報告した
drv_dma=32bit の論点そのものです。捏造を避けて BLOCKED で止め、判断を仰ぎます。

## 1. 進捗(実機, 8086:46a8 VFIO)
```
P2 set_dma_info: dma_bits=32 max_seg=0x1000000 coherent=1   [drv_dma_device の実 DMA 幅]
P2 ggtt_probe: ggms=3 entries=1048576 (4096 MiB GPU VA window)   [SNB_GMCH_CTRL 読み + GTT window map]
BLOCKED at i915_ggtt_init_hw
teardown: gtt unmap → regs unmap → release BAR → PCI restore
```
ggtt_probe_hw は config(GMCH GGMS)+ MMIO mapping のみで DMA アドレスを使わないため実装済。GGTT=4GB/1M entries は
Gen12 実値。frontier は **P2 の GGTT scratch/PTE 直前**。

## 2. 論点の整理(私の理解)
**「GPU VA」と「PTE が指す DMA アドレス」は別物**という理解を確認させてください。
- softpin VA(0x1_0000_0000 等, 4GB 超)は **GGTT/PPGTT の仮想アドレス**(48bit 空間)で、PTE が"指す先"ではなく
  PTE が"置かれる索引"側です。
- PTE の**内容**は backing page の物理/DMA アドレス。現 env は guest 仮想 IOMMU 無し=**dma ≈ guest-phys**、guest RAM は
  ≤4GB なので **PTE 内容は 32bit で表現可**。よって drv_dma=32bit は「PTE 内容の幅」としては現 env で不足しない、と考えます。
- 一方、動作 Linux は 39bit DMA mask を設定します。これは Linux が >4GB 物理や別 DMA 経路も想定するためで、
  現 env の実 mapping(guest-phys 直, ≤4GB)では 32/39 の差は PTE 内容に現れない、という理解です。
- 現 legacy zedBSD は `scratch.paddr | GEN8_PAGE_PRESENT`(kern_pmem の guest-phys 直)で PTE 符号化しており、
  これはまさにご指摘の「paddr 直渡し」パターンです。

**この理解で正しいでしょうか。** 誤りがあればご指摘ください(特に「32bit が実害になる経路」があるか)。

## 3. 判断を仰ぎたい点
### Q-A: ggtt_init_hw の scratch/object ページを DMA 経路に通すか
ご方針の 3 アドレス分離に従い、parity の GGTT/PPGTT 挿入は **backing page を drv_dma 経路(drv_dma_alloc_coherent
/ drv_dma_map)に通して device DMA アドレスを得て PTE に符号化**する、で合っていますか。
現 env では dma≈guest-phys で数値一致しますが、契約上は DMA 層を経由させ、legacy の paddr 直渡しを置換する、
という理解です(scratch page は drv_dma_alloc_coherent、object page は drv_dma_map)。
それとも、この段では scratch を drv_dma_alloc_coherent で確保し PTE=その DMA アドレス、まで実装して先へ進めてよいですか。

### Q-B: drv_dma=32bit の扱い
32bit を「この device の正当な DMA 幅」として受け入れ(backing ≤4GB で無害)、set_dma_info もその値で通す、で
よいでしょうか。それとも drv_dma 層が 32bit を report するのは zedBSD 側の制約/バグの可能性があり、Linux の 39bit に
合わせるべき論点として記録すべきでしょうか(現段階では走行に影響しないと見ていますが、differ として残すか判断を仰ぎます)。

### Q-C: MSI の P2/P4 分離
zedBSD は kern_irq_register_msi が vector+handler を結合しており、P2(vector/message setup)と P4(handler 設置)の
クリーンな分離に IRQ 基盤側の対応が要ります。当面 **P2 到達時に MSI を BLOCKED(未実装 dep)として扱い、
handler 結合型 MSI を P4 で実装**、という段取りでよいでしょうか。あるいは今 IRQ 基盤に vector-only alloc を足すべきですか。

### Q-D: sanitize_gpu の GT full reset(確認)
P1 で GEN6_GRDOM_FULL の GT full reset を実機実行(polls≈40 で HW clear)。動作 Linux の sanitize と同位置・同範囲と
理解していますが、engine 個別 reset との差や、attach 中 1 回 reset の妥当性に懸念があればご指摘ください。

### Q-E: 次の優先順
A/B の回答が要る **ggtt_init_hw(GGTT scratch/PTE→memory region→GGTT enable→bus master→MSI)** を進めるのと、
非 gate の **Work1 in-kernel 並行試験**(実 zedBSD スレッドで completion→waitq / workqueue→worker、IRQ-context complete /
cross-CPU queue / 実行中 sync-cancel の検証。現状 host 単スレッド mock まで)を先に片付けるの、どちらを優先すべきですか。
in-kernel 並行試験は sync 層に backend 抽象(completion↔waitq, workqueue↔thread)を足す設計作業を伴います。

## 4. 現状サマリ(再掲)
- 適合層 osdep 6 層 + 実 backend 4 種 + probe.c、kernel build 統合、0 error/warning。mock 契約 173 checks 全 pass
  (mock-VERIFIED)。実機 P0→P1→P2(ggtt_probe まで)走破、honest BLOCKED、安全 teardown、device 非公開。
- 単一 parity/legacy 選択(GPU 操作前・fallback なし)実機実証。累積修正(PAT/AUX/WA/golden)は legacy 側に保持、
  parity 経路は Linux 準拠で再構築中。

（成果物: `src/drivers/gpu/i915/parity/`, `plan/ws031/linux-parity/`。git 操作なし。開示済み: Makefile/vmunix.mk/i915.c。
 GPU vfio-pci 維持。ご回答いただければ Q-A/B に沿って ggtt_init_hw から P2 を続行します。）
