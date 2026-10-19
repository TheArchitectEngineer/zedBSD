<!-- awesome-plan project=zedbsd record=ws256 -->

# WS256 StarFive VisionFive 2（JH7110、RISC-V）への移植

- ID: ws256、Master: [master.md](../master.md)、Primary Milestone: MG008（関係: MG003）
- Status: incomplete（2026-10-18 Q1 作成、p002 は Q1 が実施。他の Phase は詳細を Q1 とユーザーで決めてから P2 が開始する）
- 起点（2026-10-18 ユーザー）:「WSを作成します。Vision Five 2ボードに移植します。HALにディレクトリだけ作りました。こちらをP2で実行しますが、さきに詳細をメインエージェントQ1と私で詰めてからにします。」
- ユーザーが作った空の directory: `src/hal/riscv64/bsp-visionfive2/`。
- 担当: P2（開始は詳細の合意の後）。

## ユーザーの決定（2026-10-18、クリック）

1. 最初の到達目標: 「UART の login」。S-mode の kernel が OpenSBI の上で起動し、UART の console で login、SMP 4 core、timer・PLIC の割り込み、root は initrd か SD。まず HAL の芯を固める。
2. 起動の道: 「U-Boot の UEFI＋zedBSD の loader」。U-Boot の EFI で bootloader/uefi の riscv64 版を動かし、amd64・arm64 と同じ道で kernel を渡す。DTB は U-Boot から。
3. toolchain: 「許可、Q1 が行う」。Q1 が toolchain の lock を外して LLVM_TARGETS_TO_BUILD（今は `PowerPC;AArch64;X86`、toolchain/llvm/llvm.mk）に RISCV を足し、作り直して lock し直す。P2 は toolchain に触らない。
4. 試験の環境: 「これから準備」。板はまだ host につないでいない。最初は QEMU の virt（riscv64）で HAL の芯を作り、実機は後。

## 完了の条件（案、Q1。ユーザーとの議論で確定する）

1. QEMU の virt（riscv64）で、UEFI の firmware（U-Boot の EFI か edk2）→ zedBSD の loader → kernel の順に起動し、UART の console で login できる。SMP 4 core、timer と PLIC の割り込みが動く。判定は virt に付けた画面（ramfb）を `boot-test.sh` で撮った login の prompt と、serial で打った command の答えの画面（決定 5、[design.md](design.md) §5）。
2. VisionFive 2 の実機で、板の SPL＋OpenSBI＋U-Boot の EFI → zedBSD の loader → kernel で、UART の console に login できる（SMP 4 core、timer、PLIC）。root は initrd か SD。実機の判定の方法は未決（design.md §7 #15）。
3. HAL の riscv64 の部分（`include/hal/arch/riscv64.h` などの API の追加）は、差分ごとのユーザーの事前承認の後に入れる（[Guardrail](../guardrail.md)）。
4. 変えた C のコードが plan/coding-style.md の全文に合う（最後の規約の Phase）。

## Phase

[design.md](design.md) §6 の表（2026-10-18 改訂 2）と [design-peripherals.md](design-peripherals.md) §4.1 の表（p011・p013〜p025、改訂 4）の写し（2026-10-19 に p005 から p025 を分けた: design.md §12）。実行の順は ID の順ではない: p009 ∥ p003 → p008 → p007（p010 が途中）→ p004 → p025 → p005 → p011・p013〜p024（順は design-peripherals.md §4.1 の図と §4.2）→ p006。p021 は p012 だけに依り板の前に進める。LW は 1 LW ≈ 実時間 15〜25 分、±50%。

