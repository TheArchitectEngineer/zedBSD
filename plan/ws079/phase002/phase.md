<!-- awesome-plan project=zedbsd record=ws079-p002 -->

# WS079 Phase 002: kernel のペンの入力（USB HID の digitizer）と試験用の合成の入力

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-28 main の判断、QEMU と host。実機の pen は未実施。2026-10-05 P2: ws.md の表に合わせて頭の Status を直した）
Disposition: normal
Parent: [WS079](../ws.md)
Design: [design-input-notes.md](../design-input-notes.md) §2
Resume point: 下の「残り」
<!-- awesome-plan-current:end -->

## 範囲

[設計](../design-input-notes.md) §2.1〜§2.4: USB HID の Digitizer page（0x0D）のペンを kernel の evdev の event にする。
QEMU には 4096 段階のペンが無いので、試験用の注入の device（§2.4、`CONFIG_INPUT_TEST_INJECT`、既定 n）を置く。
HAL の API には触れない。

## 2026-09-28 の作業（subagent、1 回目）

### 変更

| 場所 | 内容 |
| --- | --- |
| `include/uapi/input.h` | `ABS_PRESSURE` 0x18・`ABS_DISTANCE` 0x19・`ABS_TILT_X` 0x1a・`ABS_TILT_Y` 0x1b、`BTN_DIGI`/`BTN_TOOL_PEN` 0x140・`BTN_TOOL_RUBBER` 0x141・`BTN_TOUCH` 0x14a・`BTN_STYLUS` 0x14b・`BTN_STYLUS2` 0x14c（Linux の番号）。すべて `ABS_MAX`・`KEY_MAX` の内なので、input の層（`src/drivers/generic/input.c`）は宣言した device からそのまま受ける（input の層の変更は不要だった） |
| `include/drivers/usb/hid-report.h` | pen の switch の擬似の型 `HID_REPORT_TYPE_DIGITIZER`（0x8000、evdev の型の外）、`HID_REPORT_PEN_NONE/TABLET/DISPLAY`、`hid_report_layout_info.pen` |
| `include/drivers/usb/hid-digitizer.h`・`src/drivers/usb/hid-digitizer.c`（新） | §2.2 の状態機械。decode 済みの report から event の列（SYN_REPORT 込み）を作る純粋な関数（`drv_hid_digitizer_translate`・`drv_hid_digitizer_reset`・`drv_hid_digitizer_report_is_pen`）。道具 = In Range（Invert か Eraser で RUBBER）、In Range の無い device は接触の間だけ、Invert の無い device は Eraser の立ち上がりから離れるまで RUBBER。入るとき tool → 軸 → touch → button → SYN、出るとき pressure 0 → touch 0 → button 0 → tool 0 → SYN、道具の交替は 2 つの frame |
| `src/drivers/usb/usb-hid.c` | collection の usage の stack（`collection_usages`）、Physical Min/Max・Unit Exponent・Unit を global の状態に持つ（以前は読み飛ばし）。Pen（0x0D:0x02）か Digitizer（0x0D:0x01）の collection の中でだけ 0x0D の usage を写す: Tip Pressure → `ABS_PRESSURE`、X/Y Tilt → `ABS_TILT_X/Y`、In Range・Invert・Tip Switch・Eraser・Barrel 1/2 → 擬似の型の switch（capability は `BTN_TOOL_PEN`・`BTN_TOOL_RUBBER`・`BTN_TOUCH`・`BTN_STYLUS`・`BTN_STYLUS2` を宣言）。absinfo の resolution を Physical と Unit から（長さは単位/mm、回転は単位/radian）。pen の switch を含む report は状態機械を通して emit。pen の device の名前は「USB HID pen」。指・touch screen の collection は今どおり無視 |
| `platform/{amd64,arm64,pcat}/vmunix.mk` | `hid-digitizer.c` を `usb-hid.c` と同じ条件で build |
| `plan/ws079/tests/host-hid-pen.c`・`run-hid-pen.sh`（新） | host 試験（下） |

### 確認（実行したもの）

