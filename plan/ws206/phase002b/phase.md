<!-- awesome-plan project=zedbsd record=ws206-p002b -->

# ws206-p002b: SDIO の host と SDIO の層（Pi 4 の内蔵 WiFi）

Parent: [WS206](../ws.md)
Status: in-progress（q932、P1、2026-10-13。実装・host 試験・build は済み。clearance は Pi の実機の CMD5・CCCR・CIS の読み（Q1 がユーザーに依頼）と T1 の QEMU raspi4b の起動の回帰の結果を Q1 が判定してから）。T1-558（QEMU raspi4b の起動・SD の読み書き・reboot）PASS、残りは Pi の実機の dmesg
Phase disposition: normal
Queue: q932（[P1 の lane](../../agents/P1/queue.md)）

## 由来と承認

- 2026-10-12 ユーザー「RPi4のWiFiとBluetoothを実装し、RPi4は完了です」。2026-10-13 Q1 が P1 に p002b の実行を依頼（p002a の後）。
- 設計: [p001](../phase001/phase.md) の W2・W3・W8・W12（第 3 版）と [review-3](../phase001/review-3.md) の指摘 2・3・7・軽微 8。

## 依存の変更（review-3 の指摘 7）

設計の第 3 版は「BUG-295 の `rpi4-sdhci.c` の変更が main に入った後に `drv_rpi4_sdhci_base()` を `rpi4-sdhci.c` に追記」としていた。
review-3 の指摘 7 と Q1 の依頼に従い、**`rpi4-sdhci.c` には触れない**。SD が使った controller の base は `src/kern/platform/rpi4.c` の
`kern_platform_init`（SD の初期化、旧 82-91 行）で static に記録し、device tree の物理 address（`fdt_phys`）も同じ所で保存して、
`kern_platform_refresh_devices` から `drv_rpi4_wifi_probe(fdt_phys, sd_base)` に渡す。よって BUG-295 への依存は無い（BUG-295 は main に入った）。

## 範囲

1. `src/drivers/platform/rpi4/rpi4-mmc-irq.c`・`.h`（新規）: GIC SPI 126（kernel の 158）の 2 slot の dispatcher（W2 の API そのまま）。
2. `rpi4-sdio-host.c`・`.h`（新規、独立。`rpi4-sdhci.c` を写さず include もしない）: 0xfe300000 の旧 EMMC の SDIO host。PIO、32 bit 書きだけ、
   400 kHz 以下で書き込みの後に bus clock の 2 周期の待ち（R2）、SDHCI 3.00 の 10 bit 分周（2.00 は 2 の冪）、base clock は mailbox の clock id 1、
   card interrupt（handler は irqsave の spinlock の下で disarm して起こす、serving thread が rearm）、`SOFTWARE_RESET_ALL` の後の power・clock・
   bus 幅・enable の書き直し、100 ms の poll の保険の待ち（`drv_rpi4_sdio_host_card_irq_wait` の deadline、定数 `DRV_RPI4_SDIO_HOST_CARD_IRQ_POLL_MS`）。
3. `rpi4-sdio.c`・`.h`（新規）: SDIO の card の層（CMD5・CMD3・CMD7、CCCR・CIS の読み、4 bit・25 MHz、F1・F2 の enable と block size、CMD52・CMD53）と
   DT からの slot の記述（chip の node の親の controller、disabled、QEMU の印、claim、割り込み、pin の group）。
4. `rpi4-wifi.c`・`.h`（新規）: probe（DT と claim を見て thread を 1 つ作るだけ）と thread（mailbox、clock、WL_ON の読み、pin、host、dispatcher、
   WL_ON の pulse、enumeration、F1 の enable と block size、記録、後始末で WL_ON 0）と `drv_rpi4_wifi_stop()`。
5. `src/kern/platform/rpi4.c`（SD の base と `fdt_phys` の記録、probe の呼び出し）、`CONFIG_DRIVER_BCM2711_WIFI`（`Makefile`・
   `platform/arm64/vmunix.mk`・`config/drivers/architecture/arm64.drivers`・`config/drivers/menu.list` の WiFi の行、既定 y）。
