<!-- awesome-plan project=zedbsd record=ws256-p006 -->

# ws256-p006 規約の全文の見直し（WS256 の code）

- ID: ws256-p006、親: [WS256](../ws.md)
- Status: in-progress（前半: 2026-10-19 P2、q1307 / q1307-i01。後半は未着手）
- Disposition: normal
- 範囲: WS256 で足した・変えた code を [plan/coding-style.md](../../coding-style.md) の全文と [Guardrail](../../guardrail.md) の出典の規則（block ごとに「Value(s) referenced:」「Procedure referenced:」、file の頭に出典を書かない）に照らして直す。書き方だけで、動作は変えない。`include/hal/hal.h` は変えない。`src/kern/vm/` は P1 の作業中なので触らない。
- 道具: `python3 plan/tools/style-check.py`（機械的な規則）。加えて P2 の scratchpad の走査（split した呼び出しの 1 行に引数 2 つ、`return f(...)`、file scope の変数の comment、禁止の comment の形）。

## 前半（2026-10-19 P2、q1307-i01）

### 判断（規則の当てはめ）

- `EFI_ERROR(status)` は bitwise の macro で関数の呼び出しではない。bootx64.c と同じ形として残した（style-check は call-in-condition として数える、39 件）。
- dw-eqos の attach の `goto fail`（8 件）は「共有の後始末の label への前向きの jump」として残した。
- 条件演算子は短い対称の文字列の選びだけ（10 件）なので残した。
- HAL の `riscv64_irq_save`・`space_lock_handle` の critical section の空行（style-check の paragraph-comment、12 件）は lock の形として残した（checker の正規表現が名前を知らない）。
- `_Static_assert` は他の HAL（amd64・sparcv9・ppc64）と同じ予約名の拡張として残した。
- `(void)parameter;` は各 file に `UNUSED_PARAMETER()` を定義して置き換えた（driver は file ごとに定義する既存の形: usb-bt.c など）。
- source と build の規則の中の plan の ID（`(ws256-p019)` など）は、WS の完了で phase の directory が消えると宙に浮くので消した。

### 全文を読んで直した file

bootloader/uefi: `bootriscv64.c`・`fdt-hartid.c`・`riscv64-reserved.c`・`riscv64/start.S`（file の頭の Procedure referenced を `_start` の block へ）。
HAL: `bsp-visionfive2/board.c`（Values referenced を関数の中から定数の block へ）・`cons.c`・`fbcons.c`・`loader.c`・`bootinfo.c`・`cmain.c`。
kernel: `src/kern/platform/visionfive2.c`・`riscv-virt.c`、`src/drivers/platform/riscv-virt/virt-pci.c`。
driver: `pci-plda.c`・`pci-ecam-fdt.c`・`dw-axi-dmac.c`・`dw-mmc.c`・`dw-eqos.c`・`mii-phy.c`・`yt8531.c`、`src/drivers/platform/visionfive2/` の全部（crg・dmac・dmatest・dmatest-core・ethernet・mmc・pcie・pinctrl・pwmdac・syscon）。
tools: `platform/riscv64/tools/check-bootriscv64.py`・`make-bootriscv64.py`・`make-riscv64-disk-image.py`（module の docstring の出典を定数・関数の block の comment へ。check の 3 つの literal を名前付きの定数に）、`platform/riscv64/vmunix.mk`・`disk-image.mk`（plan の ID だけ）。

主な直し: 一つの comment の下の複数の失敗し得る操作を段落ごとに分けた（vf2-pcie の `if (error == 0) error = …` の連鎖を個別の確認に、vf2-ethernet の rate の読みも）、閉じ括弧の後の空行と段落の comment、split した呼び出しを 1 行 1 引数に（pci-plda・pci-ecam-fdt・dw-axi-dmac・vf2-dmatest・vf2-pwmdac・bootriscv64）、`return f(…)` を変数に受けて返す（vf2-dmatest の put_failure、HAL fdt.c の read_cells・read_number）、最後の文を成功の return に（bootriscv64 の exit_boot_services、fdt-hartid の fdt_name_is、pci-plda の host_intx_disestablish、pci-ecam-fdt の describe_memory・host_allocate_irqs、dw-axi-dmac の wait_idle、vf2-crg の reset_change・generator_of、vf2-pinctrl の bank_of、vf2-pcie の choose_group）、条件の中の呼び出しを外へ（HAL space.c の detach_empty_tables の pointer_entry、純粋な関数）、関数の呼び出しを引数にしない（vf2-pcie の tune_phy、vf2-pwmdac の drv_audio_register）、比較を引数にしない（dw-mmc の disk_submit の write）、3 つ以上の節の条件を行に分けた、file scope の変数の一つずつに comment（HAL の bootinfo・clock・cons・fbcons・page・plic・sbi・space・task・bsp-visionfive2、kernel の platform 2 つ、vf2-dmac・vf2-dmatest）、残っていた comment の食い違い（riscv-virt の refresh が「nothing to do」と言いながら PCI を見る）。

