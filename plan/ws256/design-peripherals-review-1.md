<!-- design-reviewer の報告（2026-10-19、design-peripherals.md 版 1）。Q1 が報告の本文をそのまま置いた -->

# ws256-p012 design-peripherals.md の敵対的レビュー

読むだけで行いました。source・plan の変更、commit、build、QEMU、rm はしていません。`.internal/` も読んでいません。照合に使ったのは `/home/awe/zedBSD-claude1` の code と plan、`/home/awe/zedBSD-worktrees/p1/build/tmp/ws256-docs/` の資料です。

## Blocker

**B1. PLDA の INTx を既存の PCI core では配れない（p016b）**
- 根拠（PCI core 側）
  - `src/drivers/pci/pci.c:3037` の `establish_intx` は、`irq->vector` をそのまま `kern_irq_register()`（pci.c:3087）に渡します。これは `src/kern/irq.c:51` で `hal_irq_register` に直に転送されます。
  - riscv64 の HAL では番号は PLIC の source の番号そのものです（`src/hal/riscv64/irq.c:12,28`、`IRQ_MAX` 1024）。仮想の番号や、二段の割り込み（親の線を受けて子へ配る形）の仕組みはありません。
  - `drv_pci_bus_ops`（`include/drivers/pci/pci.h:125-177`）にも、host が割り込みの登録や配送に加わる hook がありません。
- 根拠（PLDA 側）
  - JH7110 の PLDA は INTA〜D・MSI・event を、1 本の PLIC の線（DTS `interrupts = <56>`／`<57>`、pcie の `interrupt-map` は自分の子の `pcie_intc0/1` を指す）にまとめています。
  - host が `ISTATUS_LOCAL` を読んで振り分け、W1C で ack を書く必要があります（`obsd-stfpcie.c` の `stfpcie_intr`: 「Ack INTx late as they are level-triggered」と書いて `HWRITE4(sc, ISTATUS_LOCAL, status)`、`IMASK_LOCAL` の pin ごとの unmask は `stfpcie_intx_intr_establish`）。
- 起こる事
  - host が線 56 を自分で登録すると、PCI core の登録と衝突します（EBUSY）。
  - vector=56 を device に渡すだけにすると、誰も ack を書かないので、最初の割り込みの後に割り込みが止まらなくなる見込みです（推測: status bit が latch する前提。OpenBSD・Linux とも ack を書くので latch と推定）。
  - §3.5 の「既存の pci-xhci… をそのまま」「HAL 無し（MSI は host の中の demux）」は成り立ちません。
- 修正案
  - `drv_pci_bus_ops` に、INTx の登録・解除を host に任せる hook を足す設計を、p016b（または前の Phase）に入れます。hal.h は変えずに済む見込みです。
  - pci.c は amd64 と Pi 4 が共有するので、両方の build と Pi 4 の xHCI の回帰を試験の計画に入れます。
  - LW を足します。

**B2. pci-nvme は MSI-X・MSI しか受けないので、p017 は virt でも実機でも attach しない**
- 根拠
  - `src/drivers/pci/pci-nvme.c:1738-1740` は `DRV_PCI_IRQ_ALLOW_MSIX | DRV_PCI_IRQ_ALLOW_MSI` で、INTx を許していません。file の中に INTx の道もありません（grep で 0 件）。
  - p016a は自分で「virt に AIA が無いので INTx だけ」と書いています。
  - p016b の MSI は、PCI core が `kern_irq_register_msi` → `hal_irq_register_msi` を通ります（pci.c:3209）。riscv64 ではこれは UNSUPPORTED です（`proposed/hal-riscv64.md:6`）。
  - `pci-brcmstb.c:1510` にも「the controller's MSI receiver needs HAL support that does not exist yet」とあり、§3.5 の「HAL 無し」と真っ向から食い違います。OpenBSD の stfpcie も MSI を `#ifdef notyet` にしています。
- 起こる事: §3.6 の「pci-nvme 変えない、新規: 無し、2〜4 LW」は誤りです。§7 の「INTx で動くか未確認」は、NVMe について「動かない」と確定できます。
- 修正案
  - p017 の新規に pci-nvme の INTx の道を入れます（INTMS/INTMC、共有の level の線）。amd64 の NVMe の回帰も計画します。
  - あるいは、PLDA の MSI を PCI core の host hook で出す設計にします（HAL の外）。
  - p016b を INTx だけにするなら、それを設計に明記します。

