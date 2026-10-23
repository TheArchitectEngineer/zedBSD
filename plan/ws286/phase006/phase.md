<!-- awesome-plan project=zedbsd record=ws286-p006 -->

# ws286-p006 task・syscall・signal・VFP・cache と kernel の platform（VFP PASS と VFS initialization failed）

- 親: [ws286](../ws.md)、設計: [design.md](../design.md) 版 3 の §3.3・§3.9〜§3.11・§3.13・§5.1・§5.9・§7.2〜§7.4・§7.6・§8 の p006 の行。
- Queue: Q1 の割当（P2、2026-10-23、worktree `/home/awe/zedBSD-worktrees/p2-zybo-p006`、branch `agent/p2-zybo-p006`、基点 main a2339786d）。
- 判断: U1（HAL の事前承認の 3 点だけ。hal.h・arch/armv7.h は変えていない）、U3（QEMU zynq と実機は生きた serial で判定）、U4（担当が板を JTAG で直に操作）。
- Status: in-progress（実装済み・実機 PASS、T1 の QEMU zynq と virt の試験待ち。Q1 の判定まで cleared にしない）

## 受け入れの条件（design §8 の p006、§7.2〜§7.4）

1. zybo・armv7-virt・amd64・rpi4 の vmunix が warning 0。
2. 実機（JTAG、UART）: `ARMV7 VFP PASS` と `VFS initialization failed`。
3. QEMU xilinx-zynq-a9（T1、serial）: 同じ 2 つ。
4. QEMU virt（T1、PNG）: 同じ 2 つ。
5. user mode・syscall・signal・fork の受け入れは p007 の `abitest`（この Phase には user の program が無いので確かめられない。design §8 の p006 の行のとおり、それを明記して clear する）。

## 作った物（660617a35）

- p004 の時点で task の switch（`dispatch.S`・`task.c`）、syscall の入口と出口（`vectors.S`・`int.c` の `take_syscall`）、signal（`hal_task_signal_enter`/`return`）、TLS（TPIDRURO）、`hal_cpu_idle` の順（design §3.6 M3）、cache の L1（`cache.c`）は既に有った。p006 では読み直して、次を足した・直した。
- VFP/NEON の lazy な切り替え（新規 `src/hal/armv7/fpu.c`・`fpu.h`・`fpregs.S`、design §3.10）:
  - boot で CPACR の cp10/cp11 を full access（D32DIS・ASEDIS を落とす）、FPEXC = 0、FPSID（SW）と MVFR0（16 か 32 本）を読む。行 `ARMV7 VFP fpsid=… mvfr0=… registers=…`。
  - per-CPU の owner と selected（`percpu.h`）、context は `struct armv7_fpu_context`（`hal_fpregs` と最後に load した CPU）。switch は selected を替えて FPEXC.EN を落とす。user の Undefined で EN=0 なら、owner を替えて（同じ CPU が最後に load した owner なら load を省く）EN=1 にして同じ命令をやり直す。EN=1 なら本当の未定義（`ILLEGAL_INSN`）、FPEXC.EX/DEX が立っていれば消して `ARITHMETIC`（hal.h に FP 専用の cause が無いので SIGFPE 相当の既存の cause）。
  - sync と forget の呼び出し（design の表、mips の対応と同じ所）: fork（親を sync して写す）、exec（初期値にして forget）、signal の入口（sync して `signal_fpregs[depth]` へ）、signal の戻り（戻して forget）、task の破棄（forget）、debug の読み（sync）・書き（FPSCR を消毒して写してから forget）。
  - Thumb の Undefined の命令の address を `lr - 2` に直した（vectors.S は一律に `lr - 4`）。
  - self-test `ARMV7 VFP PASS`（`selftest.c`、design §7.6・mips の `check_float` の形）: 2 つの試験の context で、FPEXC.EN=0 からの Undefined の道を kernel の `vldm`/`vstm` で通し、switch・load の省略・sync・forget を 7 step で確かめる。boot の要約の行の最後に ` VFP PASS` を足した（前の `^… TIMER TICK` の pattern はそのまま一致する）。
  - kernel は `-mfpu=none` のまま。`llvm-objdump` で VFP の命令が `fpregs.S` の 6 関数だけにある事を確かめた。
