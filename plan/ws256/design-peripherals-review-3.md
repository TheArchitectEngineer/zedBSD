<!-- design-reviewer の再レビュー 3（2026-10-19、design-peripherals.md 改訂 3）。Q1 が報告の本文をそのまま置いた -->

# ws256-p012 改訂 3 の敵対的な再レビュー（3 回目）

読むだけで行いました。source と plan の変更、commit、build、QEMU、rm はしていません。`.internal/` も読んでいません。

照合した物:
- main（`/home/awe/zedBSD-claude1`、HEAD 12182133c）
- 資料 `/home/awe/zedBSD-worktrees/p1/build/tmp/ws256-docs/`
- 対象の文書 `/home/awe/zedBSD-claude1/plan/ws256/design-peripherals.md` と `/home/awe/zedBSD-claude1/plan/ws256/proposed/hal-riscv64-framebuffer.md`

## レビュー 2 の各指摘は直ったか

| 指摘 | 判定 | 理由 |
| --- | --- | --- |
| BL1 | 直った | `intx_line_run` は EOI を送らない。host は W1C の後に EOI を 1 回だけ送る（status が 0 の時、event だけの時も）。riscv64 の dispatch は handler がちょうど 1 回 EOI することを求める（`src/hal/riscv64/irq.c:197-213` と 437-480）。設計はこれに合う |
| MJ1 | 直った | 鍵は (host, vector)。hook の無い host の鍵は NULL で、今の全体の番号の空間のまま。子の bus は ops と host を継ぐ（`pci.c:330-331`）。`cookie->irq` を INTx で `kern_irq_*` に渡す所は他に無い |
| MJ2 | ほぼ直った | ABBA は起きない。drain の窓（host の ISR が `in_flight++` してから `run` に入るまで）も正しく覆っている。ただし Minor 3・4 が残る |
| MJ3 | 一部だけ | 土台（HAL の console、handoff 版 2、予約）は正しい。ただし p011 (1) と (a) が、今の loader の上では成り立たない（Major 1）。版の判定にも穴がある（Minor 1） |
| MJ4 | 直った | 案に `HAL_ERR_NOT_FOUND` は 0 件。形・単位は hal.h:1284-1292、`pmem.c:224-300`、boot.h:56-61 と合う |
| MJ5 | 論理は直った | ただし (a) の成立は Major 1 に依る |
| MJ6 | 一部だけ | 大きさ（64 KiB・1 way）と手順の順は正しい。陽性の対照は「別名が同じ DRAM である」ことを示せない（Major 2） |
| MJ7 | 一部だけ | 確保ごとの写像という方向は正しい。ただし 2 MiB の粒度のため、「OpenSBI・予約の別名に触れない」が成り立たない（Major 2）。`vf2_dma_coherent` にも Minor 5 |
| Minor 1 | 直った | main に入済み（pci-nvme.c:1743-1744、5557-5558、2980）。§3.6 の行の番号と「q1271 で実装中」は古い（Minor 9） |
| Minor 2〜5、7〜12 | 直った | — |
| Minor 6 | 直った | ws.md に p024・p025 がある。図の辺が 1 本足りない（Minor 9） |
| Minor 13 | 直った | design.md は q1275 で既に直っている（design.md:248・539-541） |

## Blocker

無し。

## Major

**1. p011 (1) と #20 (a) は、今の riscv64 の loader では動かない。直しが Phase にも LW にも無い**

- 根拠
  - `bootloader/uefi/bootriscv64.c:708-733` の `find_boot_hart` は `RISCV_EFI_BOOT_PROTOCOL` だけを使う。無ければ error で firmware に戻る（`efi_main` の 169-173 行の "Boot hart"）。
  - `/chosen/boot-hartid` への代わりの道はない。設計 §1.4 の「その時 boot hart は `/chosen/boot-hartid` に頼る」は、実装に無い。
  - riscv64 の loader には GOP の code も無い。build する source は `bootriscv64.c`・`elf64.c`・`volume-discovery.c`・`zedbsd-config.c` だけで（`platform/riscv64/bootloader.mk:16-17`）、video.c を含まない。
