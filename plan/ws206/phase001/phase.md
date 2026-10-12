<!-- awesome-plan project=zedbsd record=ws206-p001 -->

# ws206-p001: Raspberry Pi 4 の WiFi の調査と設計

Parent: [WS206](../ws.md)
Status: in-progress（q932、P1。2026-10-13 設計の第 3 版（第 2 版への design-reviewer の review と Q1 の判断を反映）、design-reviewer 待ち。E2・E4・E6 はユーザーの回答待ち）
Phase disposition: normal
Queue: q932（[P1 の lane](../../agents/P1/queue.md)）

## 由来

2026-10-11 ユーザー:「RPi4のWiFiを実装。」
2026-10-12 ユーザー: Pi 4 の順は 描画の最適化 → SMP → 計測 → WiFi（この WS）と Bluetooth（[WS215](../../ws215/ws.md)）で Pi 4 は完了。SMP（[WS208](../../ws208/ws.md)、4 core）は 2026-10-13 に main に入った。

## 第 3 版の対応（第 2 版への review: [review-2.md](review-2.md)）

| review | 要点 | 第 3 版の対応 | 節 |
| --- | --- | --- | --- |
| F1 致命 | R1（`rpi4-sdhci.c` を register の層に分ける）が並行の BUG-295 とぶつかり、QEMU の SD の fallback の待ちと clock を変える | **R1 をやめた**（Q1 の判断）。SDIO の host は独立の新しい file `rpi4-sdio-host.c`。`rpi4-sdhci.c` は既存の関数を変えず、SD が使った base を返す getter `drv_rpi4_sdhci_base()` を 1 つ追記するだけ（BUG-295 の main への統合の後）。SPI 126 は小さな共通の dispatcher `rpi4-mmc-irq.c`（2 slot） | W1・W2・W14 |
| S1 重大 | core と WPA2 engine の結びつき（約 117 箇所）を小さく見ていた | `wlan_wpa2_engine_*` と同じ面の **FullMAC の engine**（`wlan-fullmac.c`）を足し、station は engine の分岐だけにする（Q1 の判断）。station の全ての箇所の表を p008 の設計に。p008 は 6〜8 LW。`plan/master-design-policy.md` の「WLAN protocol-state ownership」の方針の変更は Q1 が master に記録する | W6 |
| S2 重大 | scan で `report_scan_channel_ready` が抜けている | escan の iovar が受け付けられたら ready、status 0 で complete、結果は ready と complete の間。chanspec は control（primary）の channel を解く | W5 |
| S3 重大 | 接続の event の ring が満杯の時の deadlock、connect の約 8 個の ioctl の同期の待ち | 接続の event は generation ごとの latch（満杯が無い）。`fullmac_connect` は要求を slot に置いてすぐ返し、ioctl の列は bus の thread が自分で出す。結果は event で | W7・W12 |
| S4 重大 | 失敗の errno の写し（networkd は EACCES で再試行を止める） | event・status・reason → errno の表。**鍵の誤りの証拠（M2 を送った後に M3 が来ない）がある時だけ EACCES**。表は host 試験に | W7・W11 |
| M1 | SDIO の IRQ と register | ISR は mutex を取らず instance の irqsave の spinlock で `INT_ENABLE` の bit 8 を下ろす、`SOFTWARE_RESET_ALL` の後に enable を書き直す、IRQ の番号は DT から（SPI 126 = 158）、取り落としの保険に 100 ms の poll、dispatcher の 2 slot | W2・W12 |
| M2 | `drv_rpi4_firmware_init` が SMP で安全でない | WS206 の外で Q1 が直した（8155669d4、下の P6 の記録）。この設計は「init は何度呼んでも安全」を前提にし、WiFi の thread も呼ぶ。GPIO の helper の map も同じ形の init で platform の初期化から | W3・W8 |
| M3 | 後から公開する thread と shutdown の取り合い、package が無い時の後始末 | lifecycle の mutex で公開と shutdown を直列、待ちは sleep で `stopping` を見る、失敗の時は IRQ の slot・claim・電源を返す | W8 |
| M4 | firmware の file を driver の lock の前に読む、新しい lock の rank | file は driver の lock を何も持たずに読む。lock の rank と順を表に | W8・W12 |
| M5 | GPIO・mailbox の helper の形の不一致と作る側 | **持ち主は ws206-p002a**（Q1 の判断）。関数の名前と引数をここで決めた（WS215 の p003 が引用する）。両方の Phase 表に依存 | W3・Phase 表 |
| M6 | boot の probe が同期（電源の pulse、CMD5 の最大 1 s、mailbox） | boot では DT の検査と thread を作るだけ。電源・SDIO・mailbox は全て thread | W8 |
| M7 | 43455 の save-restore・KSO・clock の要求 | v1 は HT を force し KSO・SR を使わない（推測）、p004 で確かめる。危険 R8 | W4・W14 |
| 軽微 | NVRAM の位置 | 長さの語は RAM の最後の 4 byte、NVRAM はその直前 | W4 |
| 軽微 | SDPCM の credit | `(max_seq − seq) & 0xff` が 0x80 以上なら 0 とみなす | W5 |
| 軽微 | `idsup` | 版の文字列と `cap` の iovar の両方で確かめる | W7 |
| 軽微 | set_clock が 2 の冪だけ | `rpi4-sdio-host.c` は SDHCI 3.00 の 10 bit の分周（2N）で 25 MHz を作る | W2 |
| 軽微 | `mmcnr` の status・QEMU の guard | node が disabled（`disable-wifi` の overlay）なら probe しない、QEMU の印（共通の `drv_rpi4_board_emulated()`）でも probe しない | W2 |
| 軽微 | PMK の消去 | slot・BCDC・SDPCM の送りの buffer・失敗と timeout と取り消しの経路の全部 | W7 |
| 軽微 | 長い待ち | `waitq_sleep` の deadline 付き（yield）。`kern_usleep_range` は µs の短い待ちだけ | W3・W8 |
| 軽微 | `rpi4-audio.c` の GPFSEL4 の移動 | 範囲の外として外した（audio は GPFSEL4 だけで、WiFi・BT と register を共有しない） | W3 |
| 軽微 | `userland/firmware/README.md` と Kconfig の名前 | dir `userland/firmware/bcm43455/`、package `bcm43455-firmware`、driver の設定 `CONFIG_DRIVER_BCM2711_WIFI` | W9・W8 |
| 軽微 | MAC が NVRAM の仮の値と同じ | 断る（wlan0 を作らない） | W8 |
| E2・E4・E6 | review の意見 | 9 節「判断」に review の意見を入れ、問いを平易に直した。E4 は DEP-5 の順を `debian/copyright` で確かめた（下） | 判断 |
| 段の分け | p002a・p002b | p002 を p002a（helper）と p002b（SDIO の host）に分けた | Phase 表 |

## 決まったこと

| ID | 決め | 出典 |
| --- | --- | --- |
| E1 | WPA2 の 4-way handshake は firmware の supplicant（W7）。PMK は core が passphrase から計算して driver へ渡す | 2026-10-11 ユーザー「推し通りです。」、2026-10-13 Q1 |
| E3 | WLAN の core に FullMAC の形を足す（W6）。kernel の内部の API だけ、HAL・UAPI は変えない | 同上 |
| Q-F1 | R1 をやめる。SDIO の host は独立の file、`rpi4-sdhci.c` は触らない（getter の追記だけ） | 2026-10-13 Q1（review-2 の F1） |
| Q-M5 | GPIO の helper と mailbox の GPIO の helper の持ち主は ws206-p002a。名前と引数はこの設計（W3）で決める | 2026-10-13 Q1 |
| Q-M2 | `drv_rpi4_firmware_init` の SMP は WS206 の外で Q1 が直す（8155669d4 で直った） | 2026-10-13 Q1 |
| Q-S1 | FullMAC の engine を `wlan_wpa2_engine_*` と同じ面で足す。方針の変更（FullMAC では firmware が 4-way と port の判断を持ち、core は FullMAC の engine で状態を写す）は Q1 が `plan/master-design-policy.md` に記録する | 2026-10-13 Q1 |

## 今の tree の事実（2026-10-11・2026-10-13 の読み、main 20da95a8f、17e43b7f6 の `rpi4-board.c` を含む）