| 確認 | 結果 |
| --- | --- |
| `plan/ws079/tests/run-hid-pen.sh`（host。`usb-hid.c` と `hid-digitizer.c` を kernel と同じく `-ffreestanding -nostdlibinc -D__ZEDBSD__ -Werror` で compile、試験は host の libc。合成の Wacom 風の descriptor: report ID 2、In Range・Invert・Tip・Eraser・Barrel 1/2、X 0..21600（21.6 cm）・Y 0..13500、Tip Pressure 0..4095、Tilt −60..60 度） | `host-hid-pen: ok`。確かめたこと: capability の宣言（擬似の型が漏れない）、pressure 0..4095 の absinfo、X/Y の resolution 100 単位/mm、tilt の resolution 57 単位/radian、範囲外では event 無し、入るとき `BTN_TOOL_PEN=1` が先頭、pressure を 0 から 4095 まで 13 刻み＋4095 で送り値がそのまま出て `BTN_TOUCH=1` は 1 度だけ・軸の後、`BTN_STYLUS`・`BTN_STYLUS2`、出るとき pressure 0 → touch 0 → button 0 → tool 0 の順で 1 frame、Invert で `BTN_TOOL_RUBBER`、Eraser で touch、RUBBER → PEN の交替は 2 frame で間に SYN |
| 試験の感度（mutation） | `choose_tool` の RUBBER を PEN に替えた版で 7 件 FAIL を確認（試験が振る舞いを見ている） |
| `make -j16 vmunix`（amd64、worktree の `build/amd64`、main の config.mk の写し） | exit 0、warning 0（`-Werror`）。`build/amd64/kern64/src/drivers/usb/hid-digitizer.o` ができた |
| `plan/tools/boot-test.sh`（QEMU、uefi-nvme）: 共有の `build/amd64/hdd-image.img`（2026-09-26）の写しの ESP の `vmunix` をこの build のものに替えた `build/ws079-p002-boot/hdd-image.img` | PASS、login prompt（`build/ws079-p002-boot/out/login.png`）。userland は 09-26 の image のまま |

未実施: arm64・pcat の build（rule だけ足した）、QEMU の `usb-wacom-tablet` を付けた起動（§2.4 の補助の確認）、実機（ペンタブレットの機種が未定、§8 D1）、
`git diff --check` 以外の規約の全文の確認（p009 で行う）。QEMU の証拠だけで、実機の証拠は無い。

### 設計からの差・判断

- `MSC_SERIAL`（Transducer Serial Number）と `ABS_DISTANCE` の写し（Height）は今回の範囲（依頼の usage の一覧）に入れていない。`ABS_DISTANCE` の番号だけ UAPI に足した。
- INPUT_PROP（POINTER/DIRECT）は input の層に property の仕組みが無い（`input_device_info` に property の field が無く、`INPUT_PROP_*` の番号だけがある）。
  layout は `pen`（TABLET/DISPLAY）を持つが、evdev の `EVIOCGPROP` へは出していない。compositor の分類（§3.1）は `BTN_TOOL_PEN`＋`ABS_PRESSURE` で足りるので、property は残りにした。
- `usb-hid.c` は既存の「XXX: Need coding style fitting」の file。新しく書いた関数（`digitizer_to_event`・`add_digitizer_capabilities`・`parser_in_pen`・
  `unit_exponent_value`・`axis_resolution`・`set_axis_resolution`・`usb_hid_publish_pen_report`）と `hid-digitizer.c` は規約の全文に合わせたつもりだが、既存の部分の
  書き直しはしていない。
- queue の溢れ（§2.3、200 Hz × 約 10 event）は測っていない（実機か注入が要る）。

## 2026-09-28 の作業（subagent、2 回目: 注入の device）

main の判断に従い、node は devfs の root の `/dev/input-inject`（devfs の `/dev/input` は `eventN` だけを置くため）。

### 変更

