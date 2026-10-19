<!-- awesome-plan project=zedbsd record=ws256-p008 -->

# ws256-p008 kernel 側: serial の console、ramdisk、MBR の root.img

Status: cleared（2026-10-19 Q1 の判定、T1-720。走り A は boot-test の pattern の 1 行目「riscv-virt: ramdisk ram0」が 80x25 の画面から流れて FAIL と出たが、最後の画面（build/review/t1-720/rvA-login-3.png）に「vfs: native boot disk=ram0 physical disks=1」「ram0 partition 1・2」「runtime filesystems mounted」「init /sbin/init not started (6)」があり、ramdisk の root の受け入れを満たす。init は p007 の範囲。amd64 の既定の image の回帰は PASS。UART の RX（走り B）は未確認で p007 の T1 に持ち越す。次の走りの pattern は「vfs: native boot disk=ram0」を使う）

- 親: [WS256](../ws.md)。Queue: q1269 / q1269-i01（承認: 2026-10-19 ユーザー「WS256を実施してください。」、Q1 の dispatch: p008 → p007 → p004）。
- 範囲: [design.md](../design.md) §6 の p008。platform の残り（console・ramdisk・device の表）、`ns16550.c`（RX）、`serial-console.c`、`ramdisk.c`、MBR の root.img の `rootfs.mk`、FDT・initrd の名前付き handoff（#9 の (a)、p003 の HAL がすでに出している物を kernel 側で使う）、kernel の arch の `#if` の棚卸し。
- 受け入れ: build warning 0、host の試験（ramdisk の read、partition の表の fixture）、T1: 画面に ramdisk の mount と「init が無い」。
- HAL: 変えていない（`include/hal/` も `src/hal/` も無変更）。`include/kern/boot.h` も無変更（#9 (a)）。

## 作った物・変えた物

- `src/kern/platform/riscv-virt.c`（新）: riscv64 の virt の platform の TU。p003 の後に main で riscv64 virt は共有の `hal-console.c`（boot device を出さない board の TU）に移っていたので、design §2.7 のとおり riscv64 virt の専用の TU を置き、`platform/riscv64/vmunix.mk` の `RISCV64_PLATFORM_SOURCES_virt` を差し替えた（riscv32・m68k・ppc は `hal-console.c` のまま）。
  - `kern_platform_init`: handoff の検査、`drv_serial_console_init`、`hal_get_arch_handoff("riscv64.fdt")` の木から UART を探して map（`probe_uart`）、`partition_set_scheme(&drv_partition_scheme_mbr)`・`disk_registry_reset()`、`hal_get_arch_handoff("riscv64.initrd")` の範囲を direct map で引いて `ram0` に（`publish_ramdisk`、両端が managed RAM で連続である事を確かめる、512 byte の端数は切る）。ramdisk があれば device 0 を `KERN_DEV_SD`・bios 0x80・`PRESENT | BOOT_ORIGIN`（rpi4 と同じ置き方）、無ければ 0 個（p003 の「boot partition が無い」で止まる形のまま）。log の行 `riscv-virt: serial port at …, interrupt N`・`riscv-virt: ramdisk ram0 N KiB at …`。
  - `kern_platform_input_init`: `drv_ns16550_start_input(&uart, drv_serial_console_receive)`。
  - `kern_platform_block_device`: `KERN_DEV_SD` → ramdisk の disk。halt・reboot・poweroff・debug_write は hal-console.c と同じ。
