<!-- awesome-plan project=zedbsd record=ws313-p004 -->

# ws313-p004: SD（meson の MMC）と root の mount

Status: in-progress（実装済み・実機未試験）
Queue: q1675-i02（P7 の配下 A）
WS: [WS313](../ws.md)、設計 [design.md](../design.md) §3、調べ [peripherals.md](../peripherals.md)

## 目的

microSD の slot（sd_emmc_b、0xfe08a000）の card を mmcblk0 として出し、`odroid-c5.c` が `drv_s905x5m_mmc_init(fdt_phys)`・`drv_s905x5m_mmc_disk()` で root を探せるようにする。

## 実装（2026-10-29、commit f6c1f3146・470b4720d）

- `src/drivers/platform/s905x5m/s905x5m-soc.{c,h}`: clock の gate と SD の clock の selector（read-modify-write、lock）、reset の pulse（0x0）と level（0x40、bit が立つと解除）、pinmux（4 bit/pin）、GPIO（in・out・dir・pull・drive）、FDT（compatible と reg の address で node を探す、interrupts を GIC の番号に、`<phandle pin flags>` の GPIO、1 cell の数、parent の cell 数での生の reg）、spin の delay。値は HK の pinctrl-meson-s7d.c・s7d.c・meson-gpio.h、UP の reset-meson.c・pinctrl-meson.c から（block ごとに出典）。
- `src/drivers/mmc/meson-mmc.{c,h}`: Amlogic の SD/eMMC の controller の direct mode（CMD_CFG・CMD_DAT・CMD_RSP を書いてから CMD_ARG で開始、STATUS を poll、割り込みは使わない）。64 KiB の bounce buffer を経る DMA（CPU の cache は clean・invalidate）。card の層は dw-mmc.c の手順に倣い自前で書いた（CMD0 → CMD8 → ACMD41 → CMD2/3/9 → CMD7/16 → ACMD51 → ACMD6 → CMD6 で high speed）。多 block は CMD18/25 の後に CMD12（R1b、DAT0 の待ち）。失敗した run は stop → CMD12 → CMD13 で戻して 5 回まで、high speed の CRC の誤りでは 24 MHz に落とす。mmcblk<unit>、DISK_REMOVABLE、DISK_STATS_KIND_SDMMC。clock は 400 kHz と default speed を crystal 24 MHz から（÷60、÷1）、high speed を fclk_div2 1 GHz ÷20 = 50 MHz。
- `src/drivers/platform/s905x5m/s905x5m-mmc.c`: sd_emmc_b の gate（REG0 bit 25）、GPIOC_0..5 を function 1・pull-up・3 mA、GPIOC_6（card detect）を pull-up の入力、vqmmc（GPIODV_4、3.3 V の level を DT の states から）と vmmc（GPIOH_6 high）を DT の regulator の gpio から、CLKC 0x16c の selector（mux [27:25]、div [22:16] = 0、gate 23）。bounce buffer は `kern_pmem_alloc_limited(64 KiB, 4096, 0xffffffff)`。card detect は log だけ（U-Boot が読んだ card なので、空と読めても試す）。attach の後に sector 0 を読み 55aa を log。
- `platform/arm64/s905x5m.mk`: `ARM64_S905X5M_SOURCES` から `s905x5m-stubs.c` を外し、新しい file と meson-mmc.c・dw-eqos.c・mii-phy.c・yt8531.c・pci.c を足した（stubs.c は 2026-10-29 Q1 の OK で P7 が `git rm`）。

## 確かめ

- build: `timeout 1200 make -j8 ZEDBSD_CONFIG=config/ci/config-odroid-c5.mk BUILD=build/pa build/pa/vmunix`: exit 0、`grep -cE '(error|warning):'` = 0、arm64 vmunix check PASS。image に driver の log の文字列があることを確かめた。
- host 試験（偽の register と偽の SD card）:
  ```sh
  cc -std=c99 -O2 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -Iinclude -Isrc \
     src/drivers/generic/fdt.c src/drivers/platform/s905x5m/s905x5m-soc.c \
     src/drivers/platform/s905x5m/s905x5m-mmc.c src/drivers/mmc/meson-mmc.c \
     tests/meson-mmc-host-test.c -o build/pa/host/meson-mmc-host-test
  build/pa/host/meson-mmc-host-test driver     # 39 checks, 0 failure
  build/pa/host/meson-mmc-host-test version1   # 5 checks, 0 failure
  build/pa/host/meson-mmc-host-test glue build/odroid-c5/odroid-c5.dtb   # 12 checks, 0 failure
  ```
  確かめた事: 命令の列と CMD_CFG の bit（no resp・long・no CRC・R1b・data の長さ・block mode）、clock の divider（0x3f → 60 → 1 → 20）と source（slow・slow・fast）、4 bit、block の読み書き（CMD17 は 512 byte を 0 と数える、CMD18/25 の後の CMD12、write の後の CMD13）、CRC の誤りからの回復と 24 MHz への低下、version 1 の card の byte の address。glue: 実 DTB で pinmux の word 9 = 0x00111111、pull、drive 3 mA、GPIOH_6 high・GPIODV_4 low の出力、gate、selector（1 GHz、div 0、gate）、DMA の address が bounce。
- 実機: 未実施（夜の serial の試験）。QEMU: 対象外（tree に C5 の machine が無い）。

## 夜の serial で見る行

- `s905x5m-mmc: clock selector ..., controller clock ... config ..., card detect N (0 is a card)`（U-Boot が残した値）
- `meson-mmc: mmcblk0 CMD8 echo 123, OCR ..., RCA ...`、`CID ... CSD ...`、`SDHC card ...`、`... blocks`、`bus 4-bit, 50000000 Hz, high speed`
- `s905x5m-mmc: sector 0 ends 55aa (a boot record)`
- 失敗の時: `meson-mmc: card in slot 0 unusable (errno, status ...)`、`... failed (..., status ...), try N`、`CRC errors at high speed, card clock down to 24000000 Hz`

## 未確認・危険

- 実機で何も確かめていない。特に: DATA0 の bit（STATUS bit 16）を R1b の busy に使う事、IRQ_EN = 0 でも STATUS が立つ事（Linux は raw の status を読んでから mask する）、ADDR64 の上位の byte を 0 にする事、CLOCK の always-on（bit 28）。
- CMD_CFG の length の 512 を 0 と書く事は HK の driver の 1 block の扱い（`FIELD_PREP(CMD_CFG_LENGTH_MASK, blksz)`）からの推定。
- 再開点: 夜の試験の結果を見て直す。
