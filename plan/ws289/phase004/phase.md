<!-- awesome-plan project=zedbsd record=ws289-p004 -->

# ws289-p004 ZYBO の SD（SDIO0 の glue、SLCR、CSD）と root を SD の partition に

- 親: [ws289](../ws.md)。設計: [design.md](../design.md) 版 2 の §3.4（glue）・§3.6（root と boot device）・§3.8（CSD）・§4.4 の SLCR の kernel 側・§5.3〜§5.4（image の tool）・§6（`qemu-zynq.sh --sd`）・§7・§8 の p004 の行・§9 の p004 の行。
- Queue: Q1 の割当（P3、2026-10-23、worktree `/home/awe/zedBSD-worktrees/p3-ws289-p004`、branch `agent/p3-ws289-p004`、基点 main 61d52bd54）。Fable は使わない（ユーザー）。
- 判断と許可: U1〜U7 はユーザー「はい」で推しのとおり（design §11）。U3: SD は PIO、p004 の dd の実測が 2 MB/s 未満の時だけ SDMA を前倒し。WS286 の U3（ZYBO の QEMU と実機は生きた serial で判定）・U4（担当が板を JTAG で直に操作）。host の block device には書かない（WS289 U1）。toolchain・HAL の API は変えていない。共有の LLVM・NoctLang は main の物への symlink で読むだけ。
- Status: in-progress（実装と host の確認は済み。QEMU は T1 の結果待ち。実機の SD の確認は板の card が初期化できず止まっている（下の「実機」）。）

## 受け入れの条件（design §8.2・§8.3・§9 の p004）

1. zybo・armv7-virt・rpi4・rpi5・amd64 の kernel の build が warning 0。`sdhci-host-test` PASS。
2. QEMU xilinx-zynq-a9（T1、生きた serial）: `--sd` の image で root が SD の partition（`/dev/mmcblk0p2`）、login、`dd` の書き読みの速さ、QEMU を起動し直してから書いた file の `cksum` が一致（M4）、`--initrd` と `--sd` の両方で root が ramdisk（H1）。raspi4b の boot-test をもう 1 回（CSD の変更の後）。
3. 実機の ZYBO（card が有れば）: JTAG の kernel ＋ root が SD、login、`dd` の速さ。JTAG の ramdisk の起動（initrd が有れば ramdisk が勝つ）が生きている事。

## 作った物

- core `src/drivers/mmc/sdhci.c`（ebf524d23・97305a690）:
  - CSD の復号（design §3.8）: CMD9 の後に応答の 4 word（`RESPONSE0..3`、CSD の bit 127:8 が word の bit 119:0）を読み、CSD_STRUCTURE 0（C_SIZE・C_SIZE_MULT・READ_BL_LEN）と 1（C_SIZE × 512 KiB）だけを大きさにする。他の版（SDUC の 2 など）と 1 block 未満は `0xffffffff` のまま（M6）。`sdhci: csd structure=%u blocks=%llu` を boot log に。bit の位置は QEMU の `sd_set_csd` と `sdhci_send_command` の 16 byte の応答の詰め方で照合した（host 試験の CSD の作り方がその写し）。
  - 実機で見つけた直し（下の「実機」）: 失敗した command の後に CMD line を software reset する（SDHCI の仕様の error の回復。しないと inhibit が立ったままで以後の command が全部 2 s 待って落ちる）。ACMD41 の問い直しの間に 10 ms（Value referenced: OpenBSD `sdmmc_mem_send_op_cond`）。bus の電源と識別の clock の後に各 10 ms（Value referenced: Linux `mmc_power_up` の `power_delay_ms`）。どれも Pi 4 の振る舞いも変える（成功の道は待ちが 20 ms ＋ 10 ms × ACMD41 の回数だけ増え、register の書き込みは失敗の道の CMD の reset だけ増える）→ raspi4b の boot-test を T1 に再依頼（下の D）。
