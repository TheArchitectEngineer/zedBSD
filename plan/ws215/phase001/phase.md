<!-- awesome-plan project=zedbsd record=ws215-p001 -->

# ws215-p001: Raspberry Pi 4 の内蔵の Bluetooth の調査と設計

Parent: [WS215](../ws.md)
Status: in-progress（2026-10-13 P3: 設計の第 2 版。review-1 の重大 1〜3・中 1〜11・軽微を直した。design-reviewer の再 review 待ち。判断 D1〜D5 を Q1 経由でユーザーへ）
Phase disposition: normal
Queue: P3 の lane（Q1 の割当、2026-10-13）

## 第 2 版の review-1 への対応

review-1 の要点は [review-1.md](review-1.md)。判定は「p002 は進めてよい（中-7 で範囲を直す条件）、p003・p005 は重大 1〜3 と中-1 を直すまで始めない」。

| 指摘 | 対応 | 節 |
| --- | --- | --- |
| 重大1 自己 reset が意図した遷移（電源投入・`BT_IOC_RESET`・`SET_SPEED`・Launch_RAM の後の 115200 への戻り）の途中のゴミで動き、firmware の読み込みを壊す。daemon の `BTD_FAILURES_MAX` で永久に止まる。6 節の「FIFO を読み捨てない」と矛盾 | 受けの状態を **HUNT と SYNC** に分けた（B9）。電源投入・reset・速さの切り替えのたびに RX の FIFO・ring を捨て、assembler を restart し、ICR・ECR で clear し、静かな窓（既定 10 ms）の後に HUNT へ。HUNT では誤りと形の合わない byte を 1 byte ずつ捨てるだけで reset しない。Command Complete・Command Status の event が組めた時に SYNC へ。**自己 reset は SYNC からだけ**。60 秒に 3 回を越えたら STOPPED、`BT_IOC_RESET` で出る（`bt` の RECOVER の reset の経路で kernel と daemon の両方が戻る）。daemon は Launch_RAM の Command Complete の直後に `BT_IOC_SET_SPEED 115200` を呼ぶ（4 節の 7）。6 節は「遷移では捨てる」に統一 | B9、4 節、6 節 |
| 重大2 daemon の始動が「controller は 115200 で ROM」を前提にしているが、host の速さと controller の 3M・patch 済みは close・reopen・daemon の再起動を越えて残る | **`BT_BUS_UART` の始動は必ず `BT_IOC_RESET` から**。reset の ioctl が返った時には notice が queue にあるので、daemon は自分の notice までの packet を読み捨てる（command を待つ前なので `session_command` の ECONNRESET の扱いに入らない）。代案（class に close の op を足し、最後の close で電源を落とす）は採らない理由を記録 | B14、4 節 |
| 重大3 `send()` が packet の途中で終わる時の扱い | **packet を途中で切らない**: 空きを待つ（1 s、signal で中断可、まだ 1 byte も入れていない）→ packet 全部を一度に送りの ring へ → 最後の byte が FIFO に入るのを signal で中断しない待ちで待つ（2 s）。2 s を越えたら（CTS が下りたまま）送りの ring を捨てて controller を reset し notice、呼び手には EIO | B10 |
| 中1 class の変更の列挙の不足 | class・UAPI・build・config の変更を B20 に列挙した（`set_speed` の op、ioctl の switch と `bt_hci_operate`、description の `speed_max`・`board_address`、登録の時の flags の mask、`set_bootloader` の stub、`drv_bt_hci_acl_limit()`、arm64 の build の分岐、`arm64.drivers`・`menu.list`・`config-rpi4.mk`） | B20 |
| 中2 pull の符号の逆、helper の持ち主と API | helper の持ち主は **WS206 の p002a**（Q1 の判断。名前・引数は WS206 の設計に従い、WS215 は自前の名前を決めない）。pull は列挙型で受けて helper の中で register の符号へ変える（DT の `brcm,pull` は 0 なし・1 down・2 up、BCM2711 の `GPIO_PUP_PDN_CNTRL` は 00 なし・01 up・10 down）。WS215 は DT の値を列挙型に直した定数を渡す。p003 は ws206-p002a に依存 | 2 節、B5、B12、7 節 |
| 中3 attach は sleep しないとしたのに mailbox（sleep・EBUSY）を使う。clock の ID | attach は sleep してよいと明記（`kern_platform_init` は thread の文脈で、pcie の init も mailbox を使う）。短い mailbox の読み（BT_ON の状態、clock）は attach、電源の投入と CTS の待ち（数百 ms）は worker。`GET_CLOCK_RATE` の clock ID は **mailbox の UART（2）**（DT の cprman の 0x13 ではない）。0 なら `SET_CLOCK_STATE` と `SET_CLOCK_RATE` | B3、B4、B6 |
| 中4 PL011 の細部 | IBRD・FBRD の後に LCR_H を書く、TX の割り込みは FIFO を先に詰めてから、ring が満ちたら RXIM と RTIM の両方を mask、DR の bit 8〜11 の誤りを byte と一緒に受けの ring（16 bit の entry）で worker へ、全部 32 bit の access（`arm,pl011-axi`、推測） | B6、B7、B10 |
| 中5 H4 の同期の喪失の検出 | ACL の上限は class の今の `acl_data_max`（`drv_bt_hci_acl_limit()`）、event code の妥当性、ACL・SCO の handle の範囲、packet の途中で 100 ms byte が来なければ喪失（受けを kernel が止めている間は数えない） | B8 |
| 中6 値の出典の表、cts-event-workaround の出典 | 値の出典の表（2a 節）を作り、**p003・p005 の入る条件**にした。Broadcom の vendor の command は Apache-2.0 の公開の code（Zephyr の CYW43xxx の H4 の driver、Android の `brcm_patchram_plus.c`）から値だけを取る。cts-event-workaround の手順は GPL の code にしか無い見込み → ユーザーへの問い **D5**（9 節）。「公開の patch の説明」の記述は消した | 2 節、2a 節、B19、9 節 |
| 中7 段の分け | p002 に kernel の作業（arm64 の `CONFIG_INPUT_BRIDGE` の規則と既定）を入れた。p003 は p002（smoke に arm64 の bt-probe）と ws206-p002a（helper）に依存。「p002 と p003 を並べてよい」は消した | 7 節、ws.md |
| 中8 config.txt の変更が計測の時期と重なる | config.txt の変更は **`CONFIG_DRIVER_BCM2711_BT=y` かつ `CONFIG_RPI4_SERIAL_CONSOLE=n` の時だけ**、`CONFIG_DRIVER_BCM2711_BT` の既定は **n**（計測が終わるまで）。それ以外の build の config.txt は今と byte まで同じ。利用者 5 か所を生成物に。driver が core・v3d・arm の clock を log に出し、前後を比べる。実機へ届ける手順を書いた | B2 |
| 中9 SCO（UART に isochronous の拍が無い） | SCO を p003・p005 から外し、新しい **p008**（UART の SCO、送りの拍か Synchronous Flow Control、ws197-p007b に依存）にした。p003〜p005 の transport は `sco_set`・`sco_send` を NULL、`BT_INFO_SCO` を出さない | B10、B16、7 節 |
| 中10 電源の投入の順 | PL011 の RX を有効にし RTS を出してから BT_ON を上げる。固定 100 ms でなく CTS を見て待つ（上限 500 ms） | B5 |
| 中11 firmware の license | linux-firmware 20260410 の `LICENCE.cypress` と bluez-firmware の HTML 版の本文の一致を p004 で確かめる（一致しなければ p004 を止めて Q1 へ） | B18、7 節 |
| 軽微: `_Static_assert` | `_Static_assert(sizeof(struct bt_info) == 160, ...)` | B13 |
| 軽微: `board_address` の byte の順 | DT の `local-bd-address` は HCI と同じ little endian の順。`board_address` も同じ順で、`0xFC01` にそのまま渡す | B13、B15 |
| 軽微: `BT_IOC_RESET` の UAPI の comment | USB だけの書き方を直し、UART（電源の入れ直し、host は 115200）と kernel の自己 reset の notice を書く | B20 |
| 軽微: B9 の「無ければ足す」 | 既にある（`session_dispatch` が notice で `BTD_STATE_ERROR`、`btd_loop` が close して再び open）。文を直した | B9 |
| 軽微: `physical_path` "uart0" で `btd_key` が空 | UART では load の記憶が効かず、毎回 firmware を読むと明記（重大2 の始めの reset と同じ結論） | B14 |
| 軽微: `BTD_FIRMWARE_FOLDER` は 1 つ | Broadcom の手順は自分の定数 `/lib/firmware/brcm` を使う（session の `firmware_folder` は Intel のまま） | B14 |
| 軽微: bt-probe に Read Local Name が無い、node は一度に 1 つ | p003 で bt-probe に Read Local Name を足す。smoke は bluetoothd を止めてから | B20、7 節 |
| 軽微: worker が class を呼ぶ時の lock、reset の context と lock の順 | driver の spinlock を放してから `drv_bt_hci_input`・`drv_bt_hci_notice` などを呼ぶ。lock の順は class の op の mutex → driver の電源の mutex → driver の spinlock | 6 節 |
| 軽微: `emulated_board()` は rpi4-audio.c の static | p003 で共有の関数にする（audio も使う） | B4、B20 |
| 軽微: mailbox の GPIO の向き（推測） | 「未確認」に入れた（`GET_GPIO_CONFIG` で読む） | 11 節 |
| 軽微: HAL の変更は不要 | 同意。HAL の API は変えない | — |
| D1〜D4 への意見 | 9 節に review の意見を推しとして残した | 9 節 |

