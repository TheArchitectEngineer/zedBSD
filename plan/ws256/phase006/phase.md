<!-- awesome-plan project=zedbsd record=ws256-p006 -->

# ws256-p006 規約の全文の見直し（WS256 の code）

- ID: ws256-p006、親: [WS256](../ws.md)
- Status: in-progress（前半: 2026-10-19 P2、q1307 / q1307-i01。後半: 2026-10-19 P2、q1310 / q1310-i01。どちらも T1 の回帰を待つ）
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

## 後半（2026-10-19 P2、q1310-i01）

範囲（Q1 の q1310 の依頼）: HAL の riscv64 の残りの全文（前半で機械的な規則だけ直した file と、全文を読んでいなかった file・`.S`・header）と、WS256 が変えた共有の code（WS256 が変えた所だけ。既存の他の部分は触らない）。`include/hal/hal.h` と `include/hal/arch/riscv64.h` は変えない。

### 全文を読んで直した file

HAL（`src/hal/riscv64/`）: `smp.c`・`task.c`・`space.c`・`fdt.c`・`irq.c`・`int.c`・`lib.c`・`plic.c`・`page.c`・`sbi.c`・`clock.c`・`debug.c`・`asm.c`・`font.c`・`bsp-virt/fwcfg.c`・`bsp-virt/framebuffer.c`、`locore.S`・`trap.S`・`dispatch.S`、header の `asm.h`・`sbi.h`・`int.h`・`spin.h`・`font.h`。読んで直す所の無かった file: `percpu.c`・`bsp-virt/board.c`、他の header（`bootinfo.h`・`bsp.h`・`clock.h`・`cons.h`・`defs.h`・`fbcons.h`・`fdt.h`・`irq.h`・`loader.h`・`page.h`・`percpu.h`・`plic.h`・`space.h`・`task.h`・`bsp-virt/*.h`）。
共有の code: `src/drivers/serial/ns16550.c`・`src/drivers/generic/serial-console.c`・`src/drivers/generic/ramdisk.c`（WS256 の新しい file、全部）、`src/drivers/generic/fdt.c`（WS256 が足した `drv_fdt_child`・`drv_fdt_string_index`・`drv_fdt_dma_coherent`）、`src/drivers/pci/pci.c`（INTx の hook の部分）、`src/drivers/pci/pci-nvme.c`（INTx の mask・restore、irq の許可、irq の名前、irq の pass の comment）、`bootloader/uefi/elf64.c`、`src/rtld/rtld.c`（riscv64 の relocation の枝）、`src/libc/crt/crt0-riscv64.S`・`crt1-riscv64.S`・`src/rtld/entry-riscv64.S`・`userland/base/libc/syscall-riscv64.S`、`tools/build/check-dynamic-elf.py`。読んで直す所の無かった物: `src/kern/elf.c` の `EM_RISCV` の枝（macro の 3 行）、`bootloader/include/riscv64-handoff.h`・`riscv64-kernel-image.h`、`src/libc/setjmp-riscv64.S`、libc の riscv64 の枝（`setjmp.c`・`posix.c`・`pthread.c`・`static-tls.c`、既存の arm に合わせた 1〜3 行）。

主な直し: 段落ごとの comment と空行（一つの comment の下の複数の if・loop・代入を分けた、閉じ括弧の後の空行）、最後の文を成功の return に（`riscv64_fdt_s_context`・`riscv64_clock_ticked`・`virt_fwcfg_find`・`virt_fwcfg_write`・`riscv64_virt_to_phys`・irq の `dispatch`・`take_kernel_fault`・ns16550 の `find_uart`・generic fdt の `drv_fdt_child`・`drv_fdt_string_index`。found の印で loop を抜け、loop の後で失敗と成功を分けた）、「Succeeded」の comment の下の代入を前の段落へ、関数の呼び出しを引数・条件・式に入れない（`leaf_entry(…, user_leaf_flags(attr))`、`hal_memset(hal_pmem_to_kernel(…))`、`riscv64_fdt_s_context(riscv64_boot_fdt(), …)`、`__builtin_bswap*` の入れ子、`hal_putc(__builtin_va_arg(…))` など）、3 つ以上の節の条件を行に分けた、split した呼び出しを 1 行 1 引数に（pci-nvme の INTx・`drv_pci_device_allocate_irqs`）、`return f(...)` を変数に受けた（pci-nvme の INTx の枝）、行をまたぐ条件演算子を if に（`nvme_runtime_intx_restore`）、unbraced の多行の else を braces に（rtld の `R_RISCV_TLS_DTPMOD64`）、file scope の変数を一つずつ comment に（sbi の版の 3 つ、ramdisk の 2 つ）、`(void)parameter;` を file ごとの `UNUSED_PARAMETER()` に（ns16550・serial-console・ramdisk）、`.S` の段落の comment（命令は変えていない: 下の逆アセンブルの比較）、font の出典の文を file の頭から表の block の comment へ、pci-nvme の comment の「QEMU's PLIC」を「some PLIC implementations」に（試験の環境の話を source に書かない）、check-dynamic-elf.py の入れ子の条件式を表 `BOOTSTRAP_RELATIVE` に。

