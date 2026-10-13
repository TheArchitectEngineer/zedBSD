<!-- awesome-plan project=zedbsd record=ws206-p004 -->

# ws206-p004: Broadcom の bring-up、SDPCM・BCDC・event、bus の thread と lock、wlan0 の公開と shutdown

Parent: [WS206](../ws.md)
Status: in-progress（2026-10-13 P4。実装・host 試験・build は済み。clearance は Pi 4 の実機の dmesg（下の「Pi 4 で見る dmesg の行」、Q1 がユーザーに依頼）と T1 の QEMU raspi4b の起動の回帰の結果を Q1 が判定してから）
Phase disposition: normal
Queue: P4 の lane（2026-10-13 Q1 の割当）
値の出典: [values.md](values.md)

## 由来と承認

- 2026-10-12 ユーザー「RPi4のWiFiとBluetoothを実装し、RPi4は完了です」。2026-10-13 Q1 が P4 に p004 の実行を依頼（p002b・p003 の main への merge の後）。
- 設計: [p001](../phase001/phase.md) の W4・W5・W8・W10・W12（第 3 版）と [review-3](../phase001/review-3.md) の指摘 2・3・4・軽微 8。p002b の残件（W8 の順、bus の thread、F2 の enable、CCCR の割り込みの enable、card interrupt の arm）。
- Q1 の決定（2026-10-13）: `drv_rpi4_wifi_stop` を shutdown に繋ぐ（kernel の内部の口、HAL 不変）。E6「世界共通でお願いします。」→ CLM の load の後に `country XX`、読み戻して log。p008 との境界は設計どおり、決まっていない所は仮の形を書く。実機の値の欄は空欄。由来の扱いは WS218 の方針（出典の明記、写し無し、機械比較の表）。

## やったこと（範囲）

新しい driver `src/drivers/wifi/bcm43455/`（platform に依らず、`struct bcm43455_bus_ops` だけで chip に触る）:

| file | 中身 |
| --- | --- |
| `bcm43455.h` | bus の op（CMD52、CMD53 の増加・固定 address、sleep、card interrupt の wait・enable・rearm・kick、power up・down）と device の API（create・probe・publish・destroy） |
| `bcm43455-device.h`・`bcm43455.c` | device: 起動の設定（rxglom 0、CLM、country XX と読み戻し、MAC・ver・cap と `idsup`、event mask、roam_off、PM 0、有効な channel → scan profile）、MAC の検査、wlan0 の公開と station の attach、net の open・close・transmit・poll・ioctl、station の quiesce・stop_retry、rx・tx の ring（128・64）、bus の hook |
| `bcm43455-chip.h/.c` | backplane の窓、chip id、EROM、CR4・D11 の halt と reset、RAM の大きさ、PMU と save-restore の検出、KSO・card control・PMU の reload、download（HT の上で、全部を読み戻し）、NVRAM の位置、reset vector、frame function の start |
| `bcm43455-bus.h/.c` | bus の thread（card interrupt か 100 ms の poll、kick）、受け（12 byte の header、残りを 4 byte 単位、header だけの credit の frame、長すぎる frame の読み捨て、壊れた frame の abort）、送り（credit の窓、詰め）、control の 1 つの slot と direct mode、hook |
| `bcm43455-sdpcm.*`・`-bcdc.*`・`-event.*`・`-nvram.c`・`-firmware.h`・`-bytes.h`・`-hw.h` | 純粋な encode・decode（SDPCM、BCDC の control・data、iovar、event、NVRAM の変換と MAC と位置、CLM の分割、file の形の検査） |
| `bcm43455-files.c` | 3 つの file を root から読む（kernel だけ。host 試験は置き換え） |

Pi 4 の側と kernel:

- `src/drivers/platform/rpi4/rpi4-wifi.c`: publish の thread を作り直した（**W8 の順**: root を待つ（1 s ごと、300 s まで）→ file を driver の lock なしで読む（package が無ければ hardware に一切触らずに終わる）→ device を作る → mailbox・clock・pin・host・SPI 126 → `bcm43455_device_probe` → lifecycle の mutex の下で `stopping` を見て `bcm43455_device_publish`）。公開した chip は host・slot・電源を持ち続け、公開しなかった chip は全部を返す。bus の op の実装（power_up = 400 kHz・1 bit に戻す → WL_ON の pulse → enumerate → F1・block size）。
- `rpi4-sdio-host.c/.h`: `drv_rpi4_sdio_host_card_irq_kick()`（card interrupt の待ちを割り込み無しで起こす、EAGAIN）。
- **shutdown の口**: `include/kern/platform.h` に weak の `kern_platform_shutdown_prepare()`、`src/kern/shutdown.c` が volume の sync の後・network の停止の前に呼ぶ、`src/kern/platform/rpi4.c` が `drv_rpi4_wifi_stop()` を呼ぶ。HAL・hal.h は不変。
- `platform/arm64/vmunix.mk`: `CONFIG_DRIVER_BCM2711_WIFI` の block に 8 file。

範囲の外（p005）: scan・escan と bss_info、connect と firmware の supplicant、errno の表、FullMAC の data path（`report_ethernet`）、link loss と restart、PMK の slot。p004 の station は quiesce・stop_retry だけの op で attach する（下の p008 の境界）。

## 設計からの違い（設計の範囲の中の技術の判断）

上流の bwfm（ISC）と WHD（Apache-2.0）を取得して値と手順を照合した結果、W4 の順を次のように直した（全て [values.md](values.md) に出典の行）:

| 設計 W4・W5 | p004 | 理由 |
| --- | --- | --- |
| download の前に halt、D11 は reset | attach の中で CR4 を halt、**D11 は reset に入れたまま**（disable） | bwfm `bwfm_chip_cr4_set_passive()` |
| HT は firmware の起動の後 | **download の前に HT を要求して待つ**、download の後 0 に戻し、start で再び要求 | bwfm `bwfm_sdio_load_microcode()`・`bwfm_sdio_preinit()` |
| v1 は HT を force（R8 は推測） | **save-restore を PMU から検出**（4345: PMU rev 17 以上、chip control 3 の bit 2）。SR なら wakeup の HT wait・card capability の CMD14・force HT、なければ要求を戻す。KSO は SDIO core rev 12 以上で立てる | bwfm `bwfm_chip_sr_capable()`・attach。R8 を手順の上で解いた（実機の値は Pi で読む） |
| — | attach の後に card control の WLAN reset、PMU の resource reload、F2 の disable | bwfm の attach |
| F2 は増加の address | **固定 address**（FIFO） | bwfm `bwfm_sdio_frame_read_write()`（`read_multi`） |
| 受けの最初の読みの長さ（未定） | **12 byte の header** を読み、残りを 4 byte に切り上げ（512 を超えれば block と残りの 2 回） | bwfm `bwfm_sdio_rx_frames()` |
| frame の indication の時だけ読む | **毎回読む**（無ければ 12 byte の読み 1 回） | bwfm `bwfm_sdio_task()` の「Might stall」 |
| 壊れた frame は terminate | **I/O abort → terminate → NAK** | WHD `whd_bus_sdio_abort_read()` |
| 初めの seq | **0xff、max 0**（窓 1） | bwfm attach |
| event mask 16 byte | **18 byte** | bwfm（43455 を扱う）。WHD は 16 |
| event の subtype 0x8001 を要求 | 読むが要求しない（OUI と user subtype を見る） | bwfm `bwfm_rx()` |
| country（rev は推測） | rev −1、断られたら 0、読み戻した abbrev が `XX` でなければ起動を断る（**2026-10-13 Q1 の判断で p005 が rev 0 を先、断られたら −1 に替えた**、[p005](../phase005/phase.md)） | E6。−1 は公開の出典で未確認（values.md） |
| `bus:rxglom` 0 | 送るが失敗しても続け、glom の frame が来たら 1 度 log | 名前は公開の寛容な出典で未確認 |
| open で file を読み直す（W8） | file は publish の時に 1 度読み、device が持つ（約 550 KB） | open の文脈で file system を使わない。p007 で見直してよい |
| bus の thread 以外は SDIO を使わない（W12） | **direct mode**: bus の thread が動いていない時（bring-up と停止）は lifecycle の持ち主が同じ受けの code を自分で回す | thread を 2 つの用途に分けないため。control の mutex で直列 |

