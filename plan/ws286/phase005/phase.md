<!-- awesome-plan project=zedbsd record=ws286-p005 -->

# ws286-p005 zybo の BSP を JTAG で（QEMU zynq と実機の UART）

- 親: [ws286](../ws.md)、設計: [design.md](../design.md) 版 3 の §1.3・§1.4・§2.2・§3.2・§3.6・§3.11〜§3.14・§4.1・§4.6・§5.1・§5.2・§7.3・§7.4・§8 の p005 の行。
- Queue: Q1 の割当（P2、2026-10-23、worktree `/home/awe/zedBSD-worktrees/p2-zybo-p005`、branch `agent/p2-zybo-p005`、基点 main 7a9bc6a32）。
- 判断: U1（HAL の事前承認の 3 点だけ。hal.h は変えていない）、U3（QEMU zynq と実機は生きた serial で判定）、U4（担当が板を JTAG で直に操作、Q1 の割当の許可）。
- Status: in-progress（受け入れは QEMU（T1）と実機の両方で満たした。Q1 の判定と merge 待ち）

## 受け入れの条件（design §8 の p005、§7.3・§7.4）

1. `config/ci/config-zybo.mk` の vmunix が warning 0。armv7-virt も warning 0 で PASS のまま（共有の armv7 の code を変えたので T1 で再確認）。Makefile・menuconfig を変えたので amd64・rpi4 の vmunix も warning 0。
2. QEMU xilinx-zynq-a9（T1、serial）: `ZYBO PLATFORM qemu idcode=0x00000000`、`ARMV7 HAL READY AEABI PASS ATOMIC PASS PAGING PASS TIMER TICK`、`TIMER 10S` の行が host の時計で 10 s ± 2%、VFS の段（`VFS initialization failed`）まで。
3. 実機（JTAG、UART）: `ZYBO PLATFORM board idcode=0x13722093`、ACTLR の SMP|FW と SCU の enable、同じ HAL READY の行、`TIMER 10S` が 10 s ± 1%、reset + boot を 3 回続けて、`ARMV7 PENDING ASYNC ABORT` が出たかの記録、VFS の段まで。

## 作った物

- 共有の armv7 の code（8770517b0、9686819d3）:
  - `defs.h`・`vmunix.mk`: platform の `ARMV7_LOW_RAM_SKIP`（RAM の先頭の使わない byte、zybo 1 MiB、virt 0）。BSP の asm の file（`ARMV7_BSP_ASM_SOURCES`）。
  - `locore.S`: MPIDR の Aff0 ≠ 0 の CPU は停止（`-smp 2` の保険）、MMU on の前に L1 D-cache を set/way で invalidate（Procedure referenced: Linux v6.12 `cache-v7.S` `v7_invalidate_l1`）、`bsp_cpu_early_setup` を MMU on の前に呼ぶ、boot map の direct map を `PHYS_BASE + LOW_RAM_SKIP` から。
  - `cmain.c`: `bsp_find_fdt`（板が DTB を決める）、DTB の memory から先頭の `LOW_RAM_SKIP` を外す、loader の保留の async abort の報告、boot parameter `armv7.timer-check` で `ARMV7 TIMER 0S` → 10000 tick → `ARMV7 TIMER 10S ticks=… counter=… Hz=…`（診断の口、既定では何もしない）。
  - `int.c`: percpu の前（locore の `cpsie a` の直後）に来た最初の非同期の external abort だけを記録して続ける（design §3.3 M9）。それ以外は従来どおり停止。
  - `bootinfo.c`: `armv7_boot_has_parameter`。`asm.h`: `armv7_read_id_mmfr3`。
  - `space.c`: `hal_pmem_map_uncached`・`hal_pmem_unmap_uncached`（下）。`selftest.c`: PAGING の step 10（uncached の view への store が、view を外した後の direct map で読める）。`page.c` の未実装の stub を外した。
- `bsp-virt/`: `bsp_find_fdt`（r2 をそのまま）、`early.S`（空）。
- `bsp-zybo/`（新規）: `board.c`（SLCR・MPCore・UART1 の map、`PSS_IDCODE` の判別、DTB は r2 か `0x01F00000`、PL310 は off を確かめ on なら clean+invalidate して off、GIC は固定の address で secure、`hal_reset` = REBOOT_STATUS の `0x0F000000` を消して `PSS_RST_CTRL` = 1、1 s 経っても走っていれば `ZYBO RESET FAILED`、poweroff は unsupported）、`slcr.c`（unlock/lock の書き込み、periphclk を PLL・ARM_CLK_CTRL から計算、QEMU は 100 MHz）、`uart.c`（Cadence UART、TX が無効なら SPL と同じ 115200 8N1 に初期化）、`timer.c`（private timer の periodic の tick、PPI 29、global timer の 64 bit counter）、`early.S`（Cortex-A9 だけ: SCU_CTRL bit 0、ACTLR の SMP|FW）、`zybo.h`。
- `platform/armv7/zybo.mk`（PHYS_BASE 0、LMA 0x00100000、VMA 0x80100000、boot map 512 MiB、LOW_RAM_SKIP 1 MiB）、`config/ci/config-zybo.mk`（armv7-virt の config の platform と board だけを替えた）、`Makefile`・`tools/menuconfig.py` に `zybo`（build は `build/zybo`）。
- 道具（c80af533a、2753590bb）: `plan/tools/zybo/qemu-zynq.sh`（新規、design §4.6: ELF を `-kernel`、UART1 を 2 つ目の serial の Unix socket、DTB と initrd を `-device loader,…,force-raw=on` で JTAG と同じ address に、`--expect`・`--stamp`・`--gdb PORT`・`--print`）、`uart-expect.py` に `--socket`・`--stamp`、`zybo-common.sh` に `zybo_make_dtb`（jtag-boot と共通の DTS）・`zybo_elf_phys_end`、`jtag-boot.sh` に `--stamp` と raw の時に同名の ELF の BSS の終わりを確かめる直し（design §4.1 L5）。
- host 試験の直し: `plan/ws286/tests/pte-host-test.c` の command に `-DARMV7_LOW_RAM_SKIP=0`（defs.h が要る）。