### 判断（規則の当てはめ）

- 前半と同じ: lock（`riscv64_irq_save`・`riscv64_spin_lock`・`space_lock_handle`）の critical section の中は一つの段落として残した（style-check の paragraph-comment 15 件はこれ）。`_Static_assert` の 2 引数の割り（第 2 引数を開き括弧の下に揃えた形）は許される形として残した。
- 連続する guard の if（同じ comment の下で `return` するだけの物）は §6 の例と同じ形として残した。
- `src/drivers/pci/pci.c` の `pci_intx_dispatch` の `(void)irq;` は残した: file に `UNUSED_PARAMETER` が無く、同じ file の既存の 2 か所も `(void)irq;` で、既存の部分は触らない範囲のため。
- `include/hal/arch/riscv64.h`（HAL の API の header）は変えていない。規約に合わない所: `hal_atomic_uint_try_acquire` の `return previous == 0U;`（比較の式を返す）と asm の文に comment が無い。直すなら HAL の差分として Q1 に提案する（動作は変わらない書き方だけの差分）。
- 規約の外の気付き（直していない、動作に関わる）: `src/kern/elf.c` の riscv64 の枝の `ELF32_EXPECTED_MACHINE` が `EM_386`（arm64 の枝の写し）。riscv64 では 32 bit の ELF を受けないはずなので `0xffffU` が正しい見込み。書き方ではないので p006 では変えない（Q1 の判断）。

### 確かめ

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| VF2 の vmunix と loader | `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p006-vf2 -j16 vmunix bootloader` | exit 0、`warning:` 0・`error:` 0（build/tmp/p006b-vf2.log、最後の tree） |
| virt（素）の vmunix と loader | `make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/p006-virtbase -j16 vmunix bootloader` | exit 0、warning 0・error 0（build/tmp/p006b-virt.log） |
| virt（PCI 入り） | `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt-pci.mk BUILD=build/p006-virt -j16 vmunix bootloader` | exit 0、warning 0・error 0（build/tmp/p006b-virtpci.log） |
| amd64（UEFI の loader の elf64.c を含む） | `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p006-amd64 -j16 vmunix bootloader` | exit 0、warning 0・error 0（build/tmp/p006b-amd64.log） |
| pcat | `make ZEDBSD_CONFIG=config/ci/config-pcat.mk BUILD=build/p006-pcat -j16 vmunix` | exit 0、warning 0・error 0（build/tmp/p006b-pcat.log） |
| rpi4（pci.c） | `make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p006-rpi4 -j16 vmunix` | exit 0、warning 0・error 0（build/tmp/p006b-rpi4.log） |
| riscv64 の rtld・ld.so・libc の syscall | `make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/p006-virtbase -j16 …/dynamic/obj/src/rtld/rtld.o …/entry.o …/userland/base/libc/syscall.o` と `…/dynamic/ld.so` | exit 0、warning 0（`-Wall -Wextra -Werror`）、ld.so の link で `check-dynamic-elf.py --machine riscv64 --role interpreter` が通った。amd64 の既存の ld.so（build/bug291b-amd64/dynamic/ld.so）にも `--machine amd64 --role interpreter` で exit 0 |
| `.S` の命令が変わっていない | main（a3ca679f9）の版と今の版を `clang --target=riscv64` で assemble し `llvm-objdump -d` を比べた（crt0・crt1・rtld の entry・libc の syscall・HAL の locore・trap・dispatch） | 7 つとも同じ |
| host 試験（WS256） | `sh plan/ws256/tests/<名>/run.sh build/p006b-tests/<名>`（dw-eqos・dw-mmc・ecam・fdt・framebuffer・pci-intx-hook・pci-plda・ramdisk・serial・vf2-audio・vf2-dmatest・vf2-soc）、`loader/run-fdt-hartid.sh`、`native-image/run.sh`、`loader/run.sh … build/p006-vf2/…` と `… build/p006-virtbase/…` | すべて exit 0・PASS（dw-eqos・pci-intx-hook・pci-plda は ASan/UBSan も、native-image は PASS 40 FAIL 0、loader の BOOTRISCV64.EFI は VF2・virt とも OK。log の「FAIL」の語は試験の中の陰性の例の表示） |
| tls-layout | `sh plan/tools/tls-layout/run.sh build/p006b-tests/tls` | exit 0、64 bit・32 bit とも PASS |
| style-check | `python3 plan/tools/style-check.py src/hal/riscv64/*.c src/hal/riscv64/bsp-virt/*.c` と共有の新しい file | HAL は lock の形の paragraph-comment 15 件だけ（上の判断）、ns16550・serial-console・ramdisk・elf64.c は 0 件。scratchpad の走査（split の 2 引数・`return f()`・file scope の変数・禁止の comment）も 0 件（`_Static_assert` を除く） |