## 由来

2026-10-12 ユーザー:「描画の最適化の後、計測を取ったら、SMPを入れます。さらに計測をとって、最適化を終了したいです。そのあと、RPi4のWiFiとBluetoothを実装し、RPi4は完了です。」SMP（WS208）は main に入った。WiFi は [WS206](../../ws206/ws.md)（P1 が並行して設計中）、Bluetooth はこの WS。

この Phase は設計だけを書く（code は書かない）。実機の Pi 4（10.0.30.2）は 2026-10-13 に止まっていて読めなかった。実機で読む事は「未確認」の節に置き、p003 の最初に確かめる。

## 1. 今の tree の事実（2026-10-13、agent/p3 = main fc73f5cb0 の merge の後）

| 所 | 事実 |
| --- | --- |
| HCI の class | `src/drivers/generic/bt-hci.c`・`include/drivers/generic/bt-hci.h`。transport が `drv_bt_hci_register()` で controller を登録し、組んだ event・ACL を `drv_bt_hci_input()` へ渡す（IRQ の handler からは不可、class の spinlock は `LOCK_RANK_DEVICE`、bt-hci.c 278 行の comment）。class が `/dev/bluetoothN` を出し、event・ACL・SCO の ring を持つ。open で ring を空にする（bt-hci.c 477〜479 行）。queue が満ちると input を断り（ENOSPC）、transport は packet を持ったまま controller からの取り込みを止める。read が空きを作ると class が `room` の op を呼ぶ |
| class の op | `drv_bt_hci_ops`（bt-hci.h 140〜157 行）: `send`・`set_bootloader`・`reset`・`room`・`sco_set`・`sco_send`。登録では `send`・`set_bootloader`・`reset`・`room` が NULL 不可（bt-hci.c 170〜173 行）。`send`・`reset`・`set_bootloader` は class の op の mutex の下で 1 つずつ、sleep してよい。ioctl の switch（700〜747 行）と `bt_hci_operate`（906〜958 行）が request ごとの分岐 |
| description | `drv_bt_hci_description`（bt-hci.h 160〜168 行）: vendor・product・version・bus・flags・name・physical_path。登録で flags は `BT_INFO_SCO` だけに切られる（bt-hci.c 227〜229 行） |
| 純粋な部分 | `bt-hci-proto.c`: ring、assembler（mode は event だけか ACL だけ。`bt_hci_assembler_init`・`restart` が `acl_data_max` を取る）、書きの検査、SCO の assembler・fifo。host の試験 `plan/ws143/tests/bt-hci-proto-host-test.sh` |
| UAPI | `include/uapi/bluetooth.h`: H4 の形、`BT_IOC_GET_INFO`・`SET_BOOTLOADER`・`RESET`・`SET_ACL_MAX`・`GET_STATS`・`SET_SCO`（番号 0〜5）。`struct bt_info` は 160 byte（`reserved[4]` を含む、static assert は無い）。`BT_BUS_USB` だけ。notice は `BT_PACKET_NOTICE_RESET` だけ。`BT_IOC_RESET` の comment は USB の書き方（150〜159 行）。SCO の write は「isochronous の pipe に約 36 ms を queue、満ちたら EAGAIN」（24〜29 行）。ACL の上限 `BT_ACL_DATA_MAX` 4096 |
| USB の transport | `src/drivers/usb/usb-bt.c`。assembler を `BT_ACL_DATA_MAX` で作る（602 行）。reset は worker を止め、device を reset し、`drv_bt_hci_notice(RESET)` を queue に入れる（1357 行） |
| bluetoothd | `userland/base/bluetoothd/`。始動は `btd_session_start()`: `GET_INFO` → Intel（0x8087）なら Intel の firmware → `session_core()`。`session_dispatch` は RESET の notice で `BTD_STATE_ERROR`（session.c 815〜819 行）、`btd_loop` が close し（main.c 804 行）、2 秒後に open し直す（816 行）。command を待つ間の notice は ECONNRESET（session.c 936〜938 行）。始動の失敗が 3 回続くと止まる（`BTD_FAILURES_MAX`、main.c 76 行）。`bt` の RECOVER の reset が `BT_IOC_RESET` の後に止まりを解いて open し直す（main.c 1850〜1866 行）。load の記憶の key は `physical_path` の "/device" までで、無ければ空（main.c 3004〜3022 行）。firmware の folder は `BTD_FIRMWARE_FOLDER` = `/lib/firmware/intel` の 1 つ（protocol.h 73 行） |
| **arm64 で build されない物** | bluetoothd・bt・bt-probe の package の arch が `amd64` だけ。`CONFIG_INPUT_BRIDGE` は Makefile 245 行で amd64 だけ y、kernel の規則も amd64 の vmunix.mk（349〜355 行）にだけある。arm64 の kernel の HCI の class は `CONFIG_DRIVER_USB_BT=y` の時だけ build（platform/arm64/vmunix.mk 130〜132 行） |
| Pi 4 の UART | HAL の early console（`src/hal/arm64/bsp-rpi4/uart.c`）は PL011 を GPIO 14・15 で使う。`CONFIG_RPI4_SERIAL_CONSOLE=y` の時だけ（既定 n）。n の時は HAL も `rpi4-console.c` も PL011 に触らない。`prekern_bsp_cons_irq_init()` が FDT の最初の `arm,pl011` の割り込みを mask したまま残す |
| config.txt | `platform/arm64/config.txt`（静的な 1 file）: `enable_uart=1`、`init_uart_clock=48000000`、`uart_2ndstage=1`、`dtoverlay=disable-bt`。利用者は 5 か所: `platform/arm64/bootloader.mk` 4 行、`platform/arm64/disk-image.mk` 10 行、`platform/arm64/vmunix.mk` 595・602 行、`platform/arm64/tools/check-rpi4-hdd-image.py` 87 行（渡された file と image の中を比べる） |
| Pi 4 の driver | `src/drivers/platform/rpi4/`: console、ethernet、firmware（mailbox、`drv_rpi4_firmware_property()`、buffer を atomic に取り、取れなければ sleep して再試行、長く取れなければ EBUSY、rpi4-firmware.c 250〜274 行）、pcie、sdhci、audio。GPIO の共通の helper はまだ無い（WS206 p002a が作る） |
| 起動の順 | `src/kern/platform/rpi4.c` の `kern_platform_init`（thread の文脈、sleep してよい。pcie の init が mailbox を使う）: console → sdhci → GENET → PCIe → audio → GPU。driver は `#if CONFIG_DRIVER_BCM2711_AUDIO` の形で選ばれる。QEMU の raspi4b の見分けは `rpi4-audio.c` の static な `emulated_board()` |
| config の menu | `config/drivers/architecture/arm64.drivers`（key・種類・名前・platform・既定）、`config/drivers/menu.list`（category の行）、`config/ci/config-rpi4.mk`（menu が生成、手で直さない） |
| firmware の包み | `userland/firmware/`: 1 族 1 directory、既定 off、immutable な上流から取得・検証、license・manifest を `/lib/firmware` へ |