### hal_pmem_map_uncached（p006・p008 の前提）

- device 窓を `0xC0000000`〜`0xCFFFFFFF`（256 section）に縮め、`0xD0000000`〜`0xDFFFFFFF` を uncached の窓にした（4 KiB の page、65536 page の bitmap で first fit、4 section の L2 table を 1 page に、table は作ったら残す）。entry は Normal non-cacheable（TEX=001,C=0,B=0）・S・AF・XN・kernel の RW。map の前に direct map の範囲を clean+invalidate、unmap で entry を消して TLBIMVAAIS、direct map の行を invalidate。kernel の table に移った後だけ（前は `HAL_ERR_UNSUPPORTED`）。L2 は off なので outer の操作は無い（L2 を有効にする WS で足す）。
- 確かめ: self-test の PAGING step 10（実機で PASS、下）。QEMU では T1 の A・B の後に足したので未確認（下の残件）。

### design との違い（意図して変えた所）

- zybo の PS の device を identity の 1 MiB section（design §3.2 の表の `0xE0000000`〜）でなく virt と同じ device 窓に張った（同じ code で済む。CPU1 を起こさないので OCM の Strongly-ordered の alias は要らない）。
- BSP の契約は p004 の `bsp.h` の形に `bsp_find_fdt`・`bsp_cpu_early_setup` を足した（design §3.14 の一覧と名前が一部違う）。
- DTB が r2 にも `0x01F00000` にも無い時は停止する（design §4.6 の「固定の address と既定の command line」は作らなかった。jtag-boot.sh と qemu-zynq.sh は必ず置く）。
- design §1.3・§3.13・§6 の「QEMU zynq の `PSS_RST_CTRL` はただの store」は誤り: QEMU v10.0.11 `hw/misc/zynq_slcr.c` 567〜569 行で SOFT_RST は `qemu_system_reset_request`（`-no-reboot` なら終わる）。`bsp_reset` は両方に効く形（1 s 待って `ZYBO RESET FAILED`）。
- `TIMER 10S` は boot parameter `armv7.timer-check` の時だけ（毎回 10 s 待たない）。kernel は未知の parameter として 1 行出すだけ。

## 確認

### host（2026-10-23 P2）

| command | 結果 |
| --- | --- |
| `make ZEDBSD_CONFIG=config/ci/config-zybo.mk BUILD=build/p2-zybo -j16 vmunix`（9686819d3） | rc 0、warning 0 |
| `llvm-readelf -h/-lW`・`llvm-nm`（zybo） | entry 0x00100000、`_start` 0x80100000、LOAD 3 つ（RX・R・RW）、`_end` 0x807af000（PA 0x007af000 < DTB 0x01F00000）、`vmunix.bin` 987728 = `_edata` − `_start`、exidx 無し、未定義 0 |
| `make ZEDBSD_CONFIG=config/ci/config-armv7-virt.mk BUILD=build/p2-virt -j16 vmunix`（9686819d3） | rc 0、warning 0 |
| `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p2-amd64 -j12 vmunix`（8770517b0 の Makefile） | rc 0、warning 0、kernel include check PASS（553 objects）、amd64 vmunix check PASS |
| `make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p2-rpi4 -j12 vmunix`（同） | rc 0、warning 0、arm64 vmunix check PASS |
| `pte-host-test.c`（virt の値と zybo の値の 2 通り）、`aeabi-host-test.c` | 3 つとも PASS |
| `bash -n`・`sh -n` の `plan/tools/zybo/*.sh`、`qemu-zynq.sh --print` | OK（shellcheck は host に無く未実施） |
| `uart-expect.py --socket --stamp` を自作の Unix socket の送り手で | 2 行の間 0.989 s を表示、pattern 一致で 0、socket が閉じて未一致で 1 |
| `plan/tools/zybo/spl-build.sh`（空の build/zybo-spl から、29 s） | SHA-256 `ed936c13…` で p002 と同じ（p002 の残件「bit 一致」は一致） |

