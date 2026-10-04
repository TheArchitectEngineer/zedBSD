<!-- awesome-plan project=zedbsd record=ws159-p003 -->

# ws159-p003: I2C-HID と Precision Touchpad、EVIOCGPROP、input-inject の touchpad

Status: in-progress（2026-10-05 P1 generation17 / q713-i01。実装・host の試験・build まで。QEMU は T1 の試験待ち、実機は UAT）
Disposition: normal
Parent: [WS159](../ws.md)
Queue: q713 / q713-i01（設計 [p001](../phase001/phase.md) の D4〜D7）

## 範囲と受け入れ

- D6: HID の parser と状態機械を transport に依らない場所へ移し（挙動は変えない）、Touch Pad の application、pad の button、Precision Touchpad の feature の位置を足す。触る指の状態機械に touch pad の形（BTN_TOOL_*・BTN_LEFT）を足す。kernel の input に `EVIOCGPROP`。
- D4: ACPI の PNP0C50 を見つけ、HID over I2C で立ち上げ、PTP mode にして、evdev の touch pad として出す。
- QEMU の試験の口: `/dev/input-inject` の `INPUT_INJECT_KIND_TOUCHPAD` と `touchinject` の `pad`・`press`・`release`。
- 受け入れ: host の試験（5330 の report descriptor の実物、偽の ACPI と I2C の bus で Linux と同じ byte 列）、既存の HID の host の試験が変わらず PASS、vmunix の build（warning 0、inject の有無の両方）、style-check。QEMU（T1）: 起動が変わらないこと、inject の touchpad の evdev の読み返し。実機（UAT）: I2C-HID の attach と PTP mode、指の evdev。

## 実装（2026-10-05）

- **HID の層の移動（D6、挙動は不変）**: `src/drivers/usb/usb-hid.c` の parser の部分（定義・構造体・forward 宣言・関数）を `src/drivers/generic/hid-report.c`（新）へそのまま移した。`hid-touch.c`・`hid-digitizer.c` と header 3 個を `src/drivers/generic/`・`include/drivers/generic/` へ `git mv`。`include/drivers/hid/` を作らなかったのは、WS035 の `plan/ws035/tests/refactor-refs.py` が `include/drivers/hid` を「移し終えた古い directory」として扱うため（設計の D6 の `src/drivers/hid/` をこの場所に読み替えた）。USB の側に残したのは USB の publish と `usb_hid_le16`。build の list（amd64・arm64・pcat）と host の試験の script 4 本（ws079 の touch・pen、ws081 の scantime、bug105 の bolt）を合わせた。4 本とも前と同じ結果（193・pen ok・62・PASS）。
- **Touch Pad と feature（D6）**: `hid-report.c` に application `0x000d0005`、Button page の usage 1〜3 を pad の button（`HID_TOUCH_BUTTON_CODE(n)`）、feature item の解析（`parse_feature`: report ごとの bit 数と、Device Mode・Contact Count Maximum・Surface/Button Switch・Pad Type・Latency Mode の位置）、`drv_hid_report_layout_get_feature()`。`hid_report_touch_info` に `pad`・`buttons`。
- **状態機械（D6）**: `hid-touch.c` に `drv_hid_touch_set_pad()`。pad の frame は、触れている指の数の `BTN_TOOL_FINGER`〜`BTN_TOOL_QUINTTAP`（古い数を離してから新しい数を押す）と、`BTN_LEFT`〜`BTN_MIDDLE` の変化を出す。gesture は作らない。description に `properties`（screen は `INPUT_PROP_DIRECT`、pad は `POINTER`、button が 1 つなら `BUTTONPAD`）。Confidence が 0 の指は今までどおり数えない（`MT_TOOL_PALM` は出さない）。
- **EVIOCGPROP**: `struct input_device_info` と kernel の input device に `properties`、`input.c` の ioctl の 0x09。UAPI に `INPUT_PROP_BUTTONPAD`、`BTN_TOOL_FINGER`・`DOUBLETAP`・`TRIPLETAP`・`QUADTAP`・`QUINTTAP`、`BUS_I2C`。USB の touch screen と inject の device も properties を出す。
- **I2C-HID の driver（D4）**: `src/drivers/i2c/i2c-hid.c`・`include/drivers/i2c/i2c-hid.h`。`drv_i2c_hid_probe()` が namespace を歩き、`_HID`・`_CID`（文字列・EISA・package）が PNP0C50 で present の device を拾い、`_CRS` の I2C、`_DSM`（UUID 3cdff6f7-…、rev 1、func 1）で descriptor の register、bus を引いて thread を作る。thread は HID descriptor（30 byte、version 1.00）→ SET_POWER ON → RESET → 100 ms 後に空の report を読む → report descriptor → parse → PTP なら Latency Mode 0・Surface/Button Switch 1・Device Mode 3（SET_REPORT）→ evdev の device を登録（名前 "06CB:CE65 Touchpad"、BUS_I2C）。その後は input の register を読み、長さ 0 なら何もしない、report は decode して状態機械へ。間隔は report から 1 秒の間は 6 ms、他は 25 ms（GpioInt はまだ使わない）。50 回続けて失敗したら log を出して 1 秒待つ。呼び出しは `kern_platform_input_init()` の最後（`CONFIG_DRIVER_PCI_LPSS_I2C` と `CONFIG_DRIVER_ACPI` の時）。build は `AMD64_I2C_SOURCES` に ACPI の時だけ。
- **input-inject の touchpad**: `INPUT_INJECT_KIND_TOUCHPAD`（3）。frame の reserved の下位 16 bit が Scan Time、bit 16 が左の button（`INPUT_INJECT_PAD_BUTTONS_SHIFT`）。名前は "Test touchpad (input-inject)"。`touchinject` に `pad W H [N] [scan]`・`press`・`release`、dump の名前の照合と BTN_* の名前。`touchinject -c` の「未知の kind」の試験を 3 → 4 に直した。