## 2. Pi 4 の hardware の事実（出典つき）

出典は (a) `vendor/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb` を dtc で読んだもの（firmware の fixup の前の物）、(b) `disable-bt.dtbo`、(c) RPi-Distro/bluez-firmware の tree、(d) 公開の文書（BCM2711 ARM Peripherals、ARM PL011 TRM、Raspberry Pi の mailbox の property の wiki・config.txt の文書、Bluetooth Core 仕様、DT binding）、(e) Apache-2.0 の公開の code（値だけ、2a 節）。

| 事実 | 値 | 出典 |
| --- | --- | --- |
| BT の UART | `serial@7e201000`（ARM 0xfe201000）= **PL011 の UART0**、`compatible = "arm,pl011-axi", "arm,pl011"`、割り込み SPI 0x79（GIC の 153）、`uart-has-rtscts`、`cts-event-workaround`、`skip-init` | (a) |
| BT の node | 上の子 `bluetooth`: `compatible = "brcm,bcm43438-bt"`（43455 でもこの名前）、**`max-speed = 3000000`**、`shutdown-gpios = <&expgpio 0 0>`、`local-bd-address = [00 ×6]`（HCI と同じ little endian の順、binding）、`fallback-bd-address`、status okay | (a)、(d) |
| mini UART | `serial@7e215040`（`brcm,bcm2835-aux-uart`）にも `bluetooth` の子があり **status disabled** | (a) |
| alias | 既定: `serial0` = mini UART、`serial1` = PL011。`disable-bt` は bt の node を disabled、uart0 の pin を GPIO 14・15 へ、alias を入れ替え | (a)(b) |
| pin | `uart0_pins` = GPIO 32（TXD0）・33（RXD0）、ALT3。CTS0・RTS0 は GPIO 30・31 の ALT3（`uart0-ctsrts-gpio30`）。DT の `brcm,pull` の符号は **0 なし・1 down・2 up**: uart0 は `<0 2>` = 32 なし・33 up、ctsrts は `<2 0>` = 30 up・31 なし。BCM2711 の `GPIO_PUP_PDN_CNTRL_REG1`（0xe8、pin 16〜31）・`REG2`（0xec、pin 32〜47）の符号は **00 なし・01 up・10 down**（DT と up・down が逆）。base の dtb の `bt_pins` は空なので、30・31 を ALT3 にするのは firmware か OS（未確認） | (a)、BCM2711 Peripherals の GPIO の章 |
| BT_REG_ON | firmware の GPIO の拡張（`raspberrypi,firmware-gpio`）の line 0 `BT_ON`（line 1 が `WL_ON`）。mailbox の GPIO 番号は 128 + line = **128**（WiFi は 129） | (a)、mailbox の wiki |
| clock | BT の node に `clocks` は無い（LPO は board が供給）。PL011 の clock は config.txt の `init_uart_clock=48000000`。mailbox の `GET_CLOCK_RATE` の clock ID は **UART = 2**（ARM 3、CORE 4、V3D 5）。DT の `<&clocks 0x13>` は cprman の番号で mailbox の ID ではない。3,000,000 baud = 48 MHz / 16（IBRD 1、FBRD 0、PL011 の上限ちょうど） | (a)、mailbox の wiki、PL011 TRM |
| PL011 の癖 | DT の `cts-event-workaround`: BCM の PL011 は CTS が頻繁に変わると送りが止まることがある、と Raspberry Pi の Linux が回避を入れている。**回避の具体の手順の出典は GPL の code（とその commit）しか見つかっていない** → D5 | (a)、D5 |
| firmware | `BCM4345C0.hcd`（63,806 byte、sha256 `51c45e77ddad91a19e96dc8fb75295b2087c279940df2634b23baf71b6dea42c`）。322 個の `0xFC4C`（Write_RAM）と最後の 1 個の `0xFC4E`（Launch_RAM）、record は opcode 2 byte（LE）+ 長さ 1 byte + 引数、最長の引数 204 byte（2026-10-13 P3 が file を数えた）。Pi 4 の名前 `BCM4345C0.raspberrypi,4-model-b.hcd` は同じ file への link | (c) commit `adb4d55c8f193ff16ece74dd72fd6e8801632103`（2025-10-02）の `debian/firmware/broadcom/`、`debian/bluez-firmware.links` |
| firmware の license | `debian/copyright`: `debian/firmware/broadcom/*` は Cypress Wireless Connectivity Devices の EULA（object code のまま、Cypress の IC と使う時に限り複製・再配布可、改変・逆解析の禁止）。同じ directory の `LICENSE.cypress` は kernel.org の cgit の HTML のページを保存した物。linux-firmware の `LICENCE.cypress` が同じ license の平文（本文の一致は p004 で確かめる）。linux-firmware に `.hcd` の BCM4345C0 は無い | (c)、linux-firmware の WHENCE |

## 2a. 値の出典の表（p003・p005 の入る条件）

[WS214 の hvs-values.md](../../ws214/phase003/hvs-values.md) の形。新しく使う値ごとに、値・使う所・出典・確かめ方。zedBSD の名前は実装の時に足す。**この表の p003 の行が全部出典つきで埋まっていることを p003 の入る条件、p005 の行を p005 の入る条件にする。** 出典の記号: (d) 公開の文書、(e1) Zephyr の `drivers/bluetooth/hci/h4_ifx_cyw43xxx.c`（Apache-2.0、Infineon の CYW43xxx の H4 の driver）、(e2) Android の `system/bluetooth/brcm_patchram_plus/brcm_patchram_plus.c`（Apache-2.0、Broadcom）、(f) firmware の file を自分で数えた、(c) 実機。Apache-2.0 の code からは値と順序の事実だけを取り、code・comment・構造は写さない（B19）。2026-10-13 に P3 が (e1)(e2) の license と下の値を web で読んで確かめた。

| 値 | 使う所 | 出典 | 確かめ | 段 |
| --- | --- | --- | --- | --- |
| PL011 の register の offset（DR 0x00、RSR/ECR 0x04、FR 0x18、IBRD 0x24、FBRD 0x28、LCR_H 0x2c、CR 0x30、IFLS 0x34、IMSC 0x38、RIS 0x3c、MIS 0x40、ICR 0x44）と bit（FR の CTS・BUSY・RXFE・TXFF・TXFE、CR の UARTEN・TXE・RXE・RTS・RTSEn・CTSEn、LCR_H の WLEN・FEN、IMSC の RXIM・RTIM・TXIM・CTSMIM・FEIM・PEIM・BEIM・OEIM、DR の FE・PE・BE・OE = bit 8〜11）、divisor の式、LCR_H の書きで divisor が反映されること、無効にする手順 | rpi4-bt.c | (d) ARM PL011 TRM | host: divisor の計算。実機: smoke | p003 |
| GPFSEL3（0x0c）の GPIO 30〜33 の ALT3（0b111）、pull の REG1・REG2 の符号 | WS206 p002a の helper（WS215 は列挙型の値を渡すだけ） | (d) BCM2711 ARM Peripherals | WS206 p002a の host 試験、実機で GPFSEL3 を読む | p003 |
| mailbox の tag: `GET_GPIO_STATE` 0x00030041、`SET_GPIO_STATE` 0x00038041、`GET_GPIO_CONFIG` 0x00030043、`GET_CLOCK_STATE` 0x00030001、`SET_CLOCK_STATE` 0x00038001、`GET_CLOCK_RATE` 0x00030002、`SET_CLOCK_RATE` 0x00038002、clock ID UART 2・ARM 3・CORE 4・V3D 5 | WS206 p002a の GPIO の helper、rpi4-bt.c の clock | (d) Raspberry Pi の firmware の wiki「Mailbox property interface」 | 実機: smoke の log | p003 |
| BT_ON の reset の pulse（0 → 10 ms → 1）と電源の後の CTS の待ちの上限 500 ms | rpi4-bt.c | 10 ms は WS206 の WL_ON と同じ仮の値（Infineon の CYW43455 の datasheet の BT_REG_ON の timing で p003 に確定）、500 ms は (e1) の電源の後の待ち | 実機: CTS が立つまでの時間を log | p003 |
| `HCI_Reset` 0x0C03、`Read_Local_Version_Information` 0x1001、`Read_Local_Name` 0x0C14、`Read_BD_ADDR` 0x1009、Command Complete 0x0E・Command Status 0x0F・Hardware Error 0x10 の event code、定義された event code の表（B8）、connection handle の上限 0x0EFF | bt-probe、rpi4-bt.c の HUNT と同期の検査、bluetoothd | (d) Bluetooth Core 5.4 Vol 4 Part E | host: assembler・台本 | p003 |
| 製造者 15（Broadcom） | bluetoothd | (d) Bluetooth SIG の Assigned Numbers（Company Identifiers） | 実機: Read Local Version | p005 |
| `0xFC18` Update_UART_Baud_Rate: 引数 6 byte = 0 の 2 byte + baud の 4 byte（LE）。Command Complete は古い速さで来て、その後に host が速さを変える | bluetoothd | (e1)(e2) | host: 台本。実機: 3 Mbaud で通る | p005 |
| `0xFC2E` Download_Minidriver と、その後の待ち（第 1 版の 50 ms は出典が無い。(e2) は決まった待ちを持たない） | bluetoothd | (e1)(e2)。待ちの値は p005 で出典を探し、無ければ実機で「Command Complete の後すぐ」を試して決める | 実機 | p005 |
| `0xFC4C` Write_RAM・`0xFC4E` Launch_RAM、`.hcd` の record の形 | bluetoothd | (e1)(e2)(f) | host: HCD の解析 | p005 |
| Launch_RAM の後に controller が初めの速さ（115200）に戻ること、その後の待ち 250 ms | bluetoothd | (e1) | 実機 | p005 |
| `0xFC01` Write_BD_ADDR: 引数 6 byte、最下位の byte から | bluetoothd | (e2) | 実機: Read BD_ADDR | p005 |
| `0xFC1C` Write_SCO_PCM_Int_Param: 引数 5 byte、1 byte 目が routing（0 PCM、1 transport、2 codec、3 I2S）、残りの 4 byte の意味 | bluetoothd | (e2)。残りの 4 byte は p008 で出典を埋める | 実機 | p008 |