### 機械的な規則だけ直した file（後半で全文を読む）

HAL: `space.c`・`task.c`・`fdt.c`・`smp.c`・`irq.c`・`int.c`・`lib.c`・`plic.c`・`page.c`・`sbi.c`・`clock.c`・`debug.c`、`bsp-virt/board.c`。全文を読んでいない: `asm.c`・`percpu.c`・`font.c`・`bsp-virt/fwcfg.c`・`bsp-virt/framebuffer.c`、`.S`（locore・trap・dispatch）、HAL の header。

### 確かめ

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| VF2 の vmunix と loader | `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p006-vf2 -j16 vmunix bootloader` | exit 0、`warning:` 0・`error:` 0（build/tmp/p006-vf2.log、最後の f712747d0 の tree） |
| virt（PCI 入り）の vmunix と loader | `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt-pci.mk BUILD=build/p006-virt -j16 vmunix bootloader` | exit 0、warning 0・error 0（build/tmp/p006-virt.log、2c4f75611 の tree。その後の変更は fdt.c・bootriscv64.c・tools・start.S で、下の virt の素の build で compile した） |
| virt（素）の vmunix と loader | `make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/p006-virtbase -j16 vmunix bootloader` | exit 0、warning 0・error 0（新しい BUILD で全部 compile、最後の tree） |
| amd64 | 未実施 | 共有の source（pci.c・drivers/generic/fdt.c・hal.h）は変えていない（`git diff eff345d58 --stat`） |
| host 試験 | `sh plan/ws256/tests/<名>/run.sh build/p006-tests/final-<名>`（dw-mmc・vf2-soc・vf2-audio・dw-eqos・pci-plda・pci-intx-hook・framebuffer・ecam・vf2-dmatest）、`native-image/run.sh build/p006-tests/final`、`loader/run.sh … build/p006-vf2/vmunix build/p006-vf2/uefi/BOOTRISCV64.EFI build/p006-vf2/uefi/bootriscv64.elf`（virt の素でも）、`loader/run-fdt-hartid.sh`、`fdt/run.sh`・`ramdisk/run.sh`・`serial/run.sh` | すべて PASS（dw-eqos・pci-plda・pci-intx-hook は ASan/UBSan も、native-image は PASS 40 FAIL 0、loader の check-bootriscv64.py は VF2・virt の両方で OK） |
| style-check | `python3 plan/tools/style-check.py <範囲の .c>` | 150 件 → 69 件（call-in-condition 39 は EFI_ERROR、conditional 10、goto 8、paragraph-comment 12 は上の判断） |

- `plan/ws256/tests/plic/run.sh` は前から FAIL（`bootinfo.h` が `loader.h` を include するのに試験が `loader.h` を写さない。p006 の変更の前から、bootinfo.h・loader.h は変えていない）。master.md の一覧にも未完了の Phase にも無いので、試験の整理の基準では削除の候補（Q1 の判断）。
- QEMU・実機は未実施（下の T1 の依頼）。

### commit（P2 の worktree、branch agent/p2-q1258、main eff345d58 の上）

df62e19b8（loader）、fdf69e96c（HAL の変数・pci・dmatest など）、152d8691d（HAL の mechanical・dw-axi-dmac・platform の kernel・plan の ID）、e350e857a（vf2 の glue）、180a2fe32（vf2-pinctrl・vf2-crg）、2c4f75611（dw-mmc・dw-eqos）、f712747d0（HAL fdt.c・bootriscv64・start.S・tools）。この phase.md と ws.md の行は別の commit。

## T1 への依頼文（前半の回帰、Q1 が番号を付けて送る）