| Phase | 目的・範囲 | 依存 | LW | 状態 |
| --- | --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計: JH7110・U74 の CPU（RV64GC、Sv39）・OpenSBI の SBI（timer・IPI・HSM・console）・PLIC・CLINT・UART（8250 の DW）・U-Boot の EFI の調べ、HAL の riscv64 の API の差分の案（承認の前は plan に置くだけ）、loader の riscv64 版、build（platform/riscv64・config）。code は書かない。改訂 2 でレビュー 1 の全件を反映 | — | 済 | cleared（2026-10-18 Q1: 改訂 2 でユーザーが「今の改訂 2 で判断する」、#13・#17・#18 を回答） |
| p002 | toolchain: LLVM に RISCV の target を足す（Q1） | — | 済 | cleared（2026-10-18 Q1: toolchain/llvm/llvm.mk の LLVM_TARGETS_TO_BUILD と config の identity に RISCV。共有の build/llvm を source から作り直し（log build/tmp/llvm-riscv-20261018.log、FAILED・error 0）、`clang --print-targets` に riscv64、rv64gc の object を確認、config-amd64.mk の vmunix の build と check PASS。toolchain は lock し直した。残り: install の identity は version・patch だけで target を見ない、CI の binary cache（rev-0、zedbsd8）に RISCV が無い → CI で riscv64 を build する時に patch level を上げて新しい cache を公開） |
| p009 | 試験の道具（Q1 の tools）: `boot-test.sh` の `BOOT_MODE=riscv64-virt`・`riscv64-uboot`（`-device ramfb`、`-serial unix:`）、`BOOT_PATTERN`、login の後に serial.py で command を打って 2 枚目を読む段、`plan/ws256/tests/` の config.mk と image の手順、QEMU の DTB（dumpdtb）を `tests/virt.dts` に。担当は design §7 #18 | — （p003 の T1 の試験の前） | 2〜3 | cleared（2026-10-18 Q1、T1-707） |
| p003 | HAL の芯（QEMU virt、直の起動）: `include/hal/arch/riscv64.h`（承認後）と `src/hal/riscv64/` の全部、bsp-virt（fw_cfg・ramfb の framebuffer・80x25 の console・font）、最小の `src/kern/platform/riscv-virt.c` と `elf.c` の `EM_RISCV`、`platform/riscv64/` の vmunix の build、`Makefile`・menuconfig・config。受け入れは画面の `RISCV64 PAGING PASS`・SBI の版・`TIMER TICK`・`SMP 4 ONLINE`・root 無しの失敗、`cpus=1`、`-cpu rv64,sstc=off,svpbmt=off` | HAL の差分の承認（#13）が着手の前、p009 が T1 の前 | 28〜36 | cleared（2026-10-18 Q1、T1-708） |
| [p008](phase008/phase.md) | kernel 側: platform の残り（console・ramdisk・device の表）、`ns16550.c`（RX）、`serial-console.c`、`ramdisk.c`、MBR の `root.img` の rootfs.mk、FDT・initrd の handoff（#9 の決定の後）、kernel の arch の `#if` の棚卸し。受け入れは画面の ramdisk の mount と「init が無い」 | p003、#9 | 10〜12 | cleared（2026-10-19 Q1、T1-720。RX は p007 の T1 へ持ち越し） |
| [p007](phase007/phase.md) | userland の riscv64: crt0/crt1/setjmp/syscall の .S と `user.ld`（先に書いて p010 (b) に渡す）、libc・rtld の枝と relocation（TLS は全部 dynamic）、Noct の cmake、`check-dynamic-elf.py`、最小の user program と root.img。受け入れは画面の login の prompt と、serial で打った `uname -m` の答え `riscv64` | p008、p010 (a) が libc の前、p010 (b) が link の前 | 14〜18 | cleared（2026-10-19 Q1、T1-721。Noct の差分の案は p010 の残り） |
| p010 | toolchain（Q1、ユーザーの許可）: (a) LLVM の patch zedbsd9（riscv64 の `__ZEDBSD__`、linker の emulation）、(b) `sysroot.mk` の riscv64、(c) CI の cache | (a) いつでも、(b) は p007 の .S の後 | 2〜3 | in-progress（(a)・(c) 済み。(b) sysroot.mk の riscv64 を 2026-10-19 Q1 が適用（P2 の plan/ws256/proposed/sysroot-riscv64.diff、536b101d9）、make sysroot-riscv64 は warning 0、smoke.elf は ELF64 RISC-V。sysroots を全部作り直した） |
| [p004](phase004/phase.md) | loader の riscv64（PE の header の自作、`.rela.dyn` の自己 relocation、RISCV_EFI_BOOT_PROTOCOL、DTB の複写、ZBL6 の handoff、`satp`=0 で物理の入口へ）、`riscv64-handoff.h`、`bootloader.mk`・`disk-image.mk`（ESP＋root の GPT）、U-Boot `qemu-riscv64_smode` の導入（Q1、apt）、`elf64.c` の arch の書き分け。受け入れは U-Boot → BOOTRISCV64.EFI → vmunix → 画面の login と `uname -m` | p007、p009 | 10〜14 | cleared（2026-10-19 Q1、T1-722） |
| p005 | VisionFive 2 の実機: bsp-visionfive2 の中身（ccache の dcache、mimpid・marchid の log、errata）、DW UART の RX の癖、SD の 4 partition の image（SPL・itb・ESP・root）、U-Boot の SD からの起動の手順、実機の試験の道具、実機の login（10 回）、板の版・memory の記録（#2 の決定）。skeleton（`visionfive2.c` の最小・vmunix.mk の vf2 の枝・config）は p025 に分けた（[design-peripherals.md](design-peripherals.md) §3.10） | p025・p004、板の接続・電源・UART（#10）、#15（実機の判定）、#8 | 6〜9 ＋ debug 4〜8（skeleton の 2〜3 を p025 へ） | planning |
| p006 | 規約の全文の見直し（p011〜p025 を含む） | 全部の後 | 2〜3 | planning |
| p011 | VisionFive 2 の framebuffer（2026-10-19 ユーザー「VisionFive2ってGPUもあるからまずはフレームバッファがあるとうれしい」→「WS256 に Phase を足す」）: (1) 調べ: 使う U-Boot（StarFive の配布版・mainline）が JH7110 の HDMI を初期化して UEFI の GOP を出すか、出すなら p004 の loader が GOP から ZBL6 の framebuffer を渡し、kernel は汎用の framebuffer の console で済む。(2) 出さない時は自前の display の driver の設計（VOUT の PMU・syscrg/voutcrg の clock・reset、Verisilicon DC8200、Innosilicon の HDMI の送信器と EDID。出典は StarFive の JH7110 の TRM、Linux の driver は値の引用だけ）を design-reviewer に通し、Phase に分ける。GPU（PowerVR BXE-4-32）は範囲外 | (1) p005・#15、(2) (1) の結果・#20・#29（HAL の承認）・p004（handoff 版 1 の後に版 2 へ広げる） | 11〜16（(1) 2〜4: 実機の調べ 1〜2 と loader の boot hart の代わりの道・GOP の答え 1〜2、(2) handoff 版 2・HAL の予約・console を HAL の共通に・名前付きの handoff 9〜12。自前の display は含まない。[design-peripherals.md](design-peripherals.md) §3.1） | in-progress（(1)・(2) の code と QEMU の GOP の道は済み（T1-728・T1-729）。実機の目視（ユーザー、#10・#15）を待つ） |
| [p012](phase012/phase.md) | 周辺の設計（改訂 4）: 下の p013〜p019 の調べと設計。JH7110 の clock（syscrg・aoncrg・stgcrg）・reset・pinctrl/GPIO・PMIC（AXP15060、I2C）、SD の controller（DesignWare MMC）、PCIe の host（PLDA XpressRICH）と USB の VL805（xHCI）、NVMe の M.2、GbE（Synopsys の Ethernet QoS と PHY）、sound の道（3.5mm・HDMI のどちらか、codec）を、StarFive の JH7110 の TRM・VisionFive 2 の回路図で確かめる（ここに書いた部品の名前は Q1 の覚えで、未確認）。既存の driver（pci-xhci・pci-nvme・usb-hid・audio、Pi 4 の pci-brcmstb の前例）を使う所を分ける。design-reviewer に通す | p001（今から可、実機は要らない） | 4〜6 | cleared（2026-10-19 Q1、改訂 4） |
| [p013](phase013/phase.md) | 周辺の基盤の code（板の前）: `src/drivers/platform/visionfive2/` の CRG（SYS・STG・AON、PLL から rate）・reset・pinctrl/GPIO・SYSCON、汎用の `drv_fdt_dma_coherent`（fdt.c、親を辿る）と `VF2_DMA_DEFAULT_COHERENT`。PMIC と DW I2C は外す（#22）。実機の確かめは p022 | p025・p012 | 5〜8 | cleared（2026-10-19 Q1、code と host 試験、実機は p022） |
| [p014](phase014/phase.md) | SD カード: DW MMC（PIO から、IDMAC は後）と JH7110 の glue、block の device（既存の MBR・FAT の層に繋ぐ） | p013・p022 | 6〜12 | cleared（2026-10-19 Q1、code と host 試験、実機は p015） |
| p015 | rootfs からの起動: SD の image（ESP・root）の root を mount して initrd 無しで起動し、画面（p011）で login（UART の対話での受け入れは #27） | p014・#27（画面の受け入れは p011） | 3〜5 | planning |
| p016 | PCIe の virt の部分: QEMU virt の ECAM の FDT host（`pci-ecam-fdt.c`、INTx は PLIC の線ごと）と `riscv-virt.c` の繋ぎ（xHCI・usb-hid・keyboard）、virt の `-device qemu-xhci -device usb-kbd` で login。PLDA の実機は p020、core の hook は p021（[design-peripherals.md](design-peripherals.md) §3.5） | p007 | 4〜6 | cleared（2026-10-19 14:29 Q1、T1-723 の走り B） |
| p017 | NVMe の INTx の道（virt）: pci-nvme の IRQ の許可に INTx を足す（ISR で INTMS/INTMC の往復はしない）、QEMU virt の `-device nvme` で読み書きと mount、amd64 の NVMe の回帰。M.2 の実機と NVMe の root は p024（[design-peripherals.md](design-peripherals.md) §3.6） | p016 | 3〜4 | cleared（2026-10-19 Q1、T1-724） |
| p018 | LAN: DW Ethernet QoS と PHY（MDIO で id を読む、YT8531 の推定）、gmac0、DHCP・ssh | p013・p022・p023 | 10〜14 | in-progress（q1292、P2、板の前の code、2026-10-19） |
| [p019](phase019/phase.md) | sound: 3.5 mm の PWMDAC と DW AXI DMAC の cyclic を既存の audio の層に繋ぎ、音を鳴らす（#23・#24） | p013・p022・p023 | 7〜11 | in-progress（q1293、P1、2026-10-19: 板の前の code は済み、host 試験 ALL PASS・VF2/virt build warning 0。実機の耳の確かめは p022・p023 の後） |
| p020 | PLDA の PCIe の host（実機）: `pci-plda.c`（ATR の outbound、link、PERST、PHY、INTx は p021 の hook、bridge をまたぐ swizzle、root の DMA は 32 bit から）、VL805 の xHCI と USB keyboard で login（[design-peripherals.md](design-peripherals.md) §3.5） | p013・p022・p021・p016・p023 | 8〜12 | in-progress（板の前の code と host 試験 137 PASS は済み（72bea29bc、merge 0398b03d4）、実機の確かめ（ユーザーの目視）を待つ） |
| [p021](phase021/phase.md) | PCI core の host の INTx の hook: `drv_pci_bus_ops` に省略可能な intx の 4 つ、EOI を送らない `intx_line_run`、線の鍵を (host, vector)、lock の順と drain。amd64・Pi 4・riscv64 virt の回帰（§3.5） | p012 | 5〜7 | cleared（2026-10-19 Q1、T1-727） |
| p022 | p013 の実機の確かめ: clock の rate（U-Boot の `md` と照合）、card detect、PERST（§3.2） | p013・p005・#15 | 1〜2 | planning |
| p023 | DMA の coherency の確かめ（陽性の対照、64 KiB の buffer、8 回）、非 coherent なら予備 (i)（確保ごとの uncached の別名、HAL の BSP）（§2.4・§3.9） | p013・p022・#15 | 3〜7 | planning |
| p024 | NVMe の実機と NVMe の root（p017 から分けた）: M.2 の SSD の読み書き、pcat の形の root の選び方（§3.11） | p017・p020・p015 | 3〜4 | planning |
| [p025](phase025/phase.md) | VF2 の skeleton（p005 から分けた、板の前）: `visionfive2.c` の最小、`platform/riscv64/vmunix.mk` の vf2 の枝、config・menuconfig、bsp-visionfive2 の最小（`hal_dcache_*` は ccache の FLUSH64）、build（§3.10） | p004 | 2〜3 | cleared（2026-10-19 Q1、build） |

