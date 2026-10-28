<!-- awesome-plan project=zedbsd record=ws256 -->

# WS256 StarFive VisionFive 2（JH7110、RISC-V）への移植

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: VF2 の移植は板で動く: 2026-10-20 ユーザー「画面がつきました！」（HDMI・USB keyboard の login、p015）、LAN は 2026-10-20 ユーザー「pingはOKです。」（p018）、NVMe p024・MSI p026 cleared（2026-10-26）、p006 規約 cleared。残りの p019（音）・p023（coherency）・p022 は実機の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」）
Completed: 2026-10-27（q1608）
Primary Milestone: MG008
Related Milestones: MG003
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

zedBSD を StarFive VisionFive 2（JH7110、U74、RV64GC）に移植する: QEMU の virt（riscv64）で UEFI → zedBSD の loader → kernel の順に起動し login でき（SMP 4 core、timer・PLIC）、VF2 の実機で SPL＋OpenSBI＋U-Boot の EFI → loader → kernel で login できる。周辺（framebuffer・SD・rootfs・USB keyboard・LAN・sound・NVMe）を使えるようにする。GPU は [WS269](../ws269/ws.md)、Keiland は [WS270](../ws270/ws.md)、DC8200 の display は [WS277](../ws277/ws.md)。

## ユーザーの決定

- 2026-10-18 ユーザー（起点）:「WSを作成します。Vision Five 2ボードに移植します。HALにディレクトリだけ作りました。こちらをP2で実行しますが、さきに詳細をメインエージェントQ1と私で詰めてからにします。」（ユーザーが作った空の directory: `src/hal/riscv64/bsp-visionfive2/`）
- 2026-10-18（クリック、1 回目）: 1. 最初の到達目標「UART の login」。2. 起動の道「U-Boot の UEFI＋zedBSD の loader」。3. toolchain「許可、Q1 が行う」（LLVM_TARGETS_TO_BUILD に RISCV）。4. 試験の環境「これから準備」（最初は QEMU の virt）。
- 2026-10-18（クリック、2 回目）: 5. 起動の確認「virt に画面を付ける」（serial の log で判定する例外は作らない）。6. 外部の code の参照「WS245 と同じ」（手順は参考にし、code・comment・構造は写さず、block ごとに Value(s)/Procedure referenced:）。
- 2026-10-18（クリック、3 回目）: 7. 設計は「今の改訂 2 で判断する」。8. #13 HAL の差分 3 件は「3 件とも承認」（[Guardrail](../guardrail.md) の表）。9. #17 sifive_u の走りは「gdbstub で読んで判定」（WS256 の sifive_u に限る例外）。10. #18 試験の道具の変更は「P2 に許可」。11. #9 は (a): HAL → kernel は汎用の構造体と `hal_get_arch_handoff` の名前付きの object だけ（[Guardrail](../guardrail.md)「HAL と kernel・boot loader と HAL の受け渡し」）。12. 「名前は riscv64 の時に揃える」（`bootloader/include/amd64-kernel-image.h` と同じ種類の riscv64 の約束）。
- 2026-10-19 13:00「VisionFive2はまずフレームバッファ、SDカードを実装して、rootfsから起動できるところまで持って行きます。USBキーボードで操作できるようにします。LANとサウンドとNVMeも使えるようにします。NVMe SSDがボードにつけてあり、テスト可能です。ここまでのphaseを作ってください。そのあと、PowerVRのドライバを独立WSで作ります。最終的に、VF2でkeilandを動かして、OSを完全に動作させます。」
- 2026-10-19「VisionFive2ってGPUもあるからまずはフレームバッファがあるとうれしい」→「WS256 に Phase を足す」（p011）。
- 2026-10-26（p026）: PLDA の PCIe の MSI の出典は OpenBSD stfpcie.c と Linux pcie-plda-host.c の値だけ。

## 結果