| 所 | 事実 |
| --- | --- |
| Pi 4 の driver | `src/drivers/platform/rpi4/`: console、ethernet（GENET）、firmware（mailbox の property）、PCIe（VL805 の xHCI）、audio、**sdhci（eMMC2、SD card、PIO、1079 行）**。sdhci は 1 つの global の `unit` を持ち、割り込みを使わず polling。BUG-295（SD の多 block 転送）が並行して `rpi4-sdhci.c` を大きく変えている。WiFi の SDIO の driver は無い |
| 0xfe300000 の今の使い道 | `src/kern/platform/rpi4.c:82-91`: eMMC2 の初期化が失敗し handoff が 0xfe340000 なら、**QEMU raspi4b の SD card として 0xfe300000 を使う**。実機ではこの controller が WiFi の SDIO |
| platform の初期化の順 | `kern_platform_init`: console → SD（`drv_rpi4_sdhci_init`）→ GENET → PCIe（`drv_rpi4_firmware_init` を呼ぶ。QEMU ではその前に戻る）→ audio → GPU。`kern_platform_refresh_devices`（割り込みの有効の後）: GENET の refresh、PCIe の refresh |
| QEMU の印 | `drv_rpi4_board_emulated(fdt)`（`rpi4-board.c`）: QEMU raspi4b が渡した DT なら true。PCIe・PWM1 の driver が使う |
| GPIO | 汎用の GPIO の driver は無い。`rpi4-audio.c` が GPFSEL4（pin 40〜49）だけを、HAL の `led.c` が起動の初めに GPFSEL4 を、HAL の `uart.c` が GPFSEL1 を直に書く。pull の register を書く code は無い。WiFi の pin 34〜39 と Bluetooth の pin 30〜33 は**同じ GPFSEL3**、pull の REG2（pin 32〜47）も共有 |
| mailbox | `drv_rpi4_firmware_init` は 8155669d4 から SMP で何度呼んでも安全（ABSENT→PREPARING→READY）。`drv_rpi4_firmware_property` は request buffer を atomic の claim で直列にし、取れなければ busy delay の後 EBUSY を返し得る。thread の文脈だけ |
| 割り込み | `kern_irq_register` は 1 本の IRQ に 1 つの handler（2 つ目は EBUSY）。DT の割り込みの番号は SPI + 32（`rpi4-ethernet.c:264`）。eMMC2 と旧 EMMC は**同じ GIC SPI 126（158）**。今の SD card は polling で登録していない。WS208 の D1 で SPI は全部 CPU 0 へ |
| 待ち | `kern_usleep_range` は busy delay（`include/kern/clock.h:198`）。thread が眠る待ちは `waitq_sleep`（deadline 付き） |
| lock の rank | `include/kern/lock.h`: INODE 60、NETWORK 120（WLAN の station の lock）、DEVICE 125（AX211・GENET・audio の lock）、DISK 130（sdhci の mutex）。rank は記録だけで実行時の検査は無い（`src/kern/lock.c`） |
| WLAN の core | `src/kern/net/wifi/`、`include/kern/net/wlan.h`。**SoftMAC の形**: core が scan・auth・assoc・WPA2 の 4-way（`wlan-wpa2.c` の engine）・802.11 の data の MPDU（`wlan-l2.c`）を作り、driver は radio の op。station（`wlan.c`）は engine の状態と flag（`wpa2.authorized`・`associated`・`pairwise_installed`・`connected_lifetime` など）を直に読む（W6 の表） |
| core の並行の約束 | radio の op は station の lock を持たずに thread の文脈で呼ばれ（多くは network worker から、control gate を持ったまま）、**network worker の進みを待ってはならない**。barrier の op（`scan_stop`・`disconnect`・`quiesce`）は driver の上限の時間の中で終わるか error を返す（core が再試行）。`report_frame`・`report_tx_complete`・`report_link_loss` は control gate を取る。scan の ready・complete・error の report は IRQ でも呼べる latch |
| scan の offload | `WLAN_SCAN_CHANNEL_OFFLOADED_DWELL`: 1 channel を firmware に任せる。core は TUNING の段で ready が無いと complete を ESTALE にし（`wlan.c:713-719`）、結果も ready の前は受けない（`:578-588`）。AX211 は ready（`intel-ax211.c:4235`）→ 結果 → complete（`:4328`） |
| networkd の errno | `EACCES` の失敗は「鍵が断られた」とし、その profile を store が変わるまで再び試さない（`userland/base/networkd/main.c:4659`・`:8136`）。`ENOENT`・`ETIMEDOUT` は `ENETUNREACH` として出す（`:4642`） |
| net device | `net_device_create` は作る時に MAC が要る。rx は `net_device_schedule_poll` → `poll_receive`（network worker） |
| firmware の file | AX211 は open の時に `file_openat`（`/lib/firmware/...`）で読む。`mount_root_get_ref()` で root の有無が分かる |
| firmware の包み | `userland/firmware/<名>/` → package `<名>-firmware`（例 `intelax211-firmware`）。既定は無効、immutable の上流から取得し大きさと SHA-256 を確かめる。`tools/release/license-components.json` に license の記録、`userland/firmware/README.md` に一覧 |
| driver の設定 | `config/drivers/architecture/arm64.drivers` の行（例 `CONFIG_DRIVER_BCM2711_GENET|bool|…|rpi4|y|`）と `Makefile`・`platform/arm64/vmunix.mk` |
| user の側 | Settings の Wi-Fi の頁 → … → networkd → `/sbin/wifi` → UAPI の `SIOCSWLANSCAN`・`SIOCGWLANBSS`・`SIOCSWLANCONNECT`・`SIOCGWLANSTATUS`。networkd は `wlan` で始まる名の interface を無線として扱う |
| BCM43455 | Broadcom/Cypress（今は Infineon）の **FullMAC**。MLME・scan・supplicant（firmware に機能があれば）は firmware の中、data は 802.3 の frame、制御は BCDC の ioctl・iovar、通知は firmware の event。SDIO の 3 function（F0 CCCR、F1 backplane、F2 data） |

## 設計

### W1. 全体の形

```
Settings / networkd / /sbin/wifi                       （変えない）
        │ UAPI ioctl（変えない）
WLAN の core  src/kern/net/wifi/wlan.c                   ← station は engine の分岐だけ（W6）
   ├ WPA2 engine     wlan-wpa2.c（SoftMAC、変えない）
   └ FullMAC engine  wlan-fullmac.c                      ← 新規（W6）
        │ struct wlan_radio_ops（FullMAC の op）・report の関数
bcm43455 driver  src/drivers/wifi/bcm43455/              ← 新規（W4・W5・W7・W8・W12）
        │ struct bcm43455_bus_ops（SDIO の読み書き。host の試験では偽物）
Pi 4 の SDIO の card の層  src/drivers/platform/rpi4/rpi4-sdio.c       ← 新規（W2、probe と bus_ops）
Pi 4 の SDIO の host       src/drivers/platform/rpi4/rpi4-sdio-host.c  ← 新規、独立の file（W2）
        ├ rpi4-mmc-irq.c   SPI 126 の dispatcher（2 slot）             ← 新規（W2）
        └ drv_rpi4_sdhci_base()  rpi4-sdhci.c に getter を 1 つ追記   （W2）
GPIO の helper  rpi4-gpio.c（新規）・mailbox の GPIO の helper  rpi4-firmware.c（追記）  ← p002a（W3）
```

HAL・hal.h・UAPI は変えない。kernel の内部の API の追加は `include/kern/net/wlan.h` の FullMAC の op と report の関数（W6）と、Pi 4 の driver の間の小さな関数（W2・W3）だけ。

### W2. SDIO の host（旧 EMMC、0xfe300000）

Pi 4 の SD の controller のうち、**eMMC2**（0xfe340000）は SD card、**SDHOST**（0xfe202000、SDHCI でない）は使わない、**旧 EMMC**（0xfe300000、Arasan の SDHCI、DT `mmcnr@7e300000`・`brcm,bcm2835-mmc`/`brcm,bcm2835-sdhci`）が GPIO 34〜39 の ALT3 で WiFi の SDIO に繋がる。

- **独立の file** `rpi4-sdio-host.c`（約 600 行の見込み）: instance の `struct rpi4_sdio_host`（base、IRQ、base clock、32 bit 書きの影の `transfer_mode`/`block_size`、irq の spinlock、mutex、望む割り込みの enable の値）。register の手続き（32 bit だけの書き込みと 16 bit の影、reset、clock、command、PIO の block の読み書き、status の期限付きの待ち）は SD Host Controller Simplified Specification から自分で書く。`rpi4-sdhci.c` の code は**写さず、変えず、include もしない**。同じ種類の手続きが 2 つの file にある（約 200 行の重複）ことは承知の上で、まとめるのは BUG-295 が落ち着いた後の Future Work の候補（R1'）。
- **旧 EMMC の癖**（R2）: 低い clock で register の書き込みの間に SD clock の 2 周期分の待ち（400 kHz で約 5 µs、`kern_usleep_range`）。capabilities の base clock は信じない。
- **SDIO の card の層** `rpi4-sdio.c`: CMD5（IO_SEND_OP_COND、3.3 V の窓、ready・function の数）、CMD3（RCA）、CMD7、CMD52（1 byte）、CMD53（byte mode と block mode、address の増加）、CCCR（I/O enable・ready、割り込みの enable、4 bit、high speed）、FBR（block size: F1 64 byte、F2 512 byte、p004 で確定）、CIS の manufacturer（0x02d0）と device id。値は SD Association の SDIO Simplified Specification から。`struct bcm43455_bus_ops` の実体（W4）と probe の入口 `drv_rpi4_wifi_probe()`（W8）もここ。
- **clock**: 初期化は 400 kHz、enumeration の後 25 MHz。base clock は mailbox の `GET_CLOCK_RATE`（clock id 1、EMMC）。host の版（register 0xFE の spec の版）が 3.00 以上なら **10 bit の分周（2N、N = 1〜1023）**で `base / 2N ≤ 目標` の最小の N（250 MHz なら N=5 で 25 MHz、200 MHz なら N=4）、2.00 なら 8 bit の 2 の冪。実際の周波数を log に出す。50 MHz の high speed は p006 の後の改良。DT の `brcm,overclock-50 = 0`。
- **転送は PIO**。DMA は後の改良（Future Work の候補）。4 bit・25 MHz の上限は約 12.5 MB/s。
- **割り込み**（M1）:
  - IRQ の番号は DT の `mmcnr` の `interrupts`（SPI 0x7e）+ 32 = 158。
  - SDIO の card interrupt（normal interrupt の bit 8）だけを status enable（0x34）と signal enable（0x38）で通す。command・data の完了は status の polling（短い、期限付き）。
  - handler（IRQ の文脈、mutex を取らない）: instance の **irqsave の spinlock** の下で normal status（0x30）を読み、bit 8 が立ち enable もされていれば 0x34・0x38 の bit 8 を下ろして（level の割り込みを止める。card interrupt の status は書いて消せない）`card_irq_pending` を立て、bus の thread を起こす。自分の物なら true を返す。
  - bus の thread が F1 の割り込みを処理し終えたら `rpi4_sdio_host_card_irq_rearm()` が同じ spinlock の下で bit 8 を戻す。0x34・0x38 の read-modify-write は全てこの spinlock の下。
  - `SOFTWARE_RESET_ALL` は enable の register を 0 に戻すので、reset の後に spinlock の下で instance の「望む enable」の値を書き直す。
  - 取り落としの保険: bus の thread は open の間、割り込みが無くても 100 ms ごとに F1 の割り込みの status を CMD52 で読む。
- **SPI 126 の dispatcher** `rpi4-mmc-irq.c`（約 120 行、eMMC2 と旧 EMMC が同じ線を共有するため）:

  ```c
  /* Returns true when the interrupt was this controller's. */
  typedef bool (*drv_rpi4_mmc_irq_fn)(void *argument);

  int drv_rpi4_mmc_irq_add(int irq, drv_rpi4_mmc_irq_fn handler, void *argument);
  int drv_rpi4_mmc_irq_remove(int irq, drv_rpi4_mmc_irq_fn handler, void *argument);
  ```

  slot は 2 つ。最初の add が `kern_irq_register(irq, dispatch, NULL)` を行う。満杯は EBUSY、別の番号は EINVAL。dispatch は dispatcher の irqsave の spinlock の下で登録された handler を順に呼び、`kern_irq_send_eoi` する。remove は同じ spinlock の下で slot を消す（remove が返った後に handler は呼ばれない）。remove の前に呼ぶ側が自分の signal enable を下ろす。線の登録は空でも残す（空の dispatch は EOI だけ）。lock の順は dispatcher → instance（instance の側は dispatcher の lock を取らない）。eMMC2 は今は polling で使わないが、BUG-295 の次の段で eMMC2 が割り込みを使う時は 2 つ目の slot に add する（`kern_irq_register` を直に呼ぶと EBUSY になる）。
- **QEMU の fallback との衝突を防ぐ**（claim）: `rpi4-sdhci.c` に getter を 1 つ**追記**する（既存の関数は変えない、BUG-295 の main への統合の後に足す）:

  ```c
  /* The physical base the SD card driver bound to, or 0 when it bound none. */
  uintptr_t drv_rpi4_sdhci_base(void);
  ```

  SD の初期化は `kern_platform_init` の中で、WiFi の probe（`kern_platform_refresh_devices` から呼ばれ、register に触るのはその後の thread）より必ず先に終わるので値は確定している。`drv_rpi4_sdhci_base() == 0xfe300000`（QEMU の fallback）なら WiFi は何もしない（reset も書き込みもしない）。