- `src/drivers/platform/zybo/zynq-slcr.c`・`.h`（新規、9d8927232）: `kern_device_map(0xF8000000)`、`drv_zynq_slcr_read`、`drv_zynq_slcr_modify`（spinlock の下で unlock → write → lock）、`drv_zynq_slcr_pll_hz`（FDIV × 50 MHz、bypass なら 50 MHz）。cpu_1x の計算は p005（GEM の MDC の divisor）で足す（p004 では使わないので書いていない）。
- `src/drivers/platform/zybo/zynq-sdhci.c`・`.h`（新規）: SLCR の `APER_CLK_CTRL` bit 10 と `SDIO_CLK_CTRL` の CLKACT0 を見て落ちていれば立てる、base clock = SRCSEL の PLL / DIVISOR（実機で `50000000 Hz (pll 1000000000 Hz / 20)`）、取れなければ 50 MHz と警告、`kern_device_map(0xE0100000)`、card detect は `PRESENT_STATE` bit 16（log だけ）、unit 0 で `drv_sdhci_attach`。割り込み 56 は使わない。
- `src/kern/platform/zybo.c`: boot device を 2 つまで。initrd が有れば ramdisk が 0x80（BOOT_ORIGIN）で SD は 0x81、無ければ SD が 0x80（design §3.6 H1）。`kern_platform_block_device` は bios_id で引く。SD が無い・落ちた時は `zybo: SD card unavailable (%d)` で起動は続く。
- `platform/armv7/zybo.mk`: zybo の kernel に `zynq-slcr.c`・`zynq-sdhci.c`・`src/drivers/mmc/sdhci.c`。
- image（08b070191）: `platform/armv7/tools/make-armv7-root-image.py --raw`（UFS だけ）、`rootfs.mk` の `$(BUILD)/root.ufs`（`ARMV7_SD_ROOT_MIB ?= 256`）、`platform/armv7/tools/make-zybo-sd-image.py`（MBR: p1 FAT16 bootable 0x06 の 64 MiB を mtools で、p2 0xA5 の UFS、全体は 2 の冪の MiB で既定 512 MiB = QEMU の SD の model の制約、`--boot-file DEST=SRC` で FAT に file）、`platform/armv7/tools/check-zybo-sd-image.py`（大きさ、MBR の 2 つの entry、FAT16 の boot sector と大きさ、p2 が root.ufs と byte 一致・UFS の検査・marker、boot file の中身）、`platform/armv7/disk-image.mk`（zybo だけ: `sd-image` → `$(BUILD)/zybo-sd.img`、`ZYBO_SD_BOOT_FILES` は p006 が BOOT.BIN などに使う、`zybo-sd-image-host-test`）。FIT・`system.dtb`・`--elf` の検査は p006。
- root の program（`platform/armv7/userland.mk` の `ARMV7_ROOT_COMMANDS`）に disk の確認の `cksum dd df rm sync time` を足した（zedBSD の `dd` は時間を出さないので `time dd`）。JTAG の `root.img` は 5 MiB のまま（UFS 4 MiB に収まる）。
- `plan/tools/zybo/qemu-zynq.sh --sd IMAGE`（`-drive if=sd,index=0,file=IMAGE,format=raw`）。

### design からの差（Q1 の確認用）

1. design §3.6 の「`--initrd` と `rootpart=` を両方付けても root は SD」は誤り: initrd が有ると ramdisk が 0x80 で loader の boot partition は `ram0p1`（UFS）になり、native の道は boot0 にそれを FAT として mount しようとして VFS の初期化ごと落ちる（vfs.c の `kern_boot_source_context_mount`）。SD の root は **`--initrd` を付けずに** `rootpart=/dev/mmcblk0p2`（design の手順もそう書いている）。
2. image の既定は 384 MiB でなく 512 MiB（QEMU の SD の model は 2 の冪でないと起動しない）。512 MiB の image は QEMU では SDSC（CSD 版 1、byte addressing）になる。
3. core の振る舞いの追加（CMD の reset、待ち）は design に無い。実機の card の調べで入れた（上）。

## 確認

### host（2026-10-23 P3）

