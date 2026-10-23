<!-- awesome-plan project=zedbsd record=ws289 -->

# WS289 ZYBO の単独起動: SD・LAN・SD からの起動

- Master: [plan/master.md](../master.md)（2026-10-23 作成）。Primary Milestone: MG008（HAL の移植と異種機）。Related: [WS286](../ws286/ws.md)（ARMv7 の HAL・ZYBO の BSP、この WS の前提）。
- Status: completed（2026-10-23。全 Phase p001〜p007 が cleared。到達目標「実機の ZYBO を SD の boot mode で電源を入れると JTAG 無しで起動し、root は SD、LAN で SSH に入れ、SSH で kernel を差し替えて再起動できる」を実機で確認した。完了の判定は Q1）
- 出典: 2026-10-23 ユーザー（WS286 p005 の cleared の報告の後）「メモリがあまり多くないのでSDドライバがあるとうれしいです。LANのドライバもついでに動かしてしまえませんか？単独ブートできるといいと思います。」。WS286 を作った時のユーザーの希望「まずはSDブートで、SSHでカーネルを更新して、Switchbotで再起動、というのがいいですね。」も同じ方向。WS286 の design の U8（SMP・L2・SD・LAN・SD 起動は後の WS）のうち SD・LAN・SD 起動をこの WS にした。
- 設計の決定（2026-10-23 ユーザー「rpi4を参照するよりも、共通部分は分離しましょう。」）: SDHCI と Cadence GEM の共通の部分を汎用の driver に分け、Pi 4・Pi 5・ZYBO は薄い glue にする。

## 結果

- 実機の ZYBO（Z-7010、新しい SD card、JP5 = SD）で、BootROM が FAT32 の `BOOT.BIN` を読み、U-Boot SPL が Falcon で FIT の kernel を読んで起動し、root は SD の `/dev/mmcblk0p2`、`/boot/boot0` は `mmcblk0p1`（FAT32）。LAN は 1000 Mbps、DHCP（10.0.30.2）、host から SSH（`plan/tools/zybo/ssh-check.sh`: `uname -m` = armv7、8 MiB の scp の往復の cksum と cmp が一致）。JTAG も UART の操作も要らない。
- `plan/tools/zybo/deploy.sh` で kernel を SSH から差し替えて reboot できる（`vmunix.itb`・`system.dtb` を `.new` に書いて rename、`.old` を保つ）。SSH の `reboot` でも、JTAG からの reset（JP5 が SD のまま）でも SD から戻る。
- 汎用の SDHCI の core（`src/drivers/mmc/sdhci.c`、`include/drivers/mmc/sdhci.h`）と汎用の Cadence GEM の core（`src/drivers/ethernet/cadence-gem.c`、`include/drivers/ethernet/cadence-gem.h`、32/64 bit の descriptor）を作り、Pi 4 の SD（`rpi4-sdhci.c`）、Pi 5 の Ethernet（`rp1-ethernet.c`）、ZYBO の SDIO0・GEM0（`src/drivers/platform/zybo/zynq-sdhci.c`・`zynq-ethernet.c`・`zynq-slcr.c`）はその glue。PHY は RTL8211E（`src/drivers/ethernet/rtl8211e.c`、RGMII の delay）。MAC は `mac0=` の boot parameter（無ければ DTB の `local-mac-address`、それも無ければ固定の `02:5a:59:42:4f:30`）。
- SD の性能: 16 MiB の `dd` ＋ sync が約 2.7 MB/s、64 MiB の読みが 5.32 s（約 12 MB/s）。U3 の基準 2 MB/s を超えたので SDMA は前倒ししなかった。
- armv7 の 32 bit の `off_t` による FAT の readdir の不具合を直した（`src/drivers/fs/fat.c` の `fat_readdir_unlocked` の `(off_t)UINT32_MAX` が armv7 で −1 になり、`ls /boot/boot0` が最初から EOF だった。64 bit の板では起きない）。
- 証拠の区別: p003 は実機の Pi 5、p004・p006 は実機の ZYBO（SD から単独起動）、p005 は実機の ZYBO（JTAG、ramdisk の root）と T1 の QEMU zynq。p002 は QEMU raspi4b（Pi 4 の実機はユーザーの試験 U39）。T1 の QEMU zynq（SD の root、`--net` の SSH、`deploy.sh`）は p004〜p006 の回帰。zybo・armv7-virt・rpi4・rpi5・amd64 の kernel は warning 0、host 試験 `sdhci-host-test`・`cadence-gem-host-test`（rp1 の trace が分離の前と byte 一致）・`zybo-sd-image-host-test` が PASS。規約の全文の見直し（p007）は WS289 の C の source を書き方だけ直した。