- 起こる事
  - §1.4 によれば、配布の U-Boot（2021.10）は protocol を持たない可能性がある。その時 loader は最初の段で止まる。
  - そのため p011 (1) で「見る物」とした「loader の GOP の答え」「ExitBootServices の後に画が残るか」「渡した DTB への fixup」は観察できない。
  - #20 (a) の条件「`/chosen/boot-hartid`（か protocol）」も、loader の側が protocol しか受けない。
  - (1) の LW 1〜2 には code が含まれていない。
- 修正案: p011 (1) に loader の小さな変更を明記する。
  - protocol が無い時は、EFI の DT の `/chosen/boot-hartid` を読む。どちらを使ったかを console に出す。
  - GOP の今の mode を読んで答えに出す（mode は変えない）。
  - LW は +1〜2。(2) (a) の GOP の取り込みを (1) に前倒しするのでもよい。

**2. system port の別名（物理 + 0x4_0000_0000）に依る 3 つの設計（予備 (i)、p011 の (α)、§2.4 の陽性の対照）の前提が確かめられていない。予備 (i) の安全の主張は 2 MiB の粒度で崩れる**

- 根拠（粒度）
  - device の窓は 2 MiB の leaf でしか写さない（`src/hal/riscv64/space.c:1367-1389` の `map_device_blocks`、`hal_space_map_device` の 916-975 行）。unmap は何もしない（990-1010 行付近）。
  - OpenSBI は DDR の先頭 0x4000_0000 にある（design.md:140）。同じ 2 MiB の残りは allocator が渡しうる。
  - そこから DMA の確保が出ると、別名の 2 MiB の leaf が OpenSBI の別名（0x4_4000_0000〜）も写す。§2.4 の (i) の 4「OpenSBI・予約の別名は写さない」と矛盾する。
  - PMP は物理 address で掛かるので、別名の側を守るかは未確認（§7）。
- 根拠（陽性の対照）
  - §2.4 の 0 は「古い模様が読めれば CONTROL OK」とする。別名が同じ DRAM を指さない場合（書き込みが別の所へ行く、黙って捨てられる）も、古い模様が読めて OK になる。
  - 別名が同じ DRAM である事は §7 の未確認の一覧にも無い。それなのに (i) と (α) はこれを前提にしている。
  - INCONCLUSIVE の時の次の手が無く、p023 が止まったままになる。主の試験（手順 1〜3）は別名無しでも非 coherent を検出できるので、ここで止める必要は無い。
- 起こる事: 非 coherent と出て (i) を入れた時に、次の 2 つが起こりうる。
  - 別名の取り違えによる DMA の memory の破壊。
  - S-mode から firmware の memory への写像が残る。
- 修正案
  - 対照に「別名で書く → FLUSH64 → cacheable で読み、新しい模様である事」を足す。別名が同じ DRAM である事を先に示す。
  - INCONCLUSIVE の時は、主の試験だけで判定する（別名は分類にも使わない）と決める。
  - (i) は、予約・nomap・OpenSBI の範囲と重ならない 2 MiB の block からだけ確保する（専用の pool）。または device の窓に 4 KiB の段を足す（src/hal の実装の変更）。どちらかを書く。

## Minor

1. **handoff 版 2 の読み分けが、今の HAL の判定で壊れる**（`src/hal/riscv64/cmain.c:159-162`）
   - `version < ZBL6_RISCV64_HANDOFF_VERSION` と `size < sizeof(*handoff)` は、VERSION を 2 に上げて field を足すと、版 1 の loader の handoff を HAL_FATAL にする。
   - header の約束（`riscv64-handoff.h:30-37`）と §3.1 (a) に反する。#10 (c) の「vmunix だけを TFTP で更新」で、現に起こる。
   - 修正案: 判定を `version < 1` と「版 1 の終わり（offsetof(parameters)+sizeof）」に変えると、p011 (2) (a) に明記する。
   - 配置は問題ない: 版 1 は 3160 byte（8 の倍数）、版 2 は約 3456 byte で、1 page（bootriscv64.c:208-212）に収まる。`_Static_assert` で守るとよい。
   - `reserved_flags` の意味が書かれていない。