## 3. 設計

| ID | 決め | 理由 |
| --- | --- | --- |
| B1 | **BT は PL011（UART0、GPIO 30〜33、CTS/RTS つき）**。mini UART は使わない | mini UART は core の clock に従う divisor・8 byte の FIFO・CTS/RTS の自動の制御が弱い。DT の既定も PL011 |
| B2 | **serial の console と BT は同じ PL011 を取り合うので同時に使わない**。新しい `CONFIG_DRIVER_BCM2711_BT` は **既定 n**（Pi の計測（SMP の後の計測）が終わってから、Q1 がユーザーに確かめて y に）。build は常に `$(BUILD)/config.txt` を `platform/arm64/config.txt` から作る（`cmdline.txt` と同じ「phony の target と cmp」の形で、config が変われば作り直す）。**`CONFIG_DRIVER_BCM2711_BT=y` かつ `CONFIG_RPI4_SERIAL_CONSOLE=n` の時だけ** `enable_uart=1`・`uart_2ndstage=1`・`dtoverlay=disable-bt`（と `uart_2ndstage` の直前の comment）を除く。`init_uart_clock` は残す（BT の PL011 の clock）。それ以外の build では元の file と byte まで同じ。利用者の 5 か所（1 節）を `$(BUILD)/config.txt` に変える（`bootloader.mk` の `RPI4_FIRMWARE_FILES`、`disk-image.mk` の check、`vmunix.mk` の hdd-image の依存と `--config`。check の script は渡された file と比べるので script は変えない）。**clock の前後**: `=y` の kernel の driver は attach するかどうかにかかわらず、初めに mailbox で core・v3d・arm の clock を 1 行 log に出す。同じ kernel で旧 config.txt と新 config.txt を比べる。**実機へ届ける手順**（T1）: ws208-p004 で `vmunix` を置き換えたのと同じ、Pi（10.0.30.2）の FAT の boot partition への SSH の複写。旧 file を `config.txt.ws215-bak` に残し、`$(BUILD)/config.txt` と `vmunix` を置き、再起動はユーザーの手で（今の運用）。起動しなければユーザーが SD card を別の機械で `config.txt.ws215-bak` に戻す。log の clock の行と boot-test の PNG（HDMI・GPU が今と同じ）を前後で取る | 既定の build は 2026-10-12 から serial を使わない。`enable_uart=1` を mini UART が primary のまま残すと firmware が core の clock を固定しうるので外すが、その影響（計測の条件）を計測の後に回すため既定 n。D1 |
| B3 | 新しい driver **`src/drivers/platform/rpi4/rpi4-bt.c`**（PL011 の UART の HCI の transport）。`rpi4.c` の audio の後で `#if CONFIG_DRIVER_BCM2711_BT` の下で呼ぶ。**attach（`kern_platform_init` の thread の文脈、sleep してよい）**: clock の行を log、DT を読む（B4）、mailbox で BT_ON の状態を読む（ENOTSUP なら attach しない）、`GET_CLOCK_RATE`（ID 2）で UART の clock を読む（0 なら `SET_CLOCK_STATE` で on、`SET_CLOCK_RATE` で 48 MHz、読み直して 0 なら attach しない）、register を map、PL011 を止めて全部の割り込みを mask し ICR を clear、割り込みの handler を登録（GIC の SPI はここで unmask。PL011 の IMSC は 0 のまま）、worker thread を作る。**worker の最初の仕事**: pin（B5）、PL011 の設定（B6）、電源の投入（B5）、成功したら class に登録（`drv_bt_hci_register`）。電源の投入が失敗したら log に出し、登録しない（daemon は node が無いので待つだけ）。登録の前に `send()` は呼ばれないので、第 1 版の「電源を待つ send」は要らない | boot を数百 ms 待たせない。mailbox の sleep と EBUSY は thread の文脈で扱う |
| B4 | **DT の見つけ方**: `brcm,bcm43438-bt` の node を全て見て、親が `arm,pl011` で、親と自分が両方 enabled の物を取る。`max-speed`・`local-bd-address` を読む。**attach しない条件**: その node が無い・disabled（`disable-bt`）、`CONFIG_RPI4_SERIAL_CONSOLE=y`（二重の guard）、QEMU の raspi4b（`emulated_board()` を rpi4-audio.c の static から共有の関数に移し、audio と BT が使う）、mailbox の BT_ON の読みが ENOTSUP | 誤って console の UART を奪わない |
| B5 | **pin と電源**: pin は WS206 p002a の GPIO の helper（名前・引数は WS206 の設計に従う）で GPIO 30・31・32・33 を ALT3、pull は列挙型で 30 up・31 なし・32 なし・33 up（2 節の DT の値を DT の符号から列挙型に直した定数。register の符号への変換は helper の中）。**電源の投入の順**: (1) PL011 を 115200 で設定し、UARTEN・RXE・TXE・RTSEn・CTSEn を立て、受けの割り込みを有効にする（FIFO が空なので RTS が出る）。(2) 静かな窓の始め（受けを捨てる、B9）。(3) WS206 p002a の mailbox の GPIO の helper で BT_ON（128）を 0 → 10 ms → 1。(4) FR の CTS が立つ（controller が受けられる）まで 1 ms ごとに見る、上限 500 ms。立たなければ失敗。(5) 静かな窓の終わり → HUNT。`BT_IOC_RESET` と自己 reset は同じ手順（controller は ROM・115200、host も 115200 に戻す）と、受けの途中の packet・送りの ring の破棄、RESET の notice | 電源を上げる時に host の RTS が出ていないと controller が別の始まり方をしうる（推測）。BT_ON と WL_ON は別の line なので WiFi に触らない |
| B6 | **PL011 の設定**（ARM PL011 TRM）: 8N1、FIFO 有効、CTSEn・RTSEn（自動の流れの制御）、受けの水位 1/2、送りの水位 1/8。**手順**: CR の UARTEN を 0 → FR の BUSY が落ちるのを待つ → LCR_H の FEN を 0（送りの FIFO を捨てる）→ IBRD・FBRD → **LCR_H を書く（divisor はここで反映される）** → CR を戻す。divisor は attach で読んだ UART の clock から計算し、誤差が 1.5% を越える速さは EINVAL。上限は min(DT の `max-speed`, clock/16)。**全部の register を 32 bit で読み書きする**（`arm,pl011-axi` は 32 bit の access だけの可能性、推測）。CTS の割り込み（CTSMIM）は使わない（D5） | RTS の自動の制御で、kernel が読まない間は controller が送りを止める（USB の NAK と同じ「捨てない」の性質） |
| B7 | **受け**: 割り込みの handler は driver の spinlock（irqsave）の下で、FIFO が空になるまで DR を読み、**byte と誤りの bit（DR の bit 8〜11: FE・PE・BE・OE）を 16 bit の entry として受けの ring（8192 entry = 16 KiB）へ**入れ、worker を起こす。ring が満ちたら **RXIM と RTIM の両方を mask**（level の SPI なので片方だけでは割り込みが続く）、FIFO が満ちて RTS が下り controller が止まる。worker が ring から取って空きができたら mask を戻す。worker は driver の spinlock を持って ring から局所の buffer へ写し、**spinlock を放してから** assembler（B8）に通し、組めた packet を class へ（event・ACL は `drv_bt_hci_input()`）。class が断ったら packet を持ったまま止まり、`room` で起きて続ける | class の「捨てない」約束を UART で守る。ISR は短く、SMP でどの CPU でもよい |
| B8 | **H4 の assembler を `bt-hci-proto.c` に足す**（新しい mode `BT_HCI_ASSEMBLE_H4`）: 先頭の type の byte（2 ACL、3 SCO、4 event）で header の長さ（4・3・2）と長さの field を選ぶ。**同期の喪失**: type が他の値、event code が Core 仕様の表（定義された code と 0xFF）に無い、ACL の handle が 0x0EFF を越える、ACL の長さが class の今の `acl_data_max`（`drv_bt_hci_acl_limit()`、assembler に restart で渡す）を越える、SCO の handle が範囲外、受けの誤りの bit が立った byte、**packet の途中で 100 ms byte が来ない**（受けを kernel が mask している間は時計を止める）。SCO の packet は p008 までは流さないので、来たら header の長さだけ読み飛ばして数える。host の試験で、塊の切れ目・複数の packet・断られた packet の保持・各種の喪失・HUNT の anchor を確かめる | 純粋な code なので host で試せる。USB の mode は変えない。上限が 4096 のままだと 1 byte の喪失が最大 4 KB を飲む |
| B9 | **受けの状態: HUNT と SYNC、自己 reset**。(1) **遷移**（電源の投入・`BT_IOC_RESET`・自己 reset・`BT_IOC_SET_SPEED`）のたびに、受けの割り込みを止め、FIFO を読み捨て、受けの ring を空にし、assembler を restart し、ICR と ECR を clear し、**静かな窓**（既定 10 ms、実機で詰める）の間に来た byte・誤りは捨てて数えるだけ。窓の後は HUNT。(2) **HUNT**: 誤りと形の合わない byte は 1 byte ずつ捨てて先頭を探し直す（reset しない）。組めた packet のうち **Command Complete か Command Status の event だけ**を class へ渡して SYNC に移る（daemon は遷移の後に必ず command を送るので、その答えが anchor になる）。それ以外の組めた packet は捨てる。捨てた byte の数は SYNC に移る時に 1 行 log。(3) **SYNC**: B8 の同期の喪失、overrun・framing・break の誤りで `malformed` を数え（`drv_bt_hci_count`）、**自己 reset**（B5 の電源の入れ直し）をして既存の `BT_PACKET_NOTICE_RESET` を queue に入れる。B10 の送りの止まり（2 s）による reset は受けの状態によらず行い、同じ数に入る。(4) 自己 reset は 60 秒に 3 回まで。越えたら **STOPPED**（電源を落とし、`send` は EIO、log に出す）。**STOPPED を出るのは `BT_IOC_RESET`**（数を 0 に）。daemon の側は既に、求めていない notice で session が `BTD_STATE_ERROR` になり close・open し直す（session.c 815〜819 行、main.c 804・816 行）。daemon が 3 回の失敗で止まった時は、`bt` の RECOVER の reset（`BT_IOC_RESET` の後に daemon の止まりを解く、main.c 1850〜1866 行）で kernel の STOPPED と daemon の止まりが両方戻る | 新しい notice の型を足さない。UART の誤りからの戻り方は電源の入れ直ししか無い。意図した遷移のゴミでは reset しない |
| B10 | **送り**: 送りの ring（8 KiB、`BT_ACL_PACKET_MAX` の 4101 byte を 1 つ入れられる）。`send()`: (1) packet 全部が入る空きを待つ（上限 1 s、signal で EINTR。まだ 1 byte も入れていないので呼び手の再試行は安全）、(2) **packet 全部を一度に** spinlock の下で ring に入れ、FIFO が満ちるまで先に詰めてから TXIM を unmask（PL011 の送りの割り込みは水位を下回る変化で上がるので、空の FIFO のまま待っても来ない）、(3) packet の最後の byte が FIFO に入るのを **signal で中断しない** 待ちで待つ（上限 2 s）。2 s を越えたら（CTS が下りたまま）送りの ring を捨て、自己 reset（B9 の数に入る）と notice をして **EIO**。packet は途中で切らない（ring に入った packet は最後まで送るか、controller ごと reset する）。自己 reset が別の経路で起きたら待っている `send()` は EIO。STOPPED では EIO。割り込みの handler は ring の packet の境目を守って FIFO に詰める（p008 で SCO の fifo と packet の単位で交互にする）。**SCO は p008 まで持たない**: `sco_set`・`sco_send` は NULL、`BT_INFO_SCO` を出さない（class は `SET_SCO` に ENOTSUP、SCO の write に EINVAL を返す） | H4 は packet を途中で切れない。UART には isochronous の拍が無い（p008 で設計） |
| B11 | **速さの切り替えは daemon が行い、kernel は host 側の divisor だけを変える**。UAPI に `BT_IOC_SET_SPEED`（`_IOW('b', 6, uint32_t)`、baud）。kernel（`set_speed` の op、class の op の mutex の下）: 受けの割り込みを止め、送りの ring が空で FR の BUSY が落ちるのを待つ（上限 100 ms、越えたら EBUSY）、B6 の手順で divisor を書き、B9 の遷移（FIFO・ring を捨てる、静かな窓、HUNT）。USB の transport は op が NULL で ENOTSUP。daemon は `0xFC18` の Command Complete を受けた後に呼ぶ。Command Complete の後に controller は何も送らない（daemon は 1 command ずつ、始動の間は他の event が無い）ので、切り替えで捨てる byte は全部ゴミ | firmware の読み込みの後、controller は 115200 に戻るので、切り替えは手順の中で 2 回要る。手順を daemon に置けば kernel は command を覗かなくてよい |
| B12 | **Pi 4 の共通の helper は WS206 の p002a が持ち主**（2026-10-13 Q1 の判断。WiFi が先）。GPIO の pin の function と pull（GPFSEL3 は BT の 30〜33 と WiFi の 34〜39 が同じ register、pull の REG2 も 32〜47 で共有。SMP の下で 2 つの driver が read-modify-write で互いを消さないよう、1 つの spinlock の下で書く）と、mailbox の拡張 GPIO（BT_ON 128・WL_ON 129）の読み書き。**名前・引数・型は WS206 の設計（ws206-p001 の W3、P1 の第 3 版で決まる）に従い、WS215 は自前の名前を決めない**。WS215 の要求は 3 つ: pin ごとの ALT3、pull を列挙型（なし・up・down）で受けて register の符号に変える、mailbox の GPIO の状態の読み書き（読みの ENOTSUP を呼び手に返す）。ws215-p003 は ws206-p002a の main への統合の後に始める。WS215 は GPFSEL・pull・mailbox の GPIO を直に書かない。`drv_rpi4_firmware_init` は platform の初期化で 1 回の前提（SMP の不安全は Q1 が別に直している） | 2 つの driver が同じ register を持つ |
| B13 | **`struct bt_info` の拡張（UAPI、大きさは不変）**: `BT_BUS_UART`（2）、flag `BT_INFO_SPEED`（速さを変えられる、0x0004）・`BT_INFO_ADDRESS`（board の address がある、0x0008）。`reserved[4]` の 16 byte を `uint32_t speed_max; uint8_t board_address[6]; uint8_t pad[2]; uint32_t reserved;` に。**`_Static_assert(sizeof(struct bt_info) == 160, ...)`** を足す。`board_address` は HCI と同じ little endian の順（DT の `local-bd-address` の順のまま）。`name` は "Raspberry Pi 4 Bluetooth"、`physical_path` は "uart0"、vendor・product は 0 | daemon が UART の controller と速さの上限、DT の address を知るため。既存の利用者（daemon・bt・Settings の backend）は reserved を読まない（p003 で grep して確かめる） |
| B14 | **bluetoothd の Broadcom の始動**（`broadcom.c`、Intel の `intel.c` と同じ形で、file の解析と手順を分ける）。対象は `bus == BT_BUS_UART`。**始動は必ず `BT_IOC_RESET` から**（4 節の 2）: host の速さと controller の 3 Mbaud・patch 済みの状態は close・reopen・daemon の再起動を越えて残るので、始めに既知の状態（ROM・115200）に戻す。reset の ioctl が返った時、notice は既に queue にある（transport が reset の終わりに入れる）ので、daemon はそれまでの packet を自分の notice まで読み捨てる（上限 1 s、command を待つ前なので `session_command` の ECONNRESET の扱いに入らない）。結果として **UART の controller は daemon の始動のたびに firmware を読む**（`physical_path` が "uart0" で `btd_key` が空になり、load の記憶も効かない。3 Mbaud で約 1〜2 秒の見込み、実機で測る）。firmware は自分の定数 `/lib/firmware/brcm/` から読む（session の `firmware_folder` は Intel の folder のまま）。**代案**（採らない）: class に close の op を足し、最後の close で電源を落として 115200 に戻す。class の変更が増え、daemon の異常終了の後も結局 reset が要るので、始めの reset 1 つにまとめる | firmware の読み込みは daemon の仕事（既存の設計） |
| B15 | **BD_ADDR**: (1) controller の address（Read BD_ADDR）が Broadcom の既定の値（実機で読む。BCM4345C0 の ROM の既定と、全部 0）でなければそのまま。(2) そうなら `BT_INFO_ADDRESS` の `board_address`（DT の `local-bd-address`、firmware の fixup）を `0xFC01` で書く（byte の順はそのまま）。(3) どちらも無ければ D3 | DT の `fallback-bd-address` は「controller の address を優先し、無い時に DT」の意味（binding） |
| B16 | **SCO は p008**: Pi 4 の 43455 の PCM の pin は SoC に繋がっていないので、SCO の音は HCI で運ぶ（`0xFC1C` の routing を transport に）。UART には isochronous の拍が無いので、送りの拍（transport が link の速さで SCO の packet を出す）か Synchronous Flow Control（controller が SCO の Number_Of_Completed_Packets を返す）を p008 で設計し、design-reviewer に掛ける。ws197-p007b（SCO・audiod）の後 | UAPI の「36 ms を queue、満ちたら EAGAIN」は USB の isochronous の拍が前提 |
| B17 | **arm64 で Bluetooth の userland と input bridge を build する**（p002）: bluetoothd・bt・bt-probe の package の arch に arm64。kernel の `CONFIG_INPUT_BRIDGE` を rpi4 でも y（Makefile 245 行の既定、platform/arm64/vmunix.mk に amd64 の 349〜355 行と同じ形の規則: `input-bridge.c`・`input-bridge-setup.c`、`CONFIG_DRIVER_USB_HID` が n の時の `hidraw-describe.c`）。移植の問題が出たらその Phase で直す | 今は amd64 だけ。HID の keyboard・mouse は input bridge を通る |
| B18 | **firmware の package `userland/firmware/brcmbt/`**（package `brcmbt-firmware`、既定 off、Pi 4 の UAT・release の config で選ぶ）: RPi-Distro/bluez-firmware の commit `adb4d55c…` の `debian/firmware/broadcom/BCM4345C0.hcd`（sha256 は 2 節）を `/lib/firmware/brcm/BCM4345C0.hcd` に、license は linux-firmware の tag `20260410` の平文の `LICENCE.cypress` と bluez-firmware の `debian/copyright` の該当の段落、manifest（取得元・commit・sha256・license の対応）。**p004 で `LICENCE.cypress` の本文と bluez-firmware の HTML 版の本文（tag を除いた文）の一致を確かめる**。一致しなければ p004 を止めて Q1 へ | 既存の firmware の包みの決まり。D2 |
| B19 | **出典の規則**: Linux の btbcm・hci_bcm・hci_h4・amba-pl011（GPL）は読まない・写さない（WS141 の GPL の例外は WS215 に及ばない。D5 で広げるかを問う）。使う物: Bluetooth Core 仕様、ARM PL011 TRM、BCM2711 ARM Peripherals、Raspberry Pi の公開の文書（config.txt、mailbox の property）、DT の binding の文書、firmware の file の形（自分で数えた）、**Apache-2.0 の公開の code（2a 節の (e1)(e2)）からは値と手順の順序の事実だけ**（code・comment・構造は写さない。値は 2a 節の表に出典つきで記録）。公開の tutorial（rpi4-osdev の part7-bluetooth）は事実の確かめにだけ使う | Guardrail の license の境界 |
| B20 | **class・UAPI・build・config ほかの変更の一覧（p003）**: (1) `drv_bt_hci_ops` に `int (*set_speed)(void *context, uint32_t baud);`（NULL 可、NULL なら ENOTSUP。usb-bt と loopback は NULL）。(2) ioctl の switch に `BT_IOC_SET_SPEED`（copyin して `bt_hci_operate`）、`bt_hci_operate` に分岐（0 と `speed_max` を越える値は EINVAL、op が NULL なら ENOTSUP）。(3) `drv_bt_hci_description` に `uint32_t speed_max; uint8_t board_address[6];`、登録で `info` に写す。登録の flags の mask（bt-hci.c 227〜229 行）を `BT_INFO_SCO`・`BT_INFO_SPEED`・`BT_INFO_ADDRESS` の 3 つに。usb-bt・loopback が description を 0 で埋めてから書くことを確かめる（loopback は `kern_memset`）。(4) `set_bootloader` は NULL 不可のまま、rpi4-bt は ENOTSUP を返す stub。(5) `size_t drv_bt_hci_acl_limit(struct drv_bt_hci *hci);`（class の spinlock の下で `info.acl_data_max` を読む、IRQ からは呼ばない）。(6) UAPI: `BT_BUS_UART`・flag・`bt_info` の欄（B13）・`_Static_assert`・`BT_IOC_SET_SPEED`。`BT_IOC_RESET` の comment を「USB: device を reset。UART: 電源の入れ直しで controller は ROM・host は 115200。どちらも RESET の notice を queue に入れる。transport が誤りから自分で reset した時も同じ notice が来る」に直す。(7) build: platform/arm64/vmunix.mk で `bt-hci.c`・`bt-hci-proto.c` を `CONFIG_DRIVER_USB_BT` か `CONFIG_DRIVER_BCM2711_BT` が y の時に（同じ file の 123 行の `filter` の形）、`rpi4-bt.c` を `CONFIG_DRIVER_BCM2711_BT=y` の時に。usb-bt の側の規則から bt-hci の 2 file を外す。(8) config: `arm64.drivers` に `CONFIG_DRIVER_BCM2711_BT`（bool、"BCM2711 Bluetooth: Raspberry Pi 4 onboard (PL011 UART)"、rpi4、既定 n）、`menu.list` の `Platform` の行に足す、`config/ci/config-rpi4.mk` は menu で生成し直す（`:= n`）。menu の host 試験（`plan/tools/menuconfig-target-host-test.py`）を流す。(9) `emulated_board()` を共有の関数に（rpi4-audio.c も使う）。(10) bt-probe に Read Local Name | review-1 の中1 |