## Major

**M1. p011 の framebuffer には HAL の責務の追加が要り、「HAL: 無し」は誤り（要承認）。表示の cache の問題も抜けている**
- 根拠（HAL の責務）
  - kernel は `kern_boot_framebuffer(prefix)` で `hal_get_arch_handoff("<prefix>.phys/.size/.width/.height/.pitch/.format")` を読みます（`src/kern/pmem.c:225-300`。rpi4 は `"rpi4.framebuffer"`、pcat は `"pcat.framebuffer"`）。
  - riscv64 で承認済みの名前は `riscv64.fdt`・`riscv64.initrd`・`riscv64.boot-hart` だけです（guardrail.md:92）。
  - design.md:462 は「GOP は loader が入れてよいが HAL は使わない」、design.md:204 は「VF2 の BSP に framebuffer は無い」と書いています。
  - `src/kern/platform/hal-console.c` は HAL の console を使う root 無しの platform の binding で、汎用の framebuffer の console ではありません。文字の描画は arm64・amd64 とも HAL の BSP にあります（`src/hal/arm64/bsp-rpi4/cons.c・framebuffer.c`、`src/hal/amd64/framebuffer-map.c`）。
- 根拠（cache）
  - riscv64 は Svpbmt を使わないので、DDR の上の GOP の framebuffer は cacheable の PMA になります。
  - 前例があります: Pi 4 では描画の後に `hal_dcache_clean_range` が要り、実機で画面に出ない不具合として承認を経て直しました（guardrail.md:78）。
  - DC8200 が L2 を snoop するかは不明です。
- 修正案
  - 名前付きの framebuffer の object（例 `riscv64.framebuffer.*`）と、BSP の描画か kernel 側の console の選択を、承認の差分として plan に置きます。
  - 表示の coherency を、system port の別名（物理 + 0x4_0000_0000）を device として写すか、FLUSH64 で書き出すかの二案で設計し、p011 (1) の試験に「flush 無しで描いた画が出るか」を入れます。

**M2. coherency の実機の確かめが偽の合格を出しうる。時期も文書の中で食い違う**
- 時期の食い違い
  - §1.2 は「p013 の最初」、§3.2 は「p014 の IDMAC と組」、#25 は「p013 の実機」としています。
  - p013 には DMA を使う device がありません。
- 手順の抜け
  - 試験の前に CPU で buffer に模様を書いて、L2 に dirty の line を作る手順がありません。line が cache に無ければ、非 coherent でも一致します。
  - 両方向（device→CPU、CPU→device）の確かめと、bus master ごと（SDIO・GMAC・PCIe（xHCI・NVMe）・AXI DMA・表示）の確かめもありません。
  - SD の root の FS を壊さないよう、scratch の領域で行う事も要ります。
- 根拠の弱さ
  - 「DTS に `dma-noncoherent` が無い」が意味を持つのは、Linux の riscv が既定で DMA を coherent と扱う場合だけです（`ARCH_DMA_DEFAULT_COHERENT`、記憶による推測で未取得）。この根拠を出典に足す必要があります。
  - OpenBSD の `sys/arch/riscv64/dev/sfcc.c`（ISC）の jh7110 の扱いも、取得すれば裏付けになります。

**M3. 非 coherent の時の予備の案の記述が既存の DMA 層と合わない**
- 案 (ii) の「driver の ring を streaming の sync に」はできません。
  - `src/drivers/generic/dma.c:706-725` の `drv_dma_sync_for_cpu/device` は何もしません。
  - `drv_dma_map` は coherent の確保の中しか写さず、外は ENOTSUP です（dma.c:633-660）。
  - 全部の DMA が `drv_dma_alloc_coherent` を通るので、予備は (i) だけです。
- 案 (i) は hal.h を変えません（hal.h:655-680 の契約、`src/hal/riscv64/page.c:370`）。しかし design.md §2.5 の決定とぶつかります。
  - root の上半分は init の後に封じられ、448〜509 は invalid で、「uncached の窓は無い」と決めています。
  - 契約は、写す前に line を書き出して捨てる事（ccache の FLUSH64）を求めます。
