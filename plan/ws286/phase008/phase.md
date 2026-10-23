<!-- awesome-plan project=zedbsd record=ws286-p008 -->

# ws286-p008 PL を zedBSD から（devcfg/PCAP、/dev/fpga0、fpgaload・fpgareg、GP0 の窓、PL の割り込み ID 61）

- 親: [ws286](../ws.md)、設計: [design.md](../design.md) 版 3 の §5.6・§5.6.1〜§5.6.3・§5.10・§7.3・§7.7・§8 の p008 の行。**WS286 の到達目標**。
- Queue: Q1 の割当（P2、2026-10-23、worktree `/home/awe/zedBSD-worktrees/p2-zybo-p008`、branch `agent/p2-zybo-p008`、基点 main 1a2a6b79c、途中で main 4f2ab03bc（WS289 p004 の SD を含む）を merge）。
- 判断と許可: U3（QEMU zynq と実機は生きた serial で判定）、U4（担当が板を JTAG で直に操作、host の lock を取って。P3 の WS289 p004 と共有）、U6（a）（`pl-build.sh` の出力を image に `--file` で入れる）。2026-10-23 ユーザー（Q1 経由）「FPGA work uses Vivado (2026.1, BASIC) unless something blocks it; the existing openXC7 plio bitstream may be used as is」→ RTL は変えず、既存の openXC7 の plio をそのまま使った（Vivado は使っていない）。HAL の API は変えていない。toolchain に触れていない。
- Status: in-progress（実機で §7.7 の 1〜5 PASS。T1 の QEMU zynq と Q1 の判定待ち）

## 受け入れの条件（design §8 の p008、§7.7、§7.3）

1. 実機（JTAG で起動、ramdisk の root）: `fpgaload` が plio の bitstream を書き、`fpgareg` が ID `0x504c494f`・SCRATCH・COUNTER・LED を読み書きし、割り込みを見る（§7.7 の 1〜5）。
2. QEMU zynq（T1）: `fpga.no-pl-wait=1` で `fpgaload --status` と `fpgaload plio.bit` が `EMULATED` を返す（窓は試さない）。
3. zybo・armv7-virt・amd64・rpi4 の build が warning 0。host 試験（devcfg・bitstream・uapi）PASS。

## 作った物

