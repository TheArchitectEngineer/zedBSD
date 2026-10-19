<!-- design-reviewer の再レビュー（2026-10-19、design-peripherals.md 改訂 2 と proposed/hal-riscv64-framebuffer.md）。Q1 が報告の本文をそのまま置いた -->

# ws256-p012 design-peripherals.md 改訂 2 と proposed/hal-riscv64-framebuffer.md の敵対的な再レビュー

読むだけで行いました。source・plan の変更、commit、build、QEMU、rm はしていません。`.internal/` も読んでいません。

照合した物:
- main（`/home/awe/zedBSD-claude1`、HEAD 56b73bcb5）
- 資料 `/home/awe/zedBSD-worktrees/p1/build/tmp/ws256-docs/`
- 参考として P2 の worktree `/home/awe/zedBSD-worktrees/p2` の p004 の作業中の差分（未 commit、読むだけ）

## レビュー 1 の各指摘は直ったか

| 指摘 | 判定 | 理由 |
| --- | --- | --- |
| B1 | **一部だけ** | hook の方向は正しい。ただし書かれたままでは動かない（BL1・MJ1・MJ2） |
| B2 | 直った | INTx を選んだ判断は妥当。INTx の処理の細部に Minor 1 |
| M1 | **一部だけ** | 承認の段は入った。差分の案の文面に誤りがある（MJ4）。kernel 側の console も handoff の field・予約も無い（MJ3） |
| M2 | **一部だけ** | 時期は統一された。手順の buffer の大きさで偽の合格が出る（MJ6） |
| M3 | **一部だけ** | (ii) の取り下げと uncached の根拠の訂正は正しい。(i) の具体化が過剰で危ない（MJ7） |
| M4 | 直った | pcat.c の 143・313・792-797・865 行の関数は実在する |
| M5 | 直った | ただし p011 の分岐の論理が逆（MJ5） |
| M6 | 直った | p005 の skeleton を scoped output として p005 の側に書く必要がある（Minor 6） |
| M7 | 直った | 決定 13:39 と表は合う。PLDA の MSI だけ決定の外へ広げている（Minor 3） |
| M8 | 直った | — |
| Minor 1〜6・8〜11・13〜17 | 直った | — |
| Minor 7 | 一部だけ | 代わりの `clk dump` が使えるかが未確認（Minor 8） |
| Minor 12 | 一部だけ | 「Master の next」は誤り（Minor 4） |

## Blocker

**BL1. p021 の hook が host に渡す `dispatch` を core の `pci_intx_dispatch` としているが、これは EOI を送るので riscv64 では HAL_FATAL になる**
- 根拠
  - `src/drivers/pci/pci.c:3606-3644`: `pci_intx_dispatch` は最後に必ず `kern_irq_send_eoi(acknowledge)` を呼ぶ。
  - `src/hal/riscv64/irq.c` の `hal_irq_send_eoi` は、hart が持つ `active_ack` と違う token を受けると `HAL_FATAL("invalid riscv64 IRQ acknowledgement")` で止まる。1 回目の EOI で `active_ack` は NONE に戻る。
  - 設計 §3.5 は「`dispatch`（core の `pci_intx_dispatch`）を表に置く」と書き、host は「handler の後に W1C の ack」とする。
- 起こる事（どれも PLDA の実機で、最初の数回の割り込みのうちに起こる）
  - `ISTATUS_LOCAL` に 2 本以上の pin が立つと、2 回目の dispatch の EOI で panic する。
  - host が自分でも EOI すると panic する。
  - host が EOI しないと、event や MSI の bit だけが立った割り込みで PLIC の claim が終わらず、線 56/57 が二度と来ない。
  - 1 本だけの時でも、EOI が W1C の ack より先になる。