## 4. bluetoothd の Broadcom の始動の手順（B14、p005）

値は 2a 節。括弧の中は kernel の受けの状態（B9）。

1. `GET_INFO`。`bus == BT_BUS_UART` なら Broadcom の候補。
2. **`BT_IOC_RESET`**（kernel: 電源の入れ直し、host 115200、静かな窓 → HUNT、notice を queue）。自分の notice まで読み捨てる（上限 1 s）。
3. `HCI_Reset`（その Command Complete で SYNC）。Read Local Version: manufacturer が 15 でなければ通常の始動へ。Read Local Name で chip の名前を決める（表は実機で確かめる。BCM43455 の ROM は "BCM4345C0" を返す見込み）。
4. 速さ: `BT_INFO_SPEED` があれば `0xFC18` で `speed_max` → Command Complete → `BT_IOC_SET_SPEED`（遷移 → HUNT）。失敗（時間切れ）したら 2 からやり直し、その回は速さを変えずに 115200 のまま続ける。
5. `0xFC2E`（Download_Minidriver）→ Command Complete（SYNC）→ 待ち（2a 節、p005 で決める）。
6. `/lib/firmware/brcm/<chip>.hcd` の record を順に command として送り、各々の Command Complete を待つ。file の検査: 長さの合計が file の大きさに一致、opcode は全て vendor（OGF 0x3F）、最後が `0xFC4E`、大きさは `BTD_FIRMWARE_MAX` 以下。file が無ければ状態 `FIRMWARE_NEEDED`（Intel と同じ）。
7. `0xFC4E` の Command Complete の **直後に `BT_IOC_SET_SPEED 115200`**（遷移 → HUNT。controller の再起動のゴミは HUNT が捨てる）。Command Complete が来ない場合も 1 s で 115200 に戻して進む（実機で確かめる）。250 ms 待つ。
8. `HCI_Reset`（SYNC）。もう一度 4 の速さの切り替え。Read Local Version（LMP subversion が変わったことで patch を確かめる）。
9. BD_ADDR（B15）。
10. 既存の HCI の core の set-up（`session_core()`）。以降は USB の controller と同じ。