- `src/drivers/serial/ns16550.c`・`include/drivers/serial/ns16550.h`（新）: MMIO の 16550 の受信だけ。DT の compatible `ns16550a`・`ns16550`・`snps,dw-apb-uart` の最初の present な node、`reg`・`reg-shift`・`reg-io-width`（1 か 4）・`interrupts` の 1 つ目の cell。LCR・分周は書かない（firmware の設定のまま）。start で handler を登録 → 古い byte を捨てる → `input_started` → IER の受信の bit → unmask。割り込みで DW の busy detect（IIR 0x7）なら USR を読んで消し、LSR の DR が落ちるまで最大 64 byte を受信の関数へ、EOI。値の出典は block ごとの `Values referenced:`（16550 の register 表、Linux 6.8.12 `8250_dw.c` の USR と busy の値）。
- `src/drivers/generic/serial-console.c`・`include/drivers/generic/serial-console.h`（新）: teletype の console（出力は `hal_putc`＝HAL の SBI の console と virt の ramfb の画面、80x24 の grid の問い合わせだけ答える）、`drv_serial_console_receive`（CR → LF、DEL → BS の後 `tty_console_input_byte`）。
- `src/drivers/generic/ramdisk.c`・`include/drivers/generic/ramdisk.h`（新）: memory を背にした block device（512 byte、copy で即時に完了、flush は何もしない、2 つ目は EBUSY、`DISK_STATS_KIND_OTHER`）。
- `platform/riscv64/rootfs.mk`（新、`platform/riscv64/Makefile` から include）と `platform/riscv64/tools/make-riscv64-root-image.py`（新）: MBR の root disk image。p1 = FAT16（type 0x0e、LBA 2048、16384 block、label ZEDBOOT、serial 固定）、p2 = UFS（type 0xa5、LBA 18432〜）。`--ufs-image FILE`（p007 の userland の UFS）か `--bare-root`（marker だけの 16 MiB の UFS、`tools/build/ufs_format.py`）、`--boot-file DEST=SRC`。UFS は `tools/build/check-ufs-image.py` の検査を通してから出力を置き換える（失敗時は `.partial` が残るだけで出力は置き換わらない）。target `$(BUILD)/root-bare.img`。UFS の superblock の fs_id は `ufs_format.py` が乱数で作るので image の SHA は build ごとに変わる。
- `include/kern/net/packet-buf.h`・`tcp-socket.h`: 64-bit の pool の条件に `HAL_ARCH_RISCV64`（下の棚卸し）。
- `include/uapi/reg.h`: `__riscv && __riscv_xlen == 64` の枝（`struct reg { r_x[32]; r_pc; }`、`struct fpreg { fp_f[32]; fp_fcsr; fp_reserved; }` 264 byte。design §2.6）。kernel は ptrace.c を riscv64 で build しないので今は userland（p007）の header のため。
- `src/kern/platform/README.md`（TU の一覧に riscv-virt.c）、`src/kern/platform/hal-console.c`（comment の「RISC-V」を「32-bit RISC-V」に。riscv64 はもう使わない）。

## kernel の arch の `#if` の棚卸し（2026-10-19、grep `__aarch64__`・`__x86_64__`・`HAL_ARCH_*`）

| file | 条件 | riscv64 の扱い |
| --- | --- | --- |
| `src/kern/elf.c` | machine の表（p003 で `HAL_ARCH_RISCV64` 済み）、x86 だけの static TLS（135・859・1364 行） | 他の arch と同じ既定（static TLS を持たない、design §2.6 M10） |
| `src/kern/vfs.c` | `VFS_LEGACY_NULL_AUTOROOT`（x86 以外）、ARM64 の image の overlay、RPI4 の boot FAT の保持 | legacy autoroot を使う（ramdisk の MBR の root）。overlay・boot FAT の保持は要らない |
| `src/kern/main.c` 240 | x86 以外で parameter の source が無ければ `/sbin/init` | 同じ既定 |
| `src/kern/entry.c` 50 | kernel heap 4 MiB は x86_64 だけ | 既定の 512 KiB（arm64 と同じ）。足りなければ後で |
| `src/kern/klog.c` 37 | 512 KiB は x86_64、256 KiB は RPI4 | 既定の 32 KiB |
| `src/kern/signal.c` 47 | red zone 128 は amd64 だけ | 0（RISC-V の psABI に red zone は無い） |
| `src/kern/inode.c` 61 | i386 だけ小さい表 | 既定 |
| `src/kern/ptrace.c` | x86_64・aarch64 の register | riscv64 の kernel に入れない（p003 の決め 5） |
| `src/kern/syscall.c` 9256 | x86_64・aarch64 以外は ptrace を ENOSYS | 同じ既定（ENOSYS） |
| `include/kern/net/packet-buf.h` 24・`tcp-socket.h` 52 | 64-bit の pool（AMD64・ARM64） | **`HAL_ARCH_RISCV64` を足した**（64-bit で memory のある機械。VF2 の GbE は p018） |
| `include/uapi/reg.h` | x86_64・aarch64、他は `#error` | **riscv64 の枝を足した** |
| `include/uapi/signal.h` | arch の条件は無い | 変更なし |

