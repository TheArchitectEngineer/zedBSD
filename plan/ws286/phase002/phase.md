<!-- awesome-plan project=zedbsd record=ws286-p002 -->

# ws286-p002 JTAG の道具と U-Boot SPL（kernel 無し、LLVM 不要）、板の probe

- 親: [ws286](../ws.md)、設計: [design.md](../design.md) §4（版 2）・§8 の p002 の行、review: [phase001/review-20261023.md](../phase001/review-20261023.md)（L4・L5・M9 を反映）。
- Queue: Q1 の割当（P1、2026-10-23、worktree `/home/awe/zedBSD-worktrees/p1-zybo-p002`・branch `agent/p1-zybo-p002`、基点 main eb5e334b8）。
- 判断: U4（2026-10-23 ユーザー「U項目は読みましたが、全部推しでOKでした。」）→ p002 の担当が板（JTAG・UART）を直に操作する。host の lock `/tmp/zedbsd-zybo-board.lock` を取ってから。
- Status: in-progress（実装と実機の確かめは済み。clearance は Q1 の判定）。

## 目的と受け入れの条件

1. `plan/tools/zybo/openocd-zybo.cfg`（FT2232H の digilent の配線、Zynq-7000 の TAP・DAP・2 core、PL の TAP 0x03722093）と reset・halt の helper、`jtag-boot.sh`（design §4.1 の 1〜7）。
2. U-Boot v2025.07 の SPL を ZYBO 向けに tree の外で build し、JTAG の boot mode で DDR の不定の中身へ飛ばない形にする。再現の script `plan/tools/zybo/spl-build.sh`。
3. 実機: JTAG での reset（SRST が無ければ DAP → SLCR `PSS_RST_CTRL`、DAP が落ちるか = R15）、SPL を OCM に置いて走らせ UART1 に banner、DDR を JTAG で書き読み、約 8 MB の load の速さ。
4. この phase.md に command・出力・版・license を記録し、Master の Tools の登録の文を Q1 に渡す。

## 作った物

| file | 中身 | license |
| --- | --- | --- |
| `plan/tools/zybo/openocd-zybo.cfg` | interface（install の `digilent-hs1.cfg` + `adapter serial 210279785182`）、`adapter speed` 既定 15000 kHz（`JTAG_KHZ`）、server は既定で無し（`ZYBO_GDB_PORT` で gdb を開く）、TAP 2 つ・DAP・`zynq.cpu0`・`zynq.cpu1`（`-defer-examine`、smp にしない）・`zynq.ahb`（AP0 の mem_ap）、CPU0 の work area（高位 OCM `0xFFFF0000` 32 KiB、verify の checksum 用）、examine の前の DBGDSCR の DCC fast mode の解除、proc `zybo_rd`/`zybo_wr`/`zybo_pc`/`zybo_ps_reset`/`zybo_ps_reset_via_cpu`/`zybo_check_jtag_mode`/`zybo_load`/`zybo_load_elf`/`zybo_spl`/`zybo_enter`/`zybo_pl_enable` | Zlib（値は OpenOCD 0.12.0 `target/zynq_7000.cfg`・U-Boot `hardware.h` から引いた、と block の comment） |
| `plan/tools/zybo/zybo-common.sh` | 共通: lock（`ZYBO_LOCK_HELD` で子へ渡す）、`zybo_openocd`（timeout 付き）、UART の path（by-id）、`dialout` が未だの login で `sg dialout` | Zlib |
| `plan/tools/zybo/jtag-reset.sh [--halt]` | AHB-AP で SLCR unlock + `PSS_RST_CTRL`=1（駄目なら CPU0 経由）、1 s 後に別の process で attach し直し `BOOT_MODE` を確かめる。`--halt` は CPU0 を BootROM の待ちで止めたままにする | Zlib |
| `plan/tools/zybo/jtag-probe.sh` | 状態を変えない probe: scan chain、AP0/AP1 の型、SLCR の ID・boot mode・OCM・clock・PL の register、CPU0・CPU1 の pc（止めて戻す）と CPU1 の BootROM の loop の逆アセンブル。DDR は読まない | Zlib |
| `plan/tools/zybo/jtag-boot.sh KERNEL [--dtb] [--append] [--initrd] [--no-reset] [--spl-only] [--expect REGEX]… [--timeout S]` | design §4.1。KERNEL は raw（`0x00100000` に置き先頭から入る）か ELF（PT_LOAD を物理 address に、`e_entry` の物理 address から入る）。kernel が `0x00100000`〜DTB の間に収まるか、initrd が `0x1FC00000` の下かを load の前に確かめる（review L5）。DTB を渡さない時は `memory@0` 512 MiB・`/chosen` の `bootargs`・`linux,initrd-start/end` の DTB を `build/zybo-boot/` に `dtc` で作る。`--expect` と SPL の banner は UART の生きた line で待つ（`uart-expect.py`） | Zlib |
| `plan/tools/zybo/uart-expect.py` | UART の生きた line を raw 115200 8N1 で開き、来た行を表示し、全部の REGEX が出たら 0、timeout で 1。file に残さない（U3）。`serial.py --device`（Q1）が来るまでの ZYBO の代わり | Zlib |
| `plan/tools/zybo/spl-build.sh [BUILD]` | U-Boot v2025.07 の tarball を取り SHA-256 を照合、展開、`xilinx_zynq_virt_defconfig` + fragment（下）、`make DEVICE_TREE=zynq-zybo spl/u-boot-spl.bin`、ZYBO の `ps7_init_gpl.o` が入った事と `board_boot_order` の symbol を確かめる。既定の BUILD は `build/zybo-spl` | Zlib（script）。SPL は GPL-2.0+ で build/ の中だけ |
| `plan/ws286/tests/jtag-hello.S`・`jtag-hello.sh` | zedBSD の kernel の代わりの 264 byte の program（UART1 に r2・FDT の magic・image の和を出す）と、それを 8 MiB の raw と ELF + 4 MiB の initrd で `jtag-boot.sh` に通す試験（和は host で計算、seed 固定の乱数）。p005 で jtag-boot を直した時の確かめに使う | Zlib |