- 修正案
  - core に EOI を送らない入口を足す。例: `pci_intx_line_run(line)` を `drv_pci_*` の型（ack を持たない `void (*)(void *)`）で hook に渡す。
  - EOI は host の PLIC の handler が、W1C の ack の後に 1 回だけ送る。
  - これを §3.5 に明記し、host の試験に「2 本が同時に立つ」「event だけが立つ」を入れる。

## Major

**MJ1. INTx の線を、host をまたぐ全体の list で `vector` の番号だけで引いている。2 本の PLDA で衝突する**
- 根拠
  - `pci.c:3037-3108` の `establish_intx` は `find_intx_line(irq->vector)` で全体の `intx_lines` を探す。同じ番号の線があれば handler を足すだけで、登録（hook）を呼ばない。
  - 設計は `vector` の値を決めていない。pin は「`irq->private_data` に入れる」としているが、core はそれを `cookie->capability` に写すだけで（pci.c:1833）、線（`struct pci_intx_line`、pci.c:180付近）には残らない。
  - そのため解除と mask の時（pci.c:3550-3558）に、host は pin を知る術が無い。
  - VF2 は pcie0・pcie1 の両方が `okay` です（`jh7110-starfive-visionfive-2.dtsi:36-42`）。どちらの `interrupt-map` も自分の intc の hwirq 1〜4 を指します（`jh7110.dtsi:1237`、`1280`）。
- 起こる事: brcmstb と同じく hwirq（1〜4）を vector にすると、pcie1 の device が pcie0 の線に足されます。hook が呼ばれないので、pcie1 の `IMASK_LOCAL` が unmask されず、USB か NVMe の片方が割り込みを受けません。逆に pcie0 の割り込みで pcie1 の handler も走ります。
- 修正案
  - vector を host と pin の組で全体に一意にする（例: PLIC の `riscv,ndev` より上の区間に `host index * 4 + pin`）か、線の鍵を `(bus->host, vector)` にする。
  - pin（または host の文脈）を `pci_intx_line` に持たせ、`intx_mask`・`intx_disestablish` に渡す。
  - 試験に「2 つの host・同じ hwirq」を入れる。

**MJ2. hook の並行性（lock の順序と drain）が設計に無い**
- 根拠
  - `pci.c:3087` の登録は `intx_lock` を持ち、割り込みを止めた状態で行われる。hook も同じ状態で呼ばれる。
  - 一方、host の ISR は「host の表を引く → `dispatch` → その中で `intx_lock`」の順で進む。
  - 解除の側では、core は `kern_irq_unregister` が成功したら drain が済んだ前提で `kern_free(line)` する（pci.c:3560-3585 の comment）。
- 起こる事
  - host が表の lock を持ったまま `dispatch` を呼ぶと、boot hart の ISR と別 hart の establish の間で ABBA の deadlock になる。
  - `intx_disestablish` が実行中の demux を待たずに戻ると、line の use-after-free になる。
  - 戻り値（EBUSY・EIO の再試行の約束）も決まっていない。
- 参考（範囲外）: 既存の riscv64 と arm64 の `hal_irq_unregister` も実行中の handler を待たない（`src/hal/riscv64/irq.c` の unregister と dispatch、`src/hal/arm64/irq.c:240-252`）。core の comment は実装と合っていない。
- 修正案: hook の約束として次を §3.5 に書く。
  1. hook は割り込みを止めた spin の文脈で呼ばれる。眠らない、確保しない。
  2. host は表の lock を外してから `dispatch` を呼ぶ。
  3. `intx_disestablish` は host の in-flight の数で drain してから成功を返す。
  4. 戻り値は int で、EBUSY と EIO の意味は core に合わせる。

**MJ3. p011: 「kernel 側の framebuffer の console」が存在しない。riscv64 の handoff に framebuffer も予約も無い。LW が過小**
- 根拠（console）
  - `kern_boot_framebuffer` を使うのは `src/drivers/platform/pcat/graphics/backend.c:1049`（pcat の platform に固有の text・font・splash）、rpi4.c:208（GPU に画面を渡すだけ）、i915・amdrdna・nvrtx だけです。汎用の kernel の text console はありません。
  - riscv64 の HAL には、bsp-virt に既に `cons.c`・`font.c`・`framebuffer.c` があります。