## 確かめた事（host、2026-10-19）

- `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt.mk -j16 build/riscv64-virt/vmunix build/riscv64-virt/root-bare.img`: exit 0、`warning:` 0 行・`error:` 0 行（log `build/tmp/p008-build1.log`・`p008-build2.log`、worktree p2）。新しい TU（riscv-virt.c・ns16550.c・serial-console.c・ramdisk.c）が compile された事を log で確かめた。vmunix の強い undefined 0（vmunix.mk の検査）。
- `plan/ws256/tests/serial/run.sh OUTDIR`: 26 件 PASS（virt 風の木で ns16550a・0x10000000・0x100 byte・shift 0・幅 1・IRQ 10・busy 無し、start 前の byte を捨てる、IER の受信 bit、byte の access、2 回目の start の拒否、割り込みで 'ab'、EOI 1 回、USR に触らない。DW の木で disabled の UART を飛ばし 0x10000000・shift 2・幅 4・IRQ 32・busy 有り、word の access、IIR 0x7 で USR を 1 回読む、CR → LF・DEL → BS で console へ、1 回の割り込みは 64 byte まで、次の割り込みで残り）。src は kernel の `ns16550.c`・`fdt.c`・`serial-console.c` を host の cc で、MMIO・IRQ・tty は fixture。
- `plan/ws256/tests/ramdisk/run.sh OUTDIR`: 21 件 PASS（root-bare.img を作り、ramdisk の端数・NULL・長い名前・2 つ目の拒否、ram0 の名前と block 数、2 block の read、MBR の signature、p1 FAT16・p2 0xa5、p2 が disk の終わりまで、**kernel の `mbr.c` の scan** で p1 2048/16384・p2 が root・p3/p4 無し、p2 に UFS の magic、1 block の write が memory を変える、flush）。
- `mdir`・`minfo` で p1 が FAT16（label ZEDBOOT）、python で MBR の表を確かめた。
- `git diff --check` 0。
- 未実施: QEMU での起動（T1 に依頼、下）。amd64 の build（変えた共有の header は riscv64 の条件の中、hal-console.c は comment だけ）。

## T1 への依頼（Q1 経由）

image の作り方（worktree の p2 の commit、または main に merge した後の main）:

```
make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt.mk -j16 build/riscv64-virt/vmunix build/riscv64-virt/root-bare.img
```

- 走り A（合否）: `BOOT_MODE=riscv64-virt INITRD=build/riscv64-virt/root-bare.img BOOT_PATTERN=$'riscv-virt: ramdisk ram0\nvfs: runtime filesystems mounted\ninit /sbin/init not started' OUTPUT=… plan/tools/boot-test.sh build/riscv64-virt/vmunix`。合格: 3 つの行がどれかの画面に出る（ramdisk の公開、root の UFS の mount の後の runtime の mount、init が無い）。`vfs: … failed` の行が出たら FAIL（画面の PNG を返す）。
- 走り B（情報、受け入れに数えない）: A と同じに `BOOT_THEN=$'type:zedbsd-rx-check\nexpect:zedbsd-rx-check'` を足す。init が無いので reader はいないが、console の tty の echo で打った文字が画面に出れば UART の RX の割り込み（PLIC）が動いた証拠。出なくても A の合否には影響しない（RX の受け入れは p007 の `uname -m`）。
- 判定は画面（login.png・`login-N.png`・`.txt`）。serial・console の log は判定に使わない。

## 残り

- T1 の結果の判定（Q1）。FAIL なら P2 が gdbstub で止めて解析する。
- p007（userland）、p004（loader）。