2. **`reserved[]` の上限は DT と共有**（`fdt.h:20・85-91` の `RISCV64_FDT_MAX_RESERVED`=16）
   - loader の 16 個と DT の `/reserved-memory` を同じ配列に入れる。UEFI の map の EfiReservedMemoryType は DT の予約の写しになり、二重に数えられる見込み（推測）。
   - そのため、正常な板でも HAL_FATAL に届きうる。重なる物をまとめるか、配列を分ける。
   - 「16 を超えたら画面に出して止める」の判定は、最初の ExitBootServices の前に行うと書く（bootriscv64.c:1014-1027 の再試行の中では console を使えない）。
3. **p021 の host の lock の文脈**: `intx_disestablish` は thread の文脈で host の lock を取る。その間の割り込みを止める（irq-save）と明記する。止めないと、boot hart の上で PLDA の ISR が同じ lock を待ち続ける（PLIC の source は全部 boot hart が受ける: irq.c:16-17）。
4. **p021 の試験 4 の期待が実装と合わない**
   - `run` の中に handler が居る間は `line->dispatching != 0` なので、core が先に EBUSY を返す（`pci.c:3528-3533`）。host の待ちには届かない。§3.5 の 5 の例「handler の中から解除」も同じ理由で、host の上限には届かない。
   - host の drain を試すには、fake の host を `in_flight++` と `run` の間で止める。
   - 新しい runner は `run-pci-msi-test.sh` の `mktemp`＋`rm -rf` の trap を写さない（AGENTS.md「試験の script の中の host の rm は禁止」）。
   - 既存の `plan/ws004/tests/pci-shared-intx-test.c` 等が壊れた時に、直すか消すかを決めておく（Master の一覧には無い）。
5. **`vf2_dma_coherent()` の置き場所と意味**
   - 汎用の `src/drivers/pci/pci-plda.c`・`src/drivers/dma/dw-axi-dmac.c`（§2.1）が `src/drivers/platform/visionfive2/` を呼ぶと、層が逆になる。
   - 「Linux の `of_dma_is_coherent` と同じ意味」と言うが、node だけを見ていて親を辿らない（address.c:1010-1019 は DMA の親を辿る。JH7100 の DT は soc の node に `dma-noncoherent` を置いた例）。
   - 既存の `pci-ecam-fdt.c:160-162` は「`dma-coherent` が無ければ 0」で、意味が逆。
   - 修正案: 板の既定を引数に取る汎用の FDT の helper にして、platform（visionfive2.c）から値を渡す。
6. **(α) の写像の時期**: (α) で描く別名は、`riscv64_space_init` で kernel の root に切り替える前に `map_hal_devices`（space.c:1350-1365）で写す必要がある。今の boot table は低い 64 GiB を全部写す（locore.S:100-108）が、切り替えの後は写していない所で fault し、HAL_FATAL の表示も同じ所で fault する。(β) の FLUSH64 は ccache が既に写されるので問題ない。
7. **(α) の nomap の範囲**: `hal_space_map_device` は、nomap でも `/memory` の中なら direct map の address を返す（space.c:948-952、page.c:176-204）。そのため、後の WS269・WS270 が `riscv64.framebuffer.phys` を写すと、写していない address を受け取る。(α) を選ぶ時の注意として書く。
8. **p025 の skeleton は link しない**
   - BSP は `riscv64_board_*` の 3 つの他に、`hal_dcache_clean_range` などを持つ（bsp-virt/board.c:100-138。§1.5 の「口は 3 つだけ」は不正確）。`src/kern/dcache.c:26・38` が呼ぶので、p025 に stub か FLUSH64 の実装が要る。