SPL の config の fragment（`spl-build.sh` が書く）:

- `CONFIG_DEFAULT_DEVICE_TREE="zynq-zybo"`（`board/xilinx/zynq/Makefile` がこれで `zynq-zybo/ps7_init_gpl.c` を選ぶ。make の `DEVICE_TREE=zynq-zybo` も渡す）。
- `# CONFIG_SPL_RAM_DEVICE is not set`（設計の「`board_boot_order` で止める」の帯と紐: breakpoint が外れても、RAM の boot device が無いので SPL は `failed to boot from all boot devices` → `hang()` で止まり、DDR の中身へ飛ばない）。breakpoint は `jtag-boot.sh` が ELF の `board_boot_order`（`0x00000fd8`）に置く。
- `# CONFIG_TOOLS_MKEFICAPSULE is not set`（host の tool が gnutls の header を要る。SPL には不要。apt の追加を避けた）。
- `SOURCE_DATE_EPOCH=1751918313`（release の署名の時刻）で banner の日付を固定。

## 実行した command と結果（2026-10-23、P1、実機 = ZYBO、QEMU は無し）

### host

- `git worktree add /home/awe/zedBSD-worktrees/p1-zybo-p002 -b agent/p1-zybo-p002 main`（eb5e334b8）。
- 版: OpenOCD 0.12.0（Debian 0.12.0-3+b2）、openFPGALoader v0.13.1、`gcc-arm-linux-gnueabihf` 4:14.2.0-1（gcc 14.2.0、既に入っていた、apt の追加は無し）、`binutils-arm-linux-gnueabihf` 2.44-3、`device-tree-compiler` 1.7.2-2+b1、Python 3.13.5。共有の LLVM は使っていない。
- U-Boot: `https://ftp.denx.de/pub/u-boot/u-boot-2025.07.tar.bz2`（33516210 byte）、SHA-256 `0f933f6c5a426895bf306e93e6ac53c60870e4b54cda56d95211bec99e63bec7`。`.sig` を専用の keyring（`build/zybo-dl/gnupg`）で照合: `Good signature from "Thomas Rini <trini@konsulko.com>"`、key `1A3C7F70E08FAB1707809BBF147C39FF9634B72C`（key は今は期限切れ、署名は 2025-07-08 04:58 JST で期限内）。展開した `arch/arm/mach-zynq/spl.c`・`common/spl/spl.c`・`zynq-zybo/ps7_init_gpl.c`・`xilinx_zynq_virt_defconfig` の SHA-256 の先頭 16 桁は design §0 の値と一致。
- `plan/tools/zybo/spl-build.sh`（空の `build/zybo-spl` から）: 29 s、make の出力に warning 0、`u-boot-spl.bin` 127245 byte、SHA-256 `ed936c133280ec55b01d78ad8f234a379b8677654b4eef8ec72e2173a9454632`、`board_boot_order 0x00000fd8`、ELF の LOAD は `0x00000000`（0x1e95c byte）と BSS `0x00100000`。同じ source を 2 回 build して同じ binary になるかは未確認。
- 最初の build の失敗: `tools/mkeficapsule.c: gnutls/gnutls.h: No such file or directory` → fragment で外した。
- `/dev/bus/usb/001/005`（FT2232H）が `root:root` で OpenOCD が `LIBUSB_ERROR_ACCESS` → `sudo udevadm trigger --action=change --subsystem-match=usb --attr-match=idVendor=0403 --attr-match=idProduct=6010`（host の device の権限、AGENTS.md の sudo の範囲）で udev の規則（`60-openocd.rules` の plugdev）を当て直し、以後 OpenOCD は sudo 無し。UART（dialout）はこの login に未だ入っていないので `sg dialout`（`zybo-common.sh` が自動で）。