## p008（FullMAC の engine、a9c627d6b）との境界

p004 は p008 の API を呼ばない。仮の形は書いていない。p005 で次の形にする（p008 の phase.md の「driver から見える内部の API」と同じ名前）:

| p008 の口 | p004 の今 | p005 で |
| --- | --- | --- |
| `struct wlan_radio_ops` の FullMAC の組（`fullmac_connect`・`ethernet_transmit` と `scan_channel_start`・`scan_stop`・`disconnect`・`quiesce`・`stop_retry`） | `quiesce`・`stop_retry` だけ（接続の op の無い形として attach が通る、`radio_ops_mode_valid`） | 全部を足して FullMAC の形に |
| scan profile は全 channel が `OFFLOADED_DWELL` | 既に全 channel を `OFFLOADED_DWELL` で attach（`ACTIVE_ALLOWED` なし） | 変えない |
| `ethernet_transmit` → driver の queue | `bcm43455_device_ethernet_transmit()`（tx ring 64、ENOBUFS・EMSGSIZE・ENETDOWN）、bus の thread が credit の中で送る | op から呼ぶ |
| `wlan_station_report_ethernet` は poll_receive から | rx ring 128 を `net_poll` が取り、今は捨てて `rx_dropped` に数える | 捨てる所を `report_ethernet` に |
| `report_fullmac_event`・`report_link_loss` | event は bus の thread の hook で数えて最初の 16 個を log | hook で latch に、poll で report |
| `fullmac_connect` は request を写してすぐ返る | control の slot（2048 byte、使い終わりに 0 で消す）と bus の thread が ioctl を出す仕組みは済み | connect の slot と ioctl の列 |

## lock（W12、review-3 の指摘 2・3）

| lock | 種類 | 守る物 | 中で取るもの |
| --- | --- | --- | --- |
| rpi4-wifi の lifecycle | mutex、DEVICE | publish と `drv_rpi4_wifi_stop` | device の lifecycle（publish の中） |
| device の lifecycle | mutex、DEVICE | probe・publish・open・close・quiesce・stop_retry、start・stop | bus の control、ring の spinlock、bus の spinlock |
| bus の control | mutex、DEVICE | control の要求を 1 つずつ（待つ間も持つ） | bus の spinlock |
| bus の spinlock | irqsave、DEVICE、leaf | slot・thread の状態・work の印 | 無し |
| device の ring | irqsave、DEVICE、leaf | rx・tx の ring、event の数、bus の誤り | 無し |

- bus の thread はどの mutex も取らない（spinlock だけ）。control の要求の持ち主が mutex を持って待っても閉じる。thread が止まると slot の要求に ENETDOWN を返す。
- close は station の close（WLAN の core が control gate を取る）の間 device の lifecycle を放す。gate → lifecycle（quiesce は gate の下で lifecycle を取る）の順を守るため。
- host 試験の stand-in は、driver の spinlock が全て leaf であること（spinlock の中で spinlock も mutex も取らない）を全ての取得で確かめる。

## 受け入れ

- rpi4 と amd64 の vmunix の build が warning 0。`CONFIG_DRIVER_BCM2711_WIFI=0` で `rpi4.c` が warning 0。`git diff --check` が空。
- host 試験 `bcm43455-host-test.sh`（unit: ASan・UBSan、smp: TSan）と `sdio-host-test.sh` が PASS。
- Pi 4 の実機（Q1 がユーザーに依頼）: 下の dmesg の行で chip id・版・MAC・country・wlan0 の出現が読め、Pi の起動（SD・Ethernet・USB）が壊れない。
- QEMU raspi4b（T1）: `boot-test.sh` で login prompt（WiFi は `no onboard Wi-Fi to bring up (19)` で何もしない）。

## コマンドと結果（2026-10-13 P4、host、QEMU・実機は使っていない）