- 根拠（handoff）
  - P2 の作業中の `bootloader/include/riscv64-handoff.h`（p2 の worktree、未 commit）の `struct zbl6_riscv64_handoff` には、framebuffer の field も memory map もありません。
  - P2 の作業中の cmain.c は、memory を DT から取ります。
- 起こる事
  - p011 の code は、新しい kernel の text console、console の sink の多重化、loader の GOP の取り込み、handoff の版の上げを要します。「handoff 6 行・読み手・coherency の切り替えで 2〜4 LW」には収まりません。
  - kernel 側の console では、初期の message と HAL_FATAL が画面に出ません。
  - GOP の framebuffer は DDR の中にあります。DT の `/reserved-memory` に載らない限り、page の allocator が渡してしまい、描画と memory が壊れます。§3.1 は「boot services の型なら loader が handoff の予約に入れる」と条件付きですが、その予約の受け皿がありません。
- 修正案
  - 描画は HAL に置く（bsp-virt の console を riscv64 の共通に上げる案。hal.h は不変で `src/hal/` の実装の変更）。または kernel の console を新規として LW を付け直す。
  - p011 の新規に次を明記する: riscv64 の handoff の framebuffer の field と予約の範囲（版 2）、loader の GOP、HAL が予約を allocator と direct map から外す事。依存は「p004」ではなく「p004 の後に p011 が handoff を広げる」とする。

**MJ4. HAL の差分の案（#29 の承認の文面）の誤り**
- 根拠
  - `include/hal/hal.h:1284-1292` の `hal_get_arch_handoff(const char *name)` は 1 引数で、無い時は NULL を返す。`HAL_ERR_NOT_FOUND` は tree に存在しない（grep で 0 件）。案は `hal_get_arch_handoff(name, value)` と `HAL_ERR_NOT_FOUND` と書いている。
  - 「format は rpi4・pcat と同じ値」は両立しない。
    - rpi4: format は firmware の画素の順（0 が青から、1 が赤から）、pitch は byte（`src/hal/arm64/bsp-rpi4/boot.c:50-56`）。
    - pcat: format は `KERN_BOOT_FRAMEBUFFER_*`（1・2）、pitch は pixel（`src/hal/amd64/bsp-pcat/boot.c:84-93`）。
    - `include/kern/platform.h:80-88` は単位を「名前ごとに文書で決める」としている。
  - 「board 共通で返す」と「virt では返さない」も矛盾する。virt でも U-Boot と loader の道で GOP があれば返してしまう。
- 修正案: 承認の前に次を直す。
  - 値は 64 bit の word への pointer、無い時は NULL。
  - pitch は byte、format は `KERN_BOOT_FRAMEBUFFER_*` と明記する。
  - 名前を返す条件を BSP（VF2）に限る。

**MJ5. p011 の U-Boot の分岐の論理が逆。既定の #20 では成功しても主の道に画面が出ない**
- 根拠
  - §1.4: GOP が出る見込みは配布の U-Boot だけ。
  - #20 の既定: 配布の U-Boot は p011 (1) の試しだけ。
  - §3.1 は「(1) で出ない時: (a) 配布の U-Boot を主にする」と書いているが、配布の U-Boot で出なければ、それを主にしても意味が無い。
- 起こる事: 配布の U-Boot で GOP が出ても、mainline の主の道には画面がありません。ユーザーの「まずフレームバッファ」（2026-10-19 12:56・13:00）と、p015 の「画面で login」に届きません。
- 修正案: 「配布の U-Boot で出た時に主をどちらにするか」を #20 の問いとして明示する（配布を主に＋mainline の DTB を `bootefi <img> <fdt>` で渡す、または画面は p011 (b) まで無し）。「出ない時」の分岐は (b) だけにする。