- **probe しない条件**（どれか 1 つで止まる、register に触らない）: DT に 0x7e300000 の `mmcnr` の node が無い、または disabled（`disable-wifi` の overlay）、子に `brcm,bcm4329-fmac` が無い、QEMU の印（共通の `drv_rpi4_board_emulated(fdt)`、`rpi4-board.c`、main 17e43b7f6 で入った）、SD が 0xfe300000 を使った、設定 `CONFIG_DRIVER_BCM2711_WIFI` が n。QEMU raspi4b に BCM43455 は無いので、QEMU では WiFi の試験はせず起動の回帰だけ。

### W3. pin・電源・helper（p002a、WS215 と共有）

両方の helper の持ち主は **ws206-p002a**。WS215（ws215-p003）はこの名前と引数を引用し、自分で GPFSEL・pull・mailbox の GPIO を直に書かない。

**GPIO の helper** `src/drivers/platform/rpi4/rpi4-gpio.c`・`rpi4-gpio.h`:

```c
/* The 3-bit function select code of BCM2711 GPFSELn; DT brcm,function uses the same numbers. */
enum drv_rpi4_gpio_function {
	DRV_RPI4_GPIO_FUNCTION_INPUT = 0,
	DRV_RPI4_GPIO_FUNCTION_OUTPUT = 1,
	DRV_RPI4_GPIO_FUNCTION_ALT5 = 2,
	DRV_RPI4_GPIO_FUNCTION_ALT4 = 3,
	DRV_RPI4_GPIO_FUNCTION_ALT0 = 4,
	DRV_RPI4_GPIO_FUNCTION_ALT1 = 5,
	DRV_RPI4_GPIO_FUNCTION_ALT2 = 6,
	DRV_RPI4_GPIO_FUNCTION_ALT3 = 7
};

/* A pin's resistor.  The numbers are deliberately neither the DT's nor the register's. */
enum drv_rpi4_gpio_pull {
	DRV_RPI4_GPIO_PULL_NONE = 1,
	DRV_RPI4_GPIO_PULL_UP = 2,
	DRV_RPI4_GPIO_PULL_DOWN = 3
};

int drv_rpi4_gpio_init(const struct drv_fdt *fdt);
int drv_rpi4_gpio_set_function(unsigned pin, enum drv_rpi4_gpio_function function);
int drv_rpi4_gpio_get_function(unsigned pin, enum drv_rpi4_gpio_function *function);
int drv_rpi4_gpio_set_pull(unsigned pin, enum drv_rpi4_gpio_pull pull);
int drv_rpi4_gpio_pull_from_dt(uint32_t brcm_pull, enum drv_rpi4_gpio_pull *pull);
```

- pull の 3 つの符号: DT の `brcm,pull` は 0 なし・1 down・2 up、BCM2711 の `GPIO_PUP_PDN_CNTRL_REGn` は 00 なし・**01 up・10 down**（up と down が逆）。列挙型は 1〜3 にして、どちらの生の数を渡しても EINVAL になるようにする（0 は無効）。register の符号への変換は `rpi4-gpio.c` の中の 1 箇所、DT の数の変換は `drv_rpi4_gpio_pull_from_dt()` の 1 箇所だけ。両方を host 試験で確かめる。
- register: GPFSELn は 0x00 + 4×(pin / 10)、bit 3×(pin % 10)。pull は 0xe4 + 4×(pin / 16)、bit 2×(pin % 16)。pin は 0〜57（それ以外は EINVAL）。値は BCM2711 ARM Peripherals から。
- **1 つの irqsave の spinlock**（`LOCK_RANK_DEVICE`、leaf: 中で他の lock を取らない）の下で read-modify-write。IRQ の文脈から呼んでもよいが、今の使い手は thread だけ。
- `drv_rpi4_gpio_init()`: DT の `brcm,bcm2711-gpio` の node の `reg` を map する（大きさが 0xf4 未満なら pull の REG3 まで 0xf4 を map。dtb の値は p002a で確かめる）。8155669d4 の mailbox と同じ ABSENT→PREPARING→READY の形で何度呼んでも安全にし、**`kern_platform_init`（`src/kern/platform/rpi4.c`）から 1 回**呼ぶ。他の関数は READY でなければ ENODEV。
- HAL の `led.c`（GPFSEL4、起動の初め）・`uart.c`（GPFSEL1）・`rpi4-audio.c`（GPFSEL4）は触らない（WiFi・BT と同じ register を書かない）。

**mailbox の GPIO の helper**（`rpi4-firmware.c`・`.h` に追記）:

```c
/* The firmware GPIO expander lines a driver may drive (DT gpio-line-names). */
enum drv_rpi4_firmware_gpio {
	DRV_RPI4_FIRMWARE_GPIO_BT_ON = 0,
	DRV_RPI4_FIRMWARE_GPIO_WL_ON = 1
};

int drv_rpi4_firmware_get_gpio(enum drv_rpi4_firmware_gpio line, int *state);
int drv_rpi4_firmware_set_gpio(enum drv_rpi4_firmware_gpio line, int state);
```

- 引数は**拡張の line の番号**（DT の `expgpio` の 0・1）。mailbox の GPIO の番号 128 + line への変換は helper の中だけで行う（M5 の「line か 128+n か」の不一致をここで決めた）。受ける line は BT_ON と WL_ON の 2 本だけ（同じ拡張の `SD_PWR_ON` などを誤って動かすと boot の disk が落ちるので、列挙型の外は EINVAL）。state は 0 か 1。
- tag は `GET_GPIO_STATE`（0x00030041）・`SET_GPIO_STATE`（0x00038041）、Raspberry Pi の firmware の公開の wiki（Mailbox property interface）から。応答の検査（gpio の欄・status の欄）の形は p002a で wiki と実機で確かめる。
- `drv_rpi4_firmware_property` の EBUSY（buffer の取り合い）は helper の中で 1 ms の sleep を挟み 50 ms まで再試行する。ENODEV（mailbox 未準備）、EIO（firmware が断った）、ENOTSUP はそのまま返す。thread の文脈だけ。
- 1 本ずつの設定で read-modify-write ではないので、buffer の claim のほかに lock は要らない。WL_ON は WiFi だけ、BT_ON は WS215 だけが書く。

**pin**: GPIO 34〜39 を ALT3。pull は CLK（34）なし、CMD・DAT0〜3（35〜39）は up（DT の `sdio_pins`）。

**電源**: WL_REG_ON は拡張の line 1（WL_ON）。Raspberry Pi の DT は `wifi-pwrseq` を消しており、firmware が起動時に WL_ON を上げたままと推定（未確認、p002b で `drv_rpi4_firmware_get_gpio` で読む）。bring-up のたびに 0 → 10 ms → 1 の reset の pulse、その後 CMD5 の応答を 1 s まで待つ（待ちは `waitq_sleep` の deadline 付き。値は Infineon の CYW43455 の datasheet の WL_REG_ON の timing で p002b に確定）。**BT_ON には触らない**。

### W4. Broadcom の bring-up（`src/drivers/wifi/bcm43455/`）

順（値の出典は W10）:

1. SDIO の enumeration（W2）、F1 を有効に、F1 の block size。
2. backplane の clock: F1 の chip clock CSR（0x1000e）に ALP の要求、ALP の available を待つ。
3. chipcommon（backplane 0x18000000）の chip id: 0x4345、rev 6 以上（43455 = C0）でなければ止まる。backplane の読み書きは F1 の 32 KB の窓（SBADDR の 3 つの register 0x1000a〜c）と 4 byte の access の flag（0x8000）。
4. EROM を読み、ARM CR4（core id 0x83e）・D11・SDIO の device の core・chipcommon と各 wrapper の address を得る。
5. CR4 を止める。RAM の大きさを CR4 の bank の情報から読む。RAM の base は 43455 で 0x198000。
6. firmware を RAM の base から CMD53 の block mode で書く（32 KB の窓ごと）。先頭と末尾の 1 block を読み戻して確かめる。
7. NVRAM の text を binary に直す（comment・空行・前後の空白を除き `key=value\0` を並べ、`\0` で終え、4 byte に詰める）。**長さの語 `(~words << 16) | (words & 0xffff)` は RAM の最後の 4 byte（base + 大きさ − 4）に置き、詰めた NVRAM はその直前（base + 大きさ − 4 − NVRAM の長さ）に置く**。この変換と位置の計算は host で試験できる純粋な関数。
8. firmware の先頭の語（reset vector）を address 0 に書き、CR4 の halt を外す。
9. **clock**（M7）: v1 は chip clock CSR で **HT を force** し、KSO（keep SDIO on）と save-restore を使わない（推測。43455 の firmware が SR を有効にしていれば host が KSO を扱う必要がある。p004 の最初に firmware の版の文字列と chip の SR の能力の register を読み、要れば p004 の中で KSO を足す。危険 R8）。HT を待ち、F2 を有効にして ready を待つ（上限 3 s、sleep）。SDIO の device の core の割り込みの mask、CCCR の割り込みの enable（master・F1・F2）。to-host の mailbox の SDPCM の版（4）を確かめる。
10. CLM を `clmload` の iovar で 1400 byte ずつ（版・BEGIN/END の flag・type CLM）送り、`clmload_status` を確かめる。
11. `cur_etheraddr`（MAC、chip の OTP から）、`ver`（版の文字列、log に出す）、`cap`（能力の文字列、W7 の `idsup`）、`event_msgs`、有効な channel の一覧（W6 の scan の profile へ）。`WLC_SET_PM` 0。
12. `WLC_UP`。

firmware の trap（to-host の mailbox の FWHALT）・bus の error・F2 の応答なしは、`wlan_station_report_link_loss`（EIO）と `wlan_station_restart_request` で W8 の restart へ。

### W5. SDPCM・BCDC・iovar・event・escan

