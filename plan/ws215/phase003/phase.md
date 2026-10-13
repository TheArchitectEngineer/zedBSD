<!-- awesome-plan project=zedbsd record=ws215-p003 -->

# ws215-p003: kernel の PL011 の UART の HCI の transport（Pi 4 の内蔵の Bluetooth）

Parent: [WS215](../ws.md)
Status: in-progress（2026-10-13 P2: 実装・build（warning 0）・host 試験 PASS まで。test-wait: T1 の QEMU raspi4b の起動の回帰と、Pi の実機の smoke（下の「実機で見る点」）の結果を Q1 が判定するまで cleared にしない）
Phase disposition: normal
Queue: P2 の lane（2026-10-13 Q1 の割当。承認: 2026-10-12 ユーザー「RPi4のWiFiとBluetoothを実装し」、2026-10-13 朝の D1〜D4 の答え、D5 は未回答）

## 範囲（ws.md の表の行）

kernel: PL011 の UART の HCI の transport（`rpi4-bt.c`、HUNT・SYNC と自己 reset）、H4 の assembler、class・UAPI の変更（`BT_IOC_SET_SPEED`・`bt_info` の拡張）、config.txt の生成（`CONFIG_DRIVER_BCM2711_BT`）、bt-probe の Read Local Name。実機の smoke。

設計は [p001](../phase001/phase.md) の第 2 版（3 節 B2〜B13・B20、6 節）と第 3 版の補い（中-B〜E の kernel の部分、中-F の数）。daemon の Broadcom の始動（B14・B15、4 節）は p005、UART の SCO は p008。

ユーザーの決定（2026-10-13、原文）:
- D1「シリアルコンソールはオプションにしてデフォルトでOFFです。ONにしたらBluetoothが使えなくてよいです。Bluetoothはデフォルト有効です。」→ `CONFIG_DRIVER_BCM2711_BT` は rpi4 で既定 **y**（B2 の「計測まで n」を取り消す）。`CONFIG_RPI4_SERIAL_CONSOLE=y` の build では driver が attach せず、config.txt は今のまま（`dtoverlay=disable-bt` で BT は無効）。`config/ci/config-rpi4.mk` は `CONFIG_RPI4_SERIAL_CONSOLE := n` のまま。
- D2 OK、D3 推奨で OK、D4 よい。
- **D5 は未回答のため推奨 (a)**（CTS の割り込みを使わない、回避なしで始める。p006 で中-F の観察）。

受け入れ条件:

1. rpi4 の vmunix（`config/ci/config-rpi4.mk`）が `CONFIG_DRIVER_BCM2711_BT=y` と `CONFIG_RPI4_SERIAL_CONSOLE=y` の両方で warning 0。amd64 の vmunix が warning 0。bt-probe（arm64・amd64）が warning 0。
2. host 試験: H4 の assembler、HUNT（中-C の 4 つの場合を含む）、divisor、自己 reset の履歴（中-B）、世代（中-D）、config.txt の生成（4 つの組）、既存の bt-hci-proto・bluetoothd の host 試験が PASS。
3. QEMU raspi4b で起動の回帰（login prompt）が通り、driver が attach しない（T1）。
4. 実機の smoke（T1・ユーザー）: 電源（CTS が立つまでの時間）、bluetoothd を止めて bt-probe で HCI_Reset・Read Local Version・Read Local Name・Read BD_ADDR、`BT_IOC_RESET` の notice、clock の行の前後。

## review-2 の「p003 は条件つき」の条件の確かめ