> WS256 p006 前半（P2、2026-10-19、q1307-i01）。書き方だけの変更（loader・HAL riscv64・VF2 の driver・PLDA・ECAM・DW AXI DMAC・DW MMC・DW EQOS・platform の kernel）の回帰。tree は Q1 が P2 の f712747d0 以降を merge した main。login は root/root。
> 1. riscv64-virt（直の起動）: `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt.mk -j16 build/riscv64-virt/vmunix build/riscv64-virt/root.img`、`BOOT_MODE=riscv64-virt INITRD=build/riscv64-virt/root.img BOOT_THEN=$'type:root\nexpect:[Pp]assword\ntype:root\nexpect:^root@\\S+:~\\$\ntype:uname -m\nexpect:^riscv64\\s*$' plan/tools/boot-test.sh build/riscv64-virt/vmunix`。合格: login の prompt と `riscv64`。画面に `RISCV64 SMP 4 CPUs`（流れたら数えない）。
> 2. riscv64-uboot（U-Boot → BOOTRISCV64.EFI → vmunix、loader の変更の確かめ）: `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt.mk -j16 build/riscv64-virt/vmunix build/riscv64-virt/root.img build/riscv64-virt/uefi/BOOTRISCV64.EFI build/riscv64-virt/disk.img`、`BOOT_MODE=riscv64-uboot BOOT_THEN=$'type:root\nexpect:[Pp]assword\ntype:root\nexpect:^root@\\S+:~\\$\ntype:uname -m\nexpect:^riscv64\\s*$' plan/tools/boot-test.sh build/riscv64-virt/disk.img`。合格: login と `riscv64`。画面の上の方に `RISCV64 LOADER handoff=…` が出れば loader の道を通った印（流れたら数えない）。
> 3. riscv64-uboot-gop（VF2 の kernel、GOP の画面と ramdisk の道。PLDA・DMAC・PWMDAC・EQOS・MMC の attach の道を通る）: `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk -j16 build/visionfive2/vmunix build/visionfive2/root.img build/visionfive2/uefi/BOOTRISCV64.EFI build/visionfive2/disk.img`、`BOOT_MODE=riscv64-uboot-gop BOOT_PATTERN=$'^(?:\\S+ )?login: ?' plan/tools/boot-test.sh build/visionfive2/disk.img`。合格: 画面の中央に login の prompt（Q1 が PNG で見る）。
> 4.（任意、3 の後に時間があれば）virt の PCI 入り（ECAM・xHCI・NVMe の INTx の道）: `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt-pci.mk BUILD=build/t1-p006-virt -j16 build/t1-p006-virt/vmunix build/t1-p006-virt/root.img`、`BOOT_MODE=riscv64-virt INITRD=build/t1-p006-virt/root.img QEMU_EXTRA="-device qemu-xhci -device usb-kbd" plan/tools/boot-test.sh build/t1-p006-virt/vmunix`。合格: login の prompt。
> 結果: 各 PNG・PASS/FAIL・tree の commit。FAIL は解析しない（P2 が gdbstub で）。

## 後半（未着手）

- 上の「機械的な規則だけ直した file」と、全文を読んでいない HAL の file・`.S`・header を全文で読む。
- WS256 が変えた共有・周辺の code で、この依頼の一覧に無かった物（範囲を Q1 と確かめる）: `src/drivers/serial/ns16550.c`（RX）・`src/drivers/generic/serial-console.c`・`ramdisk.c`・`drivers/generic/fdt.c` の `drv_fdt_dma_coherent`・`drivers/pci/pci.c` の INTx の hook・`pci-nvme.c` の INTx と irq の pass（d0869aca7）・`src/kern/elf.c` の `EM_RISCV`・userland の riscv64 の `.S`（crt0・crt1・setjmp・syscall）・libc・rtld の riscv64 の枝・`check-dynamic-elf.py`・`bootloader/include/riscv64-*.h`・`bootloader/uefi/elf64.c` の arch の書き分け。共有の file を触ったら amd64 の build も。
- 後半の後に T1 の回帰（今回と同じ 1〜3、共有の file を変えたら amd64 の既定の image も）。
- `plan/ws256/tests/plic/` の扱い: 2026-10-19 Q1 が試験の整理の基準で削除した（前から FAIL、どこからも参照されない）。

## 再開の条件

- 前半: T1 の 1〜3 の結果を Q1 が判定（それまで cleared にしない）。
- 後半: Q1 が次の i を割り当てた時。上の「後半」の一覧から始める。

## 前半の T1 の結果（2026-10-19 Q1）

T1-742: riscv64-virt・riscv64-uboot・riscv64-uboot-gop（VF2 の kernel）・virt の PCI 入りが全部 PASS（QEMU だけ）。前半の書き方の直しで振る舞いは変わっていない。後半（q1310）の後に p006 を判定する。
