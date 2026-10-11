<!-- awesome-plan project=zedbsd record=ws206-p001 -->

# ws206-p001: Raspberry Pi 4 の WiFi の調査と設計

Parent: [WS206](../ws.md)
Status: in-progress（q932、P1。設計の第 1 版、判断の点を Q1 へ。Pi 4 の実機の読みは未）
Phase disposition: normal
Queue: q932（[P1 の lane](../../agents/P1/queue.md)）

## 由来

2026-10-11 ユーザー:「RPi4のWiFiを実装。」

## 今の tree の事実（2026-10-11）

| 所 | 事実 |
| --- | --- |
| Pi 4 の driver | `src/drivers/platform/rpi4/`: console、ethernet（GENET）、firmware（VideoCore の mailbox の property、`drv_rpi4_firmware_property`）、PCIe（VL805 の xHCI）、**sdhci（eMMC2、SD card、PIO、1079 行）**。WiFi の SDIO の controller（BCM2711 の legacy の "emmc"、Arasan SDHCI、ARM の 0xfe300000、DT の `mmcnr@7e300000`）の driver は無い。GPIO の pin の機能（GPFSEL）の driver も無い（`src/drivers/gpio/` は intel だけ） |
| WiFi の枠 | kernel の共通の WLAN の core（`src/kern/net/wifi/`、約 11,400 行、`include/kern/net/wlan.h`、UAPI `include/uapi/wlan.h`）は **SoftMAC の形**: core が scan・auth・assoc の management frame を作り、WPA2 の 4-way も core（`wlan-wpa2.c`）、driver は radio の op（scan の channel、management の送り、key の設定、802.11 の data frame の送受）。Intel AX211 と RTL8822B がこれを使う。connect の要求は passphrase を持つ |
| BCM43455 | Broadcom/Cypress の **FullMAC**（MLME も scan も firmware の中、data は 802.3 の Ethernet frame、制御は BCDC の ioctl・iovar、event は firmware から）。SDIO の 3 つの function（F0 CCCR、F1 backplane、F2 data）。firmware（`brcmfmac43455-sdio.bin`）と NVRAM（`brcmfmac43455-sdio.raspberrypi,4-model-b.txt`）と CLM（`.clm_blob`）を host が RAM に書く |
| Pi 4 の配線 | WiFi の SDIO は GPIO 34〜39 の alt3、WL_REG_ON は firmware の GPIO の拡張（DT の `expgpio` 1 = mailbox の GPIO 129、`RPI_FIRMWARE_SET_GPIO_STATE`）、controller の clock は firmware の clock（EMMC、id 1）。**実機の Pi 4 の DT・firmware の file は未確認**（P3 が使っている間は読むだけ） |
| firmware の包み | `userland/firmware/`: 任意の package、immutable の上流から取り込み、license・WHENCE・manifest と `/lib/firmware` へ。BCM43455 の file は linux-firmware の `brcm/`（Cypress/Infineon の license: binary のまま再配布可）と、NVRAM は Raspberry Pi の `firmware-nonfree`（同じく）→ license の監査が要る |

## 設計（案）