6. host 試験 `plan/ws206/tests/sdio-host-test.c`・`.sh`・`stub/kern/thread.h`。

範囲の外: Broadcom の bring-up（firmware の書き込み、F2 の enable、card interrupt の CCCR の enable、bus の thread）は p004。HAL の API・UAPI・`rpi4-sdhci.c` は変えない。

## 設計（W2・W8・W12）からの違い（設計の範囲の中の技術の判断）

- **file を 4 つに分けた**: W2 は probe の入口と thread の骨を `rpi4-sdio.c` に置くとしていたが、probe・thread・stop は `rpi4-wifi.c` に分けた。
  `rpi4-sdio.c` は card の層と DT の slot の記述だけで、kernel の thread の header を要らず host で試験できる。`rpi4-wifi.c` も host 試験に入れた
  （kernel の `kern/thread.h` は HAL と user の signal の型を引き込み host の C library とぶつかるので、試験は `plan/ws206/tests/stub/kern/thread.h` を先に読む）。
- **slot は chip の node から**: `brcm,bcm4329-fmac` の node の親を controller とする（Pi 4 の DT には同じ 0x7e300000 の `mmc@7e300000`（disabled）と
  `mmcnr@7e300000`（okay）の 2 つがあるため、compatible で探すと取り違える）。controller・chip の node のどちらかが disabled なら probe しない。
  pin・function・pull は controller の `pinctrl-0` の group から読む（GPIO 34〜39、ALT3、CLK なし・他 up）。
- **probe の順**: QEMU の印（`drv_rpi4_board_emulated`）→ chip の node → controller の status → compatible → `reg` → claim（SD の base と同じなら EBUSY）→ 割り込み → pin。
  register・mailbox・電源には触らない。thread は detached（AX211 と同じく join しない）。
- **boot の thread が hardware に触る**: p002b は chip の driver が無いので、thread は firmware の file を待たずに pulse と enumeration を 1 度だけ行い、
  記録して WL_ON を 0 に戻す（chip の電源を落とす）。W8 の「root を待って firmware の file を確かめてから hardware に触る、file が無ければ触らない」
  （review-3 の軽微 8）は p004 で thread を作り直す時に入れる（**記録だけ**、Q1 の依頼のとおり）。
- **F2 の enable**: F2 は chip の firmware が動いてからでないと ready にならない（W4 の 9）。p002b の thread は F1 を enable し、F1 64・F2 512 の
  block size を設定するだけ。`drv_rpi4_sdio_enable_function` は F2 にも使える（p004 が呼ぶ）。
- **書き込みの待ち（R2）**: 400 kHz 以下と clock の停止中は書き込みのたびに待つ（2 周期を µs に切り上げ、400 kHz で 6 µs、停止中は 10 µs）。
  clock を止める書き込みと reset の書き込みから「停止中」とみなす（その直後の書き込みも待つ）。25 MHz では待たない。
- **dispatcher の保険**: 誰も claim しない割り込みが 100000 回続いたら線を mask し、次の add で unmask する（level の線の嵐を防ぐ）。
  最初の add が `kern_irq_mask` → `kern_irq_set_mode`（level・high、GIC は無効の間だけ設定できる）→ `kern_irq_register` を行い、slot を埋めてから unmask。
- **R6・R1・R5 の error bit**: CMD3 の R6（bit 15〜13）、CMD7 の R1（bit 23・22・19）、CMD52・53 の R5（flag の bit 7・6・3・1・0）を確かめて EIO。

## review-3 の指摘への対応（p002b の分）