### 板の probe（`jtag-probe.sh` と手の OpenOCD）

- scan chain: `zynq_pl.bs` IDCODE **`0x13722093`**（version 1）、`zynq.cpu` `0x4ba00477`。→ design §4.3 の「upstream の列に無い」は誤り: openFPGALoader は version を落として `0x03722093` と出すが、実際の値 `0x13722093` は upstream の `zynq_7000.cfg` の列に有る。自分の cfg にする理由は `target smp` を避ける事だけ（cfg は `-ignore-version` で両方を受ける）。
- AP0 = `MEM-AP AHB3`（AP ID 0x44770001、system の bus、ROM table 無し）、AP1 = `MEM-AP APB2 or APB3`（debug、ROM table 0x80000000、Cortex-A9 の debug 0x80090000・0x80092000、PTM・CTI・ETB・TPIU・ITM）。design §4.3 の「AP0 が AHB-AP は未確認」を確認。
- SLCR（reset の直後）: `PSS_IDCODE` 0x13722093、`BOOT_MODE` 0x00000000（JTAG）、`OCM_CFG` 0x00000018（低位 192 KiB + 高位 64 KiB）、PLL は全部 0x0001a008（reset 値）、`FPGA_RST_CTRL` 0x01f33f0f、`LVL_SHFTR_EN` 0、`A9_CPU_RST_CTRL` 0、`DEVCFG_STATUS` 0x40000a30、`REBOOT_STATUS` 0x00480002（SLCR の soft reset の後）。
- CPU0: BootROM の JTAG の待ち `0xFFFFFF20: dsb; wfe; b 0xffffff20`、pc 0xffffff28、System mode（cpsr 0x200001df）、MMU・cache 無し。
- CPU1（R3・R20 の材料）: pc 0xffffff34、SVC（cpsr 0x000001d3）、loop は `0xFFFFFF2C: dsb; wfe; mvn r0,#0xf; ldr lr,[r0]; cmn lr,#0xd4; beq 0xffffff2c; mcr p15,0,r0,c7,c5,0; mcr p15,0,r0,c7,c5,6; …`。`0xFFFFFFF0` = 0xffffff2c（loop 自身の address）。→ **`0xFFFFFFF0` に 0xFFFFFF2C 以外の入口を書いて `sev` すると、CPU1 は I-cache・分岐予測を invalidate して lr（= その入口）へ行く**（design §1.3 の方法 1 を実機の code で確認。jump の命令自体は 0xFFFFFF4C 以降で未読）。
- OCM の書き読み（CPU0 を止め、AHB-AP で書いて AHB-AP と CPU で読む、元の値に戻す）: `0x00000000`・`0x00010000`・`0x0002FFFC`・`0xFFFF0000`・`0xFFFF8000` の全部で一致。
- SPL の後（CPU0 で読む）: SCU `CTRL` 0x00000002（SCU は無効）、`FILTER_START` 0x00100000、`FILTER_END` 0xffe00000、PL310 `CACHE_ID` 0x410000c8（実機にも PL310。review H1 の「QEMU も同じ ID」と合わせ、判別に CACHE_ID は使えない）、L2 `CTRL` 0（無効）。
- SPL の後の CPU0 から見た PA の低位（review M9 の材料）: `0x00000000`〜`0x0002FFFF` = 低位 OCM、**`0x00030000`〜`0x000EFFFF` は data abort**（DFSR 0x1008、同期の external abort、0x8000 毎に調べた）、**`0x000F0000`〜`0x000FFFFF` は高位 OCM `0xFFFF0000`〜`0xFFFFFFFF` の alias**（`0xF8000` に書いた値が `0xFFFF8000` で読め、`0x000FFFF0` = 0xffffff2c）。→ kernel は JTAG の流れで PA `0x0`〜`0xFFFFF` に触れない（特に `0xFFF00`〜 は CPU1 の BootROM の loop の code）。design の「0x0〜0xFFFFF を予約」で足りるが、direct map の最初の 1 MiB の section を張らない事（M9 の修正案）を支持する。
- SPL の後の clock: `ARM_PLL_CTRL` 0x0001a008（FDIV 26）、`DDR_PLL_CTRL` 0x00015008（21）、`IO_PLL_CTRL` 0x00014008（20）、`ARM_CLK_CTRL` 0x1f000200（DIVISOR 2）、`CLK_621_TRUE` 1、`APER_CLK_CTRL` 0x01ec044d（design §1.2 の値と一致）、`UART_CLK_CTRL` 0x00000a02（DIV 10、UART1）、UART1 `BAUDGEN` 0xae・`BAUDDIV` 4（100 MHz / (174 × 5) = 114943 baud、U-Boot の選んだ値）、`PCAP_CLK_CTRL` 0x00000501、`FPGA0_CLK_CTRL` 0x00100a00（FCLK0 100 MHz、R22）、`FPGA_RST_CTRL` 0x01f33f0f と `LVL_SHFTR_EN` 0（`ps7_post_config` は走らない、design §4.2 のとおり）、DDRC `CTRL` 0x00000081、`MODE_STS` 0x00000001（normal）。

