<!-- awesome-plan project=zedbsd record=ws206-p001 -->

# ws206-p001: Raspberry Pi 4 の WiFi の調査と設計

Parent: [WS206](../ws.md)
Status: in-progress（q932、P1。2026-10-13 設計の第 2 版、design-reviewer 待ち。E2・E4・E6 はユーザーの回答待ち）
Phase disposition: normal
Queue: q932（[P1 の lane](../../agents/P1/queue.md)）

## 由来

2026-10-11 ユーザー:「RPi4のWiFiを実装。」
2026-10-12 ユーザー: Pi 4 の順は 描画の最適化 → SMP → 計測 → WiFi（この WS）と Bluetooth（[WS215](../../ws215/ws.md)）で Pi 4 は完了。SMP（[WS208](../../ws208/ws.md)、4 core）は 2026-10-13 に main に入った。

## 決まったこと

| ID | 決め | 出典 |
| --- | --- | --- |
| E1 | WPA2 の 4-way handshake は firmware の supplicant（W7）。PMK は core が passphrase から計算して driver へ渡す | 2026-10-11 ユーザー「推し通りです。」、2026-10-13 Q1 |
| E3 | WLAN の core に FullMAC の形を足す（W6）。kernel の内部の API だけ、HAL・UAPI は変えない | 同上 |

## 今の tree の事実（2026-10-11 と 2026-10-13 の読み）

| 所 | 事実 |
| --- | --- |
| Pi 4 の driver | `src/drivers/platform/rpi4/`: console、ethernet（GENET）、firmware（VideoCore の mailbox の property、`drv_rpi4_firmware_property`）、PCIe（VL805 の xHCI）、audio、**sdhci（eMMC2、SD card、PIO、1079 行）**。sdhci は 1 つの global の `unit`（base・RCA・32 bit 書きの影の `transfer_mode`/`block_size`・mutex）を持ち、割り込みを使わず polling。WiFi の SDIO の controller の driver は無い |
| 0xfe300000 の今の使い道 | `src/kern/platform/rpi4.c`: eMMC2（0xfe340000）の初期化が失敗し handoff が 0xfe340000 なら、**QEMU raspi4b の SD card として 0xfe300000 を使う**（「QEMU legacy-controller fallback」）。実機ではこの controller が WiFi の SDIO |
| GPIO | 汎用の GPIO の driver は無い。`rpi4-audio.c` が GPFSEL4（pin 40〜49）を自分で read-modify-write する。WiFi の pin 34〜39 と Bluetooth の pin 30〜33 は**同じ GPFSEL3** にある |
| mailbox | `drv_rpi4_firmware_property` は request buffer を atomic の claim で直列にし（取れなければ sleep して再試行）、thread の文脈で SMP でも安全。IRQ の文脈からは呼べない |
| 割り込み | `kern_irq_register` は 1 本の IRQ に 1 つの handler（2 つ目は EBUSY）。BCM2711 の DT（Raspberry Pi の `bcm2711.dtsi`）では eMMC2 と旧 EMMC（0x7e300000）が**同じ GIC SPI 126**。今の SD card は polling なので登録していない。WS208 の D1 で SPI は全部 CPU 0 へ |
| WLAN の core | `src/kern/net/wifi/`（約 11,400 行）、`include/kern/net/wlan.h`、UAPI `include/uapi/wlan.h`。**SoftMAC の形**: core が scan の段・auth・assoc の management frame・WPA2 の 4-way（`wlan-wpa2.c`）・802.11 の data の MPDU（`wlan-l2.c`）を作り、driver は radio の op（`scan_channel_start`・`connect_start`・`management_transmit`・`frame_transmit`・`key_install` など）。Intel AX211 と RTL8822B（USB、arm64 でも build）が使う |
| core の並行の約束 | radio の op は station の lock を持たずに thread の文脈で呼ばれ（多くは network worker から、control gate を持ったまま）、**network worker の進みを待ってはならない**。`wlan_station_report_frame`・`report_tx_complete`・`report_link_loss` は control gate（yield で待つ）を取る。scan の channel の ready・complete・error の report は IRQ でも呼べる latch。`report_scan_frame`・`report_scan_bss` は poll・thread の文脈 |
| scan の firmware の offload | `WLAN_SCAN_CHANNEL_OFFLOADED_DWELL`: driver は 1 channel の scan を firmware に任せ、終わりを `report_scan_channel_complete` で返す（AX211 が使う）。結果は `report_scan_frame`（beacon・probe response の形）か `report_scan_bss` |
| net device | `net_device_create` は `hwaddr_len != 0` を要する（作る時に MAC が要る）。driver の rx は `net_device_schedule_poll` → `poll_receive`（network worker）が通常の形 |
| firmware の file の読み | AX211 は interface の open の時に `file_openat`（`/lib/firmware/...`）で読み、大きさと SHA-256 を確かめる（kernel の共通の SHA-256 は無く、AX211 が自前で持つ）。root が mount された後なら driver の thread から読める。`mount_root_get_ref()` で root の有無が分かる |
| firmware の包み | `userland/firmware/<名>/`: 既定は無効、選んだ時だけ immutable の上流（commit 固定）から取得し大きさと SHA-256 を確かめ、`/lib/firmware` へ、license・WHENCE・manifest を `/usr/share/...` へ。AX211 は linux-firmware の commit `dc85cced…`（tag 20260410） |
| user の側 | Settings の Wi-Fi の頁 → libkeiland の `kl_system_*` → compositor → `libkeiland-backend-zedbsd/network-zedbsd.c` → networkd → `/sbin/wifi`（`userland/base/wifi/`）→ UAPI の `SIOCSWLANSCAN`・`SIOCGWLANBSS`・`SIOCSWLANCONNECT`（passphrase を渡す）・`SIOCGWLANSTATUS`。networkd は `wlan` で始まる名の interface を無線として扱い、WPA2・CCMP・PSK を要し、WPA1・PMF 必須・未対応の suite を断る |
| BCM43455 | Broadcom/Cypress（今は Infineon）の **FullMAC**。MLME・scan・（firmware の機能があれば）supplicant は firmware の中、data は 802.3 の Ethernet frame、制御は BCDC の ioctl・iovar、通知は firmware の event。SDIO の 3 function（F0 CCCR、F1 backplane、F2 data）。host が firmware・NVRAM を chip の RAM に書き、CLM（規制の表）を iovar で読み込ませる |