| 指摘 | 対応 |
| --- | --- |
| 2（bus の thread は control・lifecycle の mutex を取らない） | p002b の thread は lifecycle の mutex を一度も取らない（`rpi4-wifi.c` の先頭の comment と構造）。bus の thread そのものは p004 |
| 3（shutdown との直列） | `drv_rpi4_wifi_stop()`: lifecycle の mutex の下で `stopping` を立て thread を起こし、**mutex を放してから** `running` が 0 になるのを待つ。thread の眠り（`wifi_sleep`）は全て `stopping` を見て ECANCELED で抜け、後始末（host の停止、slot の remove、WL_ON 0）の後に `running` を 0 にする。host 試験 `stop` で確かめた。**shutdown の口は今は無い**（kernel の `system_shutdown_prepare` は network・USB・PCI の weak の口だけ）。`drv_rpi4_wifi_stop` を shutdown に繋ぐかは p004 の前に Q1 へ（review-3 の指摘 3 の問い） |
| 7（BUG-295 への依存を外す） | 上の「依存の変更」 |
| 軽微 8（W8 の順） | p004 で入れる（上） |

## 受け入れ

- host 試験 `sdio-host-test.sh` が PASS（unit: ASan・UBSan、stop と smp: TSan）。
- rpi4 の vmunix の build が warning 0、amd64 の build が壊れない。`CONFIG_DRIVER_BCM2711_WIFI=0` で `rpi4.c` が warning 0。
- `git diff --check` が空。
- Pi 4 の実機（Q1 がユーザーに依頼）: 下の dmesg の行で CMD5・CCCR・CIS（manufacturer 0x02d0）が読め、Pi の起動（SD card・Ethernet・USB）が壊れない。
- QEMU raspi4b（T1）: `boot-test.sh` で login prompt（WiFi は `no onboard Wi-Fi to bring up (19)` で何もしない）。

## コマンド

```sh
sh plan/ws206/tests/sdio-host-test.sh <worktree>/build/ws206-p002b/sdio <main>/vendor/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb
make -j16 -o <worktree>/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix
make -j16 -o <worktree>/build/amd64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/vmunix
```

## Pi 4 で見る dmesg の行（`dmesg | grep -E 'rpi4-(wifi|sdio)'`）

成功の時（値は例、`<>` が実機で読む所）:

```
rpi4-wifi: EMMC clock <Hz> Hz
rpi4-wifi: WL_ON was level <0|1> (<err>), direction <output|input> (<err>)
rpi4-wifi: GPIO 34 function <前> -> 7, pull 1        （35〜39 は pull 2）
rpi4-sdio: host specification <2>, capabilities <hex> (<MHz>), firmware clock <Hz> Hz
rpi4-wifi: SDIO card: <2> functions, OCR <hex>, memory 0, RCA <hex>
rpi4-wifi: CCCR <n>, SDIO <n>, capability <hex>, CIS at <hex>: manufacturer 02d0 (1), card <hex>, F0 block <n>, speed <hex>
rpi4-wifi: bus 4 lines at <25000000> Hz
rpi4-wifi: F1 enabled; block sizes F1 64, F2 512
rpi4-wifi: chip powered down until its driver is added
```

失敗の時に出る行: `rpi4-sdio: no answer to CMD5 (<errno>)`、`rpi4-sdio: card power-up not done (R4 <hex>)`、`rpi4-wifi: SDIO bring-up failed (<errno>)`、
`rpi4-wifi: no onboard Wi-Fi to bring up (<errno>)`、`rpi4-sdio: no divider of ...`、`rpi4-sdio: internal clock not stable`、`rpi4-wifi: EMMC clock answer unusable ...`。
実機で読む未確認の値（p001 の「未確認」）: WL_ON の初めの状態と向き、GPFSEL3 の今の値（pin 34〜39 の「前」）、clock id 1 の rate、旧 EMMC の版と capabilities、
CMD5 の OCR、CIS の card id、R2 の待ちで足りるか。

## 記録（2026-10-13 P1、base main 388bcb153）

- 実装: 上の「範囲」の file。rpi4 の kernel では `drv_rpi4_mmc_irq_add`・`drv_rpi4_sdio_enumerate`・`drv_rpi4_sdio_host_interrupt`・`wifi_thread`・`wifi_board` が
  `build/arm64/kernel.elf` に link され、`strings build/arm64/vmunix | grep -c rpi4-wifi:` は 26。