## 制限・残件

- **100 Mbps・10 Mbps は未確認**（試験の switch が 1000 だけ）。tx clock は PLL の rate から計算する（1000 MHz で 8/1・40/1・50/8）が、実機で通ったのは 1000 Mbps だけ。
- **Pi 5 の起動直後の ping**: p003 の実機で、gateway への起動直後の 100 回の ping が 25% 失われたのが 1 回。その後（T1 1 回・Q1 3 回・host からの 200 回）は 0。GEM の register の手順は分離の前と byte 一致（host 試験）なので分離による変化とは判定せず、観察として残す。
- **Pi 5 の `/boot/boot0` はほぼ満杯**（Q1 の引き継ぎ。kernel の差し替えで `vmunix.old` を保つと余裕が無い。この WS には根拠の記録が無い）。
- **QEMU の expect の順の都合**: T1 の QEMU の A1 は `ls -la` の後の expect の順で rc 1 になったが、中身（`system.dtb` 413・`vmunix.itb` 1033303）で PASS と判定した。
- **BootROM → SPL の鎖は QEMU では確かめられない**。実機の証拠だけ。実機で確かめた SD の起動は FAT32 の BOOT.BIN（FAT16 は実機で試していない）。
- **SD の SDMA（bounce）と Wi-Fi の SDIO host の汎用 core への乗せ替え**は Future Work [fw-sdio-host-split](../future-work.md)（`rpi4-sdio-host.c` は WS289 の core に乗せていない。ユーザーの決定 U7）。
- 規約の全文の見直し（p007）で残した物: 共有の cleanup label への前向きの `goto fail`（15 件、構造の変更になるので残す）、`sdhci.c` の `transfer_block_recovering` の最後の `return error;`、`controller_init` の長さ。`drv_cadence_gem_attach` は `result` の NULL を確かめない（glue が必ず渡す）。
- この WS の外のまま: SMP の CPU1・L2（WS286 の残件）、USB（[WS287](../ws287/ws.md)）、PL の画面（WS286 の後続）。SwitchBot は ZYBO に未接続（電源の入れ直しはユーザー）。
- docs の howto（ZYBO の SD 起動の手順）は書いていない。`docs/reference/kernel-boot-parameters.md` の `mac0=` に、消した Phase の ID（ws289-p005）の参照が残る（Future Work の fw-docs-ids の対象）。

## 設計の要点（SD からの起動の最終形。元の設計文書は main e74bdc00f の git の履歴 `plan/ws289/design.md`）