## 設計

### W1. 全体の形

```
Settings / networkd / /sbin/wifi          （変えない）
        │ UAPI ioctl（変えない）
WLAN の core  src/kern/net/wifi/wlan.c      ← FullMAC の mode を足す（W6）
        │ struct wlan_radio_ops（FullMAC の op）・report の関数
bcm43455 driver  src/drivers/wifi/bcm43455/ ← 新規（W4・W5・W8）
        │ struct bcm43455_bus_ops（SDIO の読み書き、host の試験では偽物）
Pi 4 の SDIO  src/drivers/platform/rpi4/rpi4-sdio.c        ← 新規（W2）
        │
SDHCI の register の層  rpi4-sdhci-host.c   ← rpi4-sdhci.c から分ける（W2）
        │                      └ SD card（rpi4-sdhci.c、eMMC2）も使う
GPIO の helper  rpi4-gpio.c（W3）・mailbox  rpi4-firmware.c（既存）
```

HAL・hal.h・UAPI は変えない。kernel の内部の API の追加は `include/kern/net/wlan.h` の FullMAC の op と report の関数だけ（W6）。

### W2. SDIO の host（旧 EMMC、0xfe300000）

Pi 4 の 3 つの SD の controller のうち、**eMMC2**（0xfe340000、`brcm,bcm2711-emmc2`）は SD card、**SDHOST**（0xfe202000、`brcm,bcm2835-sdhost`、SDHCI でない独自の controller）は Pi 4 では使わない、**旧 EMMC**（0xfe300000、Arasan の SDHCI、DT `mmcnr@7e300000`・`brcm,bcm2835-mmc`/`brcm,bcm2835-sdhci`）が GPIO 34〜39 の ALT3 で WiFi の SDIO に繋がる。

- **register の層を分ける**: `rpi4-sdhci.c` の register の手続き（32 bit だけの書き込みと 16 bit の影（command と transfer mode、block size と block count を 1 語で書く）、reset、clock、command、PIO の block の読み書き、割り込みの status の待ち）を、instance ごとの `struct rpi4_sdhci_host` を引数に取る `rpi4-sdhci-host.c` に移す。SD card の driver（disk、boot の device）はこの層の上に書き直し、**振る舞いは変えない**（同じ command の順・待ち・clock）。instance ごとの違いは flag で持つ: 旧 EMMC の書き込みの間の待ち（後述の危険 R2）、base clock の出所。
- **SDIO の層** `rpi4-sdio.c`: CMD5（IO_SEND_OP_COND、3.3 V の窓、ready の bit と function の数）、CMD3（RCA）、CMD7（select）、CMD52（IO_RW_DIRECT、1 byte）、CMD53（IO_RW_EXTENDED、byte mode と block mode、address の増加）、CCCR（I/O enable・ready、割り込みの enable、bus の幅 4 bit、high speed）と FBR（function の block size: F1 64 byte、F2 512 byte、p004 で firmware に合わせて確定）、CIS の manufacturer（0x02d0）と device id の読み。値は SD Association の SDIO Simplified Specification と SD Host Controller Simplified Specification（どちらも公開）から。
- **clock**: 初期化は 400 kHz、enumeration の後 25 MHz（default speed）。base clock は SDHCI の capabilities を信じず、mailbox の `GET_CLOCK_RATE`（clock id 1、EMMC）で読む（DT の `<&clocks 0x1c>` は cprman の EMMC の clock）。50 MHz の high speed（CCCR の EHS と host の HS bit）は p006 の後の改良。DT の `brcm,overclock-50 = 0`。
- **転送は PIO**。旧 EMMC の DMA（DT の DMA channel 11、BCM2711 の DMA engine）は速さが足りない時の後の改良（Future Work の候補）。4 bit・25 MHz の上限は約 12.5 MB/s で、DHCP・ping・Web の閲覧には足りる。
- **割り込み**: SDIO の card interrupt（SDHCI の normal interrupt の bit 8）だけを `SIGNAL_ENABLE` で通す。command・data の完了は今の SD card の driver と同じく status の polling（短い、期限付き）。handler は SPI 126 に 1 つだけ登録し（`rpi4-sdhci-host.c` が持つ）、登録した instance の status を順に見て自分の card interrupt なら `INT_ENABLE` の bit 8 を下ろして（level の割り込みを止める）bus の thread を起こす。eMMC2 は polling のままなので今は WiFi だけが登録する。kern の IRQ の共有は要らない。
- **QEMU の fallback との衝突を防ぐ**: `rpi4-sdhci-host.c` が controller ごとの claim を持ち、SD card が 0xfe300000 を使った時（QEMU）は WiFi の probe は claim に失敗して何もしない（reset も書き込みもしない）。DT の `mmcnr` の node の子に `brcm,bcm4329-fmac` が無い時も probe しない。QEMU raspi4b に BCM43455 は無いので、QEMU では WiFi の試験はせず起動の回帰だけ。

### W3. pin・電源・clock