- コマンドと結果（host、QEMU・実機は使っていない）:
  - `sh plan/ws206/tests/sdio-host-test.sh build/ws206-p002b/sdio <dtb>` → unit PASS、stop PASS（stop は 1 ms で戻る）、smp PASS（dispatcher → controller の入れ子の lock 4000 回、TSan の報告なし）。
    unit の中身: 分周の表（250・200・100 MHz で 25 MHz と 400 kHz、2.00 の 2 の冪、ERANGE、EINVAL）、R4・R5 の解き、voltage の窓、CIS の tuple（MANFID、
    function 0 の FUNCE、NULL、link 0xff、短い MANFID、途中で切れた tuple と chain）、実の DTB の slot（0xfe300000、158、pin 34〜39、ALT3、pull）と
    claim の EBUSY・QEMU の印・chip なし・disabled の ENODEV、dispatcher（register の失敗の再試行、mask→mode→register→unmask、2 slot、EBUSY・EEXIST・EINVAL・
    ENOENT、両方に問う、remove の後に呼ばれない、100000 回の unclaimed で mask・次の add で unmask）、host の手順（reset、3.3 V の後に power、313 の分周、
    enable の値、電源の無い card の ETIMEDOUT と line の reset、25 MHz、disarm の時に status を読まない、card interrupt の disarm・起こし・待ち・期限・
    reset の後の enable の書き直し・rearm・disable・停止、全ての遅い書き込みの後の待ち）、enumeration（CMD5 の無応答 2 回と busy 2 回の再試行、RCA、
    CCCR、CIS、4 bit と 25 MHz、F1 の enable、F2 の ETIMEDOUT、disable、block size の読み戻し、CMD53 の byte mode 63 byte と block mode 4×64 の
    読み書き、範囲の外の EINVAL、power-up が終わらない時の ETIMEDOUT、stop の ECANCELED）、thread（probe → detached の thread → mailbox・clock・
    WL_ON・pin・host・dispatcher・pulse・enumeration・F1 → 後始末。WL_ON は 0・1・0 の順だけ、BT_ON に触らない、GPFSEL3 と pull の REG2 の bit、
    slot が空、clock・bus power・signal enable が 0、2 度目の probe は EALREADY）。
  - 試験が誤りを捕まえることの確かめ（scratchpad の写しで、tree は変えていない）: 書き込みの待ちを外す、handler の disarm を外す、reset の後の enable の
    書き直しを外す、remove で slot を消さない、分周を切り捨てにする、`wifi_sleep` が `stopping` を見ない（stop が 300 ms を超える）、dispatch の lock を
    外す、PIO の ready の acknowledge を外す、4 bit の書き込みを外す、後始末で host を止めない → どれも FAIL。
  - rpi4 の vmunix（`make -j16 -o .../build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix`）→ rc 0、warning 0。
    `build/arm64/vmunix`（SHA-256 `825a25e3b0f3414c26ee5677b2f10f1410e6d14e1af7907a940a68595b3a8757`）。
  - `rpi4.c` を `-DCONFIG_DRIVER_BCM2711_WIFI=0` で同じ flag で compile → warning 0（最初は `-Wunused-but-set-global` が出たので記録の変数を設定で囲んだ）。
  - amd64 の vmunix → rc 0、warning 0。`git diff --check` → 空。
- 未実施: Pi 4 の実機（CMD5・CCCR・CIS、Pi の起動の回帰）、QEMU raspi4b の `boot-test.sh`（T1）。
- 試験の file `plan/ws206/tests/sdio-host-test.*`・`stub/` は WS206 の未完了の Phase（p004・p005 が host と card の層を使う）から参照する。WS の終わりに
  Master の Tools に登録するかを Q1 が判断。

## 残件（p004 へ）

- W8 の順（root を待って firmware の file を確かめてから hardware）。thread の作り直し（publish の thread と bus の thread）。
- shutdown の口の要否（`drv_rpi4_wifi_stop` を `system_shutdown_prepare` などから呼ぶか）を p004 の前に Q1 へ（review-3 の指摘 3）。
- F2 の enable、CCCR の割り込みの enable（master・F1・F2）、card interrupt の arm、100 ms の poll（bus の thread）。
- 実機で読んだ値（clock、WL_ON、版）を p004 の values.md に。