- **SDPCM**（F2 の frame）: 4 byte の frame tag（長さと補数）＋ 8 byte の software header（seq、channel と flag、次の長さ、data の offset、flow control、max seq、予約）。channel は 0 control、1 event、2 data、3 glom。**送信の窓**は `window = (max_seq − seq) & 0xff`。**`window & 0x80` なら 0 とみなす**（firmware の値が後ろ・壊れた時に 255 個を一度に送らない）。窓が 0 の間は送らない。frame は F2 の block size に詰める。glom は v1 では `bus:rxglom` 0（firmware が断れば p004 で channel 3 を解く）。
- **BCDC**: control は 16 byte の header（cmd、len、flags（error・set・interface・request id）、status）＋ 本体。応答は id で照合し、1 度に 1 つ。data は 4 byte の header（版 2、priority、interface、data の offset（4 byte 単位））＋ Ethernet frame。
- **iovar**: `WLC_GET_VAR`（262）・`WLC_SET_VAR`（263）に `名前\0値`。
- **event**: Ethernet の header（ethertype 0x886c）＋ OUI 00:10:18・subtype 0x8001 ＋ big-endian の `wl_event_msg` ＋ data。使う event: SET_SSID（0）、AUTH（3）、DEAUTH（5）・DEAUTH_IND（6）、ASSOC（7）、DISASSOC_IND（12）、LINK（16）、PSK_SUP（46）、ESCAN_RESULT（69）。
- **escan と core の scan の段**（S2）:
  1. core が `scan_channel_start(generation, step_index, channel, deadline)` を呼ぶ（profile の channel は全て `OFFLOADED_DWELL`）。driver は要求を scan の slot に置いて bus の thread を起こし、すぐ返る。
  2. bus の thread は `escan` の iovar（版、action 1、sync id = step と generation から作る、SSID なし、BSSID broadcast、active か passive、channel 1 つの chanspec）を出す。**応答が成功なら scan の latch に「受け付けた」を立てる**。失敗なら latch に error。
  3. `poll_receive` は latch を見て、**`report_scan_channel_ready` → その sync id の結果の `report_scan_frame` → status 0 なら `report_scan_channel_complete`** の順で core へ出す（ready の前に結果を出さない）。ESCAN_RESULT の status partial（8）は結果、0 は終わり、それ以外（中止・busy）は `report_scan_error`（EIO、busy は EBUSY）。違う sync id の結果は捨てる。
  4. `scan_stop` は `escan` の action 3（中止）を出し、scan の slot と latch を消すまで待つ（上限 500 ms、超えたら ETIMEDOUT で core が再試行）。
- **bss_info → BSS の記録**: `bss_info`（版 109、BSSID、beacon の間隔、capability、SSID、chanspec、`ctl_ch`、RSSI、IE の offset と長さ）から 802.11 の probe response の形の frame を組み、`wlan_station_report_scan_frame` に渡す。security の bit は core の既存の parser が付ける。core に新しい scan の API は要らない。
- **chanspec の解き**: band（2.4/5 GHz）と帯域（20/40/80 MHz）を読み、**control（primary）の channel** を出す。20 MHz は channel の欄そのもの、40・80 MHz は欄が中心の channel なので sideband の bit から control の channel を計算する（40: 中心 − 2 + 4×sb、80: 中心 − 6 + 4×sb）。`bss_info` の `ctl_ch` が 0 でなければそれと一致することを確かめ、違えば `ctl_ch` を採る。bit の配置（d11ac の形）は p004 の values.md で確定。
- これらの encode・decode は hardware に触らない純粋な関数（`bcm43455-sdpcm.c`・`bcm43455-bcdc.c`・`bcm43455-event.c`）にし、host で試験する。

### W6. WLAN の core の FullMAC の形（p008、kernel の内部の API の追加、E3・Q-S1）

#### W6.1 driver から見える API（`include/kern/net/wlan.h`、UAPI・hal.h は不変）

- `struct wlan_radio_ops` に 2 つの op:
  - `int (*fullmac_connect)(void *context, const struct wlan_fullmac_connect_request *request)` — request は generation、期限、選んだ BSS（`struct wlan_bss_record`）、PMK（32 byte）。**driver は request を自分の slot に写して bus の thread を起こし、すぐ返る**（W7）。結果は event で返す。core は戻った後に request の PMK を消す。
  - `int (*ethernet_transmit)(void *context, uint64_t generation, const uint8_t *frame, size_t length)` — 802.3 の frame を写して送りの ring に入れ、すぐ返る（満杯は ENOBUFS）。
- **mode は attach の時に決まる**: `fullmac_connect` が非 NULL なら FullMAC。FullMAC は `scan_channel_start`・`scan_stop`・`fullmac_connect`・`disconnect`・`ethernet_transmit`・`quiesce`・`stop_retry` を要し、SoftMAC の `connect_start`・`management_transmit`・`association_set/clear`・`frame_transmit`・`key_*`・`keys_activate` は NULL（混ぜた ops は EINVAL）。既存の SoftMAC の driver は何も変わらない。
- report の関数を 2 つ:
  - `int wlan_station_report_fullmac_event(struct wlan_station *station, uint64_t generation, enum wlan_fullmac_event event)` — event は `WLAN_FULLMAC_EVENT_ASSOCIATED`（firmware が AP に繋いだ）と `WLAN_FULLMAC_EVENT_AUTHORIZED`（firmware の supplicant が 4-way と group key を終えた）。poll・thread の文脈、control gate を取る。失敗・切断は既存の `wlan_station_report_link_loss(station, generation, error)` で返す（error は W7 の表）。
  - `int wlan_station_report_ethernet(struct wlan_station *station, uint64_t generation, const uint8_t *frame, size_t length)` — generation が今の接続で port が開いている時だけ `net_device_receive` へ。EAPOL（0x888e）は捨てる。poll・thread の文脈。
- `wlan.h` の「Drivers cannot publish AUTHORIZED directly」の comment に FullMAC の文を足す: FullMAC の driver は firmware の AUTHORIZED を `report_fullmac_event` で報告し、core の FullMAC の engine がそれを状態に写して carrier（controlled port）を上げる。port を開くのは今も core。

#### W6.2 FullMAC の engine（`src/kern/net/wifi/wlan-fullmac.c`・`.h`、kernel の private）

`wlan_wpa2_engine_*` と同じ面にし、station からは同じ種類の呼び出しで使えるようにする。

```c
enum wlan_fullmac_state {
	WLAN_FULLMAC_STATE_IDLE = 0,
	WLAN_FULLMAC_STATE_CONNECTING,   /* the request is with the driver */
	WLAN_FULLMAC_STATE_ASSOCIATED,   /* joined; the firmware supplicant runs */
	WLAN_FULLMAC_STATE_AUTHORIZED,
	WLAN_FULLMAC_STATE_FAILED
};

struct wlan_fullmac_ops {            /* the station implements these */
	int (*connect)(void *context, uint64_t generation, const struct wlan_fullmac_connect_request *request);
	int (*authorized_set)(void *context, uint64_t generation, int authorized);
	int (*radio_stop)(void *context, uint64_t generation);
};

int wlan_fullmac_engine_init(struct wlan_fullmac_engine *engine, const struct wlan_fullmac_ops *ops, void *context);
int wlan_fullmac_engine_start(struct wlan_fullmac_engine *engine, uint64_t generation, const struct wlan_fullmac_profile *profile, uint64_t now_ticks);
int wlan_fullmac_engine_report(struct wlan_fullmac_engine *engine, uint64_t generation, enum wlan_fullmac_event event, uint64_t now_ticks);
int wlan_fullmac_engine_fail(struct wlan_fullmac_engine *engine, uint64_t generation, int error);
int wlan_fullmac_engine_timer(struct wlan_fullmac_engine *engine, uint64_t now_ticks);
int wlan_fullmac_engine_stop(struct wlan_fullmac_engine *engine);
enum wlan_fullmac_state wlan_fullmac_engine_state(const struct wlan_fullmac_engine *engine);
int wlan_fullmac_engine_last_error(const struct wlan_fullmac_engine *engine);
uint64_t wlan_fullmac_engine_next_deadline(const struct wlan_fullmac_engine *engine);
```

- `start`: profile（BSS、passphrase、全体の期限）を検査し、`wlan_pbkdf2_hmac_sha1`（既存、4096 回、SSID を salt、32 byte）で PMK を作り、PMK と BSS と期限を request に入れて `connect` を呼び、passphrase の写しを消し、**戻ったら request を消す（engine は PMK を持たない）**。成功で CONNECTING、`connect` の失敗で FAILED（その error）。
- `report`: CONNECTING で ASSOCIATED → ASSOCIATED。ASSOCIATED（firmware が 1 つにまとめた時は CONNECTING も）で AUTHORIZED → `authorized_set(1)` が成功なら AUTHORIZED。古い generation は ESTALE、順の外・重複は状態を変えず 0。
- `fail`: link loss の時の印。AUTHORIZED なら `authorized_set(0)`。FAILED と error。
- `timer`: AUTHORIZED の前に全体の期限（`WLAN_CONNECT_DEADLINE_TICKS`、30 s）を過ぎたら FAILED（ETIMEDOUT）。AUTHORIZED の後は何もしない（beacon の監視は firmware）。
- `stop`: checked barrier。authorized なら `authorized_set(0)`、connect を渡していれば `radio_stop`（= driver の `disconnect`）。どちらかが失敗なら error を返して状態を保ち、後で再試行。両方成功で IDLE。
- engine は秘密を持たないので、`wlan_station_test_secrets_clear` の FullMAC の側は「engine と station に PMK・passphrase が残っていない」を確かめるだけ。

#### W6.3 station の箇所の表（p008 で全部を直す、`wlan.c` の行は main 20da95a8f）

station には `fullmac`（attach で決まる mode）と `struct wlan_fullmac_engine fullmac_engine` を足し、engine を読む箇所は次の小さな関数を通す: `station_engine_idle()`・`station_engine_failed()`・`station_engine_stop()`・`station_engine_timer()`・`station_sync_engine_locked()`・`station_port_authorized_locked()`。SoftMAC の時は今の WPA2 の engine をそのまま呼ぶので振る舞いは変わらない。

