<!-- awesome-plan project=zedbsd record=ws278-p006 -->

# ws278-p006 Pi 5 の WiFi（CYW43455、sdio2）

- 親: [ws278](../ws.md)、Queue: q1543 / q1543-i02（P3、2026-10-21）
- Status: in-progress（実装と build まで。実機の確認は WS278 の最後の一括の試験）

## 目的と受け入れの条件

Pi 5 の内蔵 WiFi（CYW43455、BCM2712 の 2 つ目の SD controller の sdio2 に接続）を既存の bcm43455 の driver で `wlan0` にする。

- build（warning 0）。
- 実機（一括の試験）: `rpi5-wifi: SDIO host at 1001100000, line 306, 200000000 Hz; WL_ON gio 28`、`rpi5-wifi: SDIO and WL_ON pins routed`、`rpi5-wifi: SDIO card: 3 functions …`、bcm43455 の版の行、`wlan0` が出る。`wifi scan` で AP が見え、接続して DHCP・`ping`。

## 設計

- `src/drivers/platform/rpi5/rpi5-wifi.c`: `rpi4-wifi.c` を写して Pi 5 向けに書き直した（ユーザーの「写して書き直す」）。違い: mailbox を使わない。device tree の `brcm,bcm4329-fmac` の親（`brcm,bcm2712-sdhci` の sdio2）の reg・interrupts（GIC SPI 274 → line 306）・clocks（fixed-clock 200 MHz）、`vmmc-supply` の wl-on の regulator（`gpio` = gio の pin 28、`enable-active-high`、`startup-delay-us` 150 ms）を読む。pin は pinctrl-0 の group（sdio2 の GPIO 30〜35 を sd2・pull、WL_ON の GPIO 28 を gpio）を当てる。電源は gio の pin の出力。
- SDIO の host・card の層・割り込みの dispatcher は Pi 4 の `rpi4-sdio-host.c`（SDHCI 3.0、PIO、32 bit の書き込み）・`rpi4-sdio.c`・`rpi4-mmc-irq.c` をそのまま使う（Pi 5 の SD card が `rpi4-sdhci.c` を使うのと同じ）。`rpi4-sdio.c` の `describe_slot` が `rpi4-gpio.c` を引くので link のために入れる（Pi 5 では呼ばない）。
- 新: `rpi5-gio.c`（BCM2712 の brcmstb の GPIO: bank 0x20、DATA 0x04、IODIR 0x08 の 1 = 入力）、`rpi5-pinctrl.c`（BCM2712 の pin の mux 4 bit・pull 2 bit、C0 と D0 で表が違う。GPIO 24〜35 の gpio・sd2・uart0 だけ）。
- firmware の file: Pi 5 の `brcmfmac43455-sdio.raspberrypi,5-model-b.{txt,bin,clm_blob}` は RPi-Distro firmware-nonfree（3bab0f8…）で Pi 4 と同じ file への link（`brcmfmac43455-sdio.txt`、`../cypress/cyfmac43455-sdio.bin`・`.clm_blob`、2026-10-21 に取得して確かめた）なので、既存の `bcm43455-firmware` package（minimal の firmware・CLM・NVRAM）をそのまま使う。`config-rpi5.mk` の USER_PROGRAMS に `bcm43455-firmware` を足した。
- `platform/arm64/vmunix.mk`: bcm43455 の source の一覧を `ARM64_BCM43455_SOURCES` に括り出し、rpi4 の CONFIG_DRIVER_BCM2711_WIFI の節と rpi5 の board の source の両方で使う（rpi4 の source の並びは同じ）。
- 出典: Raspberry Pi の Linux の gpio-brcmstb.c（GIO の register）、pinctrl-bcm2712.c（C0・D0 の pin の register の位置と function の表、2026-10-21 に source を取得して行を照合）。

## 変えた所

- 新: `src/drivers/platform/rpi5/rpi5-wifi.c/h`、`rpi5-gio.c/h`、`rpi5-pinctrl.c/h`。
- `src/kern/platform/rpi5.c`（fdt の番地を保ち、refresh で WiFi の probe）、`platform/arm64/vmunix.mk`、`config/ci/config-rpi5.mk`。

## 確認（2026-10-21 P3、host）

- rpi5 kernel build: rc=0、warning 0、vmunix check PASS。
- 未実施: rpi4 の kernel の build（vmunix.mk の WiFi の一覧の括り出しの後、rpi4 の source が同じかを p008 で確かめる）、image の build（firmware の package の取得と license の確認を含む）、実機。

## 危険