- 設計: [design.md](design.md)（改訂 2、[design-review-1.md](design-review-1.md)）、周辺は [design-peripherals.md](design-peripherals.md)（改訂 4、[review 1](design-peripherals-review-1.md)・[2](design-peripherals-review-2.md)・[3](design-peripherals-review-3.md)）。HAL の差分の案 [proposed/hal-riscv64.md](proposed/hal-riscv64.md)（承認済みの 3 件は Guardrail）、boot.h の案 (b) [proposed/boot-h-riscv64.md](proposed/boot-h-riscv64.md) は採らない（決定 11）。
- toolchain（Q1、p002・p010）: LLVM に RISCV の target、patch zedbsd9（riscv64 の `__ZEDBSD__`、linker の emulation）、`sysroot.mk` の riscv64（536b101d9）、CI の cache。
- QEMU virt（T1-707〜T1-727、画面の判定）: HAL の芯（`RISCV64 PAGING PASS`・SBI・`TIMER TICK`・`SMP 4 ONLINE`、sstc・svpbmt 無しの CPU でも）、kernel の platform・ns16550・ramdisk・FDT/initrd、userland（crt・libc・rtld、login と `uname -m` の `riscv64`、TLS は全部 dynamic）、loader（U-Boot → BOOTRISCV64.EFI → vmunix）、ECAM の PCIe と xHCI・USB keyboard、NVMe の INTx、PCI core の host の INTx の hook（amd64・Pi 4 の回帰を含む）。
- VF2 の実機（ユーザー、2026-10-20 06:11「画面がつきました！」「HDMIで画面を見ながら、USBキーボードでログインしてコマンド操作できました。」、image main d1ef60e39）: U-Boot の GOP の framebuffer（消えた表示は WS277 p005 の自前の点灯で出し直し）、SD の root から initrd 無しで起動（SD は 24.75 MHz の fallback、BUG-340）、PCIe0 の VL805 の xHCI と USB keyboard で login。LAN は link up 1000 Mbps・DHCP・ユーザー「pingはOKです。」。NVMe（PCIe1）の読み書きの一致と電源の入れ直しの後の一致（q1587）、PLDA の MSI で NVMe が動く（q1589）。
- 周辺の code（板の前、host 試験）: CRG・reset・pinctrl・SYSCON・`drv_fdt_dma_coherent`、DW MMC、DW Ethernet QoS と PHY、PWMDAC と DW AXI DMAC の sound、DMA coherency の試験の cdev（`/dev/vf2-dmatest`）。
- 規約の全文の見直し（p006、p011〜p025 を含む）: T1-742・T1-744 の QEMU の回帰が全部 PASS。

## 制限・移管