- system port が uncached である根拠も直す必要があります。
  - 本文の「属性 N」は memory port（`C A N`）にも付いているので、uncached の意味ではありません。
  - 根拠は U74 の map（TRM Table 2-5）の `0x04_4000_0000 RW A`（C 無し）です。
- 修正案: (i) を「init で DDR の system port の別名の全体を 1 GiB の leaf で kernel 半分に写してから封じる」と具体化し、design.md §2.5 の改訂として Q1 に確認します。

**M4. NVMe の root（p017）を「root の選び方に 1 項目」とするのは過小**
- rpi4 の root の道は `KERN_DEV_SD` だけを解きます（`src/kern/platform/rpi4.c:309-323`、`include/kern/boot.h:131-136` に NVMe の class は無い）。
- NVMe の root は pcat の形になります。`boot0=UUID=` の読み取り、`probe_fixed_disks`、`wait_for_boot_disk`（`src/kern/platform/pcat.c:478-530,790-800`）と、`drv_pci_nvme_driver_register`・`drv_pci_nvme_probe_namespaces` の呼び出しです。
- 修正案: これを visionfive2.c の新規として書き、LW を足します。

**M5. U-Boot と DTB の既定の組み合わせが壊れやすく、ユーザーの決定 2 も変える**
- #20 の既定（配布の U-Boot 2021.10）は、design.md:288 の既定（Debian の mainline の 2025.01 を SD に置く）と食い違います。
- 配布の U-Boot は `RISCV_EFI_BOOT_PROTOCOL` を持たない可能性があります（design.md:288。U-Boot への追加は 2022 年）。その場合 boot hart は `/chosen/boot-hartid` に頼ります。
- ところが #21 の既定（loader が ESP の mainline の DTB に差し替える）では、U-Boot の fixup が全部消えます。boot-hartid、`/memory`、OpenSBI の reserved-memory の複写、`local-mac-address` などです。
- mainline の U-Boot は、PREBOOT の `setenv fdt_addr ${fdtcontroladdr}` で mainline の DTB を既に渡しています（`u-boot-mainline-vf2-defconfig:44`。OF_UPSTREAM で mainline の DTS から作る物）。
- #21 はユーザーの決定 2（「DTB は U-Boot から」）を変える提案ですが、そう書かれていません。
- 修正案
  - 差し替えは loader でせず、U-Boot の `load` と `bootefi <img> <fdt>` で渡します。U-Boot がその DTB にも fixup を当てる見込みです（推測、p005 で確かめる）。
  - mainline の U-Boot を主にし、配布の U-Boot は p011 (1) の試しに限ります。

**M6. Phase の依存と並行の表が矛盾している**
- 段 2 で P1 が p013 の code を書く案は、依存の p005 → p013 と矛盾します。p013 が繋ぐ `visionfive2.c` は p005 が作る物です。
- 段 1 の P1 は p004 の merge を待ちますが、p016a は `-kernel` の直の起動で足り、p004 は要りません（ECAM の host が自分で BAR を割り当てる設計）。p007 の後に始められます。
- P1・P2 が共有する file は visionfive2.c の表だけではありません。`platform/riscv64/vmunix.mk`・config・menuconfig・`vf2-crg.c` の表もあります。
- 修正案: p013 を「code（板の前）」と「実機の確かめ（p005 の後）」に分けるか、依存を p005 の skeleton の merge に限ります。

**M7. GPL の手順しか無い部品の出典の扱いが決まっていない**
- ISC の手順が無い部品があります。PWMDAC、DW AXI DMAC、YT8531 の拡張 register、DC8200 と HDMI（p011 (b)）、dw_mmc の JH7110 の sample phase です。
- §0.2 は (1) ISC、(2) GPL の値の順としていますが、これらでは手順の出典が無くなります。依頼の枠の「GPL は値だけ」で読むと、p011 (b) や p019 は値の引用だけでは作れません。
- Guardrail（guardrail.md:114、117）は「Procedure referenced:」を許しているので、解釈が割れています。
- 修正案: 部品ごとの手順の出典を表にし、GPL の手順の参照を許すかを Q1 経由でユーザーに確かめます。