| # | 箇所 | 今（SoftMAC） | FullMAC |
| --- | --- | --- | --- |
| 1 | `wlan_station_attach`（:388） | WPA2 の engine の init | ops の mode の検査（混ぜた ops は EINVAL）、`wlan_fullmac_engine_init`、`station->fullmac = 1`。WPA2 の engine も init するが start しない |
| 2 | `wlan_station_report_frame`（:828-1060、:958・:974・:1005・:1017・:1025） | management・EAPOL を engine へ、data を `wlan-l2` で解き `wpa2.authorized` で門 | EOPNOTSUPP（FullMAC の driver は `report_ethernet` と `report_fullmac_event` を使う。scan の frame は `report_scan_frame` で別） |
| 3 | `wlan_station_report_tx_complete`（:1108-1115） | engine の cookie、data の誤りの数（`wpa2.authorized`） | EOPNOTSUPP（data の送りの誤りは driver が `net_device_tx_error` で数える） |
| 4 | `wlan_station_transmit`（:1166-1187） | `wpa2.authorized`・`controlled_port`・`frame_transmit`、PN と cookie、MPDU | 分岐: `station_port_authorized_locked` と `controlled_port` を lock の下で見て generation を取り、lock の外で `ethernet_transmit`（PN・cookie・MPDU なし）。古い generation は driver が捨てる |
| 5 | `wlan_station_report_link_loss` → `station_link_lost_controlled`（:3660・:3689・:3695） | engine IDLE で ENOTCONN、`wlan_wpa2_engine_stop`、`connect_driver_active` で retire | `station_engine_idle`・`wlan_fullmac_engine_fail` の後 `station_engine_stop`。BUG-157 の「最初の理由を保つ」は同じ |
| 6 | 新しい `wlan_station_report_fullmac_event` | — | control gate を取り、generation を確かめ、`wlan_fullmac_engine_report`、`station_sync_engine_locked`。SoftMAC の station では EOPNOTSUPP |
| 7 | 新しい `wlan_station_report_ethernet` | — | generation・`controlled_port`・`fullmac_engine` の AUTHORIZED を確かめ、EAPOL を捨て、`packet_buf` に写して `net_device_receive`。SoftMAC では EOPNOTSUPP |
| 8 | `station_beacon_watch_active_locked`（:2626） | `connect_driver_active` と state | FullMAC は常に 0（firmware が beacon を見て DEAUTH・LINK の event を出す）。sync が `beacon_watch_deadline` を立てないので二重の守り |
| 9 | `station_finish_connection_retire_locked`（:2850） | `connect_driver_active` などを消す | 変えない |
| 10 | `station_sync_wpa_locked`（:2955-3017、report_frame・tx_complete・timer・start から） | engine の flag を station に毎回上書きし、状態を写す | `station_sync_engine_locked` が mode で分ける。FullMAC の写し: CONNECTING → ASSOCIATING、ASSOCIATED → FOUR_WAY（`authenticated`・`associated`）、AUTHORIZED → CONNECTED（`key_installed`・`controlled_port` = 1、`terminal_error` 0、`connection_deadline` 0、beacon の監視は立てない）、FAILED → FAILED（`last_error`、`connect_stop_pending`、retry 1 tick）。`controlled_port` は FullMAC の engine の AUTHORIZED から毎回写す（WPA2 の時と同じく engine が正本） |
| 11 | `station_wpa_radio_start`（:3087） | `connect_start`、成功で `connect_driver_active = 1` | FullMAC の engine の `connect` の実体 `station_fullmac_connect`: 選んだ BSS と generation を確かめ `ops->fullmac_connect`、成功で `connect_driver_active = 1` |
| 12 | `station_wpa_transmit`・`key_install`・`keys_activate`・`key_receive_pn_advance`・`key_delete`（:3107-3500） | SoftMAC の鍵と frame | 呼ばれない（FullMAC では WPA2 の engine を start しない） |
| 13 | `station_wpa_authorized_set`（:3572） | 鍵が両方 install されていれば carrier を上げる | FullMAC の `authorized_set` の実体: generation を確かめ `net_device_set_carrier`（鍵の検査は無い。firmware の AUTHORIZED が根拠）、下げる時は `station_carrier_down_locked` |
| 14 | `station_wpa_radio_stop`（:3618） | `disconnect`、成功で `connect_driver_active = 0` | 同じ関数を FullMAC の engine の `radio_stop` にも使う |
| 15 | `ioctl_scan`（:4214） | `connect_driver_active` | 変えない |
| 16 | `ioctl_connect`（:4453-4493） | `connect_start == NULL` で EOPNOTSUPP、engine が IDLE でなければ EBUSY | mode の op（`fullmac_connect`）で検査、`station_engine_idle` |
| 17 | `station_retire_controlled`（:4538-4691） | `engine_stop_needed`、`wlan_wpa2_engine_stop`、engine が IDLE の時だけ直の `disconnect` の fallback | `station_engine_idle`・`station_engine_stop`。直の `disconnect` の fallback と再試行は同じ |
| 18 | `station_connection_start`（:5026-5092） | profile を作り `wlan_wpa2_engine_start`、passphrase を消す | FullMAC: `wlan_fullmac_profile`（BSS・passphrase・全体の期限）→ `wlan_fullmac_engine_start`、passphrase を消す、sync。engine が IDLE のまま失敗なら FAILED |
| 19 | `station_connection_timer`（:5113-5233） | beacon の監視の期限（`wpa2.connected_lifetime`）、engine の timer（**30 s の期限はここだけ**）、FAILED の engine の stop、IDLE で retire | beacon の監視は無し、`station_engine_timer`（FullMAC の engine の 30 s）、FAILED の stop と IDLE の判定は `station_engine_*` |
| 20 | test の関数（:2083-2459） | `wpa2` を直に作る・読む | `secrets_clear` は FullMAC の側も見る。`seed_authorized`・`begin_*_rekey`・`set_initial_phase`・`complete_authorized`・`transmit_eapol` は SoftMAC 専用（FullMAC では EOPNOTSUPP）。snapshot は `wpa_state` に FullMAC の engine の状態を入れ、`reserved` の 1 byte を `fullmac` の印にする |
| 21 | report の scan の関数・`wlan_station_report_link_loss` の入口・open・close・quiesce・detach・restart | — | 変えない（scan は SoftMAC と同じ段、lifecycle の約束は同じ） |
| 22 | `include/kern/net/wlan.h` の comment | 「Drivers cannot publish AUTHORIZED directly」 | W6.1 の FullMAC の文を足す |

- UAPI の status の state（IDLE・SCANNING・ASSOCIATING・FOUR_WAY・CONNECTED・FAILED…）は SoftMAC と同じ値なので networkd・`/sbin/wifi`・Settings は変えない。
- **方針の変更**（Q1 が master に記録）: `plan/master-design-policy.md` の「WLAN protocol-state ownership」（今は core が WPA2 の鍵・rekey と controlled port を持つ）に、「FullMAC の chip では firmware が 4-way・鍵・rekey を持ち、port を開く根拠は firmware の AUTHORIZED。core は FullMAC の engine で状態・期限・carrier を写し、port の開閉そのものは今も core が行う」を足す。
- 見積り: engine 約 400 行、station の分岐と report の関数 約 500 行、host の試験 約 800 行。**p008 は 6〜8 LW**。

### W7. firmware の supplicant と接続の流れ（E1、S3・S4）

- **接続は非同期**（S3）: `fullmac_connect` は driver の control の mutex の下で connect の slot に generation・BSS・期限・PMK を写し、bus の thread を起こして**すぐ 0 を返す**（slot が別の generation で使用中なら EBUSY。core は前の接続を retire してから次を始めるので通常は起きない）。
- **bus の thread が ioctl の列を自分で出す**（他の thread は待たない）: `WLC_SET_INFRA` 1、`WLC_SET_AUTH` 0（open）、`WLC_SET_WSEC` 4（AES）、`WLC_SET_WPA_AUTH` 0x80（WPA2-PSK）、`bsscfg:sup_wpa` 1、`WLC_SET_WSEC_PMK`（PMK を 64 文字の 16 進、flag は passphrase の形）、`WLC_SET_SSID`（SSID と、assoc の params に BSSID と chanspec）。各 ioctl は 500 ms の期限で応答を待つ（bus の thread 自身が F2 を読むので待ちは閉じる）。途中の失敗は残りを出さず、`WLC_DISASSOC` で半端な設定を戻し、接続の latch に「失敗（EIO または ETIMEDOUT）」を立てる。
- **PMK の消去**: connect の slot の PMK は `WSEC_PMK` を送った直後に消す。BCDC の組み立ての buffer と SDPCM の送りの buffer（frame の写し）は CMD53 で書いた直後、失敗・timeout・取り消し（`disconnect`・close・restart・generation の変化）の経路でも消す。log に PMK・passphrase を出さない。host 試験で偽の bus を使い、全ての経路の後に buffer が 0 であることを確かめる。
- **接続の event は generation ごとの latch**（ring ではない、満杯が無い）: `{generation, associated, authorized, lost, lost_error}`。bus の thread が ring の spinlock の下で bit を立て、`net_device_schedule_poll`。`poll_receive` が `associated` → `authorized` → `lost` の順に `report_fullmac_event`・`report_link_loss` を出す。latch の generation が connect の slot の generation と違う event は捨てる。
- **firmware の event → 状態**: SET_SSID の status 0 と LINK up → ASSOCIATED、PSK_SUP の keyed（完了）→ AUTHORIZED。group key の更新は firmware が行う。
- **失敗の errno の表**（S4。core の SoftMAC の写しに合わせる。PSK_SUP の status と reason の数は p004 の values.md で WHD から確定し、表を host 試験にする）:

  | firmware の印 | errno | 理由 |
  | --- | --- | --- |
  | connect の ioctl の失敗・応答なし | EIO・ETIMEDOUT | 機器の側の失敗 |
  | SET_SSID が「network が無い」（NO_NETWORKS） | ETIMEDOUT | SoftMAC で AP が答えない時と同じ |
  | SET_SSID・AUTH・ASSOC の status が失敗（AP が断った） | ECONNREFUSED | SoftMAC の auth・assoc の拒否と同じ |
  | SET_SSID の status が timeout | ETIMEDOUT | |
  | PSK_SUP の失敗で、最後の supplicant の状態が M1 を待つ所（M1 が来ない） | ETIMEDOUT | 鍵の誤りの証拠は無い |
  | **M2 を送った後（最後の状態が M3 を待つ所）に、M3 の timeout、または DEAUTH_IND・DISASSOC_IND、または AP の deauth の reason 15（4-way の timeout）** | **EACCES** | AP は M2 の MIC を確かめられなかった = 鍵の誤り（SoftMAC の `wlan.c:963-966` と同じ規則） |
  | PSK_SUP のその他の失敗（IE の不一致、cipher、GTK の解けない…） | EPROTO | 鍵の誤りの証拠ではない。networkd は再試行してよい |
  | AUTHORIZED の後の DEAUTH_IND・DISASSOC_IND・LINK down | ECONNRESET | 普通の切断 |
  | firmware の trap・bus の error | EIO | restart も頼む |
  | 30 s の全体の期限（core の FullMAC の engine） | ETIMEDOUT | |

  driver は PSK_SUP の最後の状態を接続ごとに覚えて上の表を引く。EACCES は networkd に profile の再試行を止めさせるので、証拠の無い失敗に使わない。
- 範囲の外: WPA3（SAE）・PMF・WPA1・enterprise・省電力（PM）・roaming（`roam_off` 1）。
- **`idsup` の確かめ**: 版の文字列と `cap` の iovar の両方に `idsup` があることを bring-up で確かめ、無ければ log に出して connect を EOPNOTSUPP で断る（scan は使える）。

### W8. probe・wlan0 の公開・lifecycle（M3・M4・M6）

