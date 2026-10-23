<!-- awesome-plan project=zedbsd record=ws286-p004 -->

# ws286-p004 ARMv7-A の HAL の入口（QEMU virt、1 CPU）

- 親: [ws286](../ws.md)、設計: [design.md](../design.md) 版 3 の §2.2・§3・§4.6・§7.1・§7.2・§7.6・§8 の p004 の行、boot-test の案: [proposed/boot-test-armv7.md](../proposed/boot-test-armv7.md) 版 3。
- Queue: Q1 の割当（P2、2026-10-23、worktree `/home/awe/zedBSD-worktrees/p2-zybo-p004`、branch `agent/p2-zybo-p004`）。前の担当が誤って止められた後、Q1 が作業を 3b7ccc090（本体）・4db317270（未 commit だった gic.c と host 試験 2 つ）に救い、P2 が新しい context で続けた（ユーザー「WS286を間違ってキャンセルしてしまったので、サルベージして継続をお願いします。」）。
- 判断: U1（HAL の事前承認の 3 点、Guardrail 2026-10-23 の行）。boot-test.sh の `armv7-virt` mode は Q1 の許可（2026-10-23 の割当）。
- Status: cleared（2026-10-23 Q1 の判定: 受け入れの 4 条件を満たした。残り: hal_pmem_map_uncached（p006 まで）、zybo の config と bsp（p005））

## 受け入れの条件（design §8 の p004、§7.1・§7.2）

1. `config/ci/config-armv7-virt.mk` の vmunix が warning 0。共有の code を変えたので `config-amd64.mk`・`config-rpi4.mk` の vmunix も warning 0。
2. §7.1 の link の確かめ: `e_entry` 0x40010000、`_start` が VMA の先頭 0x80010000、`vmunix.bin` の大きさ = `_edata - _start`、`.ARM.exidx` が無い、未定義の symbol・`__atomic_*`・`__sync_*` の呼び出しが無い。
3. host 試験 `plan/ws286/tests/aeabi-host-test.c`・`pte-host-test.c` が PASS。
4. QEMU virt（T1、`BOOT_MODE=armv7-virt`、PNG）の画面に `ARMV7 HAL READY AEABI PASS ATOMIC PASS PAGING PASS TIMER TICK`。gdb で入口の `r2` が DTB（`x/wx $r2` = `0xedfe0dd0`）、SVC mode。

## 作った物（3b7ccc090・4db317270 は前の担当、da63481dd 以降は P2）

- `include/hal/arch.h` の armv7 の節、`include/hal/arch/armv7.h`（U1）。
- `src/hal/armv7/`: locore・vectors・dispatch・aeabi.S・aeabi-divide.c・cmain・bootinfo（`boot.command-line`・`armv7.fdt`・`armv7.initrd`）・fdt・page・pte.h（影の table）・space（ASID、device 窓）・int・irq・gic・clock・smp（1 CPU）・percpu・task・cache・debug・lib・selftest（AEABI・ATOMIC・PAGING）、`bsp-virt/`（PL011、PSCI、generic timer、fw_cfg、ramfb、console・font）。
- `platform/armv7/`（toolchain.mk・virt.mk（PHYS_BASE 0x40000000、LMA 0x40010000、VMA 0x80010000）・vmunix.mk（`-DKERN_USER_ABI_ARMV7`）・vmunix.ld・run.mk・Makefile）、`Makefile`・`tools/menuconfig.py` の `armv7-virt`、`config/ci/config-armv7-virt.mk`、`include/kern/elf.h` の `EM_ARM`、`src/kern/elf.c` の armv7、`include/uapi/stat.h` の枝と `#error`。
- P2（da63481dd）: `plan/tools/boot-test.sh` に `BOOT_MODE=armv7-virt`（案の 5 か所、他の mode は不変）。`space.c` の `move_to_kernel_table` の順を「TTBR1 → TTBCR.N=1 → CONTEXTIDR → TTBR0」に直した（前の順では TTBR0 を 8 KiB の空の table にした直後、N=0 のまま table walk が上半分を引くと table の外を読む。QEMU では起きないが実機で起き得る）。`locore.S` に BSS を 0 にする前の「`r2` が image の中なら停止」を足した（design §2.2）。
- 救った gic.c の差分の確かめ: 全部の line を Group 0 に置き、両 group を有効、GICC_CTLR に AckCtl。virt（security extension 無し）でも zybo（Secure）でも正しく、NS で security extension の有る GIC では group の書き込みが無視されるだけ。そのまま採った。
- 未実装（design との差）: §7.6 PAGING の (7)（direct map と uncached の alias の device 窓の書き込み）は selftest に無い。p005 以降で足すか、Q1 の判定で決める。zybo の config（`config-zybo.mk`・bsp-zybo）は p005。

