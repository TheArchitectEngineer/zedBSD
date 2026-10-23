<!-- awesome-plan project=zedbsd record=ws289 -->

# WS289 ZYBO の単独起動: SD・LAN・SD からの起動

- Master: [plan/master.md](../master.md)（2026-10-23 作成）。Primary Milestone: MG008（HAL の移植と異種機）。Related: [WS286](../ws286/ws.md)（ARMv7 の HAL・ZYBO の BSP、この WS の前提）。
- Status: incomplete（2026-10-23 p002 cleared、p004 in-progress（実機の card は初期化できず、U41）、p006 in-progress（T1 の QEMU 待ち、実機の SD 起動は card 待ち））
- 出典: 2026-10-23 ユーザー（WS286 p005 の cleared の報告の後）「メモリがあまり多くないのでSDドライバがあるとうれしいです。LANのドライバもついでに動かしてしまえませんか？単独ブートできるといいと思います。」。WS286 を作った時のユーザーの希望「まずはSDブートで、SSHでカーネルを更新して、Switchbotで再起動、というのがいいですね。」も同じ方向。WS286 の design の U8（SMP・L2・SD・LAN・SD 起動は後の WS）のうち SD・LAN・SD 起動をこの WS にした。

## 目的と完了の条件

- SD: Zynq の SDIO0（Arasan SDHCI、base clock は SLCR の 50 MHz）の driver。root を SD の partition に置き、ramdisk の分の RAM（512 MiB）を使わない。Pi 4 の `src/drivers/platform/rpi4/rpi4-sdhci.c` の共通に出来る所を使う。
- LAN: GEM0（Cadence GEM、RGMII）と PHY RTL8211E（MDIO address 0、reset は gpio0 46）。Pi 5 の `src/drivers/platform/rpi5/rp1-ethernet.c`（同じ Cadence GEM）の共通に出来る所を使う。DHCP と SSH で入れる事。
- SD からの起動: SD の BOOT.BIN（U-Boot SPL、ps7_init）から zedBSD の kernel を読んで起動する。JTAG も UART の操作も無しで `login:` と SSH まで。kernel の更新は SSH で SD に書き、再起動。
- 完了: 実機の ZYBO を SD の boot mode にして電源を入れると、JTAG 無しで起動し、root は SD、LAN で SSH に入れ、SSH で kernel を差し替えて再起動できる。
- この WS の外: SMP の CPU1・L2（別の WS）、USB（WS287）、PL（WS286 p008）。

- 2026-10-23 ユーザー「rpi4を参照するよりも、共通部分は分離しましょう。」→ SDHCI と Cadence GEM の共通の部分を汎用の driver に分け、Pi 4・Pi 5・ZYBO は薄い glue にする（分離は ZYBO の glue の前の Phase、Pi 側の回帰の確認付き）。

## Phase（[design.md](design.md) 版 2 の §9。2026-10-23 ユーザー「rpi4を参照するよりも、共通部分は分離しましょう。」で p002・p003 を共通化の Phase にした）