- **GPIO の helper** `rpi4-gpio.c`: `drv_rpi4_gpio_set_function(pin, function)` と `drv_rpi4_gpio_set_pull(pin, pull)`。GPFSEL（3 bit/pin）と BCM2711 の pull の register（`GPIO_PUP_PDN_CNTRL_REG0`〜`3`、offset 0xe4〜0xf0、2 bit/pin: 0 なし、1 up、2 down）を **1 つの spinlock** の下で read-modify-write する。値は BCM2711 ARM Peripherals（Raspberry Pi の公開の datasheet）から。Bluetooth（WS215）と**2 つの register を共有する**: GPFSEL3（pin 30〜39: BT 30〜33、WiFi 34〜39）と pull の REG2（pin 32〜47: BT 32・33、WiFi 34〜39）。SMP では両方の WS がこの helper を通らなければならない（2026-10-13 Q1 経由の WS215 の設計からの知らせ）。
- **mailbox の GPIO の helper**（`rpi4-firmware.c` に足す）: `drv_rpi4_firmware_gpio_get(line, &state)`・`drv_rpi4_firmware_gpio_set(line, state)`（拡張 GPIO 128 + n、`GET_GPIO_STATE`・`SET_GPIO_STATE`）。1 本ずつの設定で read-modify-write ではないので、既存の request buffer の atomic の claim のほかに lock は要らない。WiFi は WL_ON（129）、WS215 は BT_ON（128）に使う。
- この 2 つの helper を ws206-p002 と ws215-p003 のどちらで作るかは Q1 が決める（WiFi が先の見込み）。後の側は作られた helper を使い、自分で GPFSEL・pull・mailbox の GPIO を直に書かない。`rpi4-audio.c` の GPFSEL4 の書き込みも GPIO の helper へ移す（p002、小さい変更）。HAL の `led.c`（GPIO 42、起動の初めだけ、SMP の前）は触らない。
- **pin**: GPIO 34〜39 を ALT3。pull は CLK（34）なし、CMD・DAT0〜3（35〜39）は up（DT の `sdio_pins`）。
- **電源**: WL_REG_ON は VideoCore の firmware の GPIO の拡張の 2 本目（`gpio-line-names` の "BT_ON", "WL_ON"、mailbox の GPIO 番号 128+1=129）。Raspberry Pi の DT は `wifi-pwrseq` を消しており、firmware が起動時に WL_ON を上げたままと推定（未確認、p002 で `GET_GPIO_STATE`（0x00030041）で読む）。bring-up のたびに `SET_GPIO_STATE`（0x00038041）で 0 → 10 ms → 1 の reset の pulse を出し、その後 CMD5 の応答を 1 s まで待つ（待ちの値は Infineon の CYW43455 の datasheet の WL_REG_ON の timing で p002 に確定）。**BT_ON（128）には触らない**。mailbox の tag の値は Raspberry Pi の firmware の公開の wiki（Mailbox property interface）から。

### W4. Broadcom の bring-up（`src/drivers/wifi/bcm43455/`）

順（値の出典は W10）:

1. SDIO の enumeration（W2）、F1 を有効に、F1 の block size。
2. backplane の clock: F1 の chip clock CSR（0x1000e）に ALP の要求、ALP の available を待つ。
3. chipcommon（backplane 0x18000000）の chip id を読む: 0x4345、rev 6 以上（43455 = C0）でなければ止まる。backplane の読み書きは F1 の 32 KB の窓（F1 の SBADDR の 3 つの register、0x1000a〜c に address の上位）と 4 byte の access の flag（0x8000）で行う。
4. EROM（enumeration ROM）を読み、ARM CR4（core id 0x83e）・D11・SDIO の device の core・chipcommon と各 wrapper（reset の control）の address を得る。
5. CR4 を止める（CPU halt の ioctrl）。RAM の大きさを CR4 の bank の情報から読む。RAM の base は 43455 で 0x198000。
6. firmware を RAM の base から CMD53 の block mode で書く（32 KB の窓ごと）。先頭と末尾の 1 block を読み戻して確かめる。
7. NVRAM の text を binary に直す（comment・空行・前後の空白を除き `key=value\0` を並べ、`\0` で終え、4 byte に詰め、末尾に長さの語 `(~words << 16) | words`）。RAM の末尾（base + 大きさ − 4 − NVRAM の長さ）に書く。この変換は host で試験できる純粋な関数にする。
8. firmware の先頭の語（reset vector）を address 0 に書き、CR4 の halt を外して走らせる。
9. HT の clock を要求して待ち、F2 を有効にして ready を待つ（上限 3 s）。SDIO の device の core の割り込みの mask、CCCR の割り込みの enable（master・F1・F2）。to-host の mailbox の data の SDPCM の protocol の版（4）を確かめる。
10. CLM を `clmload` の iovar で 1400 byte ずつ（版・BEGIN/END の flag・type CLM）送り、`clmload_status` を確かめる。
11. `cur_etheraddr`（MAC、chip の OTP から。NVRAM の `macaddr` は全ての Pi で同じ仮の値）、`ver`（firmware の版の文字列、log に出す）、`event_msgs`（使う event の mask）、有効な channel の一覧（W6 の scan の profile へ）。`WLC_SET_PM` 0（省電力は無効、後の改良）。
12. `WLC_UP`。

firmware の trap（to-host の mailbox の FWHALT）・bus の error・F2 の応答なしは、link loss の報告と `wlan_station_restart_request` で W8 の restart（電源の pulse からやり直し）へ。

### W5. SDPCM・BCDC・iovar・event

- **SDPCM**（F2 の frame）: 4 byte の frame tag（長さと長さの補数）＋ 8 byte の software header（seq、channel と flag、次の長さ、data の offset、flow control、**max seq**（firmware が許す送信の窓）、予約）。channel は 0 control、1 event、2 data、3 glom。送信は `(max_seq − seq) mod 256` が 0 でない間だけ（credit）、frame は F2 の block size に詰める。受信の glom（superframe）は v1 では `bus:rxglom` を 0 にして使わない（firmware が断れば p004 で channel 3 を解く）。
- **BCDC**: control は 16 byte の header（cmd、len、flags（error・set の bit、interface、request id）、status）＋ 本体。応答は id で照合し、1 度に 1 つだけ出す。data は 4 byte の header（版 2、priority、interface、data の offset（4 byte 単位））＋ Ethernet frame。
- **iovar**: `WLC_GET_VAR`（262）・`WLC_SET_VAR`（263）に `名前\0値`。
- **event**: data か event の channel に、Ethernet の header（ethertype 0x886c）＋ Broadcom の OUI（00:10:18）・subtype 0x8001 ＋ big-endian の `wl_event_msg`（版、flag、event の型、status、reason、auth の型、data の長さ、address、interface の名と番号）＋ data。使う event: SET_SSID（0）、AUTH（3）、DEAUTH（5）・DEAUTH_IND（6）、ASSOC（7）、DISASSOC_IND（12）、LINK（16）、PSK_SUP（46）、ESCAN_RESULT（69）。
- **escan**: `escan` の iovar（版、action 1 開始・3 中止、sync id、scan の params: SSID なし、BSSID は broadcast、active か passive、probe の数、dwell の時間、channel の数と chanspec の一覧）。結果は ESCAN_RESULT の status partial（8）に `bss_info`（版 109、BSSID、beacon の間隔、capability、SSID、chanspec、RSSI、IE の offset と長さ）、status 0 で終わり。
- **bss_info → BSS の記録**: driver は bss_info から 802.11 の probe response の形の frame（header・timestamp 0・beacon の間隔・capability・IE）を組み、既存の `wlan_station_report_scan_frame` に渡す。security の bit（RSN の解き）は core の既存の parser が付ける。core に新しい scan の API は要らない。chanspec から channel と帯域を解く。
- これらの encode・decode は hardware に触らない純粋な関数（`bcm43455-sdpcm.c`・`bcm43455-bcdc.c`・`bcm43455-event.c`）にし、host で試験する。