- UAPI `include/uapi/fpga.h`（新規）: ioctl の group `'F'`（未使用を確かめた）、`struct fpga_status`（20 byte）・`fpga_reg`（8）・`fpga_irq_wait`（8）、`FPGA_PROGRAM`・`STATUS`・`ENABLE`・`DISABLE`・`REG_READ`・`REG_WRITE`・`IRQ_WAIT`、flag `DONE`・`INIT`・`ENABLED`・`PROGRAMMING`・`EMULATED`、`FPGA_BITSTREAM_MAX` 4 MiB。
- kernel `src/drivers/platform/zybo/zynq-devcfg.c`（新規）と `zynq-devcfg-check.c`（hardware の要らない確かめ: CTRL の mask、sync word、DMA の揃え）、header `include/drivers/platform/zybo/zynq-devcfg.h`。`zybo.c` の `kern_platform_refresh_devices` で 1 回 attach、`platform/armv7/zybo.mk` に source。
  - `/dev/fpga0`（cdev、device number `0x00190000`、未使用を確かめた）。open は root だけ（`cred_is_superuser`、input-bridge と同じ）。`src/kern/devfs.c` に `fpga0` の mode 0600 の 1 行（Q1 の依頼、2026-10-23「Make /dev/fpga0 mode 0600 with a devfs line as well」）。
  - write(2) は open ごとの 4 MiB の uncached の DMA の buffer（最初の write で `drv_dma_alloc_coherent`、close で解放）に積む。`FPGA_PROGRAM` は design §5.6 の手順 1〜9: PCAP の clock、`FPGA_RST_CTRL`=0xF・`LVL_SHFTR_EN` 0→0xA、devcfg `UNLOCK`=0x757BDF0D、PROG_B の 1→INIT→0→!INIT→1→INIT（各 2.5 s）、`PCAP_PR|PCAP_MODE`、DMA の queue の確かめ、`PCAP_LPBK` を落とす、`INT_MASK`・`INT_STS` を全部、1 回の DMA（src | 1、dst 0xFFFFFFFF、word 数）、`D_P_DONE` か error を 5 s poll、`INT_STS` は PCFG_DONE を残して消す、`PCFG_DONE` を 2.5 s、`PCAP_PR` を落とす、有効化（`LVL_SHFTR_EN`=0xF、`FPGA_RST_CTRL`=0、窓 `0x43C00000` 64 KiB を `kern_device_map`）。CTRL は `ctrl_update(set, clear)` だけが read-modify-write し、許す bit（30・27・26・25）の外は `HAL_FATAL`。
  - 窓の `REG_READ/WRITE` は `ENABLED` の時だけ（それ以外 `EIO`）、4 の倍数で窓の中（それ以外 `EINVAL`）、device の mutex の下（`DISABLE`・`PROGRAM` が窓を外す間に access しない）。
  - 割り込み ID 61（level・high、`kern_irq_set_mode`）: handler は mask して数え、待ち手を起こす。`FPGA_IRQ_WAIT` は design §5.6.2 の L7 の意味（保留 0 なら unmask して眠る、保留を返して 0 に、待ち手 1 つ、`ETIMEDOUT`・`EINTR`・`EIO`）。`DISABLE` は mask して保留を捨てる。
  - attach は PL に触らず `INT_STS` も消さない（JTAG で書いた PL の `fpgaload --enable` が PCFG_DONE を読む）。
  - `fpga.no-pl-wait=1`: `PSS_IDCODE` が 0（QEMU）の時だけ INIT と PCFG_DONE の待ちを飛ばし `EMULATED`、有効にしない。実機では警告して無視。boot parameter の表には足さず、`kern_boot_parameters_token_present` で読む（`kmsg=quiet` と同じ。boot.c は変えていない）。`docs/reference/kernel-boot-parameters.md` の §7d。
  - SLCR は WS289 p004 の `drv_zynq_slcr_*`（spinlock の下で unlock・write・lock）を使う（main の merge の後に自分の SLCR の map から切り替えた。SD の clock の書き込みと unlock/lock が入り混じらない）。
- userland（各 1 package・1 dir。design §5.6.3 は 1 つの dir と書いたが tree の慣習に合わせた、Q1 ACK）: `userland/base/fpgaload/`（`main.c`・`bitstream.c`・`bitstream.h`: `.bit` の header（U-Boot v2025.07 `drivers/fpga/xilinx.c` の `fpga_loadbitstream` の field）を外し、sync word の向きで word の byte を反転、`.bin` はどちらの向きも受ける、64 KiB ずつ write → `FPGA_PROGRAM`、`--status`・`--enable`・`--disable`）、`userland/base/fpgareg/`（`read OFFSET`、`write OFFSET VALUE`、`wait [--ack OFFSET] [TIMEOUT_MS]`: 待って STATUS を読み、W1C で書き戻す。既定の ack は 0x18、timeout 1000 ms）。install は `/sbin`、既定では選ばれない（DEFAULT n）。
- `platform/armv7/userland.mk`: `ARMV7_ROOT_EXTRA_COMMANDS ?=`（config が既定の root の組に足す口）。
- 試験の config `plan/ws286/tests/config-zybo-fpga.mk`: zybo の config ＋ `ARMV7_ROOT_EXTRA_COMMANDS := fpgaload fpgareg`、`ZEDBSD_EXTRA_FILES` で `/usr/share/fpga/plio.bit`（`build/zybo-pl/plio/plio.bit`、先に `plan/tools/zybo/pl-build.sh platform/armv7/zybo/pl/plio`）、`ARMV7_ROOT_MIB := 6`（root.img 7 MiB、JTAG で約 29 s）。
- host 試験: `plan/ws286/tests/devcfg-host-test.c`（CTRL の mask の 9 項目、sync word 5、DMA 6、UAPI の大きさ・offset・ioctl の番号）、`bitstream-host-test.c`（作った .bit・.bin の両方の向き・壊れた header・sync 無し・半端な word、実の plio.bit を引数に）。command は各 file の頭。