求めていない RESET の notice（B9 の自己 reset）は、command を待つ間なら ECONNRESET で始動の失敗、待っていない間なら `BTD_STATE_ERROR`。どちらも daemon が close して open し直し、1 から始める（既存の扱い）。`bt` の表示の `firmware=loaded|-` は Broadcom でも出す。

## 5. WiFi（WS206）との共存

- **電源**: BT_ON（128）と WL_ON（129）は別の line。どちらの driver も相手の line に触らない。WiFi の driver の reset（WL_ON の入れ直し）が BT を止めないことを実機で確かめる。
- **pin**: WS206 p002a の GPIO の helper（GPFSEL3・pull の REG2 の共有）。
- **mailbox**: `drv_rpi4_firmware_property()` は SMP で buffer を atomic に取る（既存）。2 つの driver の初期化が重なっても安全。
- **電波**: 43455 は 2.4 GHz の antenna を BT と WLAN で共有し、chip の中の coexistence が時間を分ける。host の仕事は両方の firmware を読むことだけ。WLAN の `btc_mode` などの iovar は既定のまま。問題（A2DP の途切れ、WiFi の throughput の大きな低下）が出たら WS206 の側の iovar の調整を Future Work にする。
- **試験**（p006）: A2DP の再生中に WiFi で大きな転送（fetch）と scan、BT の mouse を動かしながら WiFi の ping の遅れ。