**M8. sd-card の切り出しを既定にすると、Pi 4 の SD の起動を VF2 の Phase の危険に入れる**
- `rpi4-sdhci.c` は PIO の driver で、SD の protocol が SDHCI と絡み合っています。arm64 の direct map の `DIRECT_BASE 0xffff000000000000ULL` も直に持っています（rpi4-sdhci.c:24・195）。
- Pi 4 は SD が root なので、退行するとその板は起動しません。
- 修正案
  - 既定を「切り出さない」にし、dw-mmc の側に最小の SD の初期化を書きます。共有化は別の Phase にします。
  - DW MMC も最初は FIFO の PIO で始め（OpenBSD の dwmmc.c は PIO を持つ）、IDMAC を後にすれば、p014・p015 を M2 の coherency の判定から切り離せます。

## Minor

1. **AXI DMA の「22 番の channel」は誤りです**（§1.1、§3.8）。`dmas = <&dma 22>` の 22 は handshake（request line）の番号で、channel は `dma-channels = <4>` です（jh7110.dtsi:557-565、1063-1074）。
2. **PWMDAC の node には `interrupts` がありません**（jh7110.dtsi:557-565）。PIO なら timer の poll になるので、DMA は事実上必須です。p019 の 5〜8 LW は過小の可能性があります（JH7110 の DMAC に固有の差は推測）。
3. **`vf2-syscon.c` の範囲に AON SYSCON が抜けています。** gmac0 は `starfive,syscon = <&aon_syscon 0xc 0x12>` です（jh7110.dtsi:1028）。
4. **TX の clock の速度ごとの set_rate は、`starfive,tx-use-rgmii-clk` が無い時だけ要ります**（obsd-if_dwqe_fdt.c:284-286）。v1.3B では要りません。
5. **PCIe の STG SYSCON の offset（0x048・0x1f8）は DT にありません。** controller の cfg の address で選ぶ形です（obsd-stfpcie.c:271-281）。§3.5 の DT の欄に足します。
6. **CRG の rate は SYS SYSCON の PLL0〜2 に依ります。** get_rate と set_rate の範囲に PLL の読み取りがありません。
7. **§3.2 の試験の「各 CRG の id の register を読む」**: TRM の CRG に ID の register があるかは確かめていません。
8. **PREBOOT の `usb start` は mainline の U-Boot だけです。** 配布の U-Boot は `run chipa_set_uboot;` です。
9. **YT8531 の名前は VisionFive2_Datasheet にありません**（grep で 0 件）。JH7110 の datasheet の対応 PHY の表からの推定です。p018 の最初に MDIO で PHY の id を読んで決めると明記します。
10. **起動の switch の値が design.md の記述と食い違います。** design.md の §3.2 の起動の段（`MSEL[1:0]: 10 SD、00 flash`）に対し、TRM Table 2-2 は `RGPIO[1:0]` 0x1 が SDIO、0x2 が eMMC で、本文書の方と合います。§1.2 と同じく、design.md の直しとして明記します。
11. **DMA の address_bits を device ごとに書いていません。** dw-mmc の IDMAC の 32/64 bit は HCON の DMA64 bit で決まります（obsd-dwmmc.c:346）。32 bit なら届くのは DDR の 3 GiB だけです。「64 bit の descriptor」は事実ではなく、HCON で決まる事です。
12. **p016a・p016b の ID は規則の形（`wsNNN-pNNN`、Master の next）から外れます。**
13. **p015 は ws.md で「画面で login」です。** p011 が (b) になれば p015 は止まるので、UART（#15 (A)）での受け入れを可にするかを決めます。
14. **p016a に、`riscv-virt.c` の USB の入力の繋ぎが要ります。** 今は ns16550 だけです（riscv-virt.c:144-160）。「既存」ではなく新規です。
15. **p013 で、参照されない clock を止めない事を明記します。** 特に VOUT の clock と PD_VOUT は、止めると GOP の画が消えます。
16. **tools の変更の担当と許可が Phase にありません。** #15 (A) の serial.py の実機の tty と、p016a の QMP `send-key` は tools の変更ですが、#18 の許可は p009 の 3 file だけです。
17. **OpenSBI が電源断のために I2C5 の PMIC を触る可能性があります**（推測）。kernel が後で I2C5 を使うと競合します。#22 で PMIC を外す既定は、この点でも妥当です。

## 確かめて問題が無かった事

- **出典の取り扱い**
  - `SHA256SUMS` は全部一致しました（`sha256sum -c` で失敗 0）。
  - `starfive,jh7110-crg.h` は `reset-jh7110-crg.h` と同一です（cmp）。