- QEMU・実機は未実施（下の T1 の依頼）。

### commit（P2 の worktree、branch agent/p2-q1258、main a3ca679f9 の上）

01e843f26（HAL の smp・space・task）、4df3b637c（HAL の残りの C）、1eaa2054b（HAL の `.S` と header）、5c3ef9c47（共有の code・userland・tools）。この phase.md と ws.md の行は別の commit。

## T1 への依頼文（後半の回帰、Q1 が番号を付けて送る。前半の依頼とまとめてよい）

> WS256 p006 後半（P2、2026-10-19、q1310-i01）。書き方だけの変更（HAL riscv64 の全部、ns16550・serial-console・ramdisk、generic fdt・pci.c の INTx の hook・pci-nvme の INTx、UEFI の elf64.c、rtld の riscv64 の枝、crt・syscall の `.S`）の回帰。tree は Q1 が P2 の 5c3ef9c47 以降を merge した main。login は root/root。前半の依頼（1〜4）と同じ image で一度に流してよい。
> 1. riscv64-virt（直の起動）: 前半の依頼の 1 と同じ。合格: login の prompt と `uname -m` の `riscv64`。画面に `RISCV64 SMP 4 ONLINE` と `RISCV64 HAL READY … PAGING PASS TIMER TICK`（流れたら数えない）。
> 2. riscv64-uboot（U-Boot → BOOTRISCV64.EFI → vmunix）: 前半の依頼の 2 と同じ。合格: login と `riscv64`。
> 3. riscv64-uboot-gop（VF2 の kernel）: 前半の依頼の 3 と同じ。合格: 画面の中央に login の prompt（Q1 が PNG で見る）。
> 4. virt の PCI 入りで NVMe の INTx（pci.c の hook と pci-nvme の INTx の mask・restore の道）: `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt-pci.mk BUILD=build/t1-p006-virt -j16 build/t1-p006-virt/vmunix build/t1-p006-virt/root.img`、disk は `truncate -s 64M`（T1 の dir）、`BOOT_MODE=riscv64-virt INITRD=build/t1-p006-virt/root.img QEMU_EXTRA="-drive file=<disk>,if=none,id=nv,format=raw -device nvme,drive=nv,serial=zt17 -device qemu-xhci -device usb-kbd" BOOT_THEN=$'type:root\nexpect:[Pp]assword\ntype:root\nexpect:^root@\\S+:~\\$\ntype:dmesg | grep nvme\nexpect:INTx' plan/tools/boot-test.sh build/t1-p006-virt/vmunix`。合格: login の prompt と画面の `nvme: PCI controller … INTx`。時間があれば ws256-p017 の依頼の 1.2（dd の書き読み）。
> 5. amd64 の既定の image（UEFI の loader の elf64.c、pci.c・pci-nvme の MSI-X の道の回帰）: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk -j16 build/amd64/hdd-image.img`、`plan/tools/boot-test.sh build/amd64/hdd-image.img`（既定の BOOT_MODE=uefi-nvme）。合格: login の prompt。
> 結果: 各 PNG・PASS/FAIL・tree の commit。FAIL は解析しない（P2 が gdbstub で）。

## 再開の条件

- 前半: T1 の 1〜3 の結果を Q1 が判定（それまで cleared にしない）。
- 後半: T1 の 1〜5 の結果を Q1 が判定（それまで cleared にしない）。前半・後半の両方が合格なら p006 を cleared にできる（WS256 の他の Phase で code が変わったら、その差分だけ同じ見直しを足す）。
- `include/hal/arch/riscv64.h` の書き方（上の判断）と `src/kern/elf.c` の riscv64 の `ELF32_EXPECTED_MACHINE` は Q1 の判断を待つ（p006 の clearance を塞がない）。