| 場所 | 内容 |
| --- | --- |
| `config/kernel-options.list`・`Makefile` | `CONFIG_INPUT_TEST_INJECT|bool|Test pen injector (/dev/input-inject, test builds only)|amd64|n|`、`CONFIG_INPUT_TEST_INJECT ?= n`、y のとき `-DINPUT_TEST_INJECT`（`CONFIG_PCAT_SERIAL_MIRROR` と同じ形） |
| `platform/amd64/vmunix.mk` | y のときだけ `src/drivers/generic/input-inject.c` を build（arm64・pcat は対象外、option の platforms も amd64） |
| `include/uapi/input-inject.h`（新） | `struct input_inject_setup { magic, kind, x_max, y_max }`、`INPUT_INJECT_MAGIC`・`INPUT_INJECT_KIND_PEN`（唯一の kind）・範囲の上限・1 回の write の event 上限 64 |
| `include/uapi/input.h` | `BUS_VIRTUAL` 0x06（Linux の番号） |
| `include/drivers/generic/input-inject.h`・`src/drivers/generic/input-inject.c`（新） | cdev `input-inject`（rdev 0x000e0000）。open は `cred_is_superuser` でなければ EPERM、同時に 1 つだけ（EBUSY）。最初の write は setup 1 個だけ（大きさ・magic・kind・x_max/y_max 1..65535 を検査）で、固定の形の pen を `drv_input_device_register` で登録: ABS_X/Y 0..max、ABS_PRESSURE 0..4095、ABS_TILT_X/Y −60..60（resolution 57/radian）、BTN_TOOL_PEN・BTN_TOOL_RUBBER・BTN_TOUCH・BTN_STYLUS・BTN_STYLUS2、EV_SYN。以後の write は `struct input_event` の配列（1..64 個、端数は EINVAL）で、全部を先に検査（EV_SYN は SYN_REPORT/0、EV_KEY は 5 つの button で 0/1、EV_ABS は宣言した 5 軸の範囲内。ほかの type・code は拒否）してから `drv_input_device_emit` に渡す（一部だけ通ることは無い）。close で `drv_input_device_unregister`（押されたままの button は input の層が離す）。宣言の ioctl は設けず（設計 §2.4 の `INJECT_IOC_CREATE` から簡略化: 任意の capability を宣言させないため） |
| `src/kern/devfs.c` | `input-inject` の node の mode を 0600（ほかの非 event の node は従来どおり 0666）。root の検査は open 側にもある |
| `src/kern/vfs.c` | `INPUT_TEST_INJECT` のときだけ `drv_input_core_init()` の後で `drv_input_inject_register()` |
| `plan/ws079/tests/peninject/peninject.c`・`stroke.pen`（新） | guest の道具。台本: `size W H`・`tool pen|rubber`・`down X Y P [TX TY]`・`move ...`・`ramp FROM TO STEPS MS`・`button stylus|stylus2 0|1`・`up`（pressure 0 → touch 0 → tool 0 → SYN）・`wait MS`・`hold MS`。1 行を 1 frame（SYN_REPORT 付き）の 1 回の write にする |

### 確認（実行したもの）

| 確認 | 結果 |
| --- | --- |
| `make -j16 vmunix CONFIG_INPUT_TEST_INJECT=y`（amd64、worktree の `build/amd64`、main の config.mk の写し、`-Werror`） | exit 0、warning 0。`build/amd64/kern64/src/drivers/generic/input-inject.o` が link された |
| `make -j16 vmunix`（既定 n、同じ build dir で y の後に） | exit 0、warning 0。`input-inject.c` は compile されず、`build/amd64/vmunix.map` に `drv_input_inject_register` が無い（既定の kernel に入らない） |
| peninject の cross compile（`build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot=<共有の build/amd64/sysroot> -Wall -Wextra -Werror -idirafter include`） | compile は warning 0 で通る。link は libc の未定義（`strtod`・`__syscall6`・`__signal_restorer` など、userland の通常の link の仕方をしていないため）で失敗。rootfs の build への組み込みは未実施 |

未実施: guest での実行（試験の image の作成、peninject の rootfs への組み込み、`/dev/input/eventN` の読み取り）、kernel の
注入の経路の host 試験、QEMU の起動（この kernel で boot test も未実施）、arm64・pcat（option は amd64 だけ）。QEMU・実機の証拠はどちらも無い。

## 2026-09-28 追記（ws079-p003 の subagent）

下の残り 1 は ws079-p003 の作業で guest（QEMU、Venus）で確かめた: peninject は `userland/base/tests/peninject/` に移して通常の規則で link し、
試験の image（`plan/ws079/tests/config-amd64-pen.mk`）で node 名・absinfo・ramp・RUBBER・拒否（非 root は mode で EACCES、mode を開けると driver が EPERM、
EBUSY、EINVAL 12 件）を確認した。詳細は [p003](../phase003/phase.md)。残り 2〜4 は未実施のまま。

## 残り（resume の条件）

1. 注入の device の guest での確認: `CONFIG_INPUT_TEST_INJECT=y` の image を作り、peninject を userland の通常の規則で build して
   rootfs（試験の build だけ）に入れ、root で `peninject stroke.pen` を走らせながら既存の道具で `/dev/input/eventN` を読む
   （名前「Test pen (input-inject)」、pressure 0..4095 の absinfo、ramp の値の列、RUBBER の frame）。非 root の open が EPERM、
   2 つ目の open が EBUSY、範囲外の値の write が EINVAL であることも確かめる。peninject の link の仕方（userland の Makefile の規則）を先に調べる。
2. QEMU の `usb-wacom-tablet` を付けて起動し、列挙と解析が壊れないこと（既存の keyboard・`usb-tablet` が動く）を確かめる。
3. INPUT_PROP を evdev へ出すか（input の層の変更）を決める。
4. arm64・pcat の build の確認。