**MJ6. coherency の確かめの手順が偽の合格を出す**
- 根拠: §2.4 は 2 MiB の src と dst を使う。L2 は 2 MiB。手順 3 で dst の 2 MiB を全部読むと、手順 2 で残した src の dirty な line（最後に書き直した 256 KiB を含む）が追い出されて DRAM に書き戻される。
- 起こる事: 非 coherent でも CPU→device の向きが一致し、合格になります。予備の要否を誤って判定します。
- 修正案
  - buffer は L2 の容量と way より十分小さくする（例 各 64〜128 KiB）。src を書く → dst を読む → 直ちに転送、の順にする。offset を変えて複数回流す。
  - 陽性の対照として、system port の別名（uncached）から dst に違う模様を書いて、検出できる事を先に示す。

**MJ7. 予備 (i) の具体化は不要に design の決定を変えさせ、しかも危ない写像を作る**
- 根拠
  - device の窓は物理 P < 64 GiB を 2 MiB の leaf で写す（`src/hal/riscv64/defs.h:55-60`、`space.c:924-975`）。RAM でない範囲は窓に写される。
  - system port の別名 0x4_4000_0000〜0x6_3FFF_FFFF は、この窓の中に入る。PMA は uncached（TRM の text の 2207 行 `RW A`）。
  - 設計は代わりに、root の 448〜509 に 8 GiB を 1 GiB の leaf で写すとしている。これは design.md §2.5 の「予約と OpenSBI の領域を map しない」（M4）を破る。2・4 GB の板では存在しない memory も写す。
- 起こる事
  - design.md §2.5 の改訂を Q1・ユーザーに求める判断（#25）が、不要に生まれる。
  - PMP の外の別名の領域で、OpenSBI の memory に S-mode から届く可能性がある（推測）。
- 修正案
  - `hal_pmem_map_uncached` は、確保ごとに `hal_space_map_device(paddr + 0x4_0000_0000)` 相当の内部の写像を作る。その前に FLUSH64 で範囲を書き出して捨てる。これで §2.5 は不変になり、#25 から design の改訂の部分を外せる。
  - 各 driver の `drv_dma_constraints.coherent` を誰がどう false にするか（DT に印が無いので板の方針）も書く（`src/drivers/generic/dma.c:337-347`・`995-1020`）。

## Minor

1. **pci-nvme の INTx の処理（q1271 で今 P1 が実装中の範囲）**
   - 設計は「今の INTMS/INTMC の扱いのまま」と言うが、`pci-nvme.c:5457-5458`（enable の後、MSI-X 以外なら `INTMC=1`）を見落としている。この行は既に INTx も覆う。
   - ISR で毎回 INTMS → INTMC すると、別 hart の `nvme_controller_disable`（2879-2880 の INTMS 全部）の後に ISR が INTMC で unmask し直す競合ができる。
   - 修正案: INTx の許可だけを足す（ISR での mask の往復はしない）。往復するなら `command_lock` の中で `controller_enabled` を見てから行う。
2. **#2 は決定済み**（2026-10-18 17:46「板の版・memory は『つないだ時に記録』」、master.md:682）。問いとしてではなく、決定として書く。「gmac0 だけ」は別の技術の既定として分ける。
3. **§0.2 の表の「PLDA の MSI は Linux の pcie-plda-host を参考可」は、決定 13:39 の部品の一覧に無い。** 外すか、別に聞く。
4. **Phase の番号は Master の next には無い**（next は BUG・q・WS だけ、master.md:40）。p020〜p023 は ws.md の次の番号なので「仮」は不要。
   - 記録の食い違いもある: ws.md の p012 の行が「改訂 3」、本文は「改訂 2」。