- **memory map と割り込み**
  - SYS/STG/AON CRG、IOMUX、SDIO0/1、GMAC0/1、DMAC、usb0（peripheral）、I2C5 は DTS・TRM と一致しました。
  - PMIC は `0x36` の AXP15060 です。PMIC の regulator を参照するのは mmc0 と cpu だけで、PMIC を外す案の前提は合っています。
  - `gpio-restart` は sysgpio 35 です。
  - 割り込みの「TRM の n + 5」は、UART0 27→32、PLDA 51→56、SDIO1 70→75 で確かめました。PLIC の `riscv,ndev` 136 の中に入ります。
- **SD と sound の pin**
  - mmc1: IRQ 75、`fifo-depth` 32、`starfive,sysreg` 0x9c、cd-gpios 41（active low）、ciu 50 MHz。
  - PWMDAC: GPO の機能の番号 28・29 を GPIO 33・34 に出す形でした。
- **PCIe**
  - cfg 0x9_4000_0000・0x9_c000_0000（DTS の reg は 16 MiB）、apb 0x2b/0x2c000000、32 bit の窓 128 MiB、64 bit の prefetch の窓（各 1 GiB）、PERST 26・28、phys、`msi-controller` は DTS と一致しました。
- **coherency の推定の材料**
  - `sifive_ccache.c` の非 coherent の cache の操作は jh7100 だけに登録されています（123-124、318-322 行）。
  - `jh7110.dtsi` に `dma-noncoherent` はありません。
  - TRM Table 2-4 の DDR の二つの窓の記述も合っています。
- **既存の driver と HAL**
  - xHCI は INTx を許します（pci-xhci.c:6459-6464）。Pi 4 の brcmstb は INTx だけを出すので、xHCI の INTx の道は Pi 4 の実機で使われている形です（xHCI に関する §7 の未確認はここで解けます）。
  - riscv64 の HAL の MMIO の fence は Linux と同じ形です（`fence w,o`、`fence i,ir`、asm.c:128-195）。descriptor と doorbell の順序は保たれます。
  - `drv_fdt_find_compatible` は `after` で次の node を探せます。`drv_fdt_translate` もあります。
  - `uncached.c` は riscv64 でも build され、非 coherent の device の coherent な確保は ENOTSUP で失敗します（本文の記述どおり）。
  - lpss-i2c は DW の core です（`IC_COMP_TYPE` 0x44570140）。
  - `RPI4_VL805_IDENTITY` は rpi4-pcie.c:50 にあります。
- **U-Boot と OpenBSD**
  - 両方の defconfig の video・MMC_DW・DWC_ETH_QOS・USB_XHCI_PCI・EFIDEBUG の記述は合っています。
  - OpenBSD に jh7110 の dwmmc・dwqe の glue があります。
- **見積もり**: LW の合計 47〜72 の計算は合っています。

## 確かめていない事

- 実機での JH7110 の DMA の coherency、および表示（DC8200）の coherency。
- Linux の riscv の `ARCH_DMA_DEFAULT_COHERENT`（記憶による推測）と、OpenBSD の sfcc.c（未取得）。
- PLDA の `ISTATUS_LOCAL` の INTx の bit が latch か level か（ack が要る、と推定）。
- U-Boot の `bootefi` が渡された DTB に当てる fixup（boot-hartid、reserved-memory の複写）。記憶による推測です。
- 配布の U-Boot 2021.10 の `RISCV_EFI_BOOT_PROTOCOL` と GOP の有無、ExitBootServices の後に画が残るか。
- VL805 の型番と、USB と M.2 がどちらの PCIe に付くか。v1.2A の PHY。
- OpenSBI の jh7110 の電源断の実装、PCIe の inbound の窓の制約。

## 結論

p012 は、このままでは設計として受け入れられません。

- **改訂が要る**: PLDA の INTx の配送（B1）と、pci-nvme が MSI 必須である事（B2）で、p016b と p017 は書いた形では動きません。
- **承認の段が抜けている**: p011 の framebuffer には HAL の名前付き handoff の追加が要りますが、承認の段が計画にありません（M1）。
- **進め方**: これらと M2〜M8 を直した改訂で、もう一度レビューに通す必要があります。

資料: `/home/awe/zedBSD-claude1/plan/ws256/design-peripherals.md`、`/home/awe/zedBSD-worktrees/p1/build/tmp/ws256-docs/`