9. **記録の食い違い**
   - ws.md の p006 は「p011〜p019 を含む」のままで、p020〜p025 が規約の Phase の範囲に無い。
   - §4.1 の図に p025 → p005 の辺が無い（ws.md の p005 は p025 に依る）。
   - §4.2 の段 1 の「p016 → p017（q1271）」と §3.5 p016 の「riscv-virt.c は ns16550 だけ」は古い（p016 は cleared、`pci-ecam-fdt.c` は入済み）。
   - §3.6 の行の番号（1738・5457・2879）と「q1271 で実装中」は古い（今は 1743・5558・2980、ws.md では p017 が T1-723 の FAIL の後、q1274）。そのため #31 は「決める問い」ではなく「済んだ事の報告」になっている。
   - `master.md:682` は今 691 行。
   - ws.md の Phase の表の見出し「design.md §6 の写し（改訂 2）」と実行の順の文に、p020〜p025 が反映されていない。
10. **p021 の回帰に riscv64 の virt を入れる**: EOI の検査が厳しいのは riscv64 だけ（2 回目で HAL_FATAL）。`pci_intx_dispatch` の分け方を変えるので、T1-723 の走り B（virt の usb-kbd）を回帰に足す。
11. **PLDA の ISR は、core が mask した pin も配りうる**: `ISTATUS_LOCAL` が mask の外の bit も示すなら（未確認）、`status & 許可した pin` で配る事を書く。vector 1〜4 と bit 24〜27 の対応（`IMASK_INT_INTA << (vector-1)`、obsd-stfpcie.c:53-57・594-641）も明記する。
12. **§2.4 の細部**
    - 「別名は試験の間だけ写す」は誤り。窓の leaf は外さない（space.c:990 付近）。
    - xHCI・GMAC・SDIO の「短い版」の手順（何を src→dst にするか）が決まっていない。
    - p023（P1）と p019（P2）が `dw-axi-dmac.c` を共有するのに、§4.2 の共有の file の一覧に無い。
13. **#15 の問いの書き方**
    - (A) は AGENTS.md の「serial log を読んで判定しない（解析・回帰・受け入れのどれでも）」の例外にあたる。「log は読まない」という言い方は誤解を招くので、「serial の出力の答えの文字列で判定する例外」と書く。
    - 決定 9（sifive_u は gdbstub で読む）と同じ系統の (D) JTAG（OpenOCD＋gdb で memory の印を読む）を、選択肢に足す事を勧める。

## 確かめて問題が無かった事

- **PCI core（pci.c）**
  - `pci_intx_line`（177-183）、`establish_intx`（3037-3108）、`find_intx_line`（3128-3145）、`disestablish_intx`（3488-3589）、`pci_intx_dispatch`（3606-3644）は、§1.5 の記述どおり。
  - mask・unmask の位置は 3095・3550・3558。
  - INTx の確保は MSIX → MSI → INTX の順（1759-1795）。
  - designated initializer（`pci-pcat.c:61`、`pci-brcmstb.c:233`）。
  - brcmstb の swizzle の前例（1505-1578）。
  - DT の `interrupt-map-mask` は pin だけ（jh7110.dtsi:1236・1279）で、swizzle が要る事と合う。
- **p021 の lock の順**: establish・mask・unmask は `intx_lock` → host の lock。ISR は host の lock を外してから `intx_lock` を取る。ABBA は無い。最後の解除の EBUSY で戻る道は、今の core の失敗の道と合う。
- **OpenBSD の手順**: attach の時に IMASK を 0、ISTATUS を全部 W1C、INTx は handler の後に W1C（obsd-stfpcie.c:364-365・747-769）。設計と合う。
- **HAL と kernel の境界**
  - `hal_get_arch_handoff` は 1 引数で、無い時 NULL（hal.h:1284-1292）。
  - `kern_boot_framebuffer` の 6 つの名前（pmem.c:224-300）。
  - `KERN_BOOT_FRAMEBUFFER_*`（boot.h:56-61）。
  - riscv64 の bootinfo.c の名前（approved の proposed/hal-riscv64.md:209 と合う）。
  - #29 の文面は Guardrail の「HAL と kernel・boot loader と HAL の受け渡し」と決定 11（#9 (a)）に合い、hal.h を変えない。
