<!-- awesome-plan project=zedbsd record=ws215-p001 -->

# ws215-p001: Raspberry Pi 4 の内蔵の Bluetooth の調査と設計

Parent: [WS215](../ws.md)
Status: in-progress（2026-10-13 P3: 設計の第 1 版。design-reviewer 待ち。判断 D1〜D4 を Q1 経由でユーザーへ）
Phase disposition: normal
Queue: P3 の lane（Q1 の割当、2026-10-13）

## 由来

2026-10-12 ユーザー:「描画の最適化の後、計測を取ったら、SMPを入れます。さらに計測をとって、最適化を終了したいです。そのあと、RPi4のWiFiとBluetoothを実装し、RPi4は完了です。」SMP（WS208）は main に入った。WiFi は [WS206](../../ws206/ws.md)（P1 が並行して設計中）、Bluetooth はこの WS。

この Phase は設計だけを書く（code は書かない）。実機の Pi 4（10.0.30.2）は 2026-10-13 に止まっていて読めなかった。実機で読む事は「未確認」の節に置き、p003 の最初に確かめる。

## 1. 今の tree の事実（2026-10-13、agent/p3 = main a66ddd30f の merge の後）

| 所 | 事実 |
| --- | --- |
| HCI の class | `src/drivers/generic/bt-hci.c`（1249 行）・`include/drivers/generic/bt-hci.h`。transport が `drv_bt_hci_register()` で controller を登録し、組み立てた event・ACL を `drv_bt_hci_input()` へ渡す。class が `/dev/bluetoothN` を出し、event と ACL の 2 つの ring を持つ。queue が満ちると input を断り、transport は packet を持ったまま controller からの取り込みを止める（USB は endpoint の NAK で controller が待つ）。read が空きを作ると class が `room` の op を呼ぶ。op は `send`・`set_bootloader`・`reset`・`room`・`sco_set`・`sco_send`。`send`・`reset` などは class の mutex の下で 1 つずつ、sleep してよい |
| 純粋な部分 | `bt-hci-proto.c`: ring、受けた byte の列を packet に組む assembler（mode は **event だけ** か **ACL だけ**。USB は endpoint ごとに type が決まるので、type の byte の混ざった H4 の流れを組む mode は無い）、書きの検査、SCO の assembler・fifo。host の試験 `plan/ws143/tests/bt-hci-proto-host-test.sh` |
| UAPI | `include/uapi/bluetooth.h`: H4 の形、`BT_IOC_GET_INFO`・`SET_BOOTLOADER`・`RESET`・`SET_ACL_MAX`・`GET_STATS`・`SET_SCO`。`struct bt_info` に `bus`（`BT_BUS_USB` だけ）と `reserved[4]`。kernel の notice は `BT_PACKET_NOTICE_RESET` だけ |
| USB の transport | `src/drivers/usb/usb-bt.c`（2125 行）。Intel の bootloader の経路、SCO は isochronous の alternate |
| bluetoothd | `userland/base/bluetoothd/`（約 48,600 行）。firmware の読み込みは daemon がする（kernel は packet を運ぶだけ、`bluetooth.h` の冒頭）。始動は `btd_session_start()`: `GET_INFO` → vendor が Intel（0x8087）なら Intel の firmware → HCI の core の set-up。firmware の folder は `BTD_FIRMWARE_FOLDER` = `/lib/firmware/intel` の 1 つ。node は `/dev/bluetooth0`〜`15` の開けた最小（`-f` で指定可） |
| **arm64 で build されない物** | `userland/base/bluetoothd/Makefile`・`userland/base/bt/Makefile`・`userland/tests/bt-probe/Makefile` の package の arch が **`amd64` だけ**。`CONFIG_INPUT_BRIDGE`（`/dev/input/bridge`、Bluetooth の HID の入力）も amd64 だけ（Makefile 245 行）。kernel の HCI の class と usb-bt は arm64 でも build される（`platform/arm64/vmunix.mk` 131 行） |
| Pi 4 の UART | HAL の early console（`src/hal/arm64/bsp-rpi4/uart.c`）は PL011（0xfe201000）を GPIO 14・15（ALT0）で使う。**`CONFIG_RPI4_SERIAL_CONSOLE=y` の時だけ**（既定 n、2026-10-12 ユーザー）。n の時は HAL も `rpi4-console.c` も PL011 に触らない。`prekern_bsp_cons_irq_init()` が FDT の最初の `arm,pl011` の割り込みを mask したまま残す（driver が登録するまで） |
| config.txt | `platform/arm64/config.txt`（静的な 1 file）: `enable_uart=1`、`init_uart_clock=48000000`、`uart_2ndstage=1`、**`dtoverlay=disable-bt`**。serial の console の有無で変わらない |
| Pi 4 の driver | `src/drivers/platform/rpi4/`: console、ethernet、firmware（mailbox の property、`drv_rpi4_firmware_property()`、SMP で buffer を atomic に取る）、pcie、sdhci、audio。GPIO の pin の機能の共通の helper は無い（audio と HAL の LED が GPFSEL4 をそれぞれ直に読み書き） |
| 起動の順 | `src/kern/platform/rpi4.c`: console → sdhci → GENET → PCIe → audio → GPU。QEMU の raspi4b の見分けは `rpi4-audio.c` の `emulated_board()`（PCIe の node が disabled） |
| firmware の包み | `userland/firmware/`: 1 族 1 directory、既定 off、immutable な上流から取得・検証、license・manifest を `/lib/firmware` へ（README.md） |