合計: p001〜p010 は 76〜106 LW（うち Q1 の p009・p010 4〜6、実機の debug 4〜8。[design.md](design.md) §6）。p011・p013〜p024 は 69〜108 LW（[design-peripherals.md](design-peripherals.md) §4.1、改訂 4。p025 の 2〜3 は p005 から移した分）。

## 未決（Q1 がユーザーに聞く。詳細と既定の案は [design.md](design.md) §7.2）

- 板の版（v1.2A・v1.3B）と memory の大きさ（2・4・8 GB）（#2）。
- 実機の試験の接続（USB-UART・電源の制御・LAN）（#10）と、**実機の判定の方法**（p011 (2) の前は画面が無い。serial.py の対話を実機に限る例外にするか: #15）。
- root の媒体（initrd か SD: #4）と #3（周辺を後の WS にするか）は 2026-10-19 のユーザーの指示で決着: framebuffer・SD・rootfs・USB keyboard・LAN・sound・NVMe はこの WS（p011〜p019）。eMMC は未定。GPU は [WS269](../ws269/ws.md)、Keiland は [WS270](../ws270/ws.md)。
- `include/kern/boot.h` を変えるか（#9: 既定は変えず HAL の名前付き handoff object）、HAL の差分 2 件＋名前付き object の承認（#13、p003 の前）、toolchain の p010（#14）。
- sifive_u の走りを gdbstub の読み取りで判定に数えるか（#17）、p009 の tools の変更の担当（#18）。
- 決定済み: 参照の方針（6、WS245 と同じ）、virt の判定（5、画面）。