- R15: BCM2712 の SDHCI（sdhci-brcmstb）が `cfg` の領域の設定を要すれば PIO の host が動かない。log の `SDIO card:` の行が出なければここ。
- R16: sdio2 の IO 電圧。3.3 V の前提（dts の `mmc-ddr-3_3v`）。
- R17: `bcm43455-firmware` の manifest の device の記述は Pi 4 のまま（WS206 の package、中身は同じ）。

## 1 回目の実機の結果と対処（2026-10-22 P3）

- 実機: `rpi5-wifi: WL_ON was level 0, input (0)`、`SDIO and WL_ON pins routed`、`rpi4-sdio: host specification 2, capabilities 55eec832 (200 MHz)`、`rpi4-sdio: no answer to CMD5 (5)`、`bcm43455: chip did not power up (5)`。
- 原因の見込み: BCM2712 の SD controller には SDHCI の外に「cfg」の block（node の 2 つ目の reg、0x10_0110_0400）がある。Linux の sdhci-brcmstb は non-removable の card で `SDIO_CFG_CTRL` の card detect を test の値で「有り」に固定し（`sdhci_brcmstb_cfginit_2712`）、clock の設定ごとに `SDIO_CFG_SD_PIN_SEL` で SD の pin を選ぶ（`sdhci_bcm2712_set_clock`）。zedBSD はどちらもしていなかった。card が無いと見える controller は bus の電源と clock を出さないので、CMD5 に答えが無い。pin の mux（D0 の GPIO 28・30〜35）と GIO の register は Linux の pinctrl-bcm2712.c・gpio-brcmstb.c と照らして合っていた。32 kHz の LPO は Pi 5 の dts に software の制御が無い（Pi 4 と同じく基板の側）。
- 対処: `rpi5-wifi.c` に `configure_controller()`（cfg の block を写像し、`CTRL` の bit 31 を立て bit 30 を落とし、`SD_PIN_SEL` を SD（2）に）を host の初期化の前に置いた。log に前後の値、host の present state（bit 16 = card 有り）・host control・clock を「started」と「before CMD5」で、WL_ON の電源を入れた後の level を足した。
- 未実施: 実機（2 回目の試験）。

## 2 回目の実機の結果と対処（2026-10-22 P3）

- 実機（log `build/review/rpi5/uat-20261022-2-boot-000001.txt`）: cfg の書き込みは効いた（`controller configuration 40003c03 pins 00000000, now 80003c03 pins 00000002`、present の bit 16 = card 有り）。bus の電源 0x0f00（on・3.3 V）、clock 0x000efa07（内部 clock・SD clock on、分周 250 = 200 MHz / 500 = 400 kHz）、WL_ON は 1・出力。それでも `rpi4-sdio: no answer to CMD5 (5)`。
- 読み: error 5 は EIO で、timeout ではない（controller が CRC・end bit などの誤りを見た）。present の bit 24（CMD の level）が 0、DAT0・DAT2・DAT3 も 0、DAT1 だけが WL_ON の後に 1。pull-up の付いた CMD が low に見えるので、controller は CMD を low のまま読んでいる（応答の start bit を即座に見て崩れた応答）。
- 確かめて合っていた物（Linux の値と照合）: 識別の clock 400 kHz、IO 電圧（sdio2 は `mmc-ddr-3_3v`、vqmmc の regulator は無い）、WL_ON の後の待ち 150 ms（wl-on-reg の startup-delay-us）、pin の function（D0 は GPIO 30〜35 の sd2 = 1、C0 は 4/4/4/3/4/3）と pad の pull の位置（pinctrl-bcm2712.c の GPIO_REGS と一致）、SD_PIN_SEL = SD（Linux の sdhci_bcm2712_set_clock と同じ）、32 kHz の LPO（Pi 5 の dts に software の制御は無い）。原因は code からは特定できなかった。
- 対処（次の試験で原因を絞る log）: pin controller の stepping と register の全部を routing の前後で（`rpi5-pinctrl: brcm,bcm2712d0-pinctrl before/after: …`）、GIO の bank 0 の level と方向（pad から見た GPIO 30〜35 の level、`rpi5-wifi: GPIO bank levels …`）、CMD が low の時は pin の選択 0〜3 ごとの present（clock を掛け直して）、CMD5 が timeout でない時の interrupt status（`rpi4-sdio: command failed, interrupt status …`）。
- 未実施: 実機（3 回目の試験）。

## 3 回目の実機の結果と対処（2026-10-22 P3）