### W6. WLAN の core の FullMAC の形（kernel の内部の API の追加、E3）

`include/kern/net/wlan.h` に足す（UAPI・hal.h は不変）:

- `struct wlan_radio_ops` に 2 つの op:
  - `int (*fullmac_connect)(void *context, const struct wlan_fullmac_connect_request *request)` — request は generation、期限、選んだ BSS（`struct wlan_bss_record`）、PMK（32 byte）。driver は返る前に必要な物を写し、自分の写しの PMK を使い終わったら消す。core は戻った後に request の PMK を消す。
  - `int (*ethernet_transmit)(void *context, uint64_t generation, const uint8_t *frame, size_t length)` — 802.3 の frame を写して送りの queue に入れ、すぐ返る（満杯は ENOBUFS）。
- **mode は attach の時に決まる**: `fullmac_connect` が非 NULL なら FullMAC。FullMAC の ops は `scan_channel_start`・`scan_stop`・`fullmac_connect`・`disconnect`・`ethernet_transmit`・`quiesce`・`stop_retry` を要し、SoftMAC の `connect_start`・`management_transmit`・`association_set/clear`・`frame_transmit`・`key_*`・`keys_activate` は NULL でなければならない（混ぜた ops は EINVAL）。既存の SoftMAC の driver は何も変わらない。
- report の関数を 2 つ:
  - `int wlan_station_report_fullmac_event(struct wlan_station *station, uint64_t generation, enum wlan_fullmac_event event, int error)` — event は ASSOCIATED（firmware が AP に繋いだ）、AUTHORIZED（4-way と group key が済んだ）、FAILED（error 付き）。poll・thread の文脈、control gate を取る（既存の `report_link_loss` と同じ）。
  - `int wlan_station_report_ethernet(struct wlan_station *station, uint64_t generation, const uint8_t *frame, size_t length)` — generation が今の接続で controlled port が開いている時だけ `net_device_receive` へ。EAPOL（0x888e）は捨てる（firmware の supplicant が使う）。poll・thread の文脈。
- **core の FullMAC の流れ**:
  - scan: 今と同じ（channel ごとの `scan_channel_start`、`OFFLOADED_DWELL`、結果は `report_scan_frame`）。
  - connect: `ioctl_connect` の検査と BSS の選びは同じ（`connect_start == NULL` の EOPNOTSUPP の検査を FullMAC では `fullmac_connect` で行う）。`station_connection_start` は FullMAC なら WPA2 の engine を使わず、`wlan_pbkdf2_hmac_sha1`（既存、`wlan-crypto.c`、4096 回、SSID を salt、32 byte）で PMK を作り、passphrase を消し、`fullmac_connect` を呼ぶ。state は ASSOCIATING。
  - ASSOCIATED → FOUR_WAY（`associated` = 1）、AUTHORIZED → CONNECTED（`authenticated`・`associated`・`key_installed`・`controlled_port` = 1、carrier を上げる、期限を消す）、FAILED・期限切れ → FAILED と既存の retire（`disconnect`）。
  - 切断・AP の消失: driver が DEAUTH・DISASSOC・LINK down・firmware の trap を既存の `wlan_station_report_link_loss` で報告。
  - **beacon の監視**（core の `beacon_watch`）は FullMAC では使わない（firmware が見て event を出す）。
  - 送信: `wlan_station_transmit` は FullMAC なら MPDU を作らず、`controlled_port` と接続の generation を確かめて `ethernet_transmit` へ。
  - UAPI の status の state（IDLE・SCANNING・ASSOCIATING・FOUR_WAY・CONNECTED・FAILED…）は SoftMAC と同じ値を返すので networkd・`/sbin/wifi`・Settings は変えない。
- 試験は `WLAN_TESTING` の偽の FullMAC の ops で host（W11）。

### W7. firmware の supplicant（E1）

- connect の時に driver が順に: `WLC_SET_INFRA` 1、`WLC_SET_AUTH` 0（open）、`WLC_SET_WSEC` 4（AES）、`WLC_SET_WPA_AUTH` 0x80（WPA2-PSK）、`bsscfg:sup_wpa` 1、`WLC_SET_WSEC_PMK`（PMK を 64 文字の 16 進にし flag は passphrase の形。firmware は 64 文字を PSK そのものと扱う）、`WLC_SET_SSID`（SSID と、assoc の params に選んだ BSS の BSSID と chanspec）。PMK を入れた buffer は送った直後に消す。log に PMK・passphrase を出さない。
- event: SET_SSID の status 0 と LINK up → ASSOCIATED、PSK_SUP の keyed（完了）→ AUTHORIZED、SET_SSID の失敗・PSK_SUP の失敗・期限 → FAILED。group key の更新は firmware が行う。
- 範囲の外: WPA3（SAE）・PMF（core と networkd は今も PMF 必須と SAE だけの AP を断る）・WPA1・enterprise。省電力（PM）・roaming（`roam_off` 1 で無効）も v1 の外。
- 選ぶ firmware は supplicant の機能（feature の文字列の `idsup`）を持つ物でなければならない（W9）。driver は読んだ firmware の版の文字列に `idsup` が無ければ connect を EOPNOTSUPP で断り、log に出す。

### W8. wlan0 の公開・MAC・lifecycle