## 確認

- `plan/ws159/tests/run-host-ptp.sh`: ok（35 checks）。5330 の descriptor を parse: pad・指 5・button 1・Contact Count・Scan Time・X 0..1336/Y 0..760・resolution 12。feature の位置: Device Mode は report 4 の 1 byte、Surface/Button Switch は report 6 の bit 0/1、Latency Mode は report 13、Contact Count Maximum と Pad Type は report 8 の 4 bit ずつ。状態機械: 1 本で TOOL_FINGER、2 本で TOOL_DOUBLETAP（FINGER を離す）、押し込みで BTN_LEFT、離して 0、全部離れて TOUCH 0・tracking -1。ASan・UBSan でも ok。
- `plan/ws159/tests/run-host-i2c-hid.sh`: ok（78 checks、ASan・UBSan でも ok）。偽の ACPI（5330 の TPD0）と偽の I2C の bus（5330 の HID descriptor と report descriptor、台本の report）で、driver が書く byte 列が Linux の log と一致: `22 00 00 08`（power on）、`22 00 00 01`（reset）、`22 00 3d 03 23 00 04 00 0d 00`（latency）、`22 00 36 03 23 00 04 00 06 03`（switch、2 回）、`22 00 34 03 23 00 04 00 04 03`（device mode）。device は BUS_I2C・06CB:CE65・"06CB:CE65 Touchpad"・POINTER|BUTTONPAD。指・押し込み・離しの event、空の読みでは何も出ない。
- 既存の HID の host の試験 4 本が前と同じ結果。
- vmunix: `config/ci/config-amd64.mk` で warning 0・check PASS。`CONFIG_INPUT_TEST_INJECT=y` でも warning 0・check PASS。`touchinject` の build（`plan/ws079/tests/config-amd64-pen.mk`）warning 0。
- style-check: 新しい file（`i2c-hid.c`・`i2c.c`・`lpss-i2c.c`・host の試験）は 0（`host-i2c-hid.c` の `setjmp` は C の規則で条件の中に置くしかないので 1 件残す、注記あり）。変えた既存の file（`hid-touch.c`・`input-inject.c`・`touchinject`）は 0。`input.c` は違反の数が変更の前後で同じ（131、既存の legacy）。`hid-report.c` は USB の driver から移した legacy の code（「XXX: Need coding style fitting」の印のまま）で、足した関数だけ規約に合わせた。
- 未実施: QEMU（T1）、実機。

## 残り

- **GpioInt を使わない sampling は電池の点で仮**（p001 の残り）。pad の RX の状態の MMIO の確認（D5 の第一案）もまだ実装していない（今は 6/25 ms の sampling だけ）。GPIO の割り込みの Phase を立てる。
- D7（PS/2 の aux の停止）は実装していない。Linux の 60 秒の記録で PS/2 は 0 event なので、二重の入力は起きない見込み。p005 の UAT で zedBSD の PS/2 の event を見て、要れば足す。
- report の時刻は読んだ時刻（`clock_milliseconds`）。Scan Time の MSC_TIMESTAMP は状態機械が出す。