| 条件 | 第 3 版での状態 | p003 での扱い |
| --- | --- | --- |
| 中-A WS206 の helper の API | Q1 が解決（ws206-p002a、main に merge 済み） | `rpi4-gpio.h`・`rpi4-firmware.h` の API をそのまま使った（`drv_rpi4_gpio_set_function`・`set_pull`・`get_function`・`pull_from_dt`、`drv_rpi4_firmware_get_gpio`・`set_gpio(BT_ON)`）。GPFSEL・pull・mailbox の GPIO を直に書かない |
| 中-B STOPPED の履歴 | 満たす（kernel: 直近 3 回の時刻の ring、`BT_IOC_RESET` は STOPPED を出るが ring を消さない） | `rpi4-bt-link.c` の history と link。host 試験「STOPPED → reset → 窓の中の 1 回で再び STOPPED」「窓の外では数え直し」。daemon の部分は p005 |
| 中-C HUNT の長い偽の候補 | 満たす（header の byte ごとの判定と replay） | `drv_rpi4_bt_hunt_*`。replay は「候補の最初の byte を捨てて残りを頭から判定し直す」で実装（buffer 260 byte）。host 試験 4 つ（偽の prefix、type の重なり、別の opcode、途中の止まりと replay） |
| 中-D reset の op と自己 reset の取り合い | 満たす（世代、`stop_request`・`parked`） | `generation` は link の中、op は worker を park してから電源の mutex。worker の自己 reset は決める前の世代で `drv_rpi4_bt_link_self_reset()` が CANCEL を返す。host 試験で重なりの順 |
| 中-E 登録の flags | 満たす（flag ごとに条件） | `bt_hci_registered_flags()`（bt-hci.c）。usb-bt・loopback は description を `kern_memset` で 0 にし、ops の表は `set_speed` を書かない（NULL）ので `BT_INFO_SPEED`・`ADDRESS` は立たず、`speed_max`・`board_address` も 0（今の `bt_info` と同じ値）。grep と code の読みで確かめた（bt-hci.c は host で build できないので host 試験は無い） |
| 2a 節の p003 の行が出典つき | 一部が「(e1) の値」「仮の値」のまま | 下の「値の出典（p003 の行の確定）」で、出典の無い時間の値を **自前の決め** と明記した。Zephyr・Android の code は読んでいない（p003 の値に要らなかった） |

大きな設計の変更は要らなかった。下の判断はどれも設計の範囲の中の細部。

## 判断（設計との違い・細部の決め）

| 項 | 決め | 理由 |
| --- | --- | --- |
| branch | `agent/p2-ws215`（main 6f0abfc3b から） | `agent/p2` は `/home/awe/zedBSD-worktrees/p6` が checkout 中で switch できない。`agent/p2` と `agent/p2-bug291b` は両方 main に入っていた |
| probe の場所（B3） | `kern_platform_refresh_devices()` の WiFi の probe の後で `drv_rpi4_bt_probe(rpi4_fdt_phys)`。probe は DT を読み thread を作るだけ（clock の 1 行の log も probe で、mailbox は `prepare_shared_blocks` の後）。BT_ON・clock・pin・PL011・IRQ・電源は thread | WS206 p002b の WiFi の probe と同じ形に合わせた（依頼「rpi4.c の変更はそれと合わせる」）。`rpi4_fdt_phys` を WiFi と BT の両方の `#if` で持つ |
| 静かな窓（B9） | 遷移（電源の入れ直し・速さの切り替え）の **中**で、op が返る前に 10 ms 待ち、その間の byte を捨ててから HUNT | 窓を op の後に置くと、daemon が reset・SET_SPEED の直後に送る command の答えが窓に入って捨てられる |
| `send()` の空き待ち（B10 の (1)） | 1 s の待ちを作らず、空きが無ければ EIO | class は op の mutex で 1 つずつ送り、各 send は最後の byte が FIFO に入るまで待つので、send の始めに送りの ring は必ず空。SCO が ring を共有する p008 で待ちを足す |
| Command Status の opcode | byte 5・6（status・credits の後）。Command Complete は byte 4・5 | Core 5.4 Vol 4 Part E 7.7.15。設計の「6 byte 目まで」は CC の形 |
| HUNT の受けの誤り | 誤りの bit の立った byte は候補を諦める（最初の byte を捨てて残りを判定し直す、byte 自体は加えない） | 誤りの byte は答えの一部になれない |
| event code の表（B8） | 0x01〜0x59（Encryption Change v2）と 0xFF | Core 5.4 Vol 4 Part E 7.7。0x00・0x5A 以上は同期の喪失 |
| pull | PL011 の `pinctrl-0` の group に書かれた pin は DT の値（`drv_rpi4_gpio_pull_from_dt`）、無い pin は vendor の DT の値（30 up・31 なし・32 なし・33 up、`uart0_pins` と `uart1_bt_pins`） | 第 3 版の中-A の行 |
| GPIO 14・15 | firmware が ALT0（UART0）にしていたら input にする（log に出す）。他の function なら触らない | 2 つの pin が UART0 の RX を同時に駆動すると受けが壊れる。`enable_uart` を外した firmware の pin の状態は未確認 |
| 中-F の数 | 割り込み・受けの byte（誤り・遷移で捨てた数）・送りの byte・自己 reset の数を klog の 1 行で、program の reset と自己 reset のたび、および 10 分ごと（動きがあった時だけ） | `bt_stats` の大きさ（ioctl の番号）を変えない。p006 は 10 分の試験の始めと終わりの行を比べる |
| STOPPED の notice | 自己 reset が上限で STOPPED になった時も RESET の notice | controller の状態が失われたことを daemon に伝える（中-B の daemon の数え方と同じ扱い） |
| GET_STATS の数 | 同期の喪失ごとに `malformed` に 1、class に断られた packet ごとに `stalls` に 1 | 設計 B9 |
| `bt_info` | `speed_max`、`board_address[6]`、`pad[2]`、`reserved`（1 語）。160 byte の `_Static_assert` | B13 |
| `config/ci/config-rpi4.mk` | 変えない（生成し直さない） | `CONFIG_DRIVER_BCM2711_BT` は Makefile の既定（rpi4 で y）が効く。GENET・AUDIO・WIFI の行もこの file に無く、生成し直すと 4 行増えるだけで値は同じ。Q1 が menu で生成し直してもよい |
| config.txt の comment | `init_uart_clock` の上の comment を「serial の console（uart.c）か内蔵の Bluetooth（rpi4-bt.c）の clock」に直した | BT の build でも残る行 |
| clang-format | 適用しない | 差分は `#define` の桁揃え、定義の引数の字下げ、ops の表の初期化子だけで、tree の形（rpi4-wifi.c）と規約 §3 に反する（automation.md の注意のとおり） |