## 6. SMP（4 core）での並行と lock

- **lock の順**: class の op の mutex（class の中。`send`・`reset`・`set_speed` の呼び出しの間は持たれている）→ driver の電源の mutex → driver の spinlock（irqsave）。driver の spinlock を持ったまま class の関数（`drv_bt_hci_input`・`drv_bt_hci_notice`・`drv_bt_hci_count`・`drv_bt_hci_acl_limit`）を呼ばない（class の spinlock も `LOCK_RANK_DEVICE`、IRQ の handler からは呼べない）。
- **割り込みの handler**（PL011 の 1 本の SPI）: driver の spinlock の下で受け（B7）・送り（B10）・誤りの bit の取り込みと ICR の clear、worker と送り手を起こす。
- **worker**: 受けの ring を取り（spinlock）、放してから assembler と HUNT・SYNC（B9）、class へ渡す。自己 reset は worker 自身が電源の mutex を取って行う（class の op の mutex は transport からは取れないので、逆順にならない）。待っている `send()` を EIO で起こす。
- **`reset` の op**（class の op の mutex の下）: 電源の mutex を取り、状態を RESETTING にして worker が止まるのを上限つきで待ち、電源の入れ直し（B5）、ring を空にし assembler を restart、HUNT、notice、worker を再開。STOPPED を出る。
- **`set_speed` の op**: 電源の mutex の下で B11。worker との取り合いは reset と同じ「worker を止めてから」。
- **`room` の op**: lock を持たずに呼ばれ sleep しない → worker を起こすだけ。
- **速さの切り替え・電源の投入・reset の間**は受けの割り込みを止め、FIFO と ring を**捨てる**（B9 の遷移。第 1 版の「読み捨てない」は誤り）。
- **負荷**: 3 Mbaud の最大で約 300 KB/s、受けの水位 1/2（16 byte）ごとの割り込みで約 2 万回/秒。実際の BR/EDR（HID・A2DP）はその 1 割以下。DMA（BCM の DMA の DREQ）は Future Work。

## 7. Phase（案）

| Phase | 内容 | 見積 | 依存・入る条件 |
| --- | --- | --- | --- |
| p001 | この設計 → design-reviewer | — | — |
| p002 | arm64 で Bluetooth の userland（bluetoothd・bt・bt-probe）の build と、**kernel の `CONFIG_INPUT_BRIDGE` の arm64 の規則と rpi4 の既定 y**（B17）。warning 0。既存の host 試験（bt-daemon・bt-hci-proto）を流す。QEMU raspi4b の起動の回帰（input bridge の入った kernel）は T1 | 2〜3 LW | p001 の review-1 の判定で開始可 |
| p003 | kernel: `rpi4-bt.c`（B3〜B7、B9〜B11）、H4 の assembler と host の試験（B8）、class・UAPI・build・config の変更（B13、B20）、config.txt の生成（B2）、bt-probe の Read Local Name。実機（T1）: 「未確認」の読み、電源、115200 で HCI_Reset・Read Local Version・Name・BD_ADDR（bluetoothd を止めて bt-probe）、clock の前後、QEMU raspi4b で attach しないこと | 6 LW | p002 と **ws206-p002a**（GPIO・mailbox の GPIO の helper）が main に統合済み。この設計の再 review の通過（重大 1〜3・中-1）、**2a 節の p003 の行**、D1・D5 |
| p004 | firmware の package（B18）と license の監査（本文の一致を含む） | 1 LW | p001、D2（p003 と並行可） |
| p005 | bluetoothd の Broadcom の始動（B14・B15、4 節）、HCD の解析と手順の host の試験（台本の controller）。実機（T1）: firmware の読み込み、3 Mbaud、scan | 4 LW | p003、p004、**2a 節の p005 の行**、D3（BD_ADDR が無い時の扱い、実機で読んだ後） |
| p006 | 実機の統合（T1 と ユーザーの UAT）: 8 節。keyboard・mouse・phone（MAP・PBAP）・WiFi との共存・A2DP（WS205 の arm64 が main にあれば）。HFP の通話は p008 | 2 LW | p005（共存は WS206 の main への統合の後、audio は WS205。その時に無い物は p006 の残りとして記録し、WS215 の完了の条件との関係を Q1 に返す） |
| p007 | 規約の全文の見直し | — | p006、p008 |
| p008 | UART の SCO（B16）: 送りの拍か Synchronous Flow Control の設計（design-reviewer）、`sco_set`・`sco_send`、`0xFC1C` の routing、CVSD から。実機の HFP の通話 | 3 LW | p005、ws197-p007b |

計 約 18 LW。p007 は番号が p008 より前だが、WS の最後に行う（p006・p008 の後）。

## 8. 試験