## 2. Pi 4 の hardware の事実（出典つき）

出典は (a) `vendor/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb` を dtc で読んだもの（firmware の fixup の前の物）、(b) `disable-bt.dtbo`、(c) RPi-Distro/bluez-firmware の tree、(d) 公開の文書（BCM2711 ARM Peripherals、ARM PL011 TRM、Raspberry Pi の mailbox の property の wiki・config.txt の文書、Bluetooth Core 仕様、DT binding `brcm,bcm43438-bt`）。

| 事実 | 値 | 出典 |
| --- | --- | --- |
| BT の UART | `serial@7e201000`（ARM 0xfe201000）= **PL011 の UART0**、`compatible = "arm,pl011-axi", "arm,pl011"`、割り込み SPI 0x79（GIC の 153）、clock は cprman の 0x13（UART clock）、`uart-has-rtscts`、`cts-event-workaround`、`skip-init` | (a) |
| BT の node | 上の子 `bluetooth`: `compatible = "brcm,bcm43438-bt"`（43455 でもこの名前）、**`max-speed = 3000000`**、`shutdown-gpios = <&expgpio 0 0>`、`local-bd-address = [00 ×6]`、`fallback-bd-address`、status okay | (a) |
| mini UART | `serial@7e215040`（`brcm,bcm2835-aux-uart`）にも `bluetooth` の子があり **status disabled**、max-speed 230400 | (a) |
| alias | 既定: `serial0` = mini UART、`serial1` = PL011。`disable-bt` は bt の node を disabled、uart0 の pin を GPIO 14・15 へ、alias を入れ替え | (a)(b) |
| pin | `uart0_pins` = GPIO 32（TXD0）・33（RXD0）、function ALT3、pull 32 なし・33 up。CTS0・RTS0 は GPIO 30・31 の ALT3（`uart0-ctsrts-gpio30`、pull 30 up・31 なし）。base の dtb の `bt_pins` は空（"-"）なので、**30・31 を ALT3 にするのは firmware か OS**（未確認、p003 で GPFSEL3 を読む） | (a)、BCM2711 Peripherals の GPIO の alt の表 |
| BT_REG_ON | firmware の GPIO の拡張（`raspberrypi,firmware-gpio`）の line 0 `BT_ON`（line 1 が `WL_ON`）。mailbox の GPIO 番号は 128 + line = **128**（WiFi は 129） | (a)、mailbox の wiki（`SET_GPIO_STATE` 0x00038041、`GET_GPIO_STATE` 0x00030041） |
| clock | BT の node に `clocks` は無い（LPO は board が供給、OS は何もしない）。PL011 の clock は config.txt の `init_uart_clock=48000000`。3,000,000 baud = 48 MHz / 16 ÷ 1（IBRD 1、FBRD 0、PL011 の上限ちょうど）。2,000,000 は IBRD 1・FBRD 32、115,200 は 26・3 | (a)、PL011 TRM の divisor の式 |
| PL011 の errata | `cts-event-workaround`: BCM の PL011 は CTS が頻繁に変わると送りが止まる（lockup）ことがある。回避は CTS の割り込みを clear する前に dummy の読みを入れる（公開の patch の説明、code は見ない） | (a)、公開の patch の説明文（Linux の code は読まない） |
| firmware | `BCM4345C0.hcd`（63,806 byte、sha256 `51c45e77ddad91a19e96dc8fb75295b2087c279940df2634b23baf71b6dea42c`）。**322 個の `0xFC4C`（Write_RAM）と最後の 1 個の `0xFC4E`（Launch_RAM）**、record は opcode 2 byte（LE）+ 長さ 1 byte + 引数、最長の引数 204 byte（2026-10-13 P3 が file を数えた）。Pi 4 の名前 `BCM4345C0.raspberrypi,4-model-b.hcd` は同じ file への link | (c) commit `adb4d55c8f193ff16ece74dd72fd6e8801632103`（2025-10-02）の `debian/firmware/broadcom/`、`debian/bluez-firmware.links` |
| firmware の license | `debian/copyright`: `debian/firmware/broadcom/*` は **Cypress Wireless Connectivity Devices の EULA**（object code のまま、Cypress の IC と使う時に限り複製・再配布可、改変・逆解析の禁止）。同じ directory の `LICENSE.cypress` は kernel.org の cgit の **HTML のページを保存した物**（本文はあるが HTML の形）。linux-firmware の `LICENCE.cypress` が同じ license の平文。**linux-firmware（2026-10 の main の WHENCE）に `.hcd` の BCM4345C0 は無い** | (c)、linux-firmware の WHENCE |