- **設定**: `config/drivers/architecture/arm64.drivers` に `CONFIG_DRIVER_BCM2711_WIFI|bool|BCM43455 SDIO: Raspberry Pi 4 onboard WiFi|rpi4|y|`（既定 y。firmware の package が無ければ wlan0 を作らないだけ）、`Makefile`・`platform/arm64/vmunix.mk` に GENET と同じ形で。
- **boot の probe は同期の部分を小さく**（M6）: `kern_platform_refresh_devices` が `drv_rpi4_wifi_probe(fdt_phys)` を呼ぶ。そこでは W2 の「probe しない条件」を DT と getter で調べ、instance を alloc して driver の thread を 1 つ作るだけ（register・mailbox・電源に触らない）。
- **driver の thread**（publish の thread）:
  1. `drv_rpi4_firmware_init(fdt)`（何度呼んでも安全、8155669d4）。ENODEV なら log に出して終わる。
  2. pin（W3）、host の初期化、`drv_rpi4_mmc_irq_add`、電源の pulse、SDIO の enumeration、chip id（W4 の 1〜3）。
  3. root の mount を待つ: `mount_root_get_ref()` を `waitq_sleep` の 1 s の deadline ごとに見て上限 300 s。各回に `stopping` を見る。
  4. **firmware の 3 つの file を driver の lock を何も持たずに読む**（M4: file の読みは INODE 60 などの rank の lock を取るので、DEVICE 125 の driver の mutex の下で読まない）。kmalloc の buffer へ、大きさと形を確かめる（下）。
  5. W4 の bring-up を 1 度行い、MAC・版・`cap`・有効な channel を読み、`WLC_DOWN`、電源を落とす（WL_ON 0）、file の buffer を放す。
  6. **MAC の検査**: 全て 0、multicast の bit、NVRAM の `macaddr` の値と同じ（全ての Pi に共通の仮の値）のどれかなら log に出して公開しない。
  7. **公開**: lifecycle の mutex を取り、`stopping` でなければ `net_device_create`（MAC 付きの `wlan0`、空いている `wlanN`）と `wlan_station_attach` を行い、mutex を放す。USB の WiFi の dongle が後から現れるのと同じ形で、networkd は後から来た interface を扱う（p006 で確かめる）。
- **shutdown との直列**（M3）: kernel の shutdown・`wlan_station_shutdown_all` の前に driver が呼ばれる口（AX211 の detach と shutdown の経路と同じ口を p004 の最初に確かめる）で、lifecycle の mutex の下に `stopping` を立て、publish の thread を起こし、終わりを待つ。publish の thread は 3 の待ちと 7 の前に `stopping` を見て止まる。`wlan_station_attach` の「net_device_gone・破棄・shutdown と直列」の約束は lifecycle の mutex で守る。
- **公開の前に止まる時の後始末**（package が無い、file の形が違う、bring-up の失敗、`stopping`）: log に 1 度出し、`drv_rpi4_mmc_irq_remove`（signal enable を下ろした後）、host の reset、電源を落とす（WL_ON 0）、instance を放す（claim を返す）。wlan0 は無い。
- **open**（Settings の Wi-Fi を入れる、`wifi up`）: file を lock の外で読み直し（約 550 KB）、電源の pulse から W4 の bring-up を全部行い、bus の thread を始め、`wlan_station_open`。**close**: 新しい要求を止める → `WLC_DOWN` → bus の thread に停止を頼み終わりを待つ → ring・latch・slot を空にし PMK を消す → 電源を落とす。restart（firmware の trap）も同じ手順。
- **file の確かめ**: 大きさが package の値と同じ、firmware の版の文字列（`43455c0-roml/` で始まる）を含む、NVRAM は text、CLM は先頭が `BLOB`。digest の照合は build の時の package の Makefile が行う（kernel に SHA-256 の共通の実装が無く、AX211 の自前の物を写さない）。
- 長い待ちは全て `waitq_sleep` の deadline 付き（yield）。`kern_usleep_range` は µs の register の待ちだけ。

### W9. firmware の BLOB の入手と license（`userland/firmware/bcm43455/`、package `bcm43455-firmware`）

2026-10-13 に上流を読んだ結果（file の中身は scratchpad に取得して確かめ、tree には入れていない）:

| 候補 | file | 版・中身 | license |
| --- | --- | --- | --- |
| linux-firmware（AX211 と同じ commit `dc85ccedc9c973682fbcf4d628ca61174bcc3120`） | `cypress/cyfmac43455-sdio.bin`（643651 B、SHA-256 `d408faa9…3edd`）、`.clm_blob`（4733 B、`15f50a27…d460`、"Cypress.CYW43455"） | 7.45.234（2021-04-15）、feature に `idsup`・`sae` | LICENCE.cypress（Cypress の binary の再配布の EULA） |
| 同 | `brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt`（1883 B、`edb6f4e4…2748`） | Pi 4 の NVRAM | **GPL-2.0+**（WHENCE「Licence: GPLv2」、file の先頭 `SPDX-License-Identifier: GPL-2.0+`、(C) Raspberry Pi (Trading) Ltd.） |
| Raspberry Pi の `RPi-Distro/firmware-nonfree`（branch trixie、commit `3bab0f823f5b53150b76aab77093adef6655b920`、2026-07-10） | `debian/added-firmware/cypress/cyfmac43455-sdio-minimal.bin`（548728 B、`3075cb0b…6e60`） | 7.45.241（2021-11-01）、feature に **`idsup`・`idauth`**、SAE なし | `debian/copyright` の `Files: debian/added-firmware/*/*43455*` → binary-redist-Cypress |
| 同 | `cypress/cyfmac43455-sdio-standard.bin`（609309 B、`d608f866…36c3`） | 7.45.265（2023-08-29）、`extsae`、**`idsup` なし** | 同上 |
| 同 | `cypress/cyfmac43455-sdio.clm_blob`（2676 B、`9823842c…a36e`、"RPi.4"） | Raspberry Pi の CLM | 同上 |
| 同 | `brcm/brcmfmac43455-sdio.txt`（2074 B、`ca709be8…9e3d`、Pi 4 の名前の file はここへの symlink） | Pi 4 の NVRAM。linux-firmware の GPL の物と値は同じ（違いは `NVRAMRev` の行と `btc_mode=1` の重複だけ） | 同上 |

**推し（E4）**: 3 つとも firmware-nonfree の同じ commit から: `cyfmac43455-sdio-minimal.bin`（`idsup` を持つ。standard は持たないので E1 と合わない）、`cyfmac43455-sdio.clm_blob`、`brcmfmac43455-sdio.txt`。予備: minimal が実機で不具合なら linux-firmware の 7.45.234（`idsup` あり）と Cypress の CLM。

- **NVRAM の license の確かめ**（2026-10-13 第 3 版で `debian/copyright` を取り直して読んだ。commit 3bab0f82、8505 行、SHA-256 `07082fd0…0e30`）:
  - `Files: debian/*`（210 行目、GPL-2+、Debian の packaging の著作者）と `Files: debian/added-firmware/*/*43455*`（597 行目、Cypress、binary-redist-Cypress）の**両方が**推しの file `debian/added-firmware/brcm/brcmfmac43455-sdio.txt` に当たる。DEP-5（machine-readable copyright format 1.0）は「同じ file に複数の `Files` が当たる時は**最後の**段が効く」なので、後の 597 行目の Cypress が効く。review の「順序が未確定」は、file の中の順で決まっていた。
  - GPL の段（`Files: … brcm/brcmfmac43455-sdio.*.txt …`、(C) 2018 Linaro・Raspberry Pi、**GPL-2**）は repo の root の `brcm/` の、名前に `.<board>.txt` を持つ file だけに当たり、推しの file には当たらない。
  - **残る不確かさ**: 推しの file は Cypress の reference の NVRAM（先頭「Cloned from bcm94345wlpagb_p2xx.txt」）と名乗るが、値は linux-firmware の GPL の Pi 4 の NVRAM と同じ（行の順と 2 行だけ違う）。どちらが元かを公開の資料からは決められない。
  - review の意見: どちらの NVRAM を使っても、package に GPL-2 の text と出所（両方の上流の URL・commit・file の対応）を同梱しておくのが安全。NVRAM は chip に書く data で、kernel にも zedBSD の code にも link しない。
- install 先: `/lib/firmware/cypress/cyfmac43455-sdio-minimal.bin`、`/lib/firmware/cypress/cyfmac43455-sdio.clm_blob`、`/lib/firmware/brcm/brcmfmac43455-sdio.txt`（上流の名前のまま）、`/usr/share/licenses/bcm43455-firmware/copyright`（`debian/copyright` の全文）、E4 の答えによって GPL-2 の text、manifest。`tools/release/license-components.json` に `bcm43455-firmware` の行、`userland/firmware/README.md` の一覧に `bcm43455/` を足す。
- 取得は `https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/<commit>/debian/added-firmware/...`（commit で固定）。大きさと SHA-256 を Makefile に固定し、AX211 の Makefile と同じ形。既定は無効、Pi 4 の image の config で選ぶ。
- Cypress の EULA: object code の形で「Cypress の IC と共に使う目的に限り」再配布してよい、改変・逆 engineering の禁止。driver は file を読んで chip に書くだけ（版の文字列の検索と NVRAM の text の変換だけ。file は改変しない）。
- Bluetooth の patchram（`.hcd`）は WS215 の別の package。

### W10. 値の出典と license の扱い

- zedBSD の code は Zlib。**Linux の brcmfmac・mmc・pinctrl・raspberrypi の GPIO の driver は写さない**（Q1 の指示。guardrail の GPL の参照の扱いは WS141・WS037・WS214 だけ）。
- 値の出典は公開の文書と寛容な license の source に限る:
  - SD Association: SDIO Simplified Specification、SD Host Controller Simplified Specification。
  - Raspberry Pi: BCM2711 ARM Peripherals（GPIO・pull）、firmware の mailbox の property の wiki、DT の source と binding。
  - Infineon（Cypress）: CYW43455 の datasheet、**wifi-host-driver（WHD、Apache-2.0）**（`sup_wpa`・`WSEC_PMK`・BCDC・iovar・event・PSK_SUP の status と reason）。
  - **OpenBSD の bwfm（ISC）**（backplane の窓・chip clock の CSR・core の id・RAM の base・SDPCM・BCDC・escan・CLM・chanspec）。bwfm は host の supplicant なので supplicant の部分は WHD。
  - FreeBSD の `bcm2835_sdhci.c`（BSD）: 旧 EMMC の癖（R2）。
- ISC・Apache の code も**字面を写さない**。値だけを使い、名前・配置・comment は自分で書く。p004 の最初に値ごとの出典の表 `plan/ws206/phase004/values.md`、WS の最後（p007）に字面と構造の類似を確かめる。

### W11. 試験