## 値の出典（2a 節の p003 の行の確定）

| 値 | 出典 |
| --- | --- |
| PL011 の register（DR・ECR・FR・IBRD・FBRD・LCR_H・CR・IFLS・IMSC・MIS・ICR）と bit、divisor の形（16 bit の整数 + 6 bit の端数、clock/(16×baud)）、LCR_H の書きで divisor が効くこと、IFLS の水位の符号（RX 1/2 = 0b010、TX 1/8 = 0b000） | (d) ARM PrimeCell UART (PL011) TRM |
| GPIO 30〜33 の ALT3、14・15 の ALT0 | (d) BCM2711 ARM Peripherals の GPIO の章、(a) vendor の dtb の `uart0_pins`・`uart0-ctsrts-gpio30` |
| mailbox の tag（`GET_CLOCK_RATE` 0x00030002、`SET_CLOCK_STATE` 0x00038001、`SET_CLOCK_RATE` 0x00038002）、clock ID（UART 2・ARM 3・CORE 4・V3D 5） | (d) Raspberry Pi の firmware の wiki「Mailbox property interface」。GPIO の tag は WS206 の helper の中 |
| UART の clock 48 MHz | config.txt の `init_uart_clock=48000000` |
| Command Complete 0x0E・Command Status 0x0F の引数の形、event code の表、ACL・SCO の handle の上限 0x0EFF、Read Local Name 0x0C14（名前 248 byte） | (d) Bluetooth Core 5.4 Vol 4 Part E 5.4・7.3.12・7.7 |
| BT_ON の low の 10 ms | **自前の決め**（WS206 の WL_ON と同じ。実機で CTS が立つまでの時間を log で見る） |
| 電源の後の CTS の待ちの上限 500 ms、1 ms ごとに見る | **自前の決め**（上限なので短すぎなければよい。実機の log「controller ready after about N ms」で確かめる） |
| 静かな窓 10 ms、packet の途中の byte の間 100 ms、send の 2 s、速さの切り替えの送りの drain 100 ms、worker の park の待ち 2 s、数の log の 10 分、divisor の誤差 1.5 %、自己 reset 60 秒に 3 回 | **自前の決め** |
| HUNT の opcode の一致（遷移の後に送った最後の command） | **自前の決め**（第 3 版の中-C） |