- 流れ: BootROM（SD の boot mode）が最初の partition（FAT32）の `BOOT.BIN`（U-Boot の build が `zynqimage` で作る `spl/boot.bin`）を OCM に置いて走らせる → SPL が `ps7_init`（clock・MIO・DDR）→ MMC → FAT から `vmunix.itb`（FIT）と `system.dtb` を読み、Falcon（U-Boot 本体無し）で kernel に jump（`r0 = 0`、`r1 = 0xffffffff`、`r2 = DTB`。JTAG の `jtag-boot.sh` の入口の約束と同じ）。
- SPL は `plan/tools/zybo/spl-build.sh` で U-Boot v2025.07 を tree の外（`build/`）に build する（GPL。binary は commit も配布もしない）。1 つの SPL が JTAG と SD の両方を扱う。Falcon の fragment: `SPL_FS_LOAD_KERNEL_NAME="vmunix.itb"`、`SPL_FS_LOAD_ARGS_NAME="system.dtb"`、`SPL_PAYLOAD_ARGS_ADDR=0x01F00000`、SPL の BSS・stack・malloc を `0x1F000000` 以上へ（defconfig の既定は kernel の `0x00100000` と重なる）。
- kernel の image は legacy の uImage でなく **FIT**（legacy は header を load の 64 byte 下に置き、`0x000FFFC0` の高位 OCM の alias に書いてしまう）。FIT は kernel の node だけ（`load = entry = 0x00100000`、`os = "linux"`、configuration に `description` が要る）で、`platform/armv7/tools/make-zybo-boot-files.py` が作り、kernel の `_end` ＋ SPL が append する fdt の余地が `0x01F00000` より下に収まる事を検査する。
- SD の image（`make ZEDBSD_CONFIG=config/ci/config-zybo.mk BUILD=… sd-image`、`ZYBO_BOOT_BIN=…/spl/boot.bin` で BOOT.BIN を入れる）: MBR、p1 = FAT32（type 0x0C、bootable、64 MiB。kernel の FAT driver は FAT16 で長い名前を扱わず、deploy の `vmunix.itb.new` が作れないので FAT32）、p2 = UFS の root（type 0xA5）。全体は 2 の冪の MiB（既定 512 MiB、QEMU の SD の model の制約）。`check-zybo-sd-image.py` が構造と中身を検査する。
- root と boot device（`src/kern/platform/zybo.c`）: initrd が有れば ramdisk が 0x80（JTAG の起動は今までどおり ramdisk の root）、無ければ SD が 0x80。SD の root は `--initrd` を付けずに `rootpart=/dev/mmcblk0p2`。SD が無い・落ちた時は警告して起動は続く。
- SDHCI の core の追加（実機の card の調べで入れた）: CSD の復号（版 1・2 の大きさ）、失敗した command の後の CMD line の software reset（しないと inhibit が立ったままになる）、電源・識別の clock・ACMD41 の問い直しの待ち。SD は PIO（poll、割り込み 56 は使わない）。
- GEM: `zynq-slcr.c` が SLCR の gate と cpu_1x（MDC の divisor）、`zynq-ethernet.c` が GEM0（32 bit の descriptor、`phy_scan` で PHY を探す: 実機は address 0 の id 001cc915、QEMU は 7）、RXUBR の restart・USRIO の RGMII、tx clock は PLL の rate から計算。DMA は uncached の slot（`coherent = 0`）。
- 復旧の道は JTAG だけ（U-Boot 本体を FAT に入れない）。JP5 が SD の間は `jtag-boot.sh` は kernel を load しない（守り）ので、kernel の更新は SSH、詰まったら JP5 を JTAG に戻す。

## ユーザーの決定（2026-10-23、推しのとおり「はい」。Master の decisions-log にも記録）

| # | 内容 |
| --- | --- |
| U1 | SD の boot mode への切り替え（JP5）、card の差し込み、`dd` での書き込みはユーザー（Q1 が頼む）。SwitchBot は繋がない |
| U2 | 復旧の道は JTAG だけ。U-Boot 本体を FAT に入れない |
| U3 | SD は PIO。p004 の `dd` が 2 MB/s を下回った時だけ SDMA（bounce）を前倒し（実測は上回ったので前倒ししない） |
| U4 | QEMU zynq の guest に host の `127.0.0.1` の転送 port から SSH で入って LAN を判定する（AGENTS.md の検証の例外） |
| U5 | 固定の MAC `02:5a:59:42:4f:30`（locally administered）を既定にし、`mac0=` の boot parameter で変えられる |
| U6 | Pi 5 の Ethernet・SD の回帰は実機の Pi 5（p003 の clear の条件）、Pi 4 の SD は QEMU raspi4b を clear の条件にし実機はユーザーの試験の一覧（U39） |
| U7 | `rpi4-sdio-host.c`（Wi-Fi の SDIO host）は共通部分の分離の範囲に入れない（Future Work fw-sdio-host-split） |