## 確認

### host（2026-10-23 P2）

| command | 結果 |
| --- | --- |
| `make ZEDBSD_CONFIG=config/ci/config-zybo.mk -j16 vmunix build/zybo/root.img`（merge の後） | rc 0、warning・error 0 |
| `make ZEDBSD_CONFIG=config/ci/config-armv7-virt.mk -j16 vmunix build/armv7-virt/vmunix.bin build/armv7-virt/root.img` | rc 0、warning 0 |
| `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/q8-amd64 -j12 vmunix` | rc 0、warning 0、kernel include check PASS（553 objects）、amd64 vmunix check PASS |
| `make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/q8-rpi4 -j12 vmunix` | rc 0、warning 0、arm64 vmunix check PASS |
| `make ZEDBSD_CONFIG=plan/ws286/tests/config-zybo-fpga.mk BUILD=build/zybo-fpga -j16 vmunix build/zybo-fpga/root.img` | rc 0、warning 0。vmunix.bin `61093c6b…`、root.img `fa2102e8…` |
| `plan/tools/zybo/pl-build.sh platform/armv7/zybo/pl/plio` | rc 0、Fmax 248.08 MHz、plio.bit 2083843 byte、`074b4785…` |
| `devcfg-host-test` | `PASS devcfg-host-test` |
| `bitstream-host-test build/zybo-pl/plio/plio.bit` | `real file: 2083740 bytes of data, sync at 48`、`PASS bitstream-host-test` |
| `git diff --check` | 出力なし |

### 実機 ZYBO（JTAG、2026-10-23 14:4x〜14:5x P2、U4、board lock を取って。実機の証拠）

`plan/tools/zybo/jtag-boot.sh build/zybo-fpga/vmunix.bin --initrd build/zybo-fpga/root.img --timeout 300 --expect 'login: *$' --then type:root … --then type:<command> --then expect:<regex> …`（4 回、全部 rc 0。最後の 2 回は merge の後の tree）。UART の抜粋（§7.7 の番号）:

```
fpga: devcfg at f8007000, status 40000a30, int_sts a802000a
crw------- 1 root wheel 0 Jan  1  1970 /dev/fpga0
1. fpgaload --status            → DONE 0 INIT 1 ENABLED 0 … devcfg_status 0x40000a30
   fpgareg read 0               → fpgareg: read 0x0: Input/output error
2. fpgaload /usr/share/fpga/plio.bit → 2083740 bytes programmed
                                  DONE 1 INIT 1 ENABLED 1 … devcfg_status 0x40000f30
   fpgareg read 0               → 0x504c494f
   fpgareg read 4               → 0x00000001
3. write 0x10 0x12345678 / read → 0x12345678;  0xdeadbeef → 0xdeadbeef;  0 → 0x00000000
   read 0x14 twice              → 0x1cad80d4, 0x1fe6cd0a（増える）
   write 8 5 / read 8           → 0x00000005;  read 0xc → 0x00000000
4. write 0x1c 2; write 0x20 2; wait --ack 0x18 1000 → irq count 1 status 0x00000002
   read 0x18                    → 0x00000000
   wait 100                     → fpgareg: wait: Connection timed out
   read 0x1000 → 0x00000000;  read 0x10002 → Invalid argument
5. fpgaload --disable; --status → DONE 1 INIT 1 ENABLED 0;  read 0 → Input/output error;  wait 10 → Input/output error
   再度 fpgaload plio.bit       → DONE 1 ENABLED 1、read 0x10 → 0x00000000（書き直しで SCRATCH が 0 に戻る）
```