| Phase | 内容 | LW | 依存 | 状態 |
| --- | --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調べと設計（[design.md](design.md)） | 10 | WS286 p005 | in-progress（版 2 = review の全指摘に対応、Q1 の判定待ち） |
| [p002](phase002/phase.md) | 汎用の SDHCI の core（`src/drivers/mmc/sdhci.c`）を rpi4-sdhci.c から分離、rpi4 は glue、host 試験、T1 の raspi4b で Pi 4 の SD の回帰 | 10 | p001 | cleared（2026-10-23 Q1: T1 の QEMU raspi4b の boot-test PASS（main 8b2837d2d、試験の config plan/ws289/tests/config-rpi4-sd.mk、SD の root で init が進み login:、PNG build/review/ws289-p002-raspi4b.png）、host 試験の register の書き込み 45,664 回が一致、rpi4・rpi5・amd64 の kernel の warning 0（build の warning は openssl など外部 package だけ）。merge 803d0dbfb。boot-test.sh は raspi4b の SD を 2 の冪に（2d9fe7019）。Pi 4 の実機はユーザーの試験の一覧 U39） |
| [p003](phase003/phase.md) | 汎用の Cadence GEM の core（`src/drivers/ethernet/cadence-gem.c`、32/64 bit の descriptor）を rp1-ethernet.c から分離、rp1 は glue、host 試験、実機の Pi 5 で SD と Ethernet の回帰（T1、kernel は p002 を含む） | 14 | p001、p002（実機の kernel に含める） | cleared（2026-10-23 Q1: T1 の実機の Pi 5（10.0.30.5、main d3c9f6505 の rpi5 kernel を入れ替え、vmunix.old を保持）で SD の root（mmcblk0 ready、block addressing）・gem: PHY 1 id 600d84a2・en0 1000 Mbps・DHCP 10.0.30.5・SSH・DMA error 0。gateway への ping は起動の直後の 1 回目が 100 中 25 の損失、その後の 4 回（T1 1・Q1 3）は 0、host からの 200 回も 0。register の手順は分離の前と byte 一致（host 試験）なので分離による変化ではないと判定し、起動直後の損失は観察として残す。merge ccf175eae） |
| [p004](phase004/phase.md) | ZYBO の SD（SDIO0 の glue、SLCR、CSD）と root を SD の partition に、SD の image の tool、QEMU の `--sd` | 12 | p002、WS286 p006、WS286 p010 | cleared（2026-10-23 Q1: 実機の ZYBO（新しい card、SD から単独起動、main 5a28d28ca）で root /dev/mmcblk0p2・/boot/boot0 = mmcblk0p1、dd 16 MiB + sync 約 2.7 MB/s・64 MiB の読み 5.32 s（約 12 MB/s）、cksum 4215202376 16777216。U3 の 2 MB/s を上回るので SDMA は前倒ししない。前の card の ACMD41 の busy は card の側（U41 は新しい card で解決）。QEMU は A〜E PASS（前の記録）。merge bf5ee6a10） |
| [p005](phase005/phase.md) | ZYBO の LAN（GEM0 の glue、RTL8211E、SLCR の tx clock、`mac0=`）、DHCP・SSH（networkd・openssh を試験の config の root に） | 14 | p003、p004、WS286 p007、Q1: armv7 の package | cleared（2026-10-23 Q1: 実機の ZYBO（JTAG、ramdisk の root、LAN）で PHY 0 id 001cc915・1000 Mbps・DHCP・host から ssh の uname -m = armv7・8 MiB の scp の往復の cksum と cmp・ping 1000/100 回と flood 20000 回の損失 0・mac0=。T1 の QEMU zynq（SD の root、--net 2289、PHY 7 id 01410cc2、DHCP 10.0.2.15、ssh-check PASS）PASS、5 つの kernel の build warning 0、cadence-gem-host-test PASS（rp1 の trace 一致）。merge d5c244472。100・10 Mbps は未確認（switch が 1000 だけ）） |
| [p006](phase006/phase.md) | SD からの起動（SPL の Falcon の BOOT.BIN、FIT の `vmunix.itb` ＋ `system.dtb`、`make sd-image`、`deploy.sh`、JP5 はユーザー） | 12 | p004、p005、U1・U2 | in-progress（2026-10-23 P3: 実装、host の確認（4 つの kernel の warning 0、`zybo-sd-image-host-test` PASS）、新しい SPL での JTAG の起動と既定の root の SSH を実機で確認。T1 の QEMU A・B 待ち。実機の SD 起動は card が初期化できず未実施（U41）） |
| p007 | 規約の全文の見直し | 6 | p002〜p006 | planning |

- 合計 78 LW（版 2）。WS286 との順（design §9）: WS286 p006 → p007 → **WS289 p002・p003（WS286 と独立、並行可）** → WS286 p010 → WS289 p004（SD）→ p005（LAN）→ p006（SD 起動）→ WS286 p008（PL）。
- p005 の前提「Q1: armv7 の package」（design §11 の Q1 への依頼 (1)）: 2026-10-23 P2（worktree p2-armv7-packages、Phase は無い）が済ませた。zlib・openssl・openssh・ca-certificates が armv7 で build できる（`userland/packages/external.mk` の triple `armv7-unknown-zedbsd-eabihf`、`gen-cross-toolchain.sh` の armv7 の code flag、OpenSSL の target `zedbsd-armv7`（patch level zedbsd8）、armv7 の `libutil.so`）。既定の ZYBO の root には入れない（2026-10-23 p006 で既定の `config-zybo.mk` に入れた）。`make ZEDBSD_CONFIG=config/ci/config-zybo.mk armv7-packages` で build だけ、root に入れるのは試験の config [tests/config-zybo-ssh.mk](tests/config-zybo-ssh.mk)（`ARMV7_ROOT_MIB := 16`、image 17 MiB、JTAG の load 69 s。OpenSSH を選ぶと `libutil.so`・`chmod`・`mkdir` も入り、root.img の作成で ELF の debug 情報を落とす）。実機（JTAG）で `ssh -V` = `OpenSSH_10.5p1, OpenSSL 3.5.8 25 Aug 2026`、`openssl version`、`openssl dgst -sha256`、`ssh-keygen -t ed25519`、boot で sshd-start が rsa・ecdsa・ed25519 の host key を作った。sshd の待ち受けと LAN は未確認（p005）。p005 が SD の root を作る時は、この config の program と package の組を元にする。
- 訂正（design §1.2）: 目的の節の「reset は gpio0 46」は誤りで、`gpio0 46` は USB の PHY の reset。GEM の RTL8211E に reset の GPIO は無い（MDIO の soft reset で足りる）。

## 現在の状態・再開点