```sh
sh plan/ws206/tests/bcm43455-host-test.sh <worktree>/build/ws206-p004/bcm43455 [<p003 の検証済みの cache>]
sh plan/ws206/tests/sdio-host-test.sh <worktree>/build/ws206-p004/sdio <main>/vendor/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb
make -j16 -o <worktree>/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix
make -j16 -o <worktree>/build/amd64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/vmunix
```

- `bcm43455-host-test.sh` → unit PASS、smp PASS（smp: 4 つの要求の thread・送りの thread・firmware の event と data の thread が bus の thread と競い、その下で停止。1.5 s で約 1.4 万の答え・約 1300 の送り・約 1400 の event と data、TSan の報告なし、seq の誤り 0）。P3 の検証済みの cache（sha256 が manifest と一致: `3075cb0b…`・`9823842c…`・`ca709be8…`）を渡すと、実の firmware の版の文字列（7.45.241、`idsup` あり）、CLM の `BLOB`、NVRAM の text（2074 byte → chip の形 1748 byte、`ccode` なし、`boardflags3=0x44200100` あり、仮の MAC `b8:27:eb:…`）も確かめる。
  - unit の中身: SDPCM（encode・decode、空の tag、補数の誤り、短い長さ、offset の誤り、header だけの frame、窓の wrap と 0x80、詰め）、BCDC（control の flag と id、受けた長さで切る、error と status、iovar の ENOSPC・EINVAL、data の版と offset）、event（big-endian、data の位置、別の ethertype、OUI、長すぎる data、mask の bit、18 byte）、NVRAM（comment・空行・CRLF・空白・`=` の無い行、長さの語、ENOSPC、MAC、top に置く）、CLM（1400 byte ずつ、BEGIN・END、1 回の時は両方）、file の形（版の文字列、大きさ、`BLOB`、text）、build の feature と cap の語、scan profile（重複・未知の channel、`OFFLOADED_DWELL`）、chip の model での bring-up（EROM の 4K/8K の規則・wide・described size・別 port・master と slave の wrapper・PMU の core、CR4 の halt と D11 の reset、PMU rev と SR、KSO・card control・PMU reload・F2 off、download の全読み戻しと NVRAM の位置と reset vector、SR あり・なしの start、ROM に CR4 が無い時の EIO）、probe の 14 の要求の順と中身、MAC の検査（仮の値）、country の拒否と rev 0 への fallback、無応答の ETIMEDOUT、公開（wlan0、MAC、MTU、op、profile、二度目の EALREADY）、open（WLC_UP、bus の thread、card interrupt の enable）、slot を通る要求、**秘密の値が slot の request・answer、tx・rx の frame、bounce に残らない**（送り・受け・読み戻しの全て）、送りの data（BCDC の header）、受けの data（ring → poll）、event、close（thread の停止、WLC_DOWN、電源断、quiesce の barrier、station の close 2 回）、閉じた後の ENETDOWN、壊れた frame（IO abort・terminate・NAK の後も次の frame が来る）、長すぎる frame（読み捨て）、credit が無い時の ETIMEDOUT と slot の掃除、無応答の ABANDONED、firmware の halt（thread が EIO で止まり、device が記録し、次の要求は ENETDOWN）。
  - 試験が誤りを捕まえることの確かめ（scratchpad の写しで、tree は変えていない）: slot の request を消さない、窓の 0x80 の guard を外す、NVRAM の位置を 4 byte ずらす、reset vector を書かない、country の rev を変える、tx の frame を消さない、answer を消さない、wrapper の規則を崩す、credit を無視する、CLM と country の順を逆にする、IO abort を外す、SR を無視する、D11 を reset から出す、EROM の大きさの規則を外す → 14 個とも FAIL。