- §7.7 の 5 の JTAG の道（zedBSD が走ったまま、`--disable` の後）: `plan/tools/zybo/pl-jtag.sh build/zybo-pl/plio/plio.bit`（`isc_done 1 … done 1`、3 s）→ `flock /tmp/zedbsd-zybo-board.lock python3 -I plan/tools/zybo/uart-expect.py --timeout 60 --then type: … ''` で: `fpgaload --status` → `DONE 1 INIT 1 ENABLED 0`、`fpgaload --enable`、`fpgareg read 0` → `0x504c494f`、SCRATCH `0xcafef00d` の書き読み、IRQ の `irq count 1 status 0x00000002`、`wait 100` → timed out。`--force` は要らなかった（JTAG の書き込みで INT_STS の PCFG_DONE が立つ、p003 の観察どおり）。
- 最後の run（style の直しの後）: `--status` DONE 0 → program → ID・SCRATCH `0x13572468`・IRQ・timeout・`--disable` で EIO・`--enable` で `read 4` = 1・`--bogus` で usage。
- 人の目が要る物（未実施、ユーザーの一覧へ）: `fpgareg write 8 5` で LD0・LD2 が光る、SW・BTN で `read 0xc` が変わる、BTN で `wait` が返る（STATUS[0]）。

### QEMU（T1 に依頼、未実施）

下の「T1 への依頼」。

## T1 への依頼（2026-10-23、Q1 経由）

worktree `/home/awe/zedBSD-worktrees/p2-zybo-p008`（4861dd5ce 以降）。image は担当が作った `build/zybo-fpga/vmunix`（ELF）・`build/zybo-fpga/root.img` をそのまま使える。作り直す時は `plan/tools/zybo/pl-build.sh platform/armv7/zybo/pl/plio` の後に `make ZEDBSD_CONFIG=plan/ws286/tests/config-zybo-fpga.mk BUILD=build/zybo-fpga -j16 vmunix build/zybo-fpga/root.img`。

- A（zynq、serial、U3）: `plan/tools/zybo/qemu-zynq.sh build/zybo-fpga/vmunix --initrd build/zybo-fpga/root.img --append 'fpga.no-pl-wait=1' --timeout 240 --expect 'fpga: devcfg at f8007000.*emulated' --expect 'login: *$' --then type:root --then expect:Password: --then type:root --then 'expect:root@kei' --then 'type:fpgaload --status' --then 'expect:EMULATED 1' --then 'type:fpgaload /usr/share/fpga/plio.bit' --then 'expect:EMULATED 1' --then 'type:fpgareg read 0' --then 'expect:Input/output error'` → exit 0 で PASS。`fpga:` の行と login 以降の行を返す。
- 失敗の時（例: `fpgaload: program: Device or resource busy` は model の DMA の queue の STATUS、`Connection timed out` は D_P_DONE）: 最後の行を返す（解析は担当）。

### QEMU zynq の 1 回目（T1、2026-10-23、f327ed43b）と直し

- T1 の結果（Q1 経由）: FAIL。`fpga: devcfg at f8007000, status 00000000, int_sts 00000000, emulated`、login は通り、`fpgaload --status` → `DONE 0 INIT 0 ENABLED 0 PROGRAMMING 0 EMULATED 1 …`、`fpgaload plio.bit` → kernel `fpga: program failed (17)`・`fpgaload: program: Device or resource busy`。原因: QEMU の devcfg の model は STATUS の DMA の queue の bit を持たず 0 を読む（空の印 `DMA_Q_E` が立たない）ので、手順 5 の queue の確かめが `EBUSY` を返した（担当の予想どおり）。
- 直し（worktree `p2-zybo-p008b`、93d2a053f）: `fpga.skip_waits`（QEMU で `fpga.no-pl-wait=1` の時だけ）の時は queue の空の bit を見ず、満ちた bit だけで拒む。実機の道（skip_waits は 0）は変わらない。`docs/reference/kernel-boot-parameters.md` §7d に 1 文。
- host: `make ZEDBSD_CONFIG=config/ci/config-zybo.mk -j16 vmunix` と `… config-zybo-fpga.mk BUILD=build/zybo-fpga … root.img` は rc 0・warning 0。実機の再試験はしていない（実機で通る道は変わらないため）。T1 の 2 回目を依頼（同じ command、vmunix `b3d60374…`、root.img `05b0c30b…`）。

### QEMU zynq の 2 回目（T1、2026-10-23、fae06f885）と QEMU の限り