- **DMA**
  - `hal_pmem_map_uncached` の契約（hal.h:653-680）、page.c:370 の UNSUPPORTED。
  - `allocation_map` は ENOTSUP で見える失敗になる（dma.c:995-1020）。
  - riscv64 は `uncached.c` を build する（vmunix.mk:67）。
  - brcmstb は coherent=0、pcat は 1。
- **HAL の写像**
  - device の窓は 64 GiB で、別名 0x4_4000_0000〜0x6_3FFF_FFFF はその中。
  - ccache は `map_hal_devices` で写される。そのため (β) の FLUSH64 は、切り替えの後も届く。
  - early console（board_init）は page_init の前で、boot table の direct map と device の窓で描ける。
- **資料の事実**
  - TRM Table 2-4・2-5 の DDR の 2 窓。
  - ccache 2 MiB・2048 set・64 byte（jh7110.dtsi:364-373）。
  - `dma-noncoherent` は 0 件。
  - riscv の Kconfig:20、`of_dma_is_coherent`。
  - mainline の defconfig の PREBOOT（44 行）と `PCI_INIT_R`（51 行）、`CMD_CLK`・`FAT_WRITE`・VIDEO は 0 件。
  - pcie0・pcie1 は `okay`。PERST 26・28、card detect 41、gpio-restart 35。
  - syscon の address（0x13030000・0x10240000・0x17010000）。
- **(β) の費用の計算**: 2560 byte × 400 行 / 64 = 16,000。x offset は 2560 byte で 64 に揃う。
- **LW**
  - 合計 68〜106 は §4.1 の表の和と一致する。
  - 改訂 2（git 82cf186b3 の表）からの差 +7〜8（p011）・+2（p021）、p017＋p024 = 改訂 2 の p017（6〜8）も一致する。
  - ws.md の p011・p013〜p025 の依存と LW は §4.1 と一致する（p005・p006 は Minor 9）。
- **決定との整合**
  - 決定 13:39 の部品の一覧と §0.2。
  - GPL の値の引用は 2026-10-15 に全部の WS へ広がっている（guardrail.md「GPL にも広げる」）ので、§0.2 の DW MMC の Linux の値も可。
  - #2（17:46）、#28 と決定 10、#21 と決定 2、#20 と design.md #8（flash を書き換えない）。
  - #30 は HAL ではなく、承認は要らない。

## 確かめていない事

- 次の 4 点は資料で確かめられなかった。
  - 別名が同じ DRAM を指すか。
  - 別名に PMP が掛かるか。
  - `ISTATUS_LOCAL` が latch か level か、mask の外の bit を示すか。
  - ccache の FLUSH64 が L1 の dirty な line も書き出すか（inclusive と推測）。
- 配布の U-Boot 2021.10 に `RISCV_EFI_BOOT_PROTOCOL` が backport されているか（Major 1 の起こりやすさ）。
- U-Boot の efi_gop が framebuffer を、EFI の map のどの型で出すか。
- VF2 の実際の `/reserved-memory` と EFI の map の数（Minor 2 は推測）。
- driver が使える 1 秒の待ちの timer の API。
- build と試験は Q1 の指示で行っていない（LLVM の作り直し中）。

## 結論

p012 を設計としてそのまま受け入れるのは、まだ不可です。

- Blocker は無く、レビュー 2 の BL1・MJ1・MJ4・MJ5 は直りました。
- p011 (1)／#20 (a) の loader の前提（Major 1）と、system port の別名の前提と 2 MiB の粒度（Major 2）を直す小さな改訂の後なら、受け入れてよいです。
- その間も、p021・p025・p013 は今の設計で着手してかまいません（Minor 3・4・5・8 を当てる）。