## 3. 設計

| ID | 決め | 理由 |
| --- | --- | --- |
| B1 | **BT は PL011（UART0、GPIO 30〜33、CTS/RTS つき）**。mini UART は使わない | mini UART は core の clock に従う divisor・8 byte の FIFO・CTS/RTS の自動の制御が弱い。DT の既定も PL011 |
| B2 | **serial の console と BT は同じ PL011 を取り合うので同時に使わない**。`CONFIG_RPI4_SERIAL_CONSOLE=n`（既定）: config.txt から `dtoverlay=disable-bt`・`enable_uart=1`・`uart_2ndstage=1` を外し、BT を使う。`=y`: 今の 3 行を保ち、bt の node が disabled なので BT の driver は attach しない。config.txt は build が `platform/arm64/config.txt` から `$(BUILD)/config.txt` を作る（serial の時だけ 3 行を足す） | 既定の build は 2026-10-12 から serial を使わない。`enable_uart=1` を mini UART が primary のまま残すと firmware が core の clock を固定し、WS141 の計測の条件が変わるので外す。serial の build は今と同じ挙動。D1 |
| B3 | 新しい driver **`src/drivers/platform/rpi4/rpi4-bt.c`**（PL011 の UART の HCI の transport）。`CONFIG_DRIVER_BCM2711_BT`（rpi4 で既定 y）。`rpi4.c` の audio の後で呼ぶ。attach は sleep しない: DT を読み、register を map し、pin を設定し、割り込みを登録して unmask し（HAL の `prekern_bsp_cons_irq_init()` が mask したまま残している）、class に登録する。電源投入は driver の worker thread が後で行い（boot を待たせない）、それまでの `send()` は電源が入るのを上限つきで待つ | 既存の Pi 4 の driver の形（`drv_rpi4_audio_init(fdt_phys)`） |
| B4 | **DT の見つけ方**: `brcm,bcm43438-bt` の node を全て見て、親が `arm,pl011` で、親と自分が両方 enabled の物を取る（mini UART の子は disabled なので外れる）。`max-speed`・`local-bd-address` を読む。**attach しない条件**: その node が無い・disabled（`disable-bt`）、`RPI4_SERIAL_CONSOLE` の build（二重の guard）、QEMU の raspi4b（`emulated_board()` と同じ印。QEMU の PL011 は serial の console なので触らない）、mailbox の `SET_GPIO_STATE` が ENOTSUP | 誤って console の UART を奪わない |
| B5 | **pin と電源**: 共通の小さな helper（下の B12）で GPIO 30・31・32・33 を ALT3、pull は 30 up・31 なし・32 なし・33 up（DT の値）。電源は mailbox で GPIO 128 を 0 → 10 ms → 1、その後 controller の起動を待つ（100 ms、実機で詰める）。`BT_IOC_RESET` は同じ電源の入れ直し（controller は ROM の状態、115200 baud に戻る）と host の UART の 115200 への戻し、受けの途中の packet の破棄、RESET の notice | DT の `shutdown-gpios` と binding。BT_ON と WL_ON は別の line なので WiFi に触らない |
| B6 | **PL011 の設定**: 8N1、FIFO 有効、**CTS と RTS の自動の制御（CR の CTSEn・RTSEn）**、受けの FIFO の水位 1/2 と受けの timeout の割り込み、送りの FIFO の水位 1/8 の割り込み。divisor は mailbox の `GET_CLOCK_RATE`（UART の clock）から計算する（48 MHz を決め打ちにしない）。速さの上限は min(DT の `max-speed`, clock/16)。CTS の割り込みの clear は 2 節の errata の回避（dummy の読み）をする | RTS の自動の制御で、kernel が読まない間は controller が送りを止める（USB の NAK と同じ「捨てない」の性質）|
| B7 | **受け**: 割り込みの handler は FIFO を driver の受けの ring（16 KiB）へ移すだけ（spinlock、irqsave）。ring が満ちたら受けの割り込みを mask し、FIFO が満ちて RTS が下り controller が止まる。worker thread が ring を **H4 の assembler**（下の B8）に通し、組めた packet を class へ（event・ACL は `drv_bt_hci_input()`、SCO は `drv_bt_hci_sco_input()`）。class が断ったら packet を持ったまま worker は止まり、`room` で起きて続ける。ring に空きができたら受けの割り込みを戻す | class の「捨てない」約束を UART で守る。ISR は短く、SMP でどの CPU でもよい |
| B8 | **H4 の assembler を `bt-hci-proto.c` に足す**（新しい mode `BT_HCI_ASSEMBLE_H4`）: 先頭の type の byte（2 ACL、3 SCO、4 event）で header の長さ（4・3・2）と長さの field を選ぶ。type が他の値、ACL が上限を越える、などは **流れの同期を失った** とみなす（H4 には再同期の印が無い）。host の試験で、塊の切れ目・複数の packet・断られた packet の保持・壊れた type を確かめる | 純粋な code なので host で試せる。USB の mode は変えない |
| B9 | **同期を失った時・PL011 の overrun・framing の誤り**: `malformed` を数え、kernel が B5 の電源の入れ直しをして **既存の `BT_PACKET_NOTICE_RESET`** を queue に入れる（UAPI の comment に「controller の誤りで kernel が自分で reset した時も来る」と足す）。daemon は求めていない RESET の notice で session を最初から（firmware から）やり直す（p005 で今の daemon の扱いを確かめ、無ければ足す）。kernel の自分の reset は 60 秒に 3 回までとし、越えたら controller を止めたままにして log に出す（壊れた配線での繰り返しを防ぐ）| 新しい notice の型を足さない。UART の誤りからの戻り方は電源の入れ直ししか無い |
| B10 | **送り**: `send()` は 1 つの H4 の packet を送りの ring に入れ、割り込みが FIFO に詰め、全部を FIFO に入れ終わったら返る（時間の上限は usb-bt と同じ 1 s・2 s）。SCO の `sco_send()` は sleep しない別の fifo（usb-bt の `bt_hci_fifo`）。送りの順は **packet の単位で交互**（1 つの packet の byte の間に他の packet を挟まない）。`sco_set()` は UART に alternate が無いので links を覚えるだけ、answer の alternate 0・mtu は packet の長さ | H4 は packet を途中で切れない |
| B11 | **速さの切り替えは daemon が行い、kernel は host 側の divisor だけを変える**。UAPI に `BT_IOC_SET_SPEED`（`_IOW('b', 6, uint32_t)`、baud）を足す。kernel は送りの FIFO が空になり PL011 の BUSY が落ちるのを待ち、UART を止め、divisor を書き、戻す。USB の transport は ENOTSUP。daemon は Broadcom の vendor の command `0xFC18`（Update_UART_Baud_Rate、引数 2 byte の 0 と 4 byte の baud、LE）の Command Complete を受けた後に呼ぶ（その間 controller は何も送らない: 1 command ずつの daemon なので、始動の間は他の event が無い） | firmware の読み込みの後、controller は 115200 に戻るので、切り替えは firmware の手順の中で 2 回要る。手順を daemon に置けば kernel は command を覗かなくてよい |
| B12 | **Pi 4 の共通の小さな helper（WS206 と共有）**: (1) `rpi4-gpio`: pin の function と pull を spinlock の下で読み書きする（GPFSEL3 は GPIO 30〜39 で、**BT の 30〜33 と WiFi の 34〜39 が同じ register**、pull の CNTRL2 も 32〜47 で共有。SMP の下で 2 つの driver が read-modify-write で互いを消さないため）。(2) `drv_rpi4_firmware_set_gpio(gpio, state)`・`get_gpio()`（mailbox の tag）。**先に入る Phase（ws206-p002 か ws215-p003）が作り、後の方が使う**（Q1 が調整） | 2 つの driver が同じ register を持つ |
| B13 | **`struct bt_info` の拡張（UAPI、大きさは不変）**: `BT_BUS_UART`（2）、flag `BT_INFO_SPEED`（速さを変えられる、0x0004）・`BT_INFO_ADDRESS`（board の address がある、0x0008）。`reserved[4]` の 16 byte を `uint32_t speed_max; uint8_t board_address[6]; uint8_t pad[2]; uint32_t reserved;` に。`name` は "Raspberry Pi 4 Bluetooth"、`physical_path` は "uart0"、vendor・product は 0 | daemon が UART の controller と速さの上限、DT の address を知るため。既存の UAPI の利用者（daemon・bt・Settings の backend）は reserved を読まないので壊れない（p003 で grep して確かめる） |
| B14 | **bluetoothd の Broadcom の始動**（`broadcom.c`、Intel の `intel.c` と同じ形で、file の解析と手順を分ける）。対象は `bus == BT_BUS_UART` かつ Read Local Version の manufacturer が 15（Broadcom）。手順は 4 節 | firmware の読み込みは daemon の仕事（既存の設計） |
| B15 | **BD_ADDR**: (1) controller の address（Read BD_ADDR）が Broadcom の既定の値（実機で読む。BCM4345C0 の ROM の既定と、全部 0）でなければそのまま。(2) そうなら DT の `local-bd-address`（firmware の fixup、`dtparam=bdaddr`）が 0 でなければ `0xFC01`（Write_BD_ADDR）で書く。(3) どちらも無ければ D3 で決める | DT の `fallback-bd-address` は「controller の address を優先し、無い時に DT」の意味（binding） |
| B16 | **SCO**: Pi 4 の 43455 の PCM の pin は SoC に繋がっていないので、daemon が始動で `0xFC1C`（Write_SCO_PCM_Int_Param）の routing を transport（HCI）にする。最初は CVSD、mSBC（transparent）は実機で Broadcom の WBS の command が通るか確かめてから（ws197-p007 の HFP の後） | SCO の音は HCI で運ぶ（USB と同じ上の層）|
| B17 | **arm64 で Bluetooth の userland を build する**: bluetoothd・bt・bt-probe の package の arch に arm64、`CONFIG_INPUT_BRIDGE` を rpi4 で y。移植の問題（x86 に固有の code、型の大きさ）が出たらその Phase で直す | 今は amd64 だけ。HID の keyboard・mouse は input bridge を通る |
| B18 | **firmware の package `userland/firmware/brcmbt/`**（package `brcmbt-firmware`、既定 off、Pi 4 の UAT・release の config で選ぶ）: RPi-Distro/bluez-firmware の commit `adb4d55c…` の `debian/firmware/broadcom/BCM4345C0.hcd`（sha256 は 2 節）を `/lib/firmware/brcm/BCM4345C0.hcd` に、license は **linux-firmware の tag `20260410`（他の firmware の包みと同じ commit）の平文の `LICENCE.cypress`** と bluez-firmware の `debian/copyright` の該当の段落、manifest（取得元・commit・sha256・license の対応）。daemon は `/lib/firmware/brcm/` を読む | 既存の firmware の包みの決まり。HTML の LICENSE.cypress は使わない。D2 |
| B19 | **出典の規則**: Linux の btbcm・hci_bcm・hci_h4・amba-pl011（GPL）は**読まない・写さない**（WS141 の GPL の例外は WS215 に及ばない）。使う物: Bluetooth Core 仕様、ARM PL011 TRM、BCM2711 ARM Peripherals、Raspberry Pi の公開の文書（config.txt、mailbox の property）、DT の binding の文書、Cypress/Infineon の公開の HCI の vendor command の文書、firmware の file の形（record の数え方は 2 節で自分で確かめた）。公開の tutorial（rpi4-osdev の part7-bluetooth）は事実の確かめにだけ使い、写さない | Guardrail の license の境界 |