### reset（R15）

- ZYBO の JTAG に SRST は無い（`digilent-hs1.cfg` は `reset_config none`）。DAP → AHB-AP → SLCR unlock（0xDF0D）+ `PSS_RST_CTRL`=1 で reset できた。書いた session は shutdown で `Invalid ACK (0) in DAP response`（DAP が system と一緒に落ちる）。1 s 後の新しい OpenOCD の process は普通に attach し、CPU0 は BootROM の待ち（pc 0xffffff28）、`REBOOT_STATUS` 0x00480002。CPU0 が止まった状態からの reset も可。→ **R15: DAP は落ちるが、別の process で立て直せる**（design §4.4 の形で正しい）。USB の抜き差しは一度も要らなかった。
- 踏んだ 2 つの罠と対処:
  1. **DDRC の設定の前に AHB-AP で DDR（`0x00100000`）を読むと AHB-AP が固まる**（次の session の mem_ap の examine が `JTAG-DP STICKY ERROR`、AHB-AP の reset も効かない）。CPU0 経由の reset（`zybo_ps_reset_via_cpu`）で戻った。→ `zybo_rd` の comment と `jtag-probe.sh` は DDR を読まない。
  2. **CPU0 経由の reset は CPU0 の DBGDSCR を DCC fast mode（ExtDCCmode=0b10、DSCR 0x03286003）のまま残し**、debug の reset domain は PS の reset で戻らないので、以後の examine が `timeout waiting for DSCR bit change` で全部落ちる。cfg の `examine-start` の event で ExtDCCmode を 0 に戻す（`zybo: CPU0 DBGDSCR 0x03206002 was in DCC fast mode; cleared` が出て直る事を、CPU0 経由の reset の直後で確かめた）。
- `jtag-reset.sh` を 2 回（`--halt` 有りと無し）、`jtag-boot.sh --spl-only` を 3 回続けて、全部 rc 0。

### SPL（`jtag-boot.sh --spl-only`、3 回続けて）

```
zybo: BOOT_MODE 0x00000000
zybo: reset done
uart| U-Boot SPL 2025.07 (Jul 07 2025 - 19:58:33 +0000)
uart| Silicon version:	3
zybo: loaded 127245 bytes at 0x00000000 in 649 ms (191.2 KiB/s)
zybo: SPL halted at 0x00000fd8
```

（`uart|` は `uart-expect.py` が生きた line から表示した行。`Silicon version: 3` は SPL の `ps7_init` が選んだ表の版。breakpoint は 3 回とも `board_boot_order` に当たった。）

### DDR と load の速さ