## 確認（2026-10-23 P2、host）

| command | 結果 |
| --- | --- |
| `make ZEDBSD_CONFIG=config/ci/config-armv7-virt.mk BUILD=build/p2-check -j16 vmunix`（空の BUILD から） | rc 0、warning 0 |
| `llvm-readelf -h/-lS`、`llvm-nm` | entry 0x40010000、`_start` 0x80010000、`.text` AX・`.rodata` R・`.data/.bss` RW の 3 つの LOAD、exidx 無し、`vmunix.bin` 991824 byte = `_edata`(0x80102250) − `_start`。`_end` 0x807bb000（BSS 6.9 MB） |
| link の未定義の確かめ（vmunix.mk の規則） | 未定義 0。`__atomic_*`・`__sync_*` 無し。定義した `__aeabi_*`: uidiv・uidivmod・idiv・idivmod・uldivmod・ldivmod・memcpy/memmove/memset/memclr{,4,8}・idiv0・ldiv0（llsl・llsr・lasr は要らなかった） |
| `cc -m32 … aeabi-host-test.c src/hal/armv7/aeabi-divide.c` | PASS |
| `cc -m32 … pte-host-test.c`（`ARMV7_PHYS_BASE` 0x40000000 と 0x0） | 両方 PASS |
| `bash -n plan/tools/boot-test.sh` | OK（shellcheck は host に無く未実施） |
| `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p2-amd64 -j16 vmunix`（`build/NoctLang` は main の共有の物への symlink） | rc 0、warning 0、kernel include check PASS（553 objects）、amd64 vmunix check PASS |
| `make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p2-rpi4 -j16 vmunix` | rc 0、warning 0、arm64 vmunix check PASS（entry 0xffff000000080000） |

## T1 への依頼（2026-10-23、Q1 経由）

- A: `BOOT_MODE=armv7-virt BOOT_PATTERN='ARMV7 HAL READY AEABI PASS ATOMIC PASS PAGING PASS TIMER TICK' plan/tools/boot-test.sh build/armv7-virt/vmunix.bin`（worktree の中で）。
- B: 同じ command に `QEMU_EXTRA='-S -gdb tcp::1234'`、gdb-multiarch で `hbreak *0x40010000`・`continue`・`p/x $r2`・`x/wx $r2`・`p/x $cpsr`。
- 結果（2026-10-23、T1、Q1 経由、QEMU の証拠）: A PASS（約 1 s で pattern を見た）。画面: `ARMV7 GIC distributor=8000000 cpu=8010000 secure=0`・`GIC lines=288`・`ARMV7 TIMER TICK ticks=10 counter=62500000 Hz irq=30`・`ARMV7 PAGING PASS asid=1,2`・`ARMV7 HAL READY AEABI PASS ATOMIC PASS PAGING PASS TIMER TICK`・`ARMV7 CPU 1 ONLINE`・`HAL initialized successfully. [cpu 1, memory 512MB, timer 1ms]`、その後 root が無いので `VFS initialization failed (12); entering idle.`（想定どおり）。PNG `/home/awe/zedBSD-claude1/build/review/ws286/p004-runA.png`（P2 が見て確かめた）。B PASS: `hbreak *0x40010000` で停止、`r2` = 0x48000000、`x/wx $r2` = 0xedfe0dd0、`cpsr` = 0x400001d3（SVC、I・F が mask）。PNG `p004-runB.png`。実機の確かめは無い（virt だけの Phase）。

## 残り・引き継ぎ

- §7.6 PAGING の (7)（uncached の alias）: `hal_pmem_map_uncached` は armv7 で未実装（`HAL_ERR_UNSUPPORTED`）。design §3.12 の device 窓の Normal NC の alias と clean+invalidate は devcfg の DMA（p008）の前、遅くとも p006 で作り、その時に self-test の (7) を足す。
- `zybo` の config・bsp-zybo は p005。
- 再開点: Q1 の p004 の判定と merge。