| 段 | 何を | どこで |
| --- | --- | --- |
| host | H4 の assembler（type の混ざり、塊の切れ目、断られた packet の保持、B8 の各種の同期の喪失、byte の間の時間切れ、`acl_data_max` の変化）、HUNT・SYNC の状態の機械（遷移の静かな窓、HUNT の 1 byte ずつの捨て、Command Complete・Status の anchor、SYNC での喪失 → 自己 reset、60 秒 3 回 → STOPPED、`BT_IOC_RESET` で出る）、PL011 の divisor の計算（48 MHz で 115200・921600・2M・3M、上限の切り詰め、誤差の検査）、config.txt の生成（`=n` で元と byte まで同じ、`=y`・serial n で 3 行と comment が消える、serial y で元と同じ）、`bt_info` の大きさ（compile の static assert）、HCD の解析（正しい file・切れた file・vendor 以外の opcode・Launch_RAM の無い file）、Broadcom の始動の手順（socket pair の台本: 始めの RESET と自分の notice の読み捨て、速さの ioctl の順、Launch_RAM の直後の 115200、求めていない notice の ECONNRESET） | 担当（`plan/ws215/tests/`、回帰に使う物は master.md の一覧へ） |
| build | arm64 の kernel（`CONFIG_DRIVER_BCM2711_BT` の y と n）と userland、warning 0。amd64 の kernel と bluetoothd も壊れない | 担当 |
| 実機 smoke（p003・p005） | 電源（CTS が立つまでの時間）、HCI_Reset、Read Local Version・Name、firmware の読み込み、3 Mbaud、scan で周りの機器が見える、`bt` の表示、`GET_STATS` の malformed が 0、HUNT の捨てた byte の log、clock の前後 | T1（Pi 4、SSH） |
| 実機の UAT（p006・p008） | keyboard・mouse（BR/EDR と LE、pairing、再接続、入力の遅れ）、audio（A2DP の headset、WS205 が main にあれば）、phone（MAP の SMS・PBAP、HFP の通話は p008）、WiFi と同時（5 節）、10 分の A2DP で malformed・stall・送りの止まり（D5）が増えない | pairing と耳の確認はユーザー、他は T1 |

QEMU は BT を emulate しないので QEMU の BT の試験は無い。QEMU の raspi4b で driver が attach しない（serial の console を奪わない）ことは T1 の rpi4 の QEMU の boot で確かめる。

## 9. 判断（Q1 経由でユーザーへ、推しつき）

| ID | 問い | 推し（review-1 の意見） | いつまでに |
| --- | --- | --- | --- |
| D1 | serial の console の build で BT を使えなくしてよいか（B2）。代わりに serial の console を mini UART へ移せば両立するが、HAL の early console の書き直しと core の clock の固定が要る | **使えなくする**（serial の build は debug 用、今の挙動のまま）。review-1 も賛成: mini UART の console は core の clock を固定するので Future Work | p003 の前 |
| D2 | Pi 4 の image に `BCM4345C0.hcd`（Cypress の EULA: object code のまま、Cypress の IC と使う時だけ再配布可、改変・逆解析の禁止）を入れてよいか | **入れる**。review-1 は条件つき賛成: 既定 off の package、Pi 4 の config だけで選ぶ、license の本文の一致を p004 で確かめる | p004 の前 |
| D3 | controller も DT も address を持たない時の BD_ADDR | **実機で読んでから決める**（review-1 も同じ）。代案は mailbox の board の MAC（`GET_BOARD_MAC_ADDRESS`）から決まった規則で作る（推測。firmware が DT に入れる値との関係は実機で確かめる） | p005 の前 |
| D4 | 実機の UAT の機器（keyboard・mouse・headset・Android の phone）は amd64 の UAT と同じ物でよいか | **同じ物**（review-1 も賛成。A2DP は WS205 の arm64 に依存） | p006 の前 |
| D5 | Pi 4 の UART の部品（PL011）には、相手からの「待って」の信号（CTS）が頻繁に変わると送りが止まることがある、という癖があり、Raspberry Pi の Linux はその回避を入れている（DT の `cts-event-workaround`）。回避の具体の手順は Linux の GPL の code（とその commit の説明）にしか書かれておらず、WS215 は GPL の code を読まない決まり（B19）。どうするか | **(a) 回避なしで始める**: zedBSD は CTS の割り込みを使わず、部品の自動の流れの制御だけを使う作りにする（回避が要る場面を避けられる見込み、推測）。実機の 10 分の A2DP と WiFi の大きな転送の同時で送りが止まらないかを見て、止まったら改めて相談する。止まっても B10 の 2 s の上限で controller を reset して戻る（その間の音は途切れる）。他の案: (b) WS141 と同じ「値・手順は事実として読み、写さない、最後に類似の監査」の例外を WS215 のこの 1 点にだけ広げる | p003 の前 |

## 10. 危うい所

| 所 | 内容 | 備え |
| --- | --- | --- |
| 3 Mbaud | PL011 の上限（clock/16）ちょうど。誤差は 0 だが余裕も無い | 実機で malformed を数え、増えるなら DT の値より下（2 Mbaud）に。`speed_max` の上限を daemon の設定で下げられるようにする |
| CTS の止まり | BCM の PL011 の癖（D5） | CTS の割り込みを使わない作り。10 分の A2DP で送りが止まらないこと。止まれば B10 の 2 s で reset して戻る |
| CTS/RTS の配線 | DT は GPIO 30・31 を mux しない（`bt_pins` が空） | 実機で GPFSEL3 を読み、ALT3 にして RTS が効くことを確かめる |
| HUNT の anchor の誤り | ゴミが偶然 Command Complete の形になる | daemon は答えの opcode を照らすので、誤りは時間切れとして始動のやり直しになる。HUNT で捨てた byte を log に出し、実機で頻度を見る |
| 電源の後の CTS | controller が受けられる合図として CTS が使えるか（推測） | 実機で測る。使えなければ固定の待ち（出典つき）に変える |
| arm64 の userland | bluetoothd（約 48,600 行）を arm64 で build したことが無い | p002 を先に。直しが大きければ p002 を分ける |
| config.txt と計測 | `enable_uart` などを外すと firmware の clock が変わりうる | `CONFIG_DRIVER_BCM2711_BT` の既定 n、clock の前後の log |
| 始動の長さ | UART では daemon の始動のたびに firmware を読む | 3 Mbaud で約 1〜2 秒の見込み、実機で測る |
| firmware の license | Cypress の EULA（D2） | ユーザーの判断の後に p004 |

## 11. 未確認（p003 の最初に実機で、kernel の中で読む）

- GPIO 30〜33 の今の GPFSEL3 と pull（firmware が ALT3 にしているか）。
- mailbox の GPIO 128 の今の状態（firmware が BT_ON を上げたままか）と向き（`GET_GPIO_CONFIG`。firmware が出力に設定済みと推測）、`GET_CLOCK_RATE` の UART（ID 2）の clock。
- 実機の DT（firmware の fixup の後）の bt の node の status、`local-bd-address` が埋まるか。
- `disable-bt`・`enable_uart`・`uart_2ndstage` を外した config.txt で、firmware・HDMI・GPU（WS141）の挙動と core・v3d・arm の clock が変わらないこと（boot-test の PNG と clock の log）。
- 電源を上げてから CTS が立つまで・HCI_Reset に答えるまでの時間。43455 の ROM の Read Local Version（manufacturer、LMP subversion）、Local Name、BD_ADDR。
- 3 Mbaud で `0xFC18` が通るか。Launch_RAM の Command Complete が来るか。Download_Minidriver の後の待ちの要否（p005）。

## 12. 他の WS への知らせ（Q1 へ）

- **WS206（W3、p002a）**: GPIO の helper と mailbox の GPIO の helper の持ち主。WS215 の要求は B12 の 3 つ（pull は列挙型、register の符号は DT と up・down が逆なので helper の host 試験で確かめる、mailbox の読みの ENOTSUP を返す）。WS215 は名前・引数を WS206 の第 3 版に合わせる。
- **WS206 の W6**: linux-firmware の WHENCE は `brcm/brcmfmac43455-sdio.raspberrypi,3-model-b-plus.txt`（`...raspberrypi,4-model-b.txt` はその link）を GPLv2 と書いている（第 1 版からの知らせの再掲）。
- **WS206 と `emulated_board()`**: WS215 p003 が rpi4-audio.c の static から共有の関数に移す。WS206 も QEMU の raspi4b を見分けるなら同じ関数を使う（先に要る方が移す、Q1 が調整）。
- **WS197（p007b）**: WS215 p008（UART の SCO）が依存する。UAPI の SCO の comment（36 ms の queue）は USB の拍が前提で、UART の場合の書き方を p008 で足す。