## 4. bluetoothd の Broadcom の始動の手順（B14）

1. `GET_INFO`。`bus == BT_BUS_UART` なら Broadcom の候補。controller は 115200 baud。
2. HCI_Reset（`0x0C03`）。Read Local Version（`0x1001`）: manufacturer が 15 でなければ通常の始動へ。LMP subversion と Read Local Name（`0x0C14`）で chip の名前を決める（表は実機で確かめる。BCM43455 の ROM は "BCM4345C0" を返す見込み）。
3. 速さ: `BT_INFO_SPEED` があれば `0xFC18` で `speed_max`（3,000,000）→ Command Complete → `BT_IOC_SET_SPEED`。失敗したら 115200 のまま続ける（firmware の転送が約 5.5 秒に伸びるだけ）。
4. `0xFC2E`（Download_Minidriver）→ Command Complete → 50 ms 待つ。
5. `/lib/firmware/brcm/<chip>.hcd` の record を順に command として送り、各々の Command Complete を待つ。file の検査: 長さの合計が file の大きさに一致、opcode は全て vendor（OGF 0x3F）、最後が `0xFC4E`、大きさは `BTD_FIRMWARE_MAX` 以下。file が無ければ状態 `FIRMWARE_NEEDED`（Intel と同じ）。
6. `0xFC4E` の後、controller は patch で走り直し、**115200 に戻る**。250 ms 待ち、`BT_IOC_SET_SPEED 115200`。HCI_Reset。
7. もう一度 3 の速さの切り替え。Read Local Version（LMP subversion が変わったことで patch を確かめる）。
8. BD_ADDR（B15）、SCO の routing（B16）。
9. 既存の HCI の core の set-up（`session_core()`）。以降は USB の controller と同じ。