- **公開の時期**: MAC は firmware を走らせた後に OTP から読むので、boot の probe（`kern_platform_init` の後、`kern_platform_refresh_devices` から）は SDIO の enumeration と chip id の確かめまで行い、driver の thread を 1 つ作る。thread は `mount_root_get_ref()` で root の mount を待ち（1 s ごと、上限 300 s）、firmware の 3 つの file を読み、W4 の bring-up を 1 度行って MAC と有効な channel を読み、`WLC_DOWN` して電源を落とし（WL_ON を 0）、**MAC の分かった `wlan0`（空いている `wlanN`）を `net_device_create` と `wlan_station_attach` で公開する**。USB の WiFi の dongle が後から現れるのと同じ形で、networkd は後から来た interface を扱う（p006 で確かめる）。firmware の package が無ければ 1 度 log に出して終わる（wlan0 は無い）。net device の API に MAC の後からの変更を足す案は採らない。
- **open**（Settings の Wi-Fi を入れる、`wifi up`）: 電源の pulse から W4 の bring-up を全部行い（file はその都度読み直す。約 600 KB）、bus の thread を始め、`wlan_station_open`。**close**: `WLC_DOWN`、bus の thread を止め、電源を落とす。restart（firmware の trap）も同じ手順。
- file の確かめ: 大きさが package の値と同じ、firmware の版の文字列（`43455c0-roml/` で始まる）を含み、NVRAM は text、CLM は先頭が `BLOB`。digest の照合は build の時の package の Makefile が行う（kernel に SHA-256 の共通の実装が無く、AX211 の自前の物を写さない）。
- core の close・quiesce・stop_retry・detach の約束は AX211・RTL8822B と同じ形で守る（W12）。

### W9. firmware の BLOB の入手と license（`userland/firmware/bcm43455/`、package `bcm43455-firmware`）

2026-10-13 に上流を読んだ結果（file の中身は scratchpad に取得して確かめ、tree には入れていない）:

| 候補 | file | 版・中身 | license |
| --- | --- | --- | --- |
| linux-firmware（AX211 と同じ commit `dc85ccedc9c973682fbcf4d628ca61174bcc3120`） | `cypress/cyfmac43455-sdio.bin`（643651 B、SHA-256 `d408faa9…3edd`）、`.clm_blob`（4733 B、`15f50a27…d460`、"Cypress.CYW43455"） | 7.45.234（2021-04-15）、feature に `idsup`・`sae` | LICENCE.cypress（Cypress の binary の再配布の EULA） |
| 同 | `brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt`（1883 B、`edb6f4e4…2748`） | Pi 4 の NVRAM | **GPL-2.0+**（WHENCE「Licence: GPLv2」、file の先頭 `SPDX-License-Identifier: GPL-2.0+`、(C) Raspberry Pi (Trading) Ltd.） |
| Raspberry Pi の `RPi-Distro/firmware-nonfree`（branch trixie、commit `3bab0f823f5b53150b76aab77093adef6655b920`、2026-07-10） | `debian/added-firmware/cypress/cyfmac43455-sdio-minimal.bin`（548728 B、`3075cb0b…6e60`） | 7.45.241（2021-11-01）、feature に **`idsup`・`idauth`**、SAE なし | debian/copyright: `debian/added-firmware/*/*43455*` は Cypress、binary-redist-Cypress |
| 同 | `cypress/cyfmac43455-sdio-standard.bin`（609309 B、`d608f866…36c3`） | 7.45.265（2023-08-29）、`extsae`、**`idsup` なし**（host の supplicant が要る） | 同上 |
| 同 | `cypress/cyfmac43455-sdio.clm_blob`（2676 B、`9823842c…a36e`、"RPi.4"） | Raspberry Pi の CLM | 同上 |
| 同 | `brcm/brcmfmac43455-sdio.txt`（2074 B、`ca709be8…9e3d`、Pi 4 の名前の file はここへの symlink） | Pi 4 の NVRAM。linux-firmware の GPL の物と値は同じ（違いは `NVRAMRev` の行と `btc_mode=1` の重複だけ。Cypress の Bluetooth の共存の値を含む） | 同上（Cypress） |

**推し（E4）**: 3 つとも Raspberry Pi の firmware-nonfree の同じ commit から取る: `cyfmac43455-sdio-minimal.bin`（firmware の supplicant の `idsup` を持つ。standard は持たないので E1 と合わない）、`cyfmac43455-sdio.clm_blob`、`brcmfmac43455-sdio.txt`。license の file は同じ commit の `debian/copyright`（その file の license の宣言の正本）をそのまま入れる。**linux-firmware の Pi 4 の NVRAM は GPL-2.0+ なので使わない**（中身は同じ値で、Cypress の license の物がある）。予備: minimal が実機で不具合なら linux-firmware の 7.45.234（`idsup` あり、Cypress の license）と Cypress の CLM。

- **NVRAM の license の確かめ直し**（2026-10-13、Q1 経由の WS215 の知らせ「linux-firmware の WHENCE は Pi の NVRAM を GPLv2 としている」への答え）: その通りで、linux-firmware の `brcm/brcmfmac43455-sdio.raspberrypi,3-model-b-plus.txt` と `,4-model-b.txt`（と link）は GPLv2（WHENCE）・file の中は `GPL-2.0+`。firmware-nonfree の `debian/copyright` にも同じ GPL-2 の stanza（`Files: … brcm/brcmfmac43455-sdio.*.txt …`、(C) 2018 Linaro・Raspberry Pi）があるが、これは repo の root の `brcm/`（linux-firmware 由来）の file を指す。推しの file は `debian/added-firmware/brcm/brcmfmac43455-sdio.txt` で、DEP-5 の path の照合では `debian/added-firmware/*/*43455*`（Cypress、binary-redist-Cypress）の stanza に当たり、GPL の stanza の `brcm/…` には当たらない。中身の先頭は「Cloned from bcm94345wlpagb_p2xx.txt」（Cypress の reference の board の NVRAM）と `NVRAMRev`。**残る危険**: 2 つの file は値が同じ（行の順と 2 行だけ違う）で、GPL の側は 2018 年の Raspberry Pi の著作権を名乗る。値の起源は Cypress の reference と読めるが、法的な判断はユーザー（E4）。代わりの案: (a) GPL-2.0+ の file を GPL の text と共に optional の package に入れる（data の file で、kernel にも zedBSD の code にも link しない）、(b) NVRAM をユーザーが自分で置く（package に入れない）。
- install 先: `/lib/firmware/cypress/cyfmac43455-sdio-minimal.bin`、`/lib/firmware/cypress/cyfmac43455-sdio.clm_blob`、`/lib/firmware/brcm/brcmfmac43455-sdio.txt`（上流の名前のまま）、`/usr/share/licenses/bcm43455-firmware/copyright`、manifest。
- 取得は `https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/<commit>/debian/added-firmware/...`（commit で固定、内容は不変）。大きさと SHA-256 を Makefile に固定し、AX211 の Makefile と同じ形（本番の値は override、fixture の goal だけ置き換え可）。既定は無効、Pi 4 の image の config で選ぶ。
- Cypress の EULA: object code の形で「Cypress の IC と共に使う目的に限り」再配布してよい、改変・逆 engineering の禁止。driver はこれらの file を読んで chip に書くだけで、中身を解かない（版の文字列の検索と NVRAM の text の変換だけ。NVRAM の変換は chip が要る形への書き換えで、file は改変しない）。
- Bluetooth の patchram（`.hcd`）は WS215 の別の package（`intelbt` と `intelax211` が別なのと同じ）。