- 実機の確認は未実施: sound（p019、ユーザーの耳）、DMA coherency（p023、`cat /dev/vf2-dmatest`）、CRG の clock の rate・card detect・PERST（p022）、rootfs の起動 10 回と U-Boot の自動起動（p015、BUG-339）、ssh（p018、UAT の image に sshd が無い）、NVMe の root（p024）。eMMC は未定。
- VF2 の Bug: [BUG-338](../bugs/BUG-338.md)（SMP が S7 の hart 0 を起こそうとして止まる）、[BUG-339](../bugs/BUG-339.md)（U-Boot の control の DTB）、[BUG-340](../bugs/BUG-340.md)（SD の data CRC）、[BUG-343](../bugs/BUG-343.md)（SD の起動の root）。
- CI の binary cache（rev-0、zedbsd8）に RISCV が無い件は p002 の時点の記録（CI で riscv64 を build する時に patch level を上げて新しい cache を公開）。今の CI の状態はこの記録では確かめていない。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws256-p001 | 設計（JH7110・U74・SBI・PLIC・CLINT・UART・U-Boot の EFI、HAL の差分の案、loader、build） | cleared（2026-10-18、改訂 2） |
| ws256-p002 | toolchain: LLVM に RISCV の target（Q1） | cleared（2026-10-18） |
| ws256-p003 | HAL の芯（QEMU virt、直の起動） | cleared（2026-10-18、T1-708） |
| ws256-p004 | loader の riscv64・U-Boot の導入・ESP＋root の GPT | cleared（2026-10-19、T1-722） |
| ws256-p005 | VF2 の実機（bsp-visionfive2 の中身、SD の image、実機の login 10 回） | planning のまま WS を閉じた（実機の login は p011・p015・p020 で達成、10 回の起動は未実施） |
| ws256-p006 | 規約の全文の見直し（p011〜p025 を含む） | cleared（2026-10-19、T1-742・T1-744） |
| ws256-p007 | userland の riscv64（crt・libc・rtld・Noct の cmake） | cleared（2026-10-19、T1-721） |
| ws256-p008 | kernel 側: platform・ns16550・ramdisk・FDT と initrd の handoff | cleared（2026-10-19、T1-720） |
| ws256-p009 | 試験の道具（boot-test.sh の riscv64 の mode、virt.dts） | cleared（2026-10-18、T1-707） |
| ws256-p010 | toolchain（Q1）: patch zedbsd9、sysroot.mk の riscv64、CI の cache | in-progress のまま WS を閉じた（(a)・(b)・(c) とも適用済みの記録） |
| ws256-p011 | VF2 の framebuffer | cleared（2026-10-20、ユーザーの実機「画面がつきました！」） |
| ws256-p012 | 周辺の設計（改訂 4） | cleared（2026-10-19） |
| ws256-p013 | 周辺の基盤の code（CRG・reset・pinctrl・SYSCON・`drv_fdt_dma_coherent`） | cleared（2026-10-19、host 試験。実機は p022） |
| ws256-p014 | SD カード（DW MMC の PIO と JH7110 の glue） | cleared（2026-10-19、host 試験。実機は p015） |
| ws256-p015 | rootfs からの起動 | in-progress のまま WS を閉じた（実機で SD の root から login、10 回の起動と U-Boot の自動起動は未実施） |
| ws256-p016 | PCIe の virt の部分（ECAM の FDT host、xHCI・USB keyboard） | cleared（2026-10-19、T1-723） |
| ws256-p017 | NVMe の INTx の道（virt） | cleared（2026-10-19、T1-724） |
| ws256-p018 | LAN: DW Ethernet QoS と PHY、DHCP・ssh | in-progress のまま WS を閉じた（実機で ping OK、ssh は未実施） |
| ws256-p019 | sound: PWMDAC と DW AXI DMAC | in-progress のまま WS を閉じた（host 試験 58 PASS、実機は未実施） |
| ws256-p020 | PLDA の PCIe の host（実機）と VL805 の USB keyboard | cleared（2026-10-20、ユーザーの実機） |
| ws256-p021 | PCI core の host の INTx の hook | cleared（2026-10-19、T1-727） |
| ws256-p022 | p013 の実機の確かめ（clock の rate・card detect・PERST） | planning のまま WS を閉じた（実機の確かめだけ、未実施） |
| ws256-p023 | DMA の coherency の確かめ | in-progress のまま WS を閉じた（試験の cdev と core は main、実機は未実施） |
| ws256-p024 | NVMe の実機と NVMe の root | cleared（2026-10-26、q1587。NVMe の root は未） |
| ws256-p025 | VF2 の skeleton（visionfive2.c・vmunix.mk・config） | cleared（2026-10-19、build） |
| ws256-p026 | PLDA の PCIe の MSI | cleared（2026-10-26、q1589） |

## 記録の所在

Phase の計画・結果・試験（`plan/ws256/phase001`〜`phase026`、`plan/ws256/tests/`）は削除する（今後も使う host 試験は plan/tools へ移す: 移した先は [plan/tools.md](../tools.md)）。git の commit `fb1f6afee` の `plan/ws256/` にある。Queue の履歴は [plan/history](../history/index.md)。