5. **#10 の既定「開発は (c) TFTP」は loader の道に合わない。** loader は自分を起動した FAT から `vmunix`・`root.img` を読む。TFTP で EFI だけを取っても kernel は SD の古い物のまま。SD の ESP への書き込み（U-Boot の `tftp` の後に `fatwrite`、要確認）などの手順を書くか、既定を (a) にする。
6. **p013 の依存「p005 の skeleton の merge」は、p005 の Phase の側に scoped output として書かれていない**（ws.md の p005 の行は 1 つの Phase）。p005 を分けるか、出力を名指す。p017 も virt の部分と実機を 1 つの Phase に持ったまま、q1271 で「virt の部分」の部分の範囲として走っている。p013/p022 と同じく分けるのが筋。
7. **判定と依存**: p022・p023・p014 の判定は #15 (A) に依る。(A) が却下されると、(B) は p011 の成功、(C) は p018 の後になる。しかし p018 は p023 に依るので、循環に近い。#15 を p022 以降の前提として依存の表に入れる。p023 の「kernel の試験の command」の形（device か test kernel の config か）も未定。
8. **p022 の `clk dump`**: mainline の VF2 の defconfig に `CMD_CLK` は無い（grep で 0 件。Debian の build の構成は未確認）。代わりに U-Boot の `md` で CRG の register を読む道を書く。
9. **PLDA の `allocate_irqs` の bridge をまたぐ swizzle が書かれていない**（root port の下の endpoint。前例は `pci-brcmstb.c:1505-1578`）。
10. **(β) の FLUSH64 の費用**: 1080p の全面の scroll は約 13 万回の MMIO と fence になる。shadow の buffer から描く形を書く。
11. **PCIe の device の DMA の address の bit 数と、PLDA の inbound の窓の値が未設計**（VL805 と NVMe の 32/64 bit、`drv_pci_bus_create_root` に渡す dma device）。
12. **U-Boot の PREBOOT の `usb start` と `PCI_INIT_R`** で、PLDA と VL805 は U-Boot が動かした状態で渡る。p020 の最初の PERST で止めてから学習し直す事と、ExitBootServices での device の remove を確かめる事を、試験に入れる。
13. （範囲外の観察）design.md §2.5 の表の「予約 30 GiB」は、entry 448〜509 なら 62 GiB。

## 確かめて問題が無かった事

- **PCI core の hook の既存の道への影響**
  - `drv_pci_bus_ops` は designated initializer で作られている（`pci-pcat.c:61`、`pci-brcmstb.c:233`）。足す field は NULL になり、amd64・Pi 4 は `!= NULL` の分岐で今の道のまま。
  - 子の bus は `ops`・`host` を親から継ぐ（pci.c:330-331）。root port の下の device にも hook が届く。
  - core の INTx の mask と unmask は 3095・3550・3558 の 3 か所だけで、設計の置き換えの位置と合う。
  - pci.c を host で compile する既存の試験がある（`plan/ws004/tests/run-pci-msi-test.sh`）。
- **B2**: `drv_pci_device_allocate_irqs` は MSIX → MSI → INTX の順（pci.c:1757-1795）。INTx の許可を足しても amd64 の MSI-X の道は変わらない。pci-nvme の許可（1738-1740）、INTMS/INTMC の行（1715・1769-1772・2874-2880）、xHCI の INTx の許可（6459-6464）は記述どおり。
- **M3**: `drv_dma_sync_*` は空（dma.c:706-725）、`drv_dma_map` は coherent の確保の中だけ（633-660）、uncached の確保は weak の `kern_pmem_map_uncached` を通る、hal.h:653-680 の契約、page.c:370 の UNSUPPORTED。どれも記述どおり。
- **M1 の土台**
  - `kern_boot_framebuffer` の 6 つの名前（pmem.c:231-300）と、承認済みの名前 3 つ（guardrail.md:92）は記述どおり。
  - 名前付きの handoff は、Guardrail の「HAL と kernel の受け渡し」（guardrail.md:97-102）に形として合う。loader → HAL の struct を include するのは HAL だけで、P2 の作業中の cmain.c もその形。
  - 新しい port の事前の承認（guardrail.md:93）は WS263〜266 だけなので、#29 の承認が要る事も正しい。