### W10. 値の出典と license の扱い

- zedBSD の code は Zlib。**Linux の brcmfmac は理解のために読むだけで写さない**（Q1 の指示。guardrail の GPL の参照の扱いは WS141・WS037・WS214 だけで、この WS には無い）。
- 値（register の address・bit、構造体の配置、ioctl・iovar・event の番号、手順の順）の出典は、公開の文書と寛容な license の source に限る:
  - SD Association: SDIO Simplified Specification、SD Host Controller Simplified Specification（CCCR・FBR・CMD5/52/53・SDHCI の register）。
  - Raspberry Pi: BCM2711 ARM Peripherals（GPIO・pull）、firmware の mailbox の property の wiki（GPIO の状態・clock の rate の tag）、device tree の source と binding（pin・割り込み・WL_ON の線・clock の番号）。
  - Infineon（Cypress）: CYW43455 の datasheet（電源の順と時間）、**wifi-host-driver（WHD、Apache-2.0）**（firmware の supplicant の `bsscfg:sup_wpa`・`WLC_SET_WSEC_PMK` の passphrase の形、BCDC・iovar・event の値）。
  - **OpenBSD の bwfm（ISC、`sys/dev/ic/bwfm.c`・`bwfmreg.h`・`sys/dev/sdmmc/if_bwfm_sdio.c`・`.h`）**（backplane の窓・chip clock の CSR・core の id・43455 の RAM の base 0x198000・SDPCM・BCDC・escan・CLM の読み込み）。OpenBSD の bwfm は host の supplicant（`sup_wpa` 0）なので supplicant の部分は WHD を見る。
  - FreeBSD の `bcm2835_sdhci.c`（BSD）: 旧 EMMC の癖（R2）。
- ISC・Apache の code も**字面を写さない**（notice の義務を生まないため）。値だけを使い、名前・配置・comment は自分で書く。p004 の最初に値ごとの出典の表 `plan/ws206/phase004/values.md` を作り、WS の最後（p007）に字面と構造の類似を確かめる。

### W11. 試験

- **host**（QEMU を使わない、実装の担当が流す）:
  - SDPCM・BCDC・iovar の encode と decode、credit の窓の算術（seq の wrap）、event の解き（big-endian、長さの検査、壊れた入力）、escan の bss_info → probe response の frame → 既存の `wlan_frame_parse_bss` の結果（WPA2-PSK・CCMP の bit）、chanspec の解き、NVRAM の text → binary と長さの語、CLM の分割。
  - bring-up の手順: `struct bcm43455_bus_ops` の偽物（backplane のメモリと台本の F2 の frame）で chip id の検査・RAM への書き込みの address・NVRAM の位置・reset vector・CLM の chunk の列を確かめる。
  - WLAN の core の FullMAC: `WLAN_TESTING` の偽の FullMAC の ops で、ops の検証（混ぜた ops の EINVAL）、scan、connect（PMK が IEEE 802.11 の付録の PBKDF2 の試験の値と一致）、ASSOCIATED → FOUR_WAY → CONNECTED と carrier、Ethernet の送受の門（接続前の拒否、古い generation の破棄、EAPOL の破棄）、FAILED と期限、link loss、disconnect、close・quiesce・detach。既存の SoftMAC の host 試験（`plan/ws005/tests/host-wlan-retire.sh`）が変わらず通ること。
- **QEMU**（T1）: raspi4b の `boot-test.sh` の起動の回帰（0xfe300000 の SD の fallback を WiFi が壊さない）。WiFi そのものは QEMU に無い。
- **Pi 4 の実機**（T1、Pi は P3 などと共有なので Q1 が調停）: p002 で CMD5・CCCR・CIS（0x02d0）、p004 で chip id・firmware の版・MAC・wlan0 の出現、p005 で `wifi search`・`wifi list`、p006 で WPA2 の AP への接続・DHCP・ping・Settings の Wi-Fi の頁から接続・切断と再接続・Wi-Fi の切と入、可能なら Bluetooth（WS215）と同時の動作。証拠は SSH の `dmesg`・`/sbin/wifi` の出力と画面の PNG（`boot-test.sh` の約束に従う）。serial の log では判定しない。
- 試験の file は `plan/ws206/tests/`。回帰に残す物は WS の終わりに Master の Tools へ（Q1 に依頼）。

### W12. SMP（4 core）での並行

- **bus の thread が SDIO を独り占めする**: 1 つの device に 1 つの kthread。SDIO の command・CMD53 は全てこの thread か、bring-up（open・restart の中、bus の thread がまだ・もう動いていない時）だけが出す。host の層の instance の mutex は二重の守り。
- **IRQ**（SPI 126、CPU 0）: SDHCI の status を読み、card interrupt なら bit 8 の enable を下ろして thread を起こすだけ。SDIO の I/O・alloc・core の呼び出しはしない。thread は F1 の割り込みの status を読んで処理し、終わったら enable を戻す。
- **control の要求**（radio の op、どの CPU からでも、thread の文脈）: driver の control の mutex で 1 つずつ。要求を 1 つの slot に置いて thread を起こし、期限付きで完了を待つ（期限は op の deadline か driver の上限 500 ms）。bus の thread は**決して network worker・core の control gate を待たない**ので、op が thread を待っても core の約束（network worker の進みを待たない）を守る。
- **送り**: `ethernet_transmit` は spinlock の下の有限の ring（64）に写して thread を起こす。thread は SDPCM の credit の分だけ F2 へ書く。
- **受け**: thread は F2 の frame を読み、data・event を有限の ring（data 128、event 32）に入れて `net_device_schedule_poll`。ring が満杯なら data は捨てて数える（control の応答と event は後ろにあるので読み続けなければならない）、event の満杯は scan の結果だけ捨てる（接続の event は捨てない、別の小さな ring）。**core の report の関数は `poll_receive`（network worker）からだけ呼ぶ**。`report_scan_channel_complete` などの IRQ でも呼べる latch も poll から呼ぶ（順序を 1 つにするため）。
- lock の順: core の control gate → driver の control の mutex → ring の spinlock。bus の thread は ring の spinlock だけを短く取る。driver の lock を持ったまま core を呼ばない。
- 共有の資源: GPFSEL3 と pull の REG2（W3 の GPIO の helper の spinlock）、mailbox（既存の atomic の claim、W3 の mailbox の GPIO の helper）、SPI 126（host の層の 1 つの handler）。
- lifecycle: close・detach は「新しい要求を止める → thread に停止を頼み終わりを待つ → ring を空にする → 電源を落とす」。core の `stop_request`・`stop_complete`・`quiesce_begin/end` の約束は既存の driver と同じ。