- `sdio-host-test.sh` → unit・stop・smp・nopackage・refused が PASS（thread の新しい順: package が無ければ mailbox・pin・電源・host のどれにも触らない、probe が断れば全部を返す、公開した chip は host を持ち続ける、kick で待ちが EAGAIN で終わる）。
- rpi4 の vmunix → rc 0、warning 0（`build/arm64/vmunix` SHA-256 `9f67adae17b8bd1c839413739a9426cac21551fa00467257f2ec3f0bac40d461`、`strings | grep -c bcm43455:` = 61）。amd64 の vmunix → rc 0、warning 0。`rpi4.c`（WIFI=0）と `shutdown.c` を同じ flag で compile → warning 0。`git diff --check` → 空。
- host 試験の compile: driver の 6 file は `-Wall -Wextra -Werror`。`bcm43455.c` と試験の file は network の header と host の `<sys/ioctl.h>` の SIOC の再定義が消せないので `-w`（kernel の build は `bcm43455.c` を `-Wall -Wextra -Werror` で compile して warning 0）。

## 由来と license の確認（WS218 の方針、2026-10-13 Q1 の依頼）

- 上流は「値と手順の事実の照合」にだけ使った。取得した file は worktree の `build/tmp/p004-upstream/`（bwfm・whd・brcmfmac）と scratchpad の `up-bwfm/`・`up-whd/` にあり、tree・git に入れていない。
- 出典は `bcm43455-hw.h` の頭と各 group、`bcm43455-chip.c`・`-bus.c`・`-sdpcm.c`・`-bcdc.c`・`-event.c`・`-nvram.c`・`bcm43455.c` の頭と手順の関数の comment に、上流の file・版・関数の名前で書いた（値の行は values.md）。
- 機械比較（P1 の WS218 の道具 `csim.py`・`cruns.py`・`ccom.py`・`cid.py` を worktree の `build/tmp/p004-tools/` に写し、接頭辞の表だけ `bcm43455_`・`bwfm_`・`brcmf_`・`whd_` などに替えて `python3 -I` で実行）。対象: `src/drivers/wifi/bcm43455/` の 18 file と `rpi4-wifi.c`（8239 行、約 2.6 万 token）。相手: bwfm（bwfm.c 1.113・bwfmreg.h 1.26・bwfmvar.h・if_bwfm_sdio.c 1.44・.h 1.3）、WHD（tree bd99da7b の 15 file）、Linux brcmfmac v6.12（sdio.c・sdio.h・chip.c・chip.h・bcdc.c・fwil.h・fwil_types.h・firmware.c・common.c・fweh.h・bcmsdh.c と include の chipcommon.h・soc.h。SPDX は ISC）。対照: zedBSD の `src/drivers/wifi/rtl8822b/*.c`。

| 比較 | bcm43455（p004） | 対照 RTL8822B |
| --- | --- | --- |
| bwfm に対する ident k=8 / k=12（名前つきの写し） | 0.00% / 0.00% | 0.00% / 0.00% |
| bwfm に対する norm k=20 MIND=10（名前を付け替えた写し） | 0.09%（24/25763） | 0.02% |
| WHD に対する ident k=8 / k=12 | 0.00% / 0.00% | 0.00% / 0.00% |
| WHD に対する norm k=20 | 0.09%（22） | 0.00% |
| brcmfmac に対する ident k=8 / k=12 | 0.00% / 0.00% | 0.00% / 0.00% |
| brcmfmac に対する norm k=20 | 0.57%（146） | 0.12% |
| ident で 10 token 以上続く一致 | 0 箇所（3 者とも） | — |
| norm で 24 token 以上続く一致 | bwfm 2・WHD 4・brcmfmac 7 箇所 | — |
| comment の 6 語の一致 | bwfm 0・WHD 2（同じ句「the arm core out of reset」が 2 箇所）・brcmfmac 0 | — |
| comment の 5 語の一致 | bwfm 0・WHD 4（上の 2 と「of an ethernet frame the」「to the end of the」）・brcmfmac 0 | — |