## 残した物

- 道具（Master の Tools に登録）: `plan/tools/zybo/ssh-check.sh`（WS から移した。`ssh-check.sh HOST PORT OUTDIR`、host に sshpass が要る）、`plan/tools/zybo/spl-build.sh`・`deploy.sh`・`qemu-zynq.sh`（`--sd`・`--net`・`--dtb`）・`uart-expect.py`（`run:` の step と名前付き group）、`plan/tools/rpi4/config-rpi4-sd.mk`（WS から移した。vendor firmware の package を外した Pi 4 の config で、QEMU raspi4b の SD の回帰の image 用。firmware の license は cache ごとにユーザーが受諾する物なので、SD の経路の確認にはこの config を使う。`config/ci/config-rpi4.mk` を include）、`platform/armv7/tools/make-zybo-sd-image.py`・`make-zybo-boot-files.py`・`check-zybo-sd-image.py`・`make-armv7-root-image.py`、`platform/armv7/zybo/zybo-sd.dts`。
- host 試験: `tests/sdhci-host-test.c`、`tests/cadence-gem-host-test.c`（`tests/cadence-gem-rp1.trace` が分離の前の Pi 5 の trace）、`make … zybo-sd-image-host-test`。
- 既定の `config/ci/config-zybo.mk` は network と OpenSSH 入りの root。armv7 の外部 package（zlib・openssl・openssh・ca-certificates）は P2 が WS の外で用意した（`userland/packages/external.mk` の triple `armv7-unknown-zedbsd-eabihf`、OpenSSL の target `zedbsd-armv7`）。
- 削除した Phase の記録・設計（`design.md` 版 2、design-reviewer の指摘と対応の表、各 Phase の「design との差」・実機の記録）は、main e74bdc00f の git の履歴にある（`plan/ws289/phase001`〜`phase007`、`plan/ws289/design.md`、`plan/ws289/tests/`）。

## Phase

| Phase | 内容 | LW | 状態 |
| --- | --- | --- | --- |
| p001 | 調べと設計（design.md 版 2。design-reviewer の全指摘に対応） | 10 | cleared（2026-10-23。merge 1680183ed） |
| p002 | 汎用の SDHCI の core を rpi4-sdhci.c から分離、rpi4 は glue、host 試験、QEMU raspi4b で Pi 4 の SD の回帰 | 10 | cleared（2026-10-23。merge 803d0dbfb。Pi 4 の実機はユーザーの試験 U39） |
| p003 | 汎用の Cadence GEM の core を rp1-ethernet.c から分離、rp1 は glue、host 試験（trace 一致）、実機の Pi 5 で SD と Ethernet の回帰 | 14 | cleared（2026-10-23、実機の Pi 5。merge ccf175eae） |
| p004 | ZYBO の SD（SDIO0 の glue、SLCR、CSD）と root を SD の partition に、SD の image の tool、QEMU の `--sd` | 12 | cleared（2026-10-23、実機の ZYBO。merge bf5ee6a10） |
| p005 | ZYBO の LAN（GEM0 の glue、RTL8211E、SLCR の tx clock、`mac0=`）、DHCP・SSH | 14 | cleared（2026-10-23、実機の ZYBO と T1 の QEMU。merge d5c244472。100・10 Mbps は未確認） |
| p006 | SD からの起動（SPL の Falcon の BOOT.BIN、FIT ＋ `system.dtb`、`make sd-image`、`deploy.sh`、JP5 はユーザー） | 12 | cleared（2026-10-23、実機の ZYBO。merge 3caf97354・5a28d28ca・89576f824） |
| p007 | 規約の全文の見直し | 6 | cleared（2026-10-23。merge 0c3c26c75） |

合計 78 LW（版 2 の見積り）。