求めていない RESET の notice（B9）を受けたら 1 から。`bt` の表示の `firmware=loaded|-` は Broadcom でも出す。

## 5. WiFi（WS206）との共存

- **電源**: BT_ON（128）と WL_ON（129）は別の line。どちらの driver も相手の line に触らない。WiFi の driver の reset（WL_ON の入れ直し）が BT を止めないことを実機で確かめる。
- **pin**: B12 の共通の helper（GPFSEL3・CNTRL2 の共有）。
- **mailbox**: `drv_rpi4_firmware_property()` は SMP で buffer を atomic に取る（既存）。2 つの driver の初期化が重なっても安全。
- **電波**: 43455 は 2.4 GHz の antenna を BT と WLAN で共有し、chip の中の coexistence（BT と WLAN の core の間の調停）が時間を分ける。host の仕事は両方の firmware を読むことだけ（BT の patch・WLAN の firmware と NVRAM の coex の設定）。WLAN の `btc_mode` などの iovar は既定のまま。共存で問題（A2DP の途切れ、WiFi の throughput の大きな低下）が出たら WS206 の側の iovar の調整を Future Work にする。5 GHz の AP なら重ならない。
- **試験**（p006）: A2DP の再生中に WiFi で大きな転送（fetch）と scan、BT の mouse を動かしながら WiFi の ping の遅れ。