- **coherency の出典**
  - riscv の Kconfig:20 の `select ARCH_DMA_DEFAULT_COHERENT`、`of_dma_is_coherent`（address.c:1001-1019）、OpenBSD sfcc.c の match（fu540・jh7100 だけ）、Linux の ccache の非標準の操作が jh7100 だけである事を確かめた。
  - JH7110 の ccache の compatible は `starfive,jh7110-ccache`、`sifive,ccache0`（jh7110.dtsi:365）。
  - TRM の text の 2097 行（`RWX N`）と 2207 行（`RW A`）。
  - `sha256sum -c SHA256SUMS` は失敗 0。
- **PLDA の DT**: interrupts 56・57、`interrupt-map` が自分の intc を指す、`starfive,stg-syscon` の phandle がある、pcie0・pcie1 とも板で `okay`。PLDA の cfg と窓は device の窓（< 64 GiB）の中に入る。
- **見積もりと決定**
  - LW の合計 59〜96 は表の和と一致する。
  - M4 の pcat.c の関数と `boot0=UUID=` の読み取り（`boot.command-line` から）は実在する。
  - 決定 13:39 の部品の一覧と §0.2 の表は一致する（Minor 3 を除く）。
  - 決定 13:16（Opus 5.5）、決定 2（DTB は U-Boot から）、design.md #8（flash を書き換えない）と、#20 (a) の既定は合う。

## 確かめていない事

- P2 の p004 の riscv64 handoff は未 commit の作業中の版で、変わりうる（MJ3 の handoff の点は、その版での観察）。
- 実機に依る事
  - `ISTATUS_LOCAL` が latch か level か、PLDA の INTx の bit が handler の後に立ち直るか。
  - DC8200 の snoop、JH7110 の DMA の coherency。
  - system port の別名に PMP が掛かるか（MJ7 の危険は推測）。uncached の別名での LR/SC。
- U-Boot に依る事
  - `bootefi <img> <fdt>` の fixup。
  - 配布の U-Boot の GOP と、ExitBootServices の後に画が残るか。
  - U-Boot の efi_gop が framebuffer を EFI の予約の型で出すか、DT に予約を足すか。
  - Debian の u-boot-starfive の `CMD_CLK`。
- 板の構成: VL805 の DMA の bit 数、USB と M.2 がどちらの host に付くか。

## 結論

p012 は、このままでは設計として受け入れられません。

- **p021 の hook は書かれたままでは動かない**: EOI で panic し（BL1）、2 本の host で衝突し（MJ1）、lock の順と drain が無い（MJ2）。
- **p011 の framebuffer は土台が無い**: kernel の console も handoff の field・予約も無く（MJ3）、承認の文面に誤りがあり（MJ4）、U-Boot の分岐が逆（MJ5）。
- **進め方**: これらを直した改訂を再レビューに通します。
- **そのまま進めてよい範囲**: p016（virt の ECAM）は今の設計のままでよい。p017 の virt の部分（q1271 で実装中）は、Minor 1 を当てれば進めてよい。

対象の file:
- `/home/awe/zedBSD-claude1/plan/ws256/design-peripherals.md`
- `/home/awe/zedBSD-claude1/plan/ws256/proposed/hal-riscv64-framebuffer.md`
- `/home/awe/zedBSD-claude1/src/drivers/pci/pci.c`
- `/home/awe/zedBSD-claude1/src/hal/riscv64/irq.c`
- `/home/awe/zedBSD-claude1/src/drivers/pci/pci-nvme.c`
- `/home/awe/zedBSD-worktrees/p2/bootloader/include/riscv64-handoff.h`（作業中の版）