### W13. Bluetooth（WS215、同じ chip の UART）との共存

- 経路は別: WiFi は SDIO（旧 EMMC、GPIO 34〜39）、Bluetooth は PL011 の UART0（GPIO 30〜33 の ALT3、CTS・RTS 付き、DT の `bluetooth` の node、`shutdown-gpios` = 拡張 GPIO 0 = BT_ON）。電源の線も別（WL_ON 129、BT_ON 128）。WiFi は BT_ON に触らず、BT の側も WL_ON に触らない。どちらが先に起きてもよい。
- 無線の共存（2.4 GHz の antenna の共有）は chip の firmware が NVRAM の `btc_mode=1`・`btc_params*`（Cypress の値、W9 の NVRAM に入っている）で行う。host がすることは無い。v1 では `btc_mode` の iovar を変えない。
- driver の間で共有するのは GPFSEL3・pull の REG2（W3 の GPIO の helper）と mailbox の GPIO（W3 の mailbox の GPIO の helper）。WS215 もこの 2 つを使う（2026-10-13 Q1 経由で両方の設計に記録、どちらが作るかは Q1）。
- 今の `config.txt` の `dtoverlay=disable-bt` は UART の割り当てだけを変え、WiFi には関係しない（Bluetooth を使う時の config の変更は WS215）。
- 試験: WS215 が先に入っていれば p006 で、Bluetooth の keyboard を使いながら WiFi で ping・DHCP の更新を確かめる。

### W14. 危険と未確認

| ID | 危険 | 対策 |
| --- | --- | --- |
| R1 | SD card（boot の device）の driver を register の層へ移すことでの回帰 | 振る舞いを変えない移動として p002 の最初の commit に分け、T1 が Pi の起動と QEMU の起動（fallback）を確かめる |
| R2 | 旧 EMMC（Arasan）の癖: 低い clock で register の書き込みの間に SD clock の 2 周期分の待ちが要るとされる、capabilities の値が当てにならない | 待ちは instance の flag で入れる（400 kHz で約 5 µs）。base clock は mailbox で読む。FreeBSD の BSD の driver と実機で確かめる |
| R3 | WL_ON の初めの状態と reset の pulse の要否、CMD5 までの待ち | p002 の最初に `GET_GPIO_STATE` で読む。datasheet の時間 |
| R4 | firmware-nonfree の minimal の firmware と RPi の CLM の組み合わせの不具合 | 予備の linux-firmware 7.45.234 と Cypress の CLM（W9） |
| R5 | PIO の速さ・CPU の使用 | v1 は機能の確認が目標。足りなければ 50 MHz と DMA を後で |
| R6 | 規制（country）の既定 | E6 |
| R7 | RTL8822BU の dongle と同時の時の wlan0・wlan1 と networkd の 1 つの managed の無線 | v1 は内蔵の WiFi だけを試験。同時は制限として記録 |

## Phase（段の分け、ws.md の表と同じ）

| Phase | 内容 | 見積 | 依存 |
| --- | --- | --- | --- |
| p001 | この設計 → design-reviewer → E2・E4・E6 の回答 | — | — |
| p002 | SDHCI の register の層（SD card を移す、振る舞いは不変）、SDIO の層、GPIO の helper（audio も移す）と mailbox の GPIO の helper（WS215 と共有、どちらの WS が作るかは Q1）、電源・clock・IRQ の handler・claim。Pi で CMD5・CCCR・CIS、Pi と QEMU の起動の回帰（T1） | 6 LW | p001 |
| p003 | firmware の package `bcm43455-firmware`（W9）と license の監査、Pi 4 の image の config | 2 LW | p001、E4 |
| p004 | Broadcom の bring-up（W4）、SDPCM・BCDC・event（W5）、firmware の file、bus の thread（W12）、wlan0 の公開（W8）、値の出典の表、host の試験。Pi で chip id・版・MAC・wlan0（T1） | 10 LW | p002、p003 |
| p008 | WLAN の core の FullMAC の形（W6）と host の試験 | 4 LW | p001（p004 と並べて進められる） |
| p005 | driver の radio の op（scan・connect・firmware の supplicant・data・link loss・restart、W5・W7）、host の試験。Pi で scan（T1） | 5 LW | p004、p008 |
| p006 | 実機の受け入れ（T1）: WPA2・DHCP・ping・Settings・切断と再接続・Wi-Fi の切と入、可能なら Bluetooth と同時 | — | p005、E2 |
| p007 | 規約の全文の見直しと値の出典の監査 | 1 LW | p006 |

計 約 28 LW。p008 は番号の上では最後だが p004 と並べて進められる（Phase の番号は付け直さない）。

## 判断（Q1 経由でユーザーへ、推し付き）

| ID | 問い | 推し |
| --- | --- | --- |
| E2 | 試験の AP | **Pi 4 から見える WPA2-PSK（AES）の AP の SSID と passphrase**、2.4 GHz と 5 GHz のどちらか（両方なら両方）。repo の root にユーザーが作った `.wifi`（WiFi の認証情報、git の外）があるので、それを Pi の試験に使ってよいかも聞く（P1 は中身を読んでいない） |
| E4 | firmware の BLOB の出所と license | **Raspberry Pi の firmware-nonfree（trixie、commit 3bab0f82）の minimal の firmware・RPi の CLM・Cypress の license の NVRAM**（全て Cypress の再配布の EULA。既定は無効の package、Pi 4 の image の config で選ぶ）。linux-firmware の Pi 4 の NVRAM は GPL-2.0+ なので使わない。ただし Cypress の NVRAM と GPL の NVRAM は値が同じなので、それを承知で Cypress の file を使ってよいか、または GPL の file を GPL の text と共に optional の package に入れるか（W9 の「NVRAM の license の確かめ直し」） |
| E6 | 規制の国（country code） | **v1 は firmware の既定（CLM の世界共通の設定、制限のある channel は passive）のまま**、日本で 5 GHz を全部使う必要があれば `JP` を設定する口を後で足す。ユーザーに `JP` を既定にするか聞く |

