<!-- awesome-plan project=zedbsd record=ws286-p010 -->

# ws286-p010 ramdisk の root と login（login: → root → uname -m = armv7、virt・zynq・実機）

- 親: [ws286](../ws.md)、設計: [design.md](../design.md) 版 3 の §7.2〜§7.4・§8 の p010 の行。
- Queue: Q1 の割当（P2、2026-10-23、worktree `/home/awe/zedBSD-worktrees/p2-zybo-p010`、branch `agent/p2-zybo-p010`、基点 main 244d876e9）。
- 判断と許可: U3（QEMU zynq と実機は生きた serial で判定）、U4（担当が板を JTAG で直に操作）。toolchain は p007 と同じ Q1 の許可の範囲（sysroot は worktree の `build/` だけで作った。今回 sysroot の規則は変えていない）。共有の LLVM は main の物への symlink で読むだけ。HAL の API は変えていない。
- Status: in-progress（実機 PASS。T1 の QEMU zynq と virt、Q1 の判定待ち）

## 受け入れの条件

1. zybo・armv7-virt・amd64・rpi4 の build が warning 0。
2. 実機（JTAG、UART、`--initrd root.img`）: `login:` → `root` → `uname -m` が `armv7`（cdns-uart の受信割り込みを通る）。
3. QEMU xilinx-zynq-a9（T1、serial）: 同じ。
4. QEMU virt（T1、boot-test の PNG）: `login:`、BOOT_THEN で login して `armv7`。

## 作った物（0b0c5f3f8）

- 最小の program の組（`platform/armv7/userland.mk` の `ARMV7_ROOT_COMMANDS`、`?=` で config が足せる。p008 の `fpgaload`・`fpgareg` はここに足す）: `init`・`getty`・`login`・`sh`・`syslogd`（getty の順の相手）・`mount`/`umount`・`swapon`（init が起動で `-a` を走らせる）・`cat`・`dmesg`・`echo`・`ls`・`ps`・`reboot`・`uname`。riscv32 の port は root を持たない（VFS の段まで）ので、参考は他の port の最小の config の組（`cat echo login ls sh uname`）と riscv64 の link の規則。
- 決め: top の Makefile は `ZEDBSD_USER_PROGRAMS` に約 40 の「必須の管理の道具」（networkd・cron・wifi など）を必ず足すので、armv7 の root はそれを使わず、上の組を platform の側で明示する（ユーザー「RAM が小さい、ramdisk の root は login と shell の要る物だけ」、WS289 で root は SD へ）。
- 決め（p007 の残件）: login の組の program は自分の `__thread` を持たない（`userland/base/common/c_parser.c` だけが持ち、組に入らない）ので、`-z undefs` も `--provider` も使わず普通の application（`check-dynamic-elf.py --role application --needed libc.so`）として link する。
- `platform/armv7/rootfs.mk`（新規）: 共通の `ZEDBSD_ROOTFS_TREE_RULE` で `build/<board>/rootfs` を作る。中身は上の program、`/lib/ld.so`・`/lib/libc.so`、account（passwd・group・shadow・services）、`/etc/rc.conf`・`/etc/fstab`、init の service 定義のうち `getty_console`・`syslogd`・`networking`（getty_console の `after=` に名前が有る。networkd が無いので init は skip する。定義が無いと getty が「broken order」で起動されない）、`/etc/os-release`。`build/<board>/root.img`（`make … build/zybo/root.img`、`root-image`）は 1 つの MBR の partition の UFS 4 MiB。
- `platform/armv7/tools/make-armv7-root-image.py`: `--tree ROOTFS`（staged の tree を mode と symlink ごと。印は tree の物）。
- `userland/base/ls/main.c`: `entry_time` を pointer でなく値で返す。armv7 では `struct stat` が `packed, aligned(4)` で `timespec` は 8 byte 揃えなので、`&status.st_mtim` が `-Waddress-of-packed-member`（-Werror）になり、実際に揃っていない pointer を作り得た。i386 は timespec が 4 揃えなので出なかった。
- `plan/tools/zybo/uart-expect.py`: `--then type:TEXT|expect:REGEX`（対話。type の前に 0.5 s 待つ: login は聞く前の入力を捨てる）。改行の無い行（`login: `・`Password: `）も一致させる。`jtag-boot.sh`・`qemu-zynq.sh` は `--then` を渡す。

## 大きさ

- tree 1,009,846 byte（libc.so 515,292、sh 195,696、init 48,700、ld.so 44,420、ls 41,116、他は各 10〜17 KiB）。
- `root.img` 5,242,880 byte（MBR の前の 1 MiB + UFS 4 MiB。UFS の最小）。JTAG の load 20.3 s（252.5 KiB/s）。design §7.4 の目安 8〜16 MiB より小さい。

## 確認

### host（2026-10-23 P2）