### QEMU xilinx-zynq-a9 と virt（T1、2026-10-23、tree 2753590bb、QEMU の証拠）

- A（zynq、生きた serial、U3）: `plan/tools/zybo/qemu-zynq.sh build/p2-zybo/vmunix --append armv7.timer-check --stamp --timeout 90 --expect 'ZYBO PLATFORM qemu idcode=0x00000000' --expect '^ARMV7 HAL READY AEABI PASS ATOMIC PASS PAGING PASS TIMER TICK' --expect 'ARMV7 TIMER 10S' --expect 'VFS initialization failed'` → exit 0。行: `ZYBO FDT r2=00000000 is none; using the tree at 01f00000`、`ZYBO PLATFORM qemu idcode=0x00000000`（design §1.4 の見込みどおり 0）、`ZYBO CPU actlr=00000000 scu_ctrl=00000001 id_mmfr3=00002111 l2_ctrl=00000000`（QEMU の ACTLR は書いても 0）、`ZYBO CLOCK periphclk=100000000 Hz`、`ARMV7 TIMER TICK ticks=10 counter=100000000 Hz irq=29`、`[0.153] ARMV7 TIMER 0S` → `[10.155] ARMV7 TIMER 10S ticks=10000 counter=999865778`（host の時計で 10.002 s、+0.02%）、HAL READY の行、`VFS initialization failed (12); entering idle.`。
- B（virt、`BOOT_MODE=armv7-virt BOOT_PATTERN='ARMV7 HAL READY AEABI PASS ATOMIC PASS PAGING PASS TIMER TICK' plan/tools/boot-test.sh build/p2-virt/vmunix.bin`）→ exit 0、PNG `build/review/ws286-p005-virt.png`（main の checkout）に HAL READY、`counter=62500000 Hz irq=30`、`VFS initialization failed (12)`。

### 実機 ZYBO（JTAG、2026-10-23 P2、U4、board lock を取って。実機の証拠）

- `plan/tools/zybo/jtag-boot.sh build/p2-zybo/vmunix.bin --append armv7.timer-check --stamp --timeout 90 --expect 'ARMV7 HAL READY' --expect 'ARMV7 TIMER 10S'`（8770517b0 の build）→ rc 0。UART の抜粋:

```
ARMV7 ENTRY midr=413fc090 mpidr=80000000 sctlr=28c5187d image=00100000
ARMV7 FDT PASS phys=01f00000 size=413 cpus=0 initrd=0-0
ZYBO PLATFORM board idcode=0x13722093
ZYBO CPU actlr=00000041 scu_ctrl=00000003 id_mmfr3=00102111 l2_ctrl=00000000
ZYBO CLOCK periphclk=325000000 Hz
ARMV7 MEMORY base=100000 total=511 MiB free=504 MiB
ARMV7 GIC distributor=f8f01000 cpu=f8f00100 secure=1
ARMV7 GIC lines=96
ARMV7 TIMER TICK ticks=10 counter=325000000 Hz irq=29
[    6.659] ARMV7 TIMER 0S
[   16.668] ARMV7 TIMER 10S ticks=10000 counter=3249988690 Hz=325000000
ARMV7 PAGING PASS asid=1,2
ARMV7 HAL READY AEABI PASS ATOMIC PASS PAGING PASS TIMER TICK
```

  host の時計で 10.009 s（+0.09%、± 1% の内）。ACTLR = 0x41（SMP|FW）、SCU_CTRL = 0x3（enable と、SPL の後から立っていた address filtering）。kernel の load 987728 byte を約 3.9 s（約 250 KiB/s）。
- reset + boot を 3 回続けて（`--expect 'ARMV7 HAL READY AEABI PASS ATOMIC PASS PAGING PASS TIMER TICK' --expect 'VFS initialization failed'`、12:18 から）: 3 回とも rc 0、`VFS initialization failed (12); entering idle.` まで。`ARMV7 PENDING ASYNC ABORT` は 4 回の起動のどれにも出なかった。
- uncached の窓を足した後（9686819d3）にもう 1 回: rc 0、PAGING PASS（step 10 を含む）、VFS の段まで。

## 残り・引き継ぎ

- 9686819d3（uncached の窓と self-test の step 10）は実機だけで確かめた。QEMU（virt と zynq）での確認は未実施: T1 に A・B の再実行を頼む（同じ command、tree 9686819d3 以降）。
- `cdns-uart.c`（kernel の UART の driver、RX の割り込み）と `src/kern/platform/zybo.c` は p006 の範囲（今の platform は `hal-console.c`）。
- PL310 の有効化と outer の cache の操作、CPU1、SCU の invalidate は後の WS（design §3.7・§3.11）。
- Master の Tools への登録（Q1）: `plan/tools/zybo/qemu-zynq.sh`、`uart-expect.py` の `--socket`・`--stamp`、`jtag-boot.sh --stamp`、boot parameter `armv7.timer-check`。
- 再開点: Q1 の判定と merge。次は p006。