## 変えた所

| file | 変更 |
| --- | --- |
| `include/uapi/bluetooth.h` | `BT_BUS_UART`、`BT_INFO_SPEED`・`BT_INFO_ADDRESS`、`BT_ADDRESS_SIZE`、`bt_info` の `speed_max`・`board_address`・`pad`・`reserved`（大きさ不変、`_Static_assert`）、`BT_IOC_SET_SPEED`（`_IOW('b', 6, uint32_t)`）、`BT_IOC_RESET` の comment（UART の電源の入れ直しと自己 reset の notice） |
| `include/drivers/generic/bt-hci.h` | `BT_HCI_ASSEMBLE_H4`、event code・handle の定数、ops の `set_speed`、description の `speed_max`・`board_address`、`drv_bt_hci_acl_limit()`、`bt_hci_assembler_load()` |
| `src/drivers/generic/bt-hci.c` | 登録の flags を flag ごとに（`bt_hci_registered_flags`）、`info` に `speed_max`・`board_address`、ioctl の `BT_IOC_SET_SPEED`、`bt_hci_operate` の分岐（op が NULL なら ENOTSUP、0 と `speed_max` 超は EINVAL）、`drv_bt_hci_acl_limit()` |
| `src/drivers/generic/bt-hci-proto.c` | H4 の mode（type の byte、header の検査で EBADMSG）、`bt_hci_assembler_load()`、`bt_hci_assembly_capacity(H4)`。EVENT・ACL の mode の動きは不変（既存の host 試験 PASS） |
| `src/drivers/platform/rpi4/rpi4-bt-link.{c,h}`（新） | 純粋な部分: divisor・速さの上限、HUNT（replay）、自己 reset の履歴、line の mode と世代 |
| `src/drivers/platform/rpi4/rpi4-bt.{c,h}`（新） | driver: probe、thread（BT_ON・clock・pin・PL011・IRQ・電源・登録）、割り込み（受けの ring 8192 entry、送りの ring 8 KiB）、worker（park、世代、HUNT・SYNC、gap、数の log）、ops（send・reset・set_speed・room、set_bootloader は ENOTSUP、SCO なし） |
| `src/kern/platform/rpi4.c` | `rpi4_fdt_phys` を WiFi か BT の時に、`kern_platform_refresh_devices()` で `drv_rpi4_bt_probe()` |
| `platform/arm64/vmunix.mk` | `rpi4-bt.c`・`rpi4-bt-link.c`（`CONFIG_DRIVER_BCM2711_BT=y`）、`bt-hci.c`・`bt-hci-proto.c` を USB_BT か BCM2711_BT の時に、`$(BUILD)/config.txt` の規則（phony + cmp、cmdline.txt と同じ形）、hdd-image の依存と `--config` |
| `platform/arm64/tools/make-rpi4-config.py`（新） | config.txt の生成（BT=y かつ serial=n の時だけ `enable_uart=1`・`uart_2ndstage=1`・`dtoverlay=disable-bt` と `uart_2ndstage` の上の comment 2 行を除く。各行がちょうど 1 回でなければ止まる） |
| `platform/arm64/bootloader.mk`・`disk-image.mk` | `$(BUILD)/config.txt` を使う |
| `platform/arm64/config.txt` | `init_uart_clock` の上の comment だけ |
| `Makefile` | `CONFIG_DRIVER_BCM2711_BT ?=`（rpi4 で y）と `-DCONFIG_DRIVER_BCM2711_BT` |
| `config/drivers/architecture/arm64.drivers`・`menu.list` | `CONFIG_DRIVER_BCM2711_BT`（bool、rpi4、既定 y、Platform の行） |
| `userland/tests/bt-probe/{main.c,Makefile}` | Read Local Name（印字できない byte は `?`）、UART の時に `speed-max` と `board-address` の行 |
| `plan/ws215/tests/rpi4-bt-host-test.{c,sh}`（新） | host 試験（下） |