- norm の長い一致の中身（全部を目で見た）: C の定型（`if (error != 0) { log(...); return error; } error = ...`、関数の終わりと次の関数の頭、`for (j = 0; j < n; j++) { if (a[j]...`、`memset(&x, 0, sizeof(x))`）。bwfm の 1 つは backplane の窓の中で bounce buffer を通して書く所（`offset |= 4 byte の flag; if (write) { memcpy(bounce, ...)`、zedBSD `bcm43455-chip.c` の `transfer_memory` ↔ bwfm `if_bwfm_sdio.c:925-929` の `bwfm_sdio_ram_read_write`）で、窓と 4 byte の flag の仕組みから必然の形（相手の関数の分け方・loop の組み方・誤りの扱いは違う）。写しと判断した所は無い。
- comment の一致は普通の英語の句（「the ARM core out of reset」「of an Ethernet frame」）で、文の写しではない。
- 名前（`cid.py`、接頭辞を外して 3 者の名前と照合）: 初めは macro 9・function 6 が同じだった（`WLC_UP`・`WLC_DOWN`・`WLC_SET_PM`・`WLC_GET_VALID_CHANNELS`・`WLC_GET_VAR`・`WLC_SET_VAR`、`SDIO_INT_STATUS`、`SR_ENABLED`、`DATA_OFFSET`、`chip_attach`・`bus_reset`・`bus_stop`・`nvram_convert`・`read_frame`・`reset_core`）。全部を付け直した（`BCM43455_COMMAND_RADIO_UP` など、`BCM43455_SDIO_CORE_INTERRUPTS`、`BCM43455_SAVE_RESTORE_BIT`、`bcm43455_chip_identify`・`bcm43455_bus_forget`・`bcm43455_bus_thread_start/stop`・`bcm43455_nvram_pack`・`receive_frame`・`restart_core`・`hold_core_in_reset`）。firmware の event の名前（`SET_SSID`・`DEAUTH_IND`・`PSK_SUP`・`ESCAN_RESULT` など）も意味の名前に替えた（`JOIN_RESULT`・`PEER_DEAUTHENTICATED`・`SUPPLICANT`・`SCAN_RESULT`）。field の `tx_max`（brcmfmac と同じ）→ `tx_credit_limit`、`data_offset` → `payload_start`・`data_start`、`auth_type` → `authentication`、struct の `sdpcm_header` → `sdpcm_fields`。
- 付け直した後: macro 0・function 0 が同じ。残る同じ名前は struct の tag の一般の語 6 つ（`bus`・`bus_ops`・`chip`・`core`・`device`・`event`、接頭辞を外すと bwfm・brcmfmac の `bwfm_chip`・`brcmf_core` などと同じ語）と field の一般の語 26（`base`・`flags`・`id`・`length`・`status`・`type`・`version`・`reason`・`channel` など。event の message の field は firmware の format の欄）。一般の英語の語なので残した（WS218 と同じ扱い）。
- 書き直せない写しは見つからなかった（ユーザーへの問いは無し）。

## Pi 4 で見る dmesg の行（`dmesg | grep -E 'rpi4-(wifi|sdio)|bcm43455'`）

成功の時（値は例、`<>` が実機で読む所、[values.md](values.md) の「Pi」の欄に写す）:

```
bcm43455: firmware file 43455c0-roml/43455_sdio-…-idsup-idauth-…-clm_min Version: 7.45.241 (1a2f2fa CY) …
rpi4-wifi: EMMC clock <Hz> Hz
rpi4-wifi: WL_ON was level <0|1> (<err>), direction <output|input> (<err>)
rpi4-wifi: GPIO 34 function <前> -> 7, pull 1        （35〜39 は pull 2）
rpi4-sdio: host specification <n>, capabilities <hex> (<MHz>), firmware clock <Hz> Hz
rpi4-wifi: SDIO card: 2 functions, OCR <hex>, memory 0, RCA <hex>
rpi4-wifi: CCCR <n>, SDIO <n>, capability <hex>, CIS at <hex>: manufacturer 02d0 (1), card <hex>, F0 block <n>, speed <hex>
rpi4-wifi: bus 4 lines at <25000000> Hz
rpi4-wifi: F1 enabled; block sizes F1 64, F2 512
bcm43455: chip 4345 revision <6> bus type 1
bcm43455: capabilities <hex>, extended <hex>
bcm43455: SDIO core <hex>, ARM CR4 <hex> (wrapper <hex>), D11 wrapper <hex>, RAM 198000 + <hex>
bcm43455: PMU at <hex> revision <n>, save-restore <0|1>
bcm43455: firmware 548728 bytes at 198000, NVRAM 1748 bytes at <hex>
bcm43455: firmware speaks SDPCM protocol 4 (mailbox <hex>)
bcm43455: CLM loaded (2676 bytes in 2 messages)
bcm43455: country XX revision <n> (code XX)
bcm43455: firmware wl0: <date> version 7.45.241 …
bcm43455: capabilities ap sta wme … idsup …
bcm43455: <n> valid channels, <n> scanned
bcm43455: chip address <xx:xx:xx:xx:xx:xx>, <n> channels
bcm43455: published as wlan0
```