## ユーザーの決定（2026-10-18、クリック、2 回目）

5. 起動の確認: 「virt に画面を付ける」。QEMU の virt に ramfb か virtio-gpu を付け、boot-test.sh の画面で判定する（serial の log で判定する例外は作らない）。
6. 外部の code の参照: 「WS245 と同じ」。手順は参考にし、code・comment・構造は写さず、block ごとに Value(s)/Procedure referenced: を書く。

## 設計（p001）

- [design.md](design.md)（P2、q1243。初版は main fc8e21f8c、改訂 2 は q1243-i02、main e4c67abc5）、HAL の差分の案 [proposed/hal-riscv64.md](proposed/hal-riscv64.md)（差分 1・2 と名前付き handoff object。未承認、適用しない）、boot.h の差分の案 [proposed/boot-h-riscv64.md](proposed/boot-h-riscv64.md)（案 (b)、既定は適用しない）。
- レビュー 1: [design-review-1.md](design-review-1.md)（blocker 1・major 13・minor 24）→ 改訂 2 で全件を反映（design.md §11 の表）。
- 改訂 2 で決めた事の要点: initrd は MBR の image（rpi4 と同じ root の道、kernel の vfs.c は変えない）、画面は ramfb を HAL の bsp-virt が fw_cfg で設定して 80x25 の console を描く（U-Boot の GOP に依らない、両方の入口で同じ）、timer は常に SBI・Svpbmt は使わない、TLS は全部 dynamic、PLIC の mask は priority、kernel 半分の table は init で封じる、loader は table を作らず satp=0 で物理の入口へ。