HAL（`include/hal/hal.h`・`src/hal/`）と toolchain は変えていない。

## 実行したコマンドと結果（agent/p2-ws215、main 6f0abfc3b の上）

| コマンド | 結果 |
| --- | --- |
| `make -j16 BUILD=build/ws215-p003-rpi4 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/ws215-p003-rpi4/vmunix build/ws215-p003-rpi4/config.txt` | rc 0、warning 0、`check-arm64-vmunix.py` PASS。config.txt は source から 5 行（3 行 + comment 2 行）を除いた物 |
| 同上 + `CONFIG_RPI4_SERIAL_CONSOLE=y`（`BUILD=build/ws215-p003-rpi4-serial`） | rc 0、warning 0。config.txt は source と byte まで同じ |
| 同上 + `CONFIG_DRIVER_BCM2711_BT=n CONFIG_DRIVER_USB_BT=n`（`BUILD=build/ws215-p003-rpi4-nobt`、HCI の class も入らない build） | rc 0、warning 0。config.txt は source と同じ |
| `make -j16 BUILD=build/ws215-p003-amd64 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/ws215-p003-amd64/vmunix` | rc 0、warning 0 |
| `make -o <wt>/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=plan/ws215/tests/config-rpi4-bt.mk build/arm64/bin/bt-probe build/arm64/bin/bluetoothd build/arm64/bin/bt` | rc 0、warning 0（`check-dynamic-elf.py` を通る） |
| `make -o <wt>/build/amd64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/bin/bt-probe build/amd64/bin/bluetoothd build/amd64/bin/bt` | rc 0、warning 0 |
| `OUT=build/ws215-p003-host sh plan/ws215/tests/rpi4-bt-host-test.sh` | `rpi4-bt: all checks passed`、`rpi4-bt config.txt: all checks passed`（ASan・UBSan）。TSan は使わない（純粋な部分は単一 thread、lock の並行は driver の中で host では動かない） |
| 突然変異の確かめ（scratchpad で履歴の `>=` を `>` に、`dropped` を数えない、event code の上限を外す） | 10 個と 1 個の check が FAIL（試験が効く） |
| `OUT=build/ws215-p003-hci-host sh plan/ws143/tests/bt-hci-proto-host-test.sh` | `bt-hci-proto: all checks passed` |
| `OUT=build/ws215-p003-daemon-host sh plan/ws143/tests/bt-daemon-host-test.sh` | daemon 90・pair 161・link 56・hid 75・hidhost 96・hog 24 の checks PASS（Intel の実 file の段は cache が無く skip） |
| `make menuconfig-host-test` | PASS（6 targets） |
| `python3 plan/tools/style-check.py`（新しい・変えた C の file） | 残りは `bt = context;` などの context の受け取りと unwind の中の `return EIO`（tree の既存の形と同じ）だけ |
| `git diff --check` | clean |

log は worktree の `build/ws215-p003-*.log`（git に入れない）。

## T1 への依頼（Q1 経由）

image: この Phase の merge の後の main（または `agent/p2-ws215` の SHA）。T1 が自分の worktree で build する。

1. **QEMU raspi4b の起動の回帰（BT=y の既定）**: `config/ci/config-rpi4.mk` の image（hdd-image、`$(BUILD)/config.txt` を使う）で `plan/tools/boot-test.sh` の login prompt。合否: login prompt が PNG に出る。できれば guest の `dmesg | grep rpi4-bt` に `rpi4-bt: no onboard Bluetooth on QEMU's raspi4b` があり、`rpi4-bt: PL011 at` が無いこと（attach しない）。
2. **QEMU raspi4b の起動（serial の console = y）**: 同じ config + `CONFIG_RPI4_SERIAL_CONSOLE=y` の image で login prompt。合否: login prompt（serial の log では判定しない）。dmesg に `rpi4-bt:` の行が出ないこと（QEMU なので `no onboard Bluetooth` が先に出る。serial=y の guard は実機の確かめ）。
3. （任意、Q1 の判断）amd64 の `plan/ws143/tests/config-amd64-bt.mk` の image で `plan/ws143/tests/bt-loopback-p002.sh`（class の登録・ioctl を変えたので、USB 型の transport で `bt_info` が今と同じ値（flags・`speed_max` 0）で、`bt-probe -L` が PASS）。