| command | 結果 |
| --- | --- |
| `make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p004-rpi4 -o build/p004-rpi4/sysroot/.zedbsd-sysroot-complete sdhci-host-test` | `sdhci-host-test: PASS`（前の検査 ＋ attach の大きさ = CSD 版 2 の 1024 block ＋ CSD の 6 case: 版 1 の 512 MiB・2 GiB、版 2 の 32 GB と 22 bit の最大、版 3 は 0、1 block 未満は 0）。mutation（`RESPONSE_DROPPED_BITS` 0、`CSD_V1_C_SIZE_BIT` 64）で FAIL する事を確かめた |
| `make ZEDBSD_CONFIG=config/ci/config-zybo.mk -j16 vmunix build/zybo/root.img build/zybo/zybo-sd.img zybo-sd-image-host-test` | rc 0、warning 0、`ZYBO SD image check: PASS build/zybo/zybo-sd.img` と `… zybo-sd-image-test.img`（boot file 2 つ、dir の中の 1 つを含む）。checker は中身の違う boot file で FAIL |
| `make ZEDBSD_CONFIG=config/ci/config-armv7-virt.mk BUILD=build/p004-armv7-virt -j16 vmunix` | rc 0、warning 0 |
| `make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p004-rpi4 -j16 vmunix` | rc 0、warning 0、`arm64 vmunix check: PASS`（sdhci.c の compile 1 行） |
| `make ZEDBSD_CONFIG=config/ci/config-rpi5.mk BUILD=build/p004-rpi5 -j16 vmunix` | rc 0、warning 0、`arm64 vmunix check: PASS`（sdhci.c の compile 1 行） |
| `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p004-amd64 -j16 vmunix` | rc 0、warning 0、`amd64 vmunix check: PASS` |
| `llvm-nm -u`（zybo の `sdhci.o`・`zynq-sdhci.o`・`zynq-slcr.o`） | core は `bio_complete disk_alloc disk_create disk_set_stats_kind kern_logf kern_mmio_read32 kern_mmio_write32 kern_rtc_read_counter mutex_*` だけ（`drv_zynq_*`・`drv_rpi4_*` 無し）。glue は `drv_sdhci_attach`・`drv_zynq_slcr_*`・`kern_device_map`・`kern_logf`・`kern_mmio_read32`。SLCR は `kern_device_map`・`kern_mmio_*`・`spin_*` |
| `plan/tools/zybo/qemu-zynq.sh build/zybo/vmunix --sd build/zybo/zybo-sd.img --append rootpart=/dev/mmcblk0p2 --print` | `-drive if=sd,index=0,file=build/zybo/zybo-sd.img,format=raw` が入る（QEMU は起動していない） |
| `git diff --check` | clean |

### 実機 ZYBO（JTAG、2026-10-23 14:1x〜14:3x P3、U4、board lock を取って。実機の証拠）

SPL は worktree で `spl-build.sh`（`ed936c13…`、127245 byte、p010 と同じ）。

- card は slot に有る: `sdhci: base=0xe0100000 caps=69ec0080 present=1ff0000 version=8901`（`caps` は QEMU の capareg と同じ値、PRESENT の bit 16 = card inserted、HOST_VERSION の下位 byte 1 = 仕様 2.00、vendor 0x89）、`zynq-sdhci: base clock 50000000 Hz (pll 1000000000 Hz / 20)`、`sdhci: CMD8 response=1aa`（card は版 2 の電圧の確認に答える）。
- 1 回目（直す前）: 最初の ACMD41 は `ff8000`（busy）、次の CMD55 が command timeout（INT_STATUS 0x18000）、以後 CMD の inhibit が立ったまま（PRESENT 0x1ff0001）で 2 s 後に `init error=42`。→ CMD line の reset を足した。
- 直した後（CMD の reset、10 ms の間、電源の後の 10 ms × 2）: ACMD41 は毎回 `ff8000`（CMD55 の R1 は 0x120 = idle・APP_CMD）のまま busy が 2 s（一時的に 5 s・1 ms 間隔でも試した）明けず、`sdhci: init error=42`、`zybo: SD card unavailable (42)`。起動は ramdisk で続き login まで（`ls /dev` に mmcblk0 は無い）。
- 対照（U-Boot 2025.07 の本体を JTAG で `0x04000000` に入れて、同じ板・同じ card）: `mmc list` は `mmc@e0100000: 0`、`mmc dev 0` と `mmcinfo` は rc=1（初期化できない）。→ この card はこの板で U-Boot の SDHCI でも初期化できない。zedBSD の driver だけの問題ではない。原因（card の不良・相性、USB 給電、slot の電源）は未確認。
- JTAG の ramdisk の起動は生きている: `--initrd root.img` で `zybo: ramdisk ram0 5120 KiB`、`vfs: native boot disk=ram0`、login、`df` で `/dev/ram0p1 on /`。
- 実機の SD の root・`dd` の速さは **未実施**（card が初期化できないため）。ユーザーに card の確認（別の card、PC で読めるか、電源）を頼む事を Q1 へ。

### QEMU（T1 に依頼、未実施）

下の「T1 への依頼」。

## T1 への依頼（2026-10-23、Q1 経由）

tree: branch `agent/p3-ws289-p004` の最新（Q1 が merge した後なら main）。build（worktree に main の `build/llvm`・`llvm-source`・`llvm-build`・`NoctLang` への symlink）:
`make ZEDBSD_CONFIG=config/ci/config-zybo.mk -j16 vmunix build/zybo/root.img build/zybo/zybo-sd.img`
（`zybo-sd.img` は QEMU が書き込むので、A の前に作り直すか複写を使う。B は A の後の同じ file）