Q1 の判断でよい技術の点（ユーザーに聞かない）: W8 の wlan0 の後からの公開（net device の API を変えない）、W2 の SD card の driver の register の層への移動（R1）、W12 の並行の形。

## Pi 4 の実機の読み（2026-10-11 P1、読むだけ、10.0.30.2、beta2 82f2dfc）

- boot: VideoCore の firmware（`start4.elf`・`fixup4.dat`）、`config.txt` は `device_tree=bcm2711-rpi-4-b.dtb`、`dtoverlay=disable-bt`（BT の UART を外すだけ、WiFi は残る）。`/lib/firmware` は `rtw88` だけ（brcmfmac の file は無い）。dmesg: eMMC2 の sdhci は 0xfe340000（caps 45ee6432、version 1002）、mailbox の firmware は動く（tag 30066 は非対応の log）。
- DT（`bcm2711-rpi-4-b.dtb` を dtc で読んだ）: `mmcnr@7e300000`（ARM 0xfe300000）、`compatible = "brcm,bcm2835-mmc", "brcm,bcm2835-sdhci"`、割り込み SPI 0x7e、clock `<&clocks 0x1c>`、DMA channel 11、`bus-width = 4`、`non-removable`、`brcm,overclock-50 = 0`、子の `wifi@1`（`brcm,bcm4329-fmac`）。pin: `sdio_pins` = GPIO 34〜39、function 7（ALT3）、pull は CLK なし・他は pull up。**WL_ON は firmware の GPIO の拡張の 2 本目（`gpio-line-names` の "BT_ON", "WL_ON", …、mailbox の GPIO 129）**。この DT には `mmc-pwrseq` の node が無い。
- 2026-10-13: Pi は SSH に応答しない（No route to host、止まっている）。Raspberry Pi の kernel の DT の source（`rpi-6.12.y` の `bcm2711.dtsi`・`bcm2711-rpi-4-b.dts`・`bcm271x-rpi-bt.dtsi`）で確かめた: eMMC2 と旧 EMMC はどちらも GIC SPI 126、Pi 4 の dts は `wifi-pwrseq` を `/delete-node/` で消す（上の観察と一致）、Bluetooth は UART0 の GPIO 30〜33 と `shutdown-gpios = <&expgpio 0>`。

## 未確認（p002 の最初に、kernel の中で）

mailbox の GPIO 129 の今の状態、clock id 1（EMMC）の rate、0xfe300000 の SDHCI の capabilities・version、R2 の書き込みの待ちの要否、BCM43455 の chip の rev（CMD5 と chip id の後）、F2 の block size。

## 記録（2026-10-13 P1）

- 読んだ tree: `src/kern/net/wifi/wlan.c`（ioctl_connect・station_connection_start・report の関数の gate）、`include/kern/net/wlan.h`・`net-device.h`・`irq.h`、`src/drivers/platform/rpi4/rpi4-sdhci.c`・`rpi4-firmware.c`・`rpi4-audio.c`、`src/kern/platform/rpi4.c`、`src/drivers/wifi/intel-ax211/`（publish と firmware の file）、`userland/firmware/`（README・intelax211）、`userland/base/networkd/`（wifi-child・managed-lan）。
- 取得して確かめた上流（scratchpad、tree に入れていない）: linux-firmware の `WHENCE`（425450 B、AX211 の manifest の SHA-256 と一致）・`LICENCE.cypress`・`cypress/cyfmac43455-sdio.bin`・`.clm_blob`・Pi 4 の NVRAM。RPi-Distro/firmware-nonfree の trixie（commit 3bab0f82）の tree・`debian/copyright`・43455 の 3 つの firmware と CLM と NVRAM（symlink の先も確かめた）、bookworm の NVRAM（trixie と同じ）。版は `strings` の feature の文字列で読んだ。OpenBSD の bwfm（ISC）と Infineon の WHD（Apache-2.0）の該当の箇所で値の出所を確かめた（書き写していない）。
- code は書いていない。QEMU・実機は使っていない。

## 記録（2026-10-13 P6、設計 review の M2: mailbox の初期化の SMP）

- 原因: `drv_rpi4_firmware_init` が `mailbox != NULL` を見てから lock 無しで初期化し、`message`・`message_physical` と `mailbox` を plain store で公開していた。別の CPU から同時に呼ぶと 2 回準備され（page の allocate・map が重なる）、`drv_rpi4_firmware_property` は barrier 無しの `mailbox` の読みで buffer の未初期化の値を見得た。
- 直し（`src/drivers/platform/rpi4/rpi4-firmware.c`・`.h`）: 準備の状態 `firmware_state`（ABSENT→PREPARING→READY）。compare-and-exchange で PREPARING を取った 1 つの caller だけが `prepare_mailbox()` で埋め、READY を release で store。他は busy delay で待つ。失敗は ABSENT に戻して次の呼び出しが再試行。`drv_rpi4_firmware_property` は READY を acquire で load してから `mailbox`・`message` を読む。request の buffer の claim（`busy` の acquire の exchange と release の store、上限 2×polling で EBUSY、`kern_usleep_range` は busy delay）は変えていない。HAL の API は不変。
- 試験: `sh plan/ws206/tests/firmware-smp-host-test.sh <OUT> <DTB>`（本物の driver の source、pthread の 8 CPU、TSan）。race: 準備 1 回・16000 answer、retry: 失敗 1 回・準備 2 回・14000 answer、PASS（3 回）。直す前の source では TSan の data race（init の `mailbox` の書きと property の読み）と準備 8 回で FAIL。WS048 の `firmware-host-test`（ASan/UBSan）200163 checks PASS。rpi4 の vmunix の build は warning 0。QEMU・実機は未実施。