## 実機で見る点（Pi 4、10.0.30.2、T1・ユーザー）

届け方は p001 の B2（FAT の boot partition に `$(BUILD)/config.txt` と `vmunix` を SSH で複写、旧 file を `config.txt.ws215-bak` に、再起動はユーザー）。config.txt が変わる（`enable_uart`・`uart_2ndstage`・`disable-bt` が消える）ので、起動しない時はユーザーが SD card を `config.txt.ws215-bak` に戻す。

**dmesg に出るはずの行**（順に）:

1. `rpi4-bt: clocks core N Hz (0), v3d N Hz (0), arm N Hz (0)` — 旧 config.txt（同じ kernel）と新 config.txt で比べる（B2 の clock の前後）。旧 config.txt では続けて `rpi4-bt: no enabled Bluetooth node on the PL011 (13)`（ENODEV、disable-bt）が出て終わる。
2. `rpi4-bt: BT_ON was level 0|1`（mailbox の GPIO 128）。
3. `rpi4-bt: GPIO 30 function F -> 7, pull 2`、`GPIO 31 … pull 1`、`GPIO 32 … pull 1`、`GPIO 33 … pull 2`（pull の数は enum: 1 なし・2 up・3 down。F は firmware が残した function、未確認の点）。
4. 出るかもしれない: `rpi4-bt: GPIO 14 was UART0, now an input (0)`（15 も）。
5. `rpi4-bt: PL011 at fe201000, irq 153, clock 48000000 Hz, speed up to 3000000 baud`。
6. `rpi4-bt: controller ready after about N ms`（CTS が立つまで。N を記録する）。
7. `bt-hci: /dev/bluetoothN: Raspberry Pi 4 Bluetooth 0000:0000`。
8. 最初の command の答えで `rpi4-bt: in step, N bytes passed over`。

失敗の時の行: `BT_ON not readable`・`UART clock …`・`controller's CTS did not rise in 500 ms`・`controller did not come up`・`line out of step (…)`・`too many resets, controller stopped until reset`。

**bt の command での確かめ**:

1. `service status bluetoothd`、`bt show` — bluetoothd が `/dev/bluetoothN` を開いている（p005 の前なので firmware は読まず ROM のまま。`bt show` の controller の行を記録する）。
2. `service stop bluetoothd`（node は一度に 1 つ）。
3. `bt-probe -r -f /dev/bluetoothN`（`plan/ws215/tests/config-rpi4-bt.mk` の image に入っている）: `BT info … bus=2 flags=0x…` と `BT uart speed-max=3000000 board-address=…`（flags は 0x4 か、DT の address があれば 0xc）、`BT reset status=0x00`、`BT local-version … manufacturer=0x000f`（Broadcom 15、Cypress なら 0x0131）、`BT local-name status=0x00 name="BCM4345C0"`（見込み、実際の文字列を記録）、`BT bd-addr …`、`BT ioctl-reset notice=1`、`BT stats … malformed=0`、最後に `BT PASS`。
4. もう一度 `bt-probe -r`（reset の後にまた in step になる。dmesg に `rpi4-bt: at the program's reset: …` の行が 2 つ）。
5. `service start bluetoothd`。

## 未実施・残件

- QEMU と実機の試験は未実施（担当は QEMU を起動していない）。実機の証拠はまだ無い（上は code・build・host 試験だけ）。
- p001 の「未確認」（11 節）の実機の読み: GPFSEL3 の初めの値、BT_ON の初めの値、firmware の fixup の後の bt の node の status と `local-bd-address`、CTS が立つまでの時間、43455 の ROM の Local Version・Name・BD_ADDR、14・15 の function。
- daemon の部分（始めの `BT_IOC_RESET`、`BT_IOC_SET_SPEED`、求めていない notice の数え方、Broadcom の firmware）は p005。
- p008: UART の SCO（今は SCO の packet を受けたら捨てる、`BT_INFO_SCO` を出さない）と、SCO が送りの ring を共有する時の空き待ち。