## 6. SMP（4 core）での並行

- 割り込みの handler（PL011 の 1 本の SPI）と worker thread、`send()`・`sco_send()` を呼ぶ process は別の CPU で走りうる。PL011 の register と受け・送りの ring は 1 つの spinlock（irqsave）で守る。H4 の assembler は worker だけが触る（lock なし）。
- class の約束: `room` は lock を持たずに呼ばれ sleep しない → worker を起こすだけ。`send`・`reset`・`set_speed` は class の mutex で 1 つずつ。reset と worker の取り合いは usb-bt と同じく「worker を止めて ring を空にしてから電源」。
- 速さの切り替えの間（UART を止める短い間）は受けの割り込みを止め、FIFO を読み捨てない（controller は Command Complete の後は送らない）。
- 負荷: 3 Mbaud の最大で約 300 KB/s、16 byte ごとの割り込みで約 2 万回/秒。実際の BR/EDR（HID・A2DP）はその 1 割以下。DMA（BCM の DMA の DREQ）は Future Work。

## 7. Phase（案）

| Phase | 内容 | 見積 | 依存 |
| --- | --- | --- | --- |
| p001 | この設計 → design-reviewer | — | — |
| p002 | arm64 で Bluetooth の userland（bluetoothd・bt・bt-probe・input bridge）を build（B17）。warning 0。既存の host 試験（bt-daemon・bt-hci-proto）を流す | 2 LW | p001 |
| p003 | kernel: 共通の helper（B12、WS206 と調整）、`rpi4-bt.c`（B3〜B7、B9〜B11）、H4 の assembler と host の試験（B8）、UAPI（B11・B13）、config.txt の生成と `CONFIG_DRIVER_BCM2711_BT`（B2）。実機（T1）: 「未確認」の読み、電源、115200 で HCI_Reset・Read Local Version・Name・BD_ADDR（bt-probe） | 6 LW | p002 |
| p004 | firmware の package（B18）と license の監査（ユーザーの D2 の後） | 1 LW | p001（p003 と並行可） |
| p005 | bluetoothd の Broadcom の始動（B14〜B16、4 節）、HCD の解析と手順の host の試験（台本の controller）。実機（T1）: firmware の読み込み、3 Mbaud、scan | 4 LW | p003、p004 |
| p006 | 実機の統合（T1 と ユーザーの UAT）: 8 節 | 2 LW | p005（audio は WS205、HFP は ws197-p007、共存は WS206 の main への統合の後。その時に無い物は p006 の残りとして記録し、WS215 の完了の条件との関係を Q1 に返す） |
| p007 | 規約の全文の見直し | — | p006 |