- T1 の結果（Q1 経由）: FAIL。`fpgaload plio.bit` → kernel `fpga: program failed (42)`、`fpgaload: program: Connection timed out`（その前の status は `devcfg_status 0x00000000`、`EMULATED 1`）。
- 原因（QEMU の source を読んだ、推測でない）: host の QEMU は Debian の 10.0.11。その版の `hw/dma/xlnx-zynq-devcfg.c`（local の `/home/awe/qemu-pc98` の履歴で、2026-05-18 の修正の前の版 = commit 32222dc3bb の file）では、machine の reset で全 register の reset が `UNLOCK` の `post_write`（`r_unlock_post_write`）を値 0 で呼び、0 は magic でないので「failed unlock」として devcfg の memory region を無効にする（`memory_region_set_enabled(&s->iomem, false)`）。無効の region は読みが 0、書き込みが捨てられ（machine の `ignore_memory_transaction_failures`）、driver の `UNLOCK` の書き込みも届かないので二度と有効にならない。だから STATUS・INT_STS が 0 のまま（attach の行の `status 00000000, int_sts 00000000` もこれ）、`DMA_DST_LEN` の書き込みも DMA を始めず、`D_P_DONE` は来ない。上流の修正は QEMU commit b9b3ca738d「hw/arm/zynq-devcfg: Prevent unintended unlock during initialization」（2026-05-18、`device_is_in_reset` の間は unlock の処理をしない）とその後の b9528999b3（DST 0xFFFFFFFF の bitstream の load で DMA_DONE・DMA_P_DONE・PCFG_DONE）・d5fb3d22d1・0b397a40ff・6457fa3b9d で、10.0.11 には入っていない。
- 判断（Q1 の指示 2026-10-23「if the model cannot complete a PCAP transfer at all, instead change the QEMU acceptance to what it can show (status, EIO window guard, EBUSY/timeout handling) and record that in phase008/phase.md as a QEMU limitation」）: **QEMU zynq の受け入れを、10.0.11 の model が示せる物に改める**: (1) attach の行に `emulated`、(2) `fpgaload --status` が `EMULATED 1`・`DONE 0`、(3) 窓の守り: `fpgareg read 0` が `EIO`、`fpgaload --enable` が `EIO`（DONE 0）、(4) program の timeout の扱い: `fpgaload plio.bit` が 5 s 余りで `Connection timed out` を返して止まらず、続く `fpgaload --status` が `error 42`（ETIMEDOUT）・`ENABLED 0` を示し、shell が使えたまま。PCAP の転送の完了・`EMULATED` での program の成功は QEMU 10.0.11 では示せない（QEMU の限り）。上流の修正の入った QEMU なら示せる見込み（未確認）。driver は変えない（p008b の queue の直しは残す: 修正の入った model では STATUS の reset 値に `DMA_CMD_Q_E` が有るので害はない）。
- T1 の 3 回目を依頼（改めた受け入れ、同じ image: vmunix `b3d60374…`、root.img `05b0c30b…`）。

## design との違い

- userland は 2 つの dir（上）。
- `sysctl hw.fpga.done`（§5.6）は作らなかった: `FPGA_STATUS` と `fpgaload --status` で足りる。要るなら後で。
- boot parameter `fpga.window=BASE,SIZE`（§5.6.2）は作らなかった: 窓は `0x43C00000` 64 KiB に固定（plio だけ）。後の画面の RTL で要れば足す。
- devcfg の割り込み（ID 40）は使わず poll（1 tick ごと、design は「割り込みか poll」）。
- `FPGA_PROGRAM` の同時の呼び出しは `EBUSY` でなく device の mutex で順に待つ（design §6 の「同時の program は EBUSY」と違う。`FPGA_STATUS` は lock を取らずに読むので program の間も `PROGRAMMING` が見える）。

## 残り・引き継ぎ

- T1 の A の結果と Q1 の判定・merge。
- ユーザーの一覧: LED の目視と SW・BTN（上）。
- 後の WS: mmap の窓（画面の RTL）、`fpga.window`、partial reconfiguration。
- p009（規約の全文の見直し）に入れる file: `zynq-devcfg.c`・`zynq-devcfg-check.c`・`zynq-devcfg.h`・`uapi/fpga.h`・`fpgaload/*`・`fpgareg/main.c`・host 試験 2 つ。