`ifconfig wlan0 up`（または Settings の Wi-Fi を入れる）で同じ bring-up の行がもう一度出て（`rpi4-wifi: SDIO card` の行は最初の 1 回だけ）、`bcm43455: event <n> status <n> reason <n>` が最大 16 行。

失敗の時に出る行: `rpi4-wifi: package bcm43455-firmware not installed; no onboard Wi-Fi`、`bcm43455: <path> not opened (<errno>)`・`not read`・`is not the 43455 C0 firmware`、`rpi4-wifi: no root file system after 300 s`、`bcm43455: chip <id> revision <n> bus type <n>` の後の ENODEV、`bcm43455: backplane clock not available`・`fast clock not available`・`frame function not ready`、`bcm43455: core <id> stays in reset`、`bcm43455: RAM at <hex> does not read back what was written`、`bcm43455: firmware halted`、`bcm43455: firmware refused command <n> (status <n>)`、`bcm43455: country XX revision 0 refused … trying the firmware's revision`（p005 の後の行）、`bcm43455: country XX not set`、`bcm43455: CLM refused`、`bcm43455: receive glomming not turned off`、`bcm43455: glommed frame dropped; …`、`bcm43455: firmware has no supplicant (idsup: …)`、`bcm43455: chip reports the NVRAM's placeholder address`、`bcm43455: bus thread stopped on error <n>`、`rpi4-wifi: onboard Wi-Fi not published (<errno>)`。

実機で読む未確認の値: PMU の場所と rev・save-restore の有無（R8）、HT・F2 が来る時間、firmware の版と SDPCM の版、country の rev（−1 か 0 か）、`bus:rxglom` が受け付けられるか、event mask の 18 byte が受け付けられるか、有効な channel の数、chip の MAC、MAC が NVRAM の仮の値でないこと、card interrupt の数（R9 の poll との比較は p005）。

## 未実施・残件

- Pi 4 の実機（上の dmesg）、QEMU raspi4b の `boot-test.sh`（T1）。どちらも未実施。
- p005: scan・connect・data・link loss・restart（FullMAC の op、p008 の API）。PMK の slot の消去の試験は p005 で connect と一緒に（slot・frame の消去の仕組みと試験は p004 で済み）。
- open の時に file を読み直すか（今は publish の時の 1 度だけ、memory に約 550 KB）を p007 で判断。
- 試験の file `plan/ws206/tests/bcm43455-host-test.*` は WS206 の未完了の Phase（p005 が同じ model に scan・connect を足す）から使う。WS の終わりに Master の Tools に登録するかを Q1 が判断。

## 削除の依頼（Q1 へ）

- `/home/awe/zedBSD-worktrees/p4/build/tmp/p004-upstream/`（bwfm・WHD・brcmfmac の取得物、比較の後は不要）、`/home/awe/zedBSD-worktrees/p4/build/tmp/p004-tools/`、`/home/awe/zedBSD-worktrees/p4/build/tmp/p004-cid/`
- `/home/awe/zedBSD-worktrees/p4/build/ws206-p004/`（host 試験の出力）、`/home/awe/zedBSD-worktrees/p4/build/tmp-p004-rpi4.log`・`tmp-p004-amd64.log`
- scratchpad の `up-bwfm/`・`up-whd/`・`mut/`・`mutate.py`・`mutate2.py`・`split.py`