| command | 結果 |
| --- | --- |
| `make ZEDBSD_CONFIG=config/ci/config-zybo.mk -j16 vmunix build/zybo/root.img` | rc 0、warning・error 0。各 program の check-dynamic-elf（application、needed libc.so）PASS、UFS の check PASS |
| `make ZEDBSD_CONFIG=config/ci/config-armv7-virt.mk -j16 vmunix build/armv7-virt/vmunix.bin build/armv7-virt/root.img` | rc 0、warning 0 |
| `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p010-amd64 -j12 vmunix build/p010-amd64/bin/ls` | rc 0、warning 0、amd64 vmunix check PASS（ls の変更を amd64 でも compile） |
| `make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p010-rpi4 -j12 sysroot-arm64` の後 `… vmunix build/p010-rpi4/bin/ls` | rc 0、warning 0、arm64 vmunix check PASS（sysroot を先に作らないと bin/ls が header を見つけない。既存の依存の欠け、今回は直していない） |
| `uart-expect.py --socket` を host の偽の console（login → Password → prompt → `uname -m`）に | 正しい時 rc 0、違う出力で rc 1・`never saw '^armv7$'` |
| `bash -n plan/tools/zybo/jtag-boot.sh plan/tools/zybo/qemu-zynq.sh`、`git diff --check` | 出力なし（shellcheck は host に無い） |

### 実機 ZYBO（JTAG、2026-10-23 13:5x P2、U4、board lock を取って。実機の証拠）

SPL は worktree で `plan/tools/zybo/spl-build.sh`（`ed936c13…`、127245 byte）。vmunix.bin `a823f9e1…`、root.img `758f9957…`。

- `plan/tools/zybo/jtag-boot.sh build/zybo/vmunix.bin --initrd build/zybo/root.img --timeout 240 --expect 'boot: starting init /sbin/init' --expect 'login: *$' --then type:root --then expect:Password: --then type:root --then 'expect:root@kei' --then 'type:uname -m' --then 'expect:^armv7$'` → rc 0。UART の抜粋:

```
uart| zybo: serial port at e0001000, interrupt 82
uart| zybo: ramdisk ram0 5120 KiB at 4000000
uart| boot: starting init /sbin/init
uart| init: cron: no definition in /etc/service.d
uart| init: homes: no definition in /etc/service.d
uart| init: networkd: no definition in /etc/service.d
uart| init: ntpdate: no definition in /etc/service.d
uart| init: started syslogd pid 4
uart| init: skipped networking: required dependency failed
uart| init: started getty_console pid 5
uart| init: system running
uart| login:
uart-expect: type 'root'
uart| Password:
uart-expect: type 'root'
uart| root@kei:~$
uart-expect: type 'uname -m'
uart| armv7
```

- 2 回目・3 回目（同じ image）: `ls /`（bin boot dev etc home lib root run sbin shm tmp usr var）、`cat /etc/os-release`、`ps`（login・-sh・ps）、`mount`（`/dev/ram0p1 on / type ufs (rw)`、tmpfs・devfs）、`uname -a`（`zedBSD kei 1.0.0-beta2+g244d876 … armv7`）が通った（rc 0）。打った文字は全部 cdns-uart の受信割り込みで届いた（p006 の残件）。
- 4 回目: `reboot` → `init: stopping services`・`init: executing system action reboot`・`shutdown: the volumes are synced; flushing the disks`・`… stopping the network` の後、`ZYBO RESET FAILED` は出ず、続く `jtag-probe.sh` で CPU0 は BootROM（pc `0xffffff28`）、PLL・ARM_CLK_CTRL は reset の値、REBOOT_STATUS `0x00480002`: SLCR の reset が効いた。
- init の 4 行の「no definition」と 1 行の skip は、rc.conf が名を挙げる service の program が root に無い事の報告（想定どおり）。

### QEMU（T1 に依頼、未実施）

下の「T1 への依頼」。

## T1 への依頼（2026-10-23、Q1 経由）

worktree `/home/awe/zedBSD-worktrees/p2-zybo-p010`（0b0c5f3f8 以降）。担当が作った image をそのまま使える（build/zybo/vmunix・root.img、build/armv7-virt/vmunix.bin・root.img）。作り直す時は `make ZEDBSD_CONFIG=config/ci/config-zybo.mk -j16 vmunix build/zybo/root.img` と `make ZEDBSD_CONFIG=config/ci/config-armv7-virt.mk -j16 vmunix build/armv7-virt/vmunix.bin build/armv7-virt/root.img`（build/llvm・llvm-source・llvm-build・NoctLang は main への symlink が要る）。

- A（zynq、serial、U3）: `plan/tools/zybo/qemu-zynq.sh build/zybo/vmunix --initrd build/zybo/root.img --timeout 180 --expect 'boot: starting init /sbin/init' --expect 'login: *$' --then type:root --then expect:Password: --then type:root --then 'expect:root@kei' --then 'type:uname -m' --then 'expect:^armv7$'` → exit 0 で PASS。`init:` の行と login 以降の行を写して返す。
- B（virt、PNG）: `BOOT_MODE=armv7-virt INITRD=build/armv7-virt/root.img BOOT_PATTERN='login:' BOOT_THEN=$'type:root\nexpect:Password:\ntype:root\nexpect:root@kei\ntype:uname -m\nexpect:armv7' plan/tools/boot-test.sh build/armv7-virt/vmunix.bin` → exit 0 と PNG（login の画面と、login の後の画面に `armv7`）。prompt が違えば画面に合わせてよい（報告に書く）。
- 失敗の時: 最後に見えた行（A は uart の行、B は PNG）を返す（解析は担当）。

## 残り・引き継ぎ

- T1 の A・B の結果と Q1 の判定・merge。
- `make … build/p010-rpi4/bin/ls` は arm64 の sysroot を先に作らないと header が見つからない（`bin/ls` の規則が sysroot の完了の印に依存していない）。今回の範囲の外、既存。
- WS289 で root を SD に移す時は、この最小の組を元に SD の root の組を決める（rootfs.mk の tree はそのまま使える）。
