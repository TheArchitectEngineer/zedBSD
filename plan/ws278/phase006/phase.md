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