- 2026-10-23 P3（worktree p3-ws289-p006、[p006](phase006/phase.md)）: `spl-build.sh` に Falcon の fragment（`vmunix.itb`・`system.dtb` を `0x01F00000`、SPL の BSS・stack・pool を `0x1F000000` 以上）と `spl/boot.bin`、`make-zybo-boot-files.py`（FIT と DTB、layout の検査）、`zybo-sd.dts`、`sd-image` の FAT に `vmunix.itb`・`system.dtb`・`BOOT.BIN`（`ZYBO_BOOT_BIN`）、`check-zybo-sd-image.py --kernel`、`deploy.sh`、`qemu-zynq.sh --dtb`。既定の `config-zybo.mk` に network と SSH を入れた（理由は phase.md。`tests/config-zybo-ssh.mk` は不要になった）。U-Boot v2025.07 の source で、FIT は SPL の pool に読まれる事、configuration に `description` が要る事を見つけた（design §5.1 との差）。再開点: T1 の QEMU A・B、Q1 の判定、card（U41）の後の実機の手順（phase.md）。
- 2026-10-23 P3（同じ worktree）: T1 の 1 回目の A は FAIL（`ls /boot/boot0` が空）。kernel の FAT の driver が FAT16 では長い名前を扱わない（deploy の `vmunix.itb.new` が作れない）ので、SD の image の p1 を FAT32（0x0C、64 MiB、cluster 129022）にした（design L13 を取り消し）。`ls` が空だった直接の原因は未特定。`deploy.sh --wait`。T1 の 2 回目は短い A1（network 無し）→ A2（deploy）→ B（mtools で FAT を読む）。
- 2026-10-23: U1〜U7 はユーザー「はい」で推しのとおり決定（design §11、Master の decisions-log）。p001 は Q1 が merge（1680183ed）。p002 は P1 が開始。

- 2026-10-23: 作成。p001 の設計から。
- 2026-10-23 P1（worktree p1-ws289-p001）: design.md 版 1（§0〜§14）と phase001。ユーザーの決定（共通部分の分離、Q1 経由）を D1・D2・p002・p003 に反映。design-reviewer（Opus）の Critical 1・High 1・Medium 7・Low 15 の全部に対応して版 2（§15 の表。主な変更: SD の kernel は legacy の uImage でなく FIT、SD と ramdisk の両方が有る時は initrd の有無で boot device を決める）。再開点: Q1 の判定、U1〜U7、Q1 への依頼（design §11）。
- 2026-10-23 P1（worktree p1-ws289-p003、[p003](phase003/phase.md)）: 汎用の Cadence GEM の core `src/drivers/ethernet/cadence-gem.c`・`include/drivers/ethernet/cadence-gem.h`（32/64 bit の descriptor、RXUBR の restart・USRIO の RGMII・PHY の走査の flag）を作り、`rp1-ethernet.c` を glue にした。rpi5・rpi4・amd64 の vmunix は warning 0、`cadence-gem-host-test` PASS、Pi 5 の glue の全 access の trace が分離の前と byte 一致（812 行、`tests/cadence-gem-rp1.trace` に固定）。再開点: T1 の実機の Pi 5（SD・DHCP・SSH・ping）の結果（Q1 の判定）。
- 2026-10-23 P3（worktree p3-ws289-p005、[p005](phase005/phase.md)）: GEM0 の glue `zynq-ethernet.c`（SLCR の gate、cpu_1x の MDIO、32 bit の descriptor、PHY の走査、RXUBR・USRIO、tx clock、GIC 54、MAC は `mac0=` → DTB → 固定）、`rtl8211e.c`、`mac0=` の parameter、armv7 の root に network の program と networkd の service、`qemu-zynq.sh --net`、`uart-expect.py` の `run:`、`tests/ssh-check.sh`。実機の ZYBO で LAN・DHCP・SSH・scp を確かめた。再開点: T1 の QEMU の A、Q1 の判定。
- 2026-10-23 P3（worktree p3-ws289-p004、[p004](phase004/phase.md)）: core に CSD の復号、失敗した command の後の CMD line の reset と電源・ACMD41 の待ち（実機で見つけた）、`zynq-slcr.c`・`zynq-sdhci.c`、`zybo.c` の boot device（initrd が有れば ramdisk が 0x80）、`root.ufs`・`make-zybo-sd-image.py`・`check-zybo-sd-image.py`・`make sd-image`、`qemu-zynq.sh --sd`、root に `cksum dd df rm sync time`。実機の ZYBO（JTAG）では card は有るが ACMD41 が busy のまま（U-Boot 2025.07 の `mmc dev 0` も失敗）。再開点: T1 の A〜E、ユーザーの card の確認、Q1 の判定。
- 2026-10-23 P1（worktree p1-ws289-p002、[p002](phase002/phase.md)）: 汎用の SDHCI の core `src/drivers/mmc/sdhci.c`・`include/drivers/mmc/sdhci.h` を作り、`rpi4-sdhci.c` を glue（90 行）にした。rpi4・rpi5・amd64 の vmunix は warning 0、`sdhci-host-test` PASS、前の driver と register の書き込みの列が一致。再開点: T1 の QEMU raspi4b の boot-test の結果（Q1 の判定）。