- A（zynq、SD の root、serial U3）:
  `plan/tools/zybo/qemu-zynq.sh build/zybo/vmunix --sd build/zybo/zybo-sd.img --append 'rootpart=/dev/mmcblk0p2' --timeout 300 --expect 'sdhci: mmcblk0 ready' --expect 'rootpart selector /dev/mmcblk0p2 resolved to /dev/mmcblk0p2' --expect 'login: *$' --then type:root --then expect:Password: --then type:root --then 'expect:root@kei' --then 'type:df /' --then 'expect:/dev/mmcblk0p2' --then 'type:ls /boot/boot0; echo boot0-rc=$?' --then 'expect:boot0-rc=0' --then 'type:time dd if=/dev/zero of=/root/x bs=1048576 count=16' --then 'expect:^real ' --then 'type:time sync' --then 'expect:^real ' --then 'type:cksum /root/x' --then 'expect:^4215202376 16777216' --then 'type:time dd if=/dev/mmcblk0p2 of=/dev/null bs=1048576 count=64' --then 'expect:^real '`
  → exit 0 で PASS。`sdhci:`・`zynq-sdhci:`・`vfs:` の行と、`time` の `real` の行（書き 16 MiB・sync・読み 64 MiB）を写して返す。
- B（同じ image で QEMU を起動し直して、M4）:
  `plan/tools/zybo/qemu-zynq.sh build/zybo/vmunix --sd build/zybo/zybo-sd.img --append 'rootpart=/dev/mmcblk0p2' --timeout 300 --expect 'login: *$' --then type:root --then expect:Password: --then type:root --then 'expect:root@kei' --then 'type:time cksum /root/x' --then 'expect:^4215202376 16777216' --then 'expect:^real '`
  → exit 0 で PASS（書いた file が SD に残り、cache の外から読める）。`real` の行を返す。
- C（initrd と SD の両方、rootpart 無し、H1）:
  `plan/tools/zybo/qemu-zynq.sh build/zybo/vmunix --initrd build/zybo/root.img --sd build/zybo/zybo-sd.img --timeout 240 --expect 'sdhci: mmcblk0 ready' --expect 'vfs: native boot disk=ram0' --expect 'login: *$' --then type:root --then expect:Password: --then type:root --then 'expect:root@kei' --then 'type:df /' --then 'expect:/dev/ram0p1'`
  → exit 0 で PASS（card が有っても ramdisk が root）。
- D（Pi 4 の SD の回帰、CSD と CMD の reset と待ちの後）: p002 と同じ形。`make ZEDBSD_CONFIG=plan/ws289/tests/config-rpi4-sd.mk BUILD=<新しい dir> -j16 disk-image` の image で `BOOT_MODE=raspi4b plan/tools/boot-test.sh <image>` → PNG に login prompt。
- E（任意、virt の root の program を足した回帰）: `make ZEDBSD_CONFIG=config/ci/config-armv7-virt.mk -j16 vmunix build/armv7-virt/vmunix.bin build/armv7-virt/root.img` の後 WS286 p010 の B と同じ `BOOT_MODE=armv7-virt INITRD=build/armv7-virt/root.img BOOT_PATTERN='login:' … plan/tools/boot-test.sh build/armv7-virt/vmunix.bin`。
- 失敗の時: 最後に見えた serial の行（A〜C）か PNG（D・E）を返す（解析は担当）。

## 残り・引き継ぎ

- T1 の A〜E の結果と Q1 の判定・merge。A の速さが 2 MB/s を下回れば U3 で SDMA の前倒しの判断（ただし QEMU の速さは実機の速さでない。実機の数字は card が使えるようになってから）。
- 実機の SD: card が初期化できない（zedBSD と U-Boot の両方）。ユーザーに card の確認を頼む事（Q1）。card が使えるようになったら、実機の手順は「JTAG で `--initrd` に SD の image（小さい FAT と UFS の版）を入れた ramdisk の root で起動 → zedBSD の中から `dd` で `/dev/mmcblk0` に書く → `--append rootpart=/dev/mmcblk0p2`、`--initrd` 無しで JTAG 起動 → login・`dd` の速さ」（host の block device には書かない）。
- Master の Tools に `make-zybo-sd-image.py`・`check-zybo-sd-image.py`（`zybo-sd-image-host-test`）・`qemu-zynq.sh --sd` を（design §11 の Q1 への依頼 (4) は p006 の後とあるが、`--sd` と image の tool は p004 で出来た）。