計 約 15 LW。p002 は p003 と並べてもよい（衝突しない）。

## 8. 試験

| 段 | 何を | どこで |
| --- | --- | --- |
| host | H4 の assembler（type の混ざり、塊の切れ目、断られた packet の保持、壊れた type → 同期の喪失）、PL011 の divisor の計算（48 MHz で 115200・921600・2M・3M、上限の切り詰め）、HCD の解析（正しい file・切れた file・vendor 以外の opcode・Launch_RAM の無い file）、Broadcom の始動の手順（socket pair の台本: 速さの ioctl の順、Launch_RAM の後の 115200、RESET の notice からのやり直し） | 担当（`plan/ws215/tests/`、回帰に使う物は master.md の一覧へ） |
| build | arm64 の kernel と userland、warning 0。amd64 の bluetoothd も壊れない | 担当 |
| 実機 smoke（p003・p005） | 電源、HCI_Reset、Read Local Version、firmware の読み込み、3 Mbaud、scan で周りの機器が見える、`bt` の表示、`GET_STATS` の malformed が 0 | T1（Pi 4、SSH） |
| 実機の UAT（p006） | keyboard・mouse（BR/EDR と LE、pairing、再接続、入力の遅れ）、audio（A2DP の headset、WS205 が main にあれば）、phone（MAP の SMS・PBAP、HFP の通話は ws197-p007 の後）、WiFi と同時（5 節）、10 分の A2DP で malformed・stall が増えない | pairing と耳の確認はユーザー、他は T1 |