- cache（`cache.c`）: A9 の I-cache は VIPT（CTR.L1Ip）なので、`hal_sync_instruction_stream` は PIPT の時だけ address で、それ以外は ICIALLUIS で全部を落とす（kernel が direct map から書いた code を user の別の index から引く alias を残さない。Procedure referenced: Linux v6.12 `cacheid_init`・`__sync_icache_dcache`）。BPIALL を BPIALLIS に（両 CPU とも MP extension）。L2 は p005 の `CONTROL` を読むだけのまま。
- kernel の platform（design §5.9）:
  - `src/kern/platform/armv7-virt.c`（新規、`riscv-virt.c` の形）: `drv_serial_console_init`、FDT の PL011（`armv7.fdt`）、`armv7.initrd` の ramdisk を `ram0`（KERN_DEV_SD、MBR）、PSCI の poweroff・reset。
  - `src/kern/platform/zybo.c`（新規）: UART1（`0xE0001000`、GIC 82）を固定の address で（JTAG・QEMU の DTB は device を書かない）、ramdisk は同じ、reboot は `hal_reset`、poweroff は `EOPNOTSUPP`。boot device の口は ramdisk 1 つだけの単純な形（WS289 で SD を足す時は `kern_platform_init` に disk を 1 つ足し、`kern_platform_block_device` で返す）。
  - driver（新規）: `src/drivers/serial/pl011.c`（FDT の `arm,pl011`、GIC の 3 cell の interrupt、RX と RX timeout の割り込み）、`src/drivers/serial/cdns-uart.c`（RXWM 1・RXTOUT 10、ISR を書き戻して消す、RXTRIG と TOUT）。どちらも HAL が設定した line をそのまま使い、受信だけを割り込みで `drv_serial_console_receive` へ（`ns16550.c` の形）。
  - `platform/armv7/virt.mk`・`zybo.mk` の `ARMV7_PLATFORM_SOURCES`（`hal-console.c` から替えた）、`src/kern/platform/README.md` の一覧。

### design との違い（意図して変えた所）

- switch で FPEXC.EN が立っていれば、その時に owner の register を保存する（design §3.10 は「switch では EN を落とすだけ、transfer で元の CPU で保存」）。transfer は別の CPU から呼ばれ得るので元の CPU の hardware を読めない。Linux v6.12 `vfpmodule.c` の SMP の道（THREAD_NOTIFY_SWITCH で EN なら保存、復元は lazy、`vfp_state_in_hw` で最後に load した CPU を確かめる）に合わせた。load は lazy のままで、同じ CPU に戻った owner の load は省く。1 CPU の今は差が出ない。
- 浮動小数点の例外は `HAL_TRAP_CAUSE_ARITHMETIC`（design の `FP_EXCEPTION` は hal.h に無い。hal.h は変えない）。
- zybo の UART は固定の address（design §5.1 の `drv_cdns_uart_probe(fdt|fixed, …)` の fdt の枝は作らなかった。今の DTB に device が無いため）。
- ramdisk の partition の scheme は MBR だけ（`riscv-virt.c` の pcat_auto でなく `rpi4.c` と同じ）。

## 確認

### host（2026-10-23 P2）

| command | 結果 |
| --- | --- |
| `make ZEDBSD_CONFIG=config/ci/config-zybo.mk BUILD=build/p2-zybo -j16 vmunix`（660617a35） | rc 0、warning 0、未定義の symbol 0（vmunix.mk の確かめ） |
| `make ZEDBSD_CONFIG=config/ci/config-armv7-virt.mk BUILD=build/p2-armv7-virt -j16 vmunix`（同） | rc 0、warning 0 |
| `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p2-amd64 -j12 vmunix`（同） | rc 0、warning 0、kernel include check PASS（553 objects）、amd64 vmunix check PASS |
| `make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p2-rpi4 -j12 vmunix`（同） | rc 0、warning 0、arm64 vmunix check PASS |
| `llvm-objdump -d build/p2-zybo/vmunix` で `v*` の命令のある関数を数えた | `armv7_fpu_{save,load,read_fpexc,write_fpexc,read_fpsid,read_mvfr0}` だけ |
| `git diff --check` | 出力なし |
| `plan/tools/zybo/spl-build.sh`（自分の `build/zybo-spl`、jtag-boot の入力） | SHA-256 `ed936c13…`（p002・p005 と同じ） |

- host の unit test は足していない: lazy の切り替えは hardware の register の状態の機械で、boot の self-test（実機・QEMU）が同じ道を通る。2 CPU の移動の場合（loaded_cpu の確かめ）は 1 CPU では通らず、SMP の WS の確認に残す。

### 実機 ZYBO（JTAG、2026-10-23 12:5x P2、U4、board lock を取って。実機の証拠）

- `plan/tools/zybo/jtag-boot.sh build/p2-zybo/vmunix.bin --stamp --timeout 90 --expect 'ARMV7 VFP PASS' --expect 'ARMV7 HAL READY AEABI PASS ATOMIC PASS PAGING PASS TIMER TICK VFP PASS' --expect 'VFS initialization failed'`（660617a35 と同じ tree の build、vmunix.bin の SHA-256 `fbb52341…`）→ rc 0。UART の抜粋:

```
ARMV7 VFP fpsid=41033094 mvfr0=10110222 registers=32
ARMV7 PAGING PASS asid=1,2
ARMV7 VFP PASS registers=32
ARMV7 HAL READY AEABI PASS ATOMIC PASS PAGING PASS TIMER TICK VFP PASS
zybo: serial port at e0001000, interrupt 82
boot: platform devices detected: 0
VFS initialization failed (12); entering idle.
```

- ramdisk の道: 64 KiB の 0 の file を `--initrd build/p2-zero.img` で → rc 0、`ARMV7 FDT PASS … initrd=4000000-4010000`、`zybo: ramdisk ram0 64 KiB at 4000000`、`boot: platform devices detected: 1`、`vfs: native boot disk=ram0 physical disks=1`、`vfs: scan ram0 geometry error=21`（中身が無いので想定どおり）、`VFS initialization failed (12)`。
- 未確認: UART の受信の割り込み（`kern_platform_input_init` は root の mount の後に呼ばれるので、root の無い p006 では通らない。p010 の login で確かめる）。

### QEMU（T1 に依頼、未実施）

- 下の「T1 への依頼」。

## T1 への依頼（2026-10-23、Q1 経由）

worktree `/home/awe/zedBSD-worktrees/p2-zybo-p006`（commit は Q1 への報告の SHA）。image は担当が作った物をそのまま使える（`build/p2-zybo/vmunix`・`build/p2-armv7-virt/vmunix.bin`・`build/p2-zero.img`）。作り直す時は `make ZEDBSD_CONFIG=config/ci/config-zybo.mk BUILD=build/p2-zybo -j16 vmunix` と `make ZEDBSD_CONFIG=config/ci/config-armv7-virt.mk BUILD=build/p2-armv7-virt -j16 vmunix`、`head -c 65536 /dev/zero > build/p2-zero.img`。

- A（zynq、serial、U3）: `plan/tools/zybo/qemu-zynq.sh build/p2-zybo/vmunix --timeout 90 --expect 'ZYBO PLATFORM qemu idcode=0x00000000' --expect '^ARMV7 VFP PASS' --expect '^ARMV7 HAL READY AEABI PASS ATOMIC PASS PAGING PASS TIMER TICK VFP PASS' --expect 'VFS initialization failed'` → exit 0 で PASS。`ARMV7 VFP fpsid=…` の行を写して返す。
- A2（zynq、ramdisk）: `plan/tools/zybo/qemu-zynq.sh build/p2-zybo/vmunix --initrd build/p2-zero.img --timeout 90 --expect 'zybo: ramdisk ram0 64 KiB' --expect 'VFS initialization failed'` → exit 0。
- B（virt、PNG）: `BOOT_MODE=armv7-virt BOOT_PATTERN=$'ARMV7 VFP PASS\nARMV7 HAL READY AEABI PASS ATOMIC PASS PAGING PASS TIMER TICK VFP PASS\nVFS initialization failed' plan/tools/boot-test.sh build/p2-armv7-virt/vmunix.bin` → PASS と PNG。
- B2（virt、ramdisk）: `BOOT_MODE=armv7-virt INITRD=build/p2-zero.img BOOT_PATTERN=$'armv7-virt: serial port at 9000000, interrupt 33\narmv7-virt: ramdisk ram0 64 KiB\nVFS initialization failed' plan/tools/boot-test.sh build/p2-armv7-virt/vmunix.bin` → PASS と PNG。
- 見込みの外れの扱い: virt で `ARMV7 VFP NONE cpacr=…` が出たら、QEMU が Non-secure の NSACR で cp10/cp11 を許していない（design §3.10 の推測が外れた）。FAIL として PNG を返す（解析は担当）。

## 残り・引き継ぎ

- UART の受信（`pl011.c`・`cdns-uart.c` の割り込みの道）は p010 の login で初めて通る。
- 2 CPU での VFP の移動（`loaded_cpu`）と BPIALLIS・ICIALLUIS の broadcast は SMP の WS で確かめる。
- `ptrace.c` の `hal_icache_invalidate_range(user の address)` は armv7 では user の VA への MVA の maintenance になる（design §3.3 の erratum 775420 の規則に当たる）。armv7 で ptrace を使う WS で、direct map 経由にするか fault の fixup を足す。
- 再開点: T1 の A・A2・B・B2 の結果と Q1 の判定・merge。次は p007。