- `zybo_load`（CPU0 の `load_image` + `verify_image`、verify は work area の checksum）。random の 4 MiB を `0x00200000`、8 MiB を `0x00800000`、4 MiB を `0x1FC00000`（512 MiB の最後）に置き、全部 verify が通った。8 MiB の後に `0x00C00000` の 1 word を書き換えると verify が `diff 0 address 0x00c00000` で落ちる（verify が本当に比べている事の確かめ）。
- address の alias の確かめ: `0x00100000`・`0x04000000`・`0x08000000`・`0x10000000`・`0x1FFFFFFC` に別の値を書き、CPU と AHB-AP の両方で各々の値が読めた（512 MiB の全体が別々に見える、SPL の後は AHB-AP でも DDR を読める）。
- 速さ（CPU0 の DCC 経由）: 10 MHz で 4 MiB 20.3 s（202 KiB/s）、15 MHz で 4 MiB 15.9 s（258 KiB/s）、20 MHz（`ftdi tdo_sample_edge falling`）も 258 KiB/s で頭打ち（USB の往復が律速）、30 MHz は scan chain の IR capture が崩れて使えない。AHB-AP（`zynq.ahb` の `load_image`）は 10 MHz で 4 MiB 47.2 s（87 KiB/s）で遅い。→ 既定 15 MHz、**8 MiB = 31.6〜32.7 s（約 250〜259 KiB/s）**。R17: 最小の ramdisk（数 MB）+ kernel で 1 回の起動の load は 30〜60 s の見込み。
- 全体の道（`plan/ws286/tests/jtag-hello.sh`、1 分 0 秒で PASS）: 8 MiB の raw の stand-in と、ELF + 4 MiB の initrd の 2 つを `jtag-boot.sh` で起動し、UART に `JTAG HELLO r2=01f00000 fdt=ok sum=37ae407b`（raw、host の和と一致）と `JTAG HELLO r2=01f00000 fdt=ok sum=c1cf916b`（ELF）。r2 の DTB の受け渡し、CPU から見た DDR の中身（8 MiB 全部の和）、ELF の物理 address への load と入口、`/chosen` の `linux,initrd-start` 0x4000000・`-end` 0x4400000・`bootargs` を確かめた。範囲の外の kernel（40 MiB）は load の前に `jtag-boot: the kernel (0x00100000-0x02900000) is not within 0x00100000-0x01F00000` で止まる。

### PL の有効化（p003 の前の確かめだけ）

- `zybo_pl_enable` の後に `LVL_SHFTR_EN` 0x0000000f、`FPGA_RST_CTRL` 0x00000000 を読み戻した（PL は未設定のまま、GP0 には触れていない。直後に reset した）。

## license

- U-Boot（SPL、`ps7_init_gpl.c` を含む）は GPL-2.0+。tarball・source・binary は worktree の `build/`（`.gitignore` の `/build/`）にだけ置き、commit も配布もしない。tree に入れたのは自分の Zlib の script と cfg だけ（OpenOCD・U-Boot の値は「Values referenced:」で block に出典を書いた）。
- OpenOCD（GPL-2.0-or-later）の install の `digilent-hs1.cfg` は写さず `source [find …]` で読む。

## 未実施・残件

- 同じ source からの SPL の build が bit 一致するか（reproducible）は未確認。
- `jtag-boot.sh --no-reset` の道（SPL を走らせず、前の run が上げた DDR に置く）は書いたが板では未実施。
- `ZYBO_GDB_PORT` で gdb を付ける道は未実施（cfg の記述だけ）。
- CPU1 の BootROM の loop の最後の jump の命令（0xFFFFFF4C 以降）は読んでいない。
- OpenOCD を timeout で kill した時の DBGDSCR の halting debug の bit（review L4）は試していない。`jtag-boot.sh`・`jtag-reset.sh` は `shutdown` で終え、kill の後は `jtag-reset.sh` を挟む運用。DCC fast mode の残りは cfg が直す。
- design §4 への書き戻しは「p002 の実測」の段落（§4.8）だけ。§4.3 の IDCODE の文、§1.3 の CPU1 の起こし方、§3.12 の低位 1 MiB の扱いは、review の反映（版 3）の時に直す事を推す。
- Master の Tools への登録（Q1）。`serial.py --device`（Q1）が来たら `uart-expect.py` は置き換えてよい。
- `build/zybo-uboot/`・`build/zybo-dl/`（手で最初に作った build と tarball）、`build/rand-4m.bin`・`build/rand-8m.bin` は不要（Q1 が消してよい、worktree の build/ の中）。