QEMU は BT を emulate しないので QEMU の試験は無い。QEMU の raspi4b で driver が attach しない（serial の console を奪わない）ことは T1 の rpi4 の QEMU の boot で確かめる。

## 9. 判断（Q1 経由でユーザーへ、推しつき）

| ID | 問い | 推し |
| --- | --- | --- |
| D1 | serial の console の build で BT を使えなくしてよいか（B2）。代わりに serial の console を mini UART へ移せば両立するが、HAL の early console の書き直しと core の clock の固定が要る | **使えなくする**（serial の build は debug 用、今の挙動のまま）。mini UART の console は Future Work |
| D2 | Pi 4 の image に `BCM4345C0.hcd`（Cypress の EULA: object code のまま、Cypress の IC と使う時だけ再配布可、改変・逆解析の禁止）を入れてよいか。既定 off の package で、Pi 4 の UAT・release の config だけで選ぶ | **入れる**（intelbt・rtl8822b と同じ扱い。daemon は file の record を command として送るだけで逆解析しない） |
| D3 | controller も DT も address を持たない時の BD_ADDR | **実機で読んでから決める**。どちらも無ければ、board の serial 番号から作った address を書く案を示す |
| D4 | 実機の UAT の機器（keyboard・mouse・headset・Android の phone）は amd64 の UAT と同じ物でよいか | 同じ物 |

## 10. 危うい所

| 所 | 内容 | 備え |
| --- | --- | --- |
| 3 Mbaud | PL011 の上限（clock/16）ちょうど。誤差は 0 だが余裕も無い | 実機で malformed を数え、増えるなら DT の値より下（2 Mbaud）に。`speed_max` の上限を daemon の設定で下げられるようにする |
| CTS の lockup | BCM の PL011 の errata（2 節） | 回避を入れ、10 分の A2DP で送りが止まらないこと |
| CTS/RTS の配線 | DT は GPIO 30・31 を mux しない（`bt_pins` が空） | 実機で GPFSEL3 を読み、ALT3 にして RTS が効くことを確かめる |
| arm64 の userland | bluetoothd（約 48,600 行）を arm64 で build したことが無い | p002 を先に。直しが大きければ p002 を分ける |
| firmware の license | Cypress の EULA（D2） | ユーザーの判断の後に p004 |

## 11. 未確認（p003 の最初に実機で、kernel の中で読む）

- GPIO 30〜33 の今の GPFSEL3 と pull（firmware が ALT3 にしているか）。
- mailbox の GPIO 128 の今の状態（firmware が BT_ON を上げたままか）、`GET_CLOCK_RATE` の UART の clock。
- 実機の DT（firmware の fixup の後）の bt の node の status、`local-bd-address` が埋まるか。
- `disable-bt`・`enable_uart` を外した config.txt で、firmware・HDMI・GPU（WS141）の挙動が変わらないこと（boot-test の PNG）。
- 43455 の ROM の Read Local Version（manufacturer、LMP subversion）、Local Name、BD_ADDR。3 Mbaud で `0xFC18` が通るか。電源を上げてから HCI_Reset に答えるまでの時間。

## 12. 他の WS への知らせ（Q1 へ）

- **WS206 の W6**: linux-firmware の WHENCE は `brcm/brcmfmac43455-sdio.raspberrypi,3-model-b-plus.txt`（`...raspberrypi,4-model-b.txt` はその link）を **GPLv2** と書いている（ws206-p001 は Cypress の license と書いている）。NVRAM の file の license の扱いを WS206 で確かめる要がある。
- **WS206 の W2 と B12**: GPIO の helper と mailbox の GPIO の helper を共有する。先に入る Phase が作る。