- 実機（log `build/review/rpi5/uat-20261022-3-boot-000001.txt`）: `rpi5-pinctrl: brcm,bcm2712c0-pinctrl before: 00006677 00000011 00000000 00000000 15555699 …`、`after: … 15553434 …`、`rpi5: SError pending after rpi5-wifi: routing the pins`、`command line low`、`rpi4-sdio: command failed, interrupt status 00048001`（bit 0 command complete・bit 15 error・bit 18 command end bit error。CMD が low のままで応答の start bit をすぐ見て end bit が 0）。
- 原因: firmware が渡した device tree は C1 の物（`brcm,bcm2712c0-pinctrl`、block 0x30）だが、chip は D0。register の値が D0 の配置でしか筋が通らない: word 0 の 0x6677 は D0 なら GPIO 1〜4 が全部 vc_spi0（基板の boot flash の 2712_BOOT_CS_N〜SCLK）、C0 と読むと GPIO 0 = ir_in・GPIO 3 = vc_uart3 でばらばら。word 4 の 0x…699 は C0 の mux と読むと選択子 9（存在しない）。words 4〜6 は D0 の pad（pull）の形（EMMC の CMD・DAT が pull-up）。C0 の表で書いたので、GPIO 30・31 の mux は D0 の word 3 の未使用の bit へ、GPIO 32〜35 は D0 の pad の word 4 へ、pull は block の外の 0x24 へ書いた（D0 の block は 0x20 → SError）。SDIO の pin は GPIO の function のまま pull-down → CMD が low。2 回目も同じ原因。
- D0 の SD2: GPIO 30 = CLK・31 = CMD・32〜35 = DAT0〜3、function sd2 は選択子 1、pull は CLK なし・CMD と DAT は pull-up（device tree の sdio2_30_pins、C1・D0 の tree で同じ）。
- 対処（`rpi5-pinctrl.c`）: stepping を chip から決める。boot loader は boot flash の pin（GPIO 1〜4）を SPI の function に残すので、mux の word 0 で D0 は下位 16 bit = 0x6677、C0 は bit 4〜19 = 0x6567（Linux の pinctrl-bcm2712.c の C0・D0 の表から）。ちょうど一方だけ合えばそれ、どちらでもなければ tree の compatible。log `rpi5-pinctrl: <stepping> registers (the tree names …, register 0 is …)`。読み書きはその stepping の block の大きさ（C0 0x30・D0 0x20）の中だけ（write_field にも防御）。
- host の確認（scratch の使い捨ての試験、tree に入れない）: UAT の register の値と C1 の tree で、D0 と判定し、GPIO 30〜35 が mux 1・pull CLK 0/他 2、block の外への access 0。C0 の値では C0 の表（mux 4/4/4/3/4/3、pad 0x24）。
- 未実施: 実機（4 回目の試験）。

## 5 回目の実機の結果と対処（2026-10-22 P3）

- 実機（kernel 9d78dc950、ssh で dmesg を読んだ）: SDIO の bus が動き、firmware 7.45.241 が走り 20 channel を scan、その後 `bcm43455: chip reports the NVRAM's placeholder address` → `rpi5-wifi: onboard Wi-Fi not published (37)`。
- 原因: Pi 4 では chip の OTP に基板の address があり、driver は `cur_etheraddr` でそれを読む（Pi 4 の側は何も渡さない）。Pi 5 の chip の OTP には address が無く、chip は NVRAM の `macaddr`（全基板共通の placeholder）を返す。Pi 5 では boot の firmware が device tree の Wi-Fi の node（`brcm,bcm4329-fmac`、alias `wifi0`、dts の `local-mac-address = [00 00 00 00 00 00]` を埋める）に address を入れ、Linux の brcmfmac は `of_get_mac_address` で読んで chip に `cur_etheraddr` を set する（bcm2712-rpi-5-b.dts・bcm2712-rpi.dtsi rpi-6.12.y を取得して確かめた。live の tree は zedBSD から読めない）。
- 対処: bcm43455 に `bcm43455_device_set_address()`（probe の前に呼ぶ、0・multicast を拒む）を足し、start ごとの `read_identity()` が読む前に `cur_etheraddr` を set する（set されない Pi 4 は変わらない）。`rpi5-wifi.c` の `describe_address()` が Wi-Fi の node の `local-mac-address` を読み、0 なら Ethernet（`cdns,macb`）の `local-mac-address` の次（下位 3 octet + 1、Raspberry Pi の番号の振り方）を使う。どちらから取ったかを log に出す。
- 確認（host）: rpi5 の image の build rc=0・warning 0・vmunix check PASS、rpi4 の kernel の build rc=0・warning 0・vmunix check PASS。
- 未実施: 実機（6 回目）。見る所: `rpi5-wifi: address …:… from the tree's Wi-Fi node`（または `after the Ethernet's`）、`bcm43455: chip address` がその値、`published as wlan0`。
- 注: `usb-hid-diag: usb0 device 1 interface 2 refused error=21 stage=parse item=0x0036` は keyboard の vendor の interface で、Pi 4 でも同じ。keyboard は動いており、変えていない。