## ユーザーの決定（2026-10-18、クリック、3 回目）

7. 設計は「今の改訂 2 で判断する」（レビュー 2 は回さない）。
8. #13 HAL の差分 3 件は「3 件とも承認」（[Guardrail](../guardrail.md) の表）。
9. #17 sifive_u の走りは「gdbstub で読んで判定」（WS256 の sifive_u に限る例外。serial の log は使わない）。
10. #18 試験の道具の変更は「P2 に許可」（p009 で plan/tools の 3 file だけ。既存の mode は変えない。Q1 が merge の前に読む）。

11. #9 は (a): HAL → kernel は汎用の構造体と `hal_get_arch_handoff` の名前付きの object だけ（2026-10-18 ユーザーの規約、[Guardrail](../guardrail.md)「HAL と kernel・boot loader と HAL の受け渡し」）。[proposed/boot-h-riscv64.md](proposed/boot-h-riscv64.md) の案 (b) は採らない。
12. `bootloader/include/amd64-kernel-image.h` と同じ種類の riscv64 の約束（kernel の置き場所）を p004 で足す時に、arch ごとの命名と置き場所を揃える（2026-10-18 ユーザー「名前は riscv64 の時に揃える」）。

## ユーザーの指示（2026-10-19 13:00）

「VisionFive2はまずフレームバッファ、SDカードを実装して、rootfsから起動できるところまで持って行きます。USBキーボードで操作できるようにします。LANとサウンドとNVMeも使えるようにします。NVMe SSDがボードにつけてあり、テスト可能です。ここまでのphaseを作ってください。そのあと、PowerVRのドライバを独立WSで作ります。最終的に、VF2でkeilandを動かして、OSを完全に動作させます。」
→ p012〜p019 を planning で足した（p011 は同日の framebuffer）。順は p004 → p005 → p011 → p013 → p014 → p015、p016 → p017、p018、p019（p012 の設計は今から可）。GPU は WS269、Keiland は WS270。