- **host**（QEMU を使わない、実装の担当が流す）:
  - p002a（実機は無し、p002b で使って確かめる）: GPIO の register の位置と bit（pin 0〜57、範囲の外）、pull の 2 つの変換（列挙型 → register の符号、DT の数 → 列挙型、生の数の EINVAL）、mailbox の GPIO の helper（line → 128 + line、列挙型の外の EINVAL、EBUSY の再試行の上限）。偽の register と偽の property で。
  - p002b: dispatcher（2 slot、EBUSY、EINVAL、remove の後に呼ばれない）、clock の分周（base 250・200・100 MHz で 25 MHz と 400 kHz、版 2.00 の 2 の冪）、card interrupt の enable の書き直し（reset の後）。
  - p004・p005: SDPCM・BCDC・iovar の encode と decode、送りの窓（seq の wrap、`& 0x80` の guard）、event の解き（big-endian、長さ、壊れた入力）、escan の bss_info → probe response → `wlan_frame_parse_bss`（WPA2-PSK・CCMP の bit）、chanspec の control の channel（20・40・80、2.4・5、`ctl_ch` との一致）、NVRAM の変換と位置（長さの語が最後の 4 byte）、CLM の分割、**scan の report の順（ready → 結果 → complete、ready の前の結果を出さない）**、**errno の表**、**接続の latch（満杯が無い、古い generation の破棄、順）**、**connect が待たずに返ること（偽の bus を遅くしても）**、**PMK の消去（成功・ioctl の失敗・timeout・disconnect・close の後に slot・BCDC・SDPCM の buffer が 0）**、bring-up の手順（偽の backplane で chip id・RAM の address・NVRAM の位置・reset vector・CLM の chunk）、MAC の検査。
  - p008: FullMAC の engine（状態の遷移、順の外の event、期限、stop の barrier と再試行、PMK を持たない）、station の表の全ての行（ops の検証、scan、connect、PMK が IEEE 802.11 の付録の PBKDF2 の試験の値と一致、ASSOCIATED → FOUR_WAY → CONNECTED と carrier、送受の門、link loss と errno、disconnect、close・quiesce・detach、SoftMAC の入口の EOPNOTSUPP）。既存の SoftMAC の host 試験（`plan/ws005/tests/host-wlan-retire.sh`）が変わらず通ること。
- **QEMU**（T1）: raspi4b の `boot-test.sh` の起動の回帰（0xfe300000 の SD の fallback を WiFi が壊さない。p002b と p004 の後）。
- **Pi 4 の実機**（T1、Pi は P3 などと共有なので Q1 が調停）: p002b で GPFSEL3 と pull の読み（helper の get）、WL_ON の状態・pulse・CMD5・CCCR・CIS（0x02d0）と Pi の起動の回帰、p004 で chip id・版・MAC・wlan0 の出現、p005 で `wifi search`・`wifi list`、p006 で WPA2 の接続・DHCP・ping・Settings・切断と再接続・Wi-Fi の切と入、誤った passphrase で EACCES、可能なら Bluetooth（WS215）と同時。証拠は SSH の `dmesg`・`/sbin/wifi` の出力と画面の PNG。serial の log では判定しない。
- 試験の file は `plan/ws206/tests/`。回帰に残す物は WS の終わりに Master の Tools へ（Q1 に依頼）。

### W12. SMP（4 core）での並行と lock

- **bus の thread が SDIO を独り占め**: 1 つの device に 1 つの kthread。SDIO の command は全てこの thread か、bring-up（open・restart の中、bus の thread がまだ・もう動いていない時）だけが出す。host の instance の mutex は二重の守り。
- **IRQ**（SPI 126、CPU 0）: W2 の handler だけ。SDIO の I/O・alloc・core の呼び出しはしない。
- **control の要求**（radio の op、どの CPU からでも）: driver の control の mutex で 1 つずつ slot に置き、bus の thread を起こす。`fullmac_connect`・`ethernet_transmit`・`scan_channel_start` は待たずに返る。待つのは barrier の op（`scan_stop`・`disconnect`・`quiesce`）だけで、期限は driver の上限（500 ms〜1 s）。超えたら ETIMEDOUT（core が再試行）。**bus の thread は決して network worker・core の control gate・ring の空きを待たない**ので、op が bus の thread を待っても閉じる。
- **送り**: `ethernet_transmit` は ring の spinlock の下の有限の ring（64）に写す。bus の thread は送りの窓の分だけ F2 へ書く。
- **受け**: bus の thread は F2 の frame を読み、data は ring（128）、scan の結果は ring（32）、**接続の event は latch**（W7）、control の応答は control の slot へ。data と scan の ring が満杯なら捨てて数える（bus の thread は止まらず読み続ける）。**core の report の関数は `poll_receive`（network worker）からだけ呼ぶ**（scan の ready・complete も、順序を 1 つにするため）。
- **lock の表**（M4）:

  | lock | 種類・rank | 守る物 | 中で取ってよい物 |
  | --- | --- | --- | --- |
  | lifecycle の mutex | mutex、`LOCK_RANK_DEVICE` | 公開・shutdown・open・close | control の mutex、ring の spinlock |
  | control の mutex | mutex、`LOCK_RANK_DEVICE` | 要求の slot（connect・scan・barrier） | ring の spinlock |
  | ring の spinlock | irqsave の spinlock、`LOCK_RANK_DEVICE` | 送りと受けの ring、接続と scan の latch | 無し（leaf） |
  | SPI 126 の dispatcher の spinlock | irqsave、`LOCK_RANK_DEVICE` | slot の表 | host の irq の spinlock |
  | host の irq の spinlock | irqsave、`LOCK_RANK_DEVICE` | 0x34・0x38 の read-modify-write、`card_irq_pending` | 無し |
  | host の mutex | mutex、`LOCK_RANK_DEVICE` | SDHCI の command の列（二重の守り） | host の irq の spinlock |
  | GPIO の spinlock | irqsave、`LOCK_RANK_DEVICE` | GPFSEL・pull の read-modify-write | 無し |

  順: （file の読み・mailbox は lock を持たずに）→ core の control gate → lifecycle の mutex → control の mutex → ring の spinlock。driver の lock を持ったまま core の関数・`file_openat`・`drv_rpi4_firmware_property` を呼ばない。rank は lock.c で実行時に検査されないので、この順は review と host 試験の lock の順の記録で守る。新しい rank は足さない。
- 共有の資源: GPFSEL3 と pull の REG2（GPIO の helper）、mailbox（helper と既存の claim）、SPI 126（dispatcher）。
- lifecycle: close・detach は「新しい要求を止める → thread に停止を頼み終わりを待つ → ring と latch を空にし PMK を消す → 電源を落とす」。core の `stop_request`・`stop_complete`・`quiesce_begin/end` の約束は既存の driver と同じ。

### W13. Bluetooth（WS215、同じ chip の UART）との共存

- 経路は別: WiFi は SDIO（GPIO 34〜39）、Bluetooth は PL011 の UART0（GPIO 30〜33 の ALT3）。電源の線も別（拡張の line 1 WL_ON、line 0 BT_ON）。どちらが先に起きてもよい。
- 無線の共存は chip の firmware が NVRAM の `btc_mode=1`・`btc_params*` で行う。v1 では `btc_mode` の iovar を変えない。
- driver の間で共有するのは W3 の 2 つの helper だけ（持ち主 ws206-p002a、ws215-p003 が依存）。SPI 126 の dispatcher は SD の controller の物で、BT（UART）は使わない。
- `config.txt` の `dtoverlay=disable-bt` は UART の割り当てだけを変え、WiFi には関係しない。
- 試験: WS215 が先に入っていれば p006 で、Bluetooth の keyboard を使いながら WiFi で ping・DHCP の更新を確かめる。

### W14. 危険と未確認

| ID | 危険 | 対策 |
| --- | --- | --- |
| R1' | `rpi4-sdhci.c` と `rpi4-sdio-host.c` に SDHCI の手続きが 2 つ（約 200 行の重複） | 第 3 版で R1（共有の層）をやめた代わりの危険。BUG-295 が落ち着いた後に Future Work の候補として Q1 に登録を頼む。getter の追記は BUG-295 の統合の後 |
| R2 | 旧 EMMC（Arasan）の癖: 低い clock で書き込みの間の待ち、capabilities が当てにならない | 待ちは常に入れる（約 5 µs）。base clock は mailbox。実機で確かめる |
| R3 | WL_ON の初めの状態と reset の pulse の要否、CMD5 までの待ち | p002b の最初に `drv_rpi4_firmware_get_gpio` で読む。datasheet の時間 |
| R4 | minimal の firmware と RPi の CLM の組み合わせの不具合 | 予備の linux-firmware 7.45.234 と Cypress の CLM |
| R5 | PIO の速さ・CPU | v1 は機能の確認。足りなければ 50 MHz と DMA |
| R6 | 規制（country）の既定 | E6 |
| R7 | RTL8822BU の dongle と同時の時の wlan0・wlan1 | v1 は内蔵だけを試験。同時は制限として記録 |
| R8 | 43455 の save-restore・KSO（M7） | v1 は HT の force（推測）。p004 で firmware と chip の SR の能力を読み、要れば KSO を足す |
| R9 | card interrupt の取り落とし（level・enable の扱いの誤り） | 100 ms の poll の保険。p004 で割り込みの数と poll で拾った数を log に出して比べる |
| R10 | `drv_rpi4_firmware_property` の EBUSY（GPU・audio・BT と同時） | helper の中で 50 ms まで再試行 |

## Phase（段の分け、ws.md の表と同じ）

| Phase | 内容 | 見積 | 依存 |
| --- | --- | --- | --- |
| p001 | この設計 → design-reviewer → E2・E4・E6 の回答 | — | — |
| p002a | **GPIO の helper**（`rpi4-gpio.c`）と **mailbox の GPIO の helper**（`rpi4-firmware.c` に追記）、`kern_platform_init` から GPIO の init、host の試験と build（warning 0）。実機の確かめは使い手が入る p002b（と ws215-p003）で。**ws215-p003 がこの Phase に依存** | 1.5 LW | p001 |
| p002b | SDIO の host（`rpi4-sdio-host.c`、独立）、SPI 126 の dispatcher（`rpi4-mmc-irq.c`）、`drv_rpi4_sdhci_base()` の追記、SDIO の card の層と probe（`rpi4-sdio.c`、DT・QEMU の guard・claim・thread の骨）、pin と電源、`CONFIG_DRIVER_BCM2711_WIFI`、host の試験。Pi で WL_ON の pulse・CMD5・CCCR・CIS、Pi と QEMU の起動の回帰（T1） | 5 LW | p002a、BUG-295 の `rpi4-sdhci.c` の変更が main に入った後（getter の追記の衝突を避ける） |
| p003 | firmware の package `bcm43455-firmware`（W9）と license の監査、Pi 4 の image の config | 2 LW | p001、E4 |
| p004 | Broadcom の bring-up（W4）、SDPCM・BCDC・event（W5）、firmware の file、bus の thread と lock（W12）、wlan0 の公開と shutdown（W8）、値の出典の表、host の試験。Pi で chip id・版・MAC・wlan0（T1） | 10 LW | p002b、p003 |
| p008 | WLAN の core の FullMAC の engine と station の分岐（W6 の表の全部）と host の試験 | 6〜8 LW | p001（p004 と並べて進められる） |
| p005 | driver の radio の op（scan と escan の順、非同期の connect と firmware の supplicant、errno の表、data、link loss、restart、W5・W7）、host の試験。Pi で scan（T1） | 5 LW | p004、p008 |
| p006 | 実機の受け入れ（T1）: WPA2・DHCP・ping・Settings・切断と再接続・Wi-Fi の切と入・誤った passphrase、可能なら Bluetooth と同時 | — | p005、E2 |
| p007 | 規約の全文の見直しと値の出典の監査 | 1 LW | p006 |