| ID | 決め | 理由 |
| --- | --- | --- |
| W1 | **SDHCI を 2 つ目の instance に広げる**: `rpi4-sdhci.c` の register の手続き（PIO）を SD card と SDIO で共有する形に分け、WiFi の controller（0xfe300000）を SDIO の mode で使う: 4 bit、25 MHz（後で 50 MHz の high speed）、CMD5・CMD3・CMD7・CMD52・CMD53（byte と block）、SDIO の card の割り込み（INT の bit 8）。PIO で始め、速さが足りなければ DMA（ADMA2）を後で | 既存の SDHCI の code を使う。PIO でも 20 Mbit/s 程度は出る見込み |
| W2 | 電源と pin: GPFSEL で GPIO 34〜39 を alt3（pull up）、mailbox の SET_GPIO_STATE で 129 を 1（WL_REG_ON）、firmware の clock の rate を GET_CLOCK_RATE で読む | DT の値と同じ |
| W3 | Broadcom の SDIO の bring-up（`src/drivers/wifi/brcm43455/`）: F1 の backplane の窓で chipcommon の id、ARM CR4 の core を止めて firmware を RAM に、NVRAM を RAM の末尾に、core を走らせ、F2 を有効に、SDPCM の framing（control・event・data の channel、credit）、BCDC の ioctl・iovar | brcmfmac と同じ手順（GPL の code は写さず、Broadcom の公開の資料と挙動から書く。Linux の brcmfmac は GPL/ISC の両方の license（ISC は可）だが写さない） |
| W4 | **WLAN の core に FullMAC の形を足す（kernel の内部の API、UAPI は変えない）**: driver の op が `connect(ssid, bssid, security, pmk)`・`disconnect`・`scan` を持ち、firmware の event で core の state（scanning・associating・connected・failed）を進める。data は 802.3 の frame をそのまま net_device へ | SoftMAC の op では firmware の MLME と合わない |
| W5 | **WPA2-PSK は firmware の supplicant**（`sup_wpa` 1、`wsec_pmk` に PMK）: core が passphrase から PMK（既存の PBKDF2、`wlan-crypto.c`）を作って driver へ。WPA3（SAE）は 43455 の firmware が非対応なので範囲の外 | firmware が 4-way をする。host の 4-way（core の wlan-wpa2.c）を FullMAC の EAPOL に繋ぐ案より小さい |
| W6 | firmware の package `userland/firmware/brcm43455/`: linux-firmware の tag（AX211 と同じ commit）から `brcm/brcmfmac43455-sdio.bin`・`.clm_blob`、Raspberry Pi の firmware-nonfree の NVRAM、license・WHENCE・manifest。kernel が `/lib/firmware/brcm/` から読む（AX211 と同じ firmware の読み方） | 既存の firmware の包みの決まり |
| W7 | 試験: host で SDPCM・BCDC の framing と event の解析（偽の chip の台本）、core の FullMAC の state。実機（T1、Pi 4）: 電源と CMD5 の応答、chip id、firmware の読み込み、scan の結果、WPA2 の AP に接続・DHCP・ping。Pi 4 は P3 と共有: 実機の試験は Q1 の調停で | |

## Phase（案）

| Phase | 内容 | 見積 | 依存 |
| --- | --- | --- | --- |
| p001 | この設計（Pi 4 の DT・firmware の file・GPIO の拡張の実機の読みを足して確定） | — | — |
| p002 | SDHCI の SDIO の mode と 2 つ目の instance、pin・電源・clock（W1・W2）、Pi 4 で CMD5 と CCCR の読み | 6 LW | p001 |
| p003 | firmware の package（W6）と license の監査 | 2 LW | p001 |
| p004 | Broadcom の bring-up（chip id、firmware・NVRAM の読み込み、F2）と SDPCM・BCDC（W3）、host の試験 | 10 LW | p002、p003 |
| p005 | WLAN の core の FullMAC の形と firmware の supplicant（W4・W5）、scan・connect・data | 8 LW | p004 |
| p006 | 実機（T1）: scan・WPA2・DHCP・ping、networkd の WiFi の画面から | — | p005 |
| p007 | 規約の全文の見直し | — | p006 |

計 約 26 LW。

## 判断（Q1 経由でユーザーへ、推し付き）

| ID | 問い | 推し |
| --- | --- | --- |
| E1 | WPA2 の 4-way をどこでするか | **firmware の supplicant**（W5）。host の 4-way（core の wlan-wpa2.c を FullMAC に繋ぐ）は後で WPA3 などが要る時に |
| E2 | 試験の AP | **Pi 4 から見える WPA2-PSK の AP の SSID と passphrase** をユーザーに（2.4 GHz と 5 GHz のどちらも） |
| E3 | WLAN の core の FullMAC の形（kernel の内部の API の追加） | **足す**（W4、HAL・UAPI は変えない） |

## 未確認（次に読む、Pi 4 は読むだけ）

Pi 4（10.0.30.2）の DT（`mmcnr` の status、`expgpio`、`wifi_pwrseq`）、`/boot` の firmware の版、mailbox の GPIO 129 の今の状態、0xfe300000 の SDHCI の capabilities・version、BCM43455 の chip の rev（CMD5 の後）。