計 約 31〜33 LW。p008 は番号の上では後だが p004 と並べて進められる（Phase の番号は付け直さない）。

## 判断（Q1 経由でユーザーへ。問いは平易に、推しと review の意見付き）

| ID | ユーザーへの問い | 推し | review の意見 |
| --- | --- | --- | --- |
| E2 | Pi 4 の WiFi の試験に、repo の root の `.wifi` に書いてある AP（名前と passphrase）を使ってよいですか。その AP は 2.4 GHz と 5 GHz のどちらで出ていますか（両方なら両方で試します）。 | `.wifi` を使う。試験の担当 T1 だけが試験の時に読み、passphrase は SSH の標準入力で Pi に渡し、log・plan・画面の記録に出さない。P1 は中身を読んでいない | 賛成（2.4 と 5 GHz の両方が望ましい） |
| E4 | WiFi の chip に読ませる設定の file（NVRAM）は、同じ中身の file が 2 つの出所で違う license を名乗っています。Raspberry Pi の配布物では Cypress の再配布の license、Linux の firmware 集では GPL-2 です。(1) Raspberry Pi の配布物の file を Cypress の license として使ってよいですか。(2) 念のため、どちらの file を使う場合も、package に GPL-2 の全文と両方の出所の記録を一緒に入れてよいですか。 | (1) はい（Raspberry Pi の配布物の file、firmware 本体と CLM と同じ出所、W9 で license の記録の順も確かめた）。(2) はい（GPL-2 の全文と出所の記録を同梱）。代わりの案は、NVRAM を package に入れずユーザーが自分で置く | DEP-5 の順を確かめよ（→ 第 3 版で確かめた: 後の Cypress の段が効く）。どちらの NVRAM でも GPL-2 の text と出所の同梱が安全。firmware 本体と CLM は Cypress の EULA で可。判断はユーザー |
| E6 | 無線の国の設定は、最初は firmware の既定（どの国でも許される範囲で動き、5 GHz の一部の channel は受けるだけ）のままでよいですか。日本の設定（JP）を既定にするのは、Pi の規制の表が JP に対応していることを実機で確かめた後にします。 | 既定のまま始め、JP は確かめた後に口を足す | firmware の既定に賛成。JP を既定にするのは、RPi の CLM に JP と rev があるか確かめるまで反対 |

Q1 の判断でよい技術の点（ユーザーに聞かない）: W8 の wlan0 の後からの公開（net device の API を変えない）、W2 の独立の SDIO の host と重複の承知（R1'）、W6 の FullMAC の engine の形、W12 の並行と lock の形。

## Pi 4 の実機の読み（2026-10-11 P1、読むだけ、10.0.30.2、beta2 82f2dfc）

- boot: VideoCore の firmware（`start4.elf`・`fixup4.dat`）、`config.txt` は `device_tree=bcm2711-rpi-4-b.dtb`、`dtoverlay=disable-bt`（BT の UART を外すだけ、WiFi は残る）。`/lib/firmware` は `rtw88` だけ（brcmfmac の file は無い）。dmesg: eMMC2 の sdhci は 0xfe340000（caps 45ee6432、version 1002）、mailbox の firmware は動く（tag 30066 は非対応の log）。
- DT（`bcm2711-rpi-4-b.dtb` を dtc で読んだ）: `mmcnr@7e300000`（ARM 0xfe300000）、`compatible = "brcm,bcm2835-mmc", "brcm,bcm2835-sdhci"`、割り込み SPI 0x7e、clock `<&clocks 0x1c>`、DMA channel 11、`bus-width = 4`、`non-removable`、`brcm,overclock-50 = 0`、子の `wifi@1`（`brcm,bcm4329-fmac`）。pin: `sdio_pins` = GPIO 34〜39、function 7（ALT3）、pull は CLK なし・他は pull up。**WL_ON は firmware の GPIO の拡張の 2 本目（`gpio-line-names` の "BT_ON", "WL_ON", …、mailbox の GPIO 129）**。この DT には `mmc-pwrseq` の node が無い。
- 2026-10-13: Pi は SSH に応答しない（No route to host、止まっている）。Raspberry Pi の kernel の DT の source（`rpi-6.12.y` の `bcm2711.dtsi`・`bcm2711-rpi-4-b.dts`・`bcm271x-rpi-bt.dtsi`）で確かめた: eMMC2 と旧 EMMC はどちらも GIC SPI 126、Pi 4 の dts は `wifi-pwrseq` を `/delete-node/` で消す（上の観察と一致）、Bluetooth は UART0 の GPIO 30〜33 と `shutdown-gpios = <&expgpio 0>`。

## 未確認（p002a・p002b の最初に、kernel の中で）

GPIO の DT の `reg` の大きさ、GPFSEL3 と pull の REG2 の今の値、拡張の WL_ON・BT_ON の今の状態、`SET_GPIO_STATE` の応答の形、clock id 1（EMMC）の rate、0xfe300000 の SDHCI の capabilities と版、R2 の書き込みの待ちの要否、BCM43455 の chip の rev、F2 の block size、firmware の SR の有無（p004）。

## 記録（2026-10-13 P1）

- 読んだ tree: `src/kern/net/wifi/wlan.c`（ioctl_connect・station_connection_start・report の関数の gate）、`include/kern/net/wlan.h`・`net-device.h`・`irq.h`、`src/drivers/platform/rpi4/rpi4-sdhci.c`・`rpi4-firmware.c`・`rpi4-audio.c`、`src/kern/platform/rpi4.c`、`src/drivers/wifi/intel-ax211/`（publish と firmware の file）、`userland/firmware/`（README・intelax211）、`userland/base/networkd/`（wifi-child・managed-lan）。
- 取得して確かめた上流（scratchpad、tree に入れていない）: linux-firmware の `WHENCE`（425450 B、AX211 の manifest の SHA-256 と一致）・`LICENCE.cypress`・`cypress/cyfmac43455-sdio.bin`・`.clm_blob`・Pi 4 の NVRAM。RPi-Distro/firmware-nonfree の trixie（commit 3bab0f82）の tree・`debian/copyright`・43455 の 3 つの firmware と CLM と NVRAM（symlink の先も確かめた）、bookworm の NVRAM（trixie と同じ）。版は `strings` の feature の文字列で読んだ。OpenBSD の bwfm（ISC）と Infineon の WHD（Apache-2.0）の該当の箇所で値の出所を確かめた（書き写していない）。
- code は書いていない。QEMU・実機は使っていない。

## 記録（2026-10-13 P6、設計 review の M2: mailbox の初期化の SMP）

- 原因: `drv_rpi4_firmware_init` が `mailbox != NULL` を見てから lock 無しで初期化し、`message`・`message_physical` と `mailbox` を plain store で公開していた。別の CPU から同時に呼ぶと 2 回準備され（page の allocate・map が重なる）、`drv_rpi4_firmware_property` は barrier 無しの `mailbox` の読みで buffer の未初期化の値を見得た。
- 直し（`src/drivers/platform/rpi4/rpi4-firmware.c`・`.h`）: 準備の状態 `firmware_state`（ABSENT→PREPARING→READY）。compare-and-exchange で PREPARING を取った 1 つの caller だけが `prepare_mailbox()` で埋め、READY を release で store。他は busy delay で待つ。失敗は ABSENT に戻して次の呼び出しが再試行。`drv_rpi4_firmware_property` は READY を acquire で load してから `mailbox`・`message` を読む。request の buffer の claim（`busy` の acquire の exchange と release の store、上限 2×polling で EBUSY、`kern_usleep_range` は busy delay）は変えていない。HAL の API は不変。
- 試験: `sh plan/ws206/tests/firmware-smp-host-test.sh <OUT> <DTB>`（本物の driver の source、pthread の 8 CPU、TSan）。race: 準備 1 回・16000 answer、retry: 失敗 1 回・準備 2 回・14000 answer、PASS（3 回）。直す前の source では TSan の data race（init の `mailbox` の書きと property の読み）と準備 8 回で FAIL。WS048 の `firmware-host-test`（ASan/UBSan）200163 checks PASS。rpi4 の vmunix の build は warning 0。QEMU・実機は未実施。

## 記録（2026-10-13 P1、第 3 版）

- 入力: design-reviewer の第 2 版への review（[review-2.md](review-2.md)）と Q1 の判断（F1・M5・M2・S1、その他は全て反映、E2・E4・E6 は問いを平易に）。
- 読み直した tree（main 20da95a8f）: `wlan.c` の WPA2 の engine の全ての参照（W6.3 の表）、`wlan-wpa2.h`、`wlan.h`、`wlan-crypto.h`、scan の report の latch（`:560-600`・`:690-730`）、`station_sync_wpa_locked`・`station_link_lost_controlled`・`station_retire_controlled`・`station_connection_start`・`station_connection_timer`、networkd の EACCES の扱い（`main.c:4650-4668`・`:8125-8145`）、`include/kern/lock.h` と `src/kern/lock.c`（rank は記録だけ）、`include/kern/irq.h`、`rpi4.c` の初期化の順、`rpi4-firmware.c`・`.h`（8155669d4 の後）、`rpi4-audio.c`（GPFSEL4 だけ、pull なし、`emulated_board()`）、HAL の `led.c`・`uart.c` の GPIO の書き込み、`config/drivers/architecture/arm64.drivers`、`userland/firmware/README.md`・`tools/release/license-components.json`、WS215 の p001 の B12。
- 取り直した上流（scratchpad）: firmware-nonfree の commit 3bab0f82 の `debian/copyright`（8505 行、SHA-256 `07082fd0bb65e32c73af39a2292dd6c83f169be7427d338ba1fe34f9b86c0e30`）で `Files: debian/*`（210 行目）と `debian/added-firmware/*/*43455*`（597 行目）の順と、GPL の段（`brcm/brcmfmac43455-sdio.*.txt`）が 1 つだけで推しの file に当たらないことを確かめた。
- code は書いていない。QEMU・実機は使っていない。
