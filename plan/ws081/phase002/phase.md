<!-- awesome-plan project=zedbsd record=ws081-p002 -->

# ws081-p002: kernel: 1 報告 1 時刻と Scan Time の `MSC_TIMESTAMP`、注入の device と touchinject の Scan Time

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-29、WS081 の作業用サブエージェント。実装・host 試験・amd64 の kernel と試験 image の build・QEMU の guest 試験・boot test。実機は未実施）
Disposition: normal
Parent: [WS081](../ws.md)
Queue: main の指示（WS081 の作業用サブエージェント、2026-09-29 の範囲の承認）。Awesome Plan の Queue の item ではない
Resume point: なし（実機の Scan Time は p007。下の「残り」）
<!-- awesome-plan-current:end -->

## 範囲（2026-09-29 main の判断）

[design.md](../design.md) §2.2: 1 つの報告に 1 つの時刻（URB の完了の時刻）、Touch Screen の Scan Time（0x0D:0x56）を `EV_MSC`/`MSC_TIMESTAMP`（µs）で出す、
input の層の `EV_MSC`、注入の device の Scan Time の宣言と touchinject の Scan Time・µs の間隔の台本・自己試験。率の測定は p003（library）。
変更してよい file（main の承認）: `usb-hid.c`・`hid-touch.c`・`hid-digitizer.c`、`input.c`、`include/uapi/input.h`（追加）、`include/drivers/usb/hid-touch.h`・
`hid-report.h`、`include/kern/input-device.h`・`input-capability.h`（ABI を壊さない追加）、`input-inject.c`・`include/uapi/input-inject.h`、
touchinject の source、`plan/ws079/tests/host-hid-touch.c` の期待値（Scan Time による capability の数と event の列の変化だけ）。HAL・toolchain は変えない。

## 実装

| file | 内容 |
| --- | --- |
| `include/uapi/input.h` | `MSC_TIMESTAMP`（0x05）・`MSC_MAX`（0x07）を追加（Linux と同じ値。既存の値と `input_event` は不変） |
| `include/kern/input-capability.h`・`src/drivers/generic/input.c` | capability の `msc_bits`（`INPUT_CAPABILITY_COUNT_MAX` に `MSC_MAX + 1`）、`EVIOCGBIT(EV_MSC)`、`capability_code_valid` の `EV_MSC`、`drv_input_capability_event` は宣言された MSC の code を状態なしで通す |
| `include/kern/input-device.h`・`input.c` | `drv_input_device_emit_at(device, type, code, value, milliseconds)`: 呼び手の取った時刻で出す。今の `drv_input_device_emit` はそれを今の時刻で呼ぶ |
| `src/drivers/usb/usb-hid.c` | `usb_hid_completion` で `clock_milliseconds()` を読み、`work_pending` と同じ lock の下で `completed_milliseconds` に置く。worker は publish の始めに lock の下で `report_milliseconds` に取り、その報告の event（keyboard・mouse・pen・touch）を全部 `emit_at` でその時刻に出す。parser: Touch Screen の Finger の外の Scan Time を報告の field（`HID_TOUCH_SCAN_TIME_CODE`）にし、logical maximum と単位（Unit が秒の 1 乗で Unit Exponent が -9..0 ならそれ、そうでなければ 100 µs。X・Y の cm が残っているときも 100 µs）を layout に持ち、`drv_hid_report_layout_get_touch` で渡す。attach で `drv_hid_touch_set_scan_time` |
| `include/drivers/usb/hid-report.h` | `hid_report_touch_info` に `scan_time_present`・`scan_time_maximum`・`scan_time_unit_ns` |
| `include/drivers/usb/hid-touch.h`・`src/drivers/usb/hid-touch.c` | `HID_TOUCH_SCAN_TIME_CODE`、`HID_TOUCH_EVENT_MAX` を 1 frame 1 つ増やす（`2 × (4·16 + 5)`）、`HID_TOUCH_CAPABILITY_COUNT` 9。状態に Scan Time の設定と積算。`drv_hid_touch_set_scan_time()`、`drv_hid_touch_translate_at(state, input, milliseconds, output)`（今の `drv_hid_touch_translate` は時刻不明でそれを呼ぶ）。報告ごとに前の報告からの差（wrap は logical maximum + 1）を単位で ns に積み、最初の報告と host の時刻で 1 秒以上の空きの後は 0 から。frame は始めた報告の値（µs、`uint32_t` で wrap）を SYN_REPORT の直前に `MSC_TIMESTAMP` で出す。Scan Time のある screen は、何かが変わった frame と、指が触れている間の全 frame を書く（変化の無い frame は `MSC_TIMESTAMP` と SYN_REPORT だけ）。指の無い空の報告は何も書かない。Scan Time の無い screen は今と同じ |
| `include/uapi/input-inject.h`・`src/drivers/generic/input-inject.c` | setup の `reserved` の bit 0（`INPUT_INJECT_TOUCH_SCAN_TIME`）で Scan Time のある touch screen（100 µs 単位、0..65535）を宣言。そのとき frame の `reserved` が Scan Time（65535 超は EINVAL）、無いときは 0 のまま。他の bit と pen の Scan Time は EINVAL。frame の全 event を write の時刻 1 つで出し、状態機械は `translate_at` |
| `userland/base/tests/touchinject/main.c`（WS079 の道具、main の許可） | `size W H N scan`（Scan Time のある screen。Scan Time は台本の時間（swipe の間隔と wait）で進み、実際の sleep ではない）、`swipe DX DY STEPS MS [JITTER]`（MS は小数、JITTER は ±ms の決定的な乱数）、`wait` も小数、`-s`（Scan Time の自己試験 15 件）、`-t MS`（dump に evdev の時刻）。`-c` は 14 件のまま（「setup-reserved」は bit 0 が意味を持ったので未知の bit 2 で EINVAL を見る）。dump の名前の表に `MSC_TIMESTAMP`（Scan Time の無い screen は出さないので、WS079 の期待の file は不変） |
| `plan/ws079/tests/host-hid-touch.c`（WS079 の試験、期待値だけ） | 下の一覧 |
| `plan/ws081/tests/host-hid-scantime.c`・`run-hid-scantime.sh`（新） | host 試験 |
| `plan/ws081/tests/p002-guest.sh`（新） | guest 試験 |

### WS079 の試験で変えた期待値（main の指示の一覧）

| file:行 | 前 | 後 | 理由 |
| --- | --- | --- | --- |
| `plan/ws079/tests/host-hid-touch.c` の `test_ten_fingers` | `description.capability_count == 8`「eight capabilities」 | `== 9`「nine capabilities (MSC_TIMESTAMP for the Scan Time, ws081-p002)」 | fixture の screen は Scan Time を持つので `MSC_TIMESTAMP` を宣言する |

event の列の期待値は変わらない: WS079 の試験は `drv_hid_touch_set_scan_time` を呼ばないので、状態機械は Scan Time を数えず、今までどおりの event を出す
（decode した報告に Scan Time の値が入っても、状態機械は設定が無ければ使わない）。fixture の comment「a Scan Time (which the driver ignores)」は指示どおり触れていない。

## 確認（実行したもの）

| 確認 | 結果 |
| --- | --- |
| `plan/ws081/tests/run-hid-scantime.sh`（host、driver は kernel と同じく freestanding・`-Werror`） | `host-hid-scantime: ok (62 checks)`。parser: 単位なし・秒 e-4・秒 e-6（1 µs）・X・Y の cm の残り・Scan Time なしの 5 つの descriptor で、有無・最大 65535・単位（100000/100000/1000/100000 ns）、capability 9（`MSC_TIMESTAMP` あり）と 8（なし）、decode した報告の Scan Time の値。状態機械: 最初の報告 0、83 単位で 8300 µs、動かない frame は `MSC_TIMESTAMP` と SYN_REPORT の 2 event、wrap（65530 → 4）、離す frame も stamp、指の無い空の報告は 0 event、1 秒の空きで 0 から、999 ms では続ける、時刻不明では続ける、1 µs 単位で 1000 で wrap、最大 0 の Scan Time は出さない、hybrid の frame は最初の報告の時刻、失われた報告で早く閉じた frame は自分の時刻・新しい frame は新しい時刻、Scan Time なしは今と同じ（変化の無い frame は 0 event）、最悪の報告（16 本の早い書き出しと 16 本の新しい frame）が event の表に入る |
| 同（`EXTRA_CFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all"`） | `ok (62 checks)` |
| 試験の感度（worktree の使い捨ての `build/ws081-p002-mutate.sh`、source の写しを 1 か所ずつ壊す） | 9 つ全部を検出: wrap を 0 にする 3 件 FAIL、1 秒の reset を外す 3、frame の時刻を報告の時刻にしない 9、指が触れている間の frame を書かない 1、常に書く 1、capability を出さない 4、単位を常に既定にする 1、長さの単位を時間とみなす 1、wrap の +1 を外す 3 |
| `plan/ws079/tests/run-hid-touch.sh`（WS079 の host 試験、上の期待値を 1 つ直した後） | `host-hid-touch: ok (193 checks)`。sanitizer でも ok。直す前は「eight capabilities」の 1 件だけが FAIL |
| `plan/ws079/tests/run-hid-pen.sh` | `host-hid-pen: ok` |
| `make -j16 ZEDBSD_CONFIG=plan/ws079/tests/config-amd64-pen.mk BUILD=build/amd64 build/amd64/vmunix`（worktree、注入の device 入り、`-Werror`） | rc=0、warning 0、kernel include check PASS、amd64 vmunix check PASS |
| touchinject の target の compile（`x86_64-unknown-zedbsd`、`-Wall -Wextra -Werror`） | rc=0 |
| `plan/tools/style-check.py`・`plan/ws079/tests/style-extra.py` | hid-touch.c・hid-touch.h・input-inject.c・input-inject.h・touchinject は両方で指摘 0（下の移管の是正の後）。usb-hid.c・input.c は既存の指摘が多い file で、変更前と比べた新しい指摘は既存と同じ形のものだけ（usb-hid.c の critical section の unlock の行と中の代入、input.c の既存の if の連鎖に揃えた `bit_test` の条件）。clang-format は host に無く未実施 |

## sysroot の写しの扱い（main の許可、2026-09-29）

worktree の image の build が、`include/uapi/input.h`・`input-inject.h` を変えたために `toolchain/llvm/sysroot.mk` の sysroot の作り直しを始め、
worktree の中に `build/llvm-source` を展開して LLVM の patch を当て始めた。そこで止め、main に報告した（共有の `build/llvm-source` は読み取り専用のままで
変更なし、worktree の `build/llvm-source` は削除、sysroot の写しは作り直しの一時 dir の前で止まったので無事）。main の許可（案 A）で次のとおり進めた:

1. worktree の `build/amd64/sysroot` は main の `build/amd64/sysroot` の複写（`cp -a`）。`build/llvm`・`build/NoctLang` は main の checkout への symlink（読むだけ）。
2. 写しに上書きした header: `include/uapi/input.h` → `build/amd64/sysroot/usr/include/uapi/input.h`、`include/uapi/input-inject.h` → `build/amd64/sysroot/usr/include/uapi/input-inject.h`。
   （libc の互換の `usr/include/dev/evdev/input.h` は別の header で、変えていない。`MSC_TIMESTAMP` を持たない。）
3. `touch build/amd64/sysroot/.zedbsd-sysroot-complete` で作り直しを止めた（`make -n … .zedbsd-sysroot-complete` が「Nothing to be done」）。
4. それでも sysroot の rule は前提として `build/llvm-source/compiler-rt/lib/builtins/*.c` の存在を要求し、worktree に無いので LLVM の source の展開と patch が
   worktree の中で再び始まった（2 回目。共有の tree には書いていない）。止めて、worktree の `build/llvm-source` を消し、main の `build/llvm-source` への
   symlink（読むだけ。書き込もうとする rule は `ZEDBSD_LLVM_REQUIRE_OWNED` が止める）にした。以後の `make -n` と build の log に展開・patch は 0 件。
5. header を変えた後（規約の是正で `input-inject.h` の comment を足した）は、2 つの header を写し直して stamp を touch し直した。
6. main が merge の後に main の sysroot を作り直すときは、上の 2 つの header が入ることを確かめる。

## WS079 p009 からの移管: 規約の是正（2026-09-29 main の指示）

WS079 p009（規約の照合）が見つけた `src/drivers/generic/input-inject.c`・`include/uapi/input-inject.h`・`userland/base/tests/touchinject/main.c` の
plan/coding-style.md の違反は、p002 がこれらを変えているので p002 で是正した（WS079 は触らない）。意味は変えていない。

| file | 是正 |
| --- | --- |
| `input-inject.c` | `goto fail` を `inject_release_node()` に替えた。条件の中の呼び出し（`cred_is_superuser(cred_current())`、`inject_event_valid`）を変数に取ってから判定。論理式の return（`inject_event_valid` の `return a && b`）を if に。呼び出しの結果の直接の return（`drv_input_device_register`）と裸の `return error;` を分けた。条件演算子（`inject_axis`）を if に。4 節の条件（setup の範囲）を `inject_setup_valid()` の 1 節ずつの判定に。書き込みを pen（`inject_write_pen()`）と touch に分け、pen の宣言を `inject_declare_pen()` に、指の検査を `inject_contact_valid()` に。file-scope の変数の comment（保護と寿命）。critical section は tool が通る既存の形（空行なしの 1 段落） |
| `include/uapi/input-inject.h` | `struct input_inject_setup` の型の comment |
| `touchinject/main.c` | 条件演算子 3 つ（結果の印字は `check_report()`、`errno` は変数に）、閉じ brace の後の空行、main の最後の裸の `return status;`、5 節の条件（`swipe` の引数）を分けた、`check_scan` を `check_capable_msc`・`check_read_events`・`check_scan_frames`・`frame_timestamp`・`frame_same_time` に分けた、3 節の条件を行に分けた |

確認: `plan/tools/style-check.py` と `plan/ws079/tests/style-extra.py`（main の版。worktree では git に入らない `plan/ws081/temp/` に写して実行）で 3 つとも指摘 0。
hid-touch.c・hid-touch.h も両方で 0（hid-touch.c の frame_write の `&&` と `||` の混ざった条件も行を分けた）。回帰: host 試験（下）、kernel の build（warning 0）、guest の `touchinject -c`（14/14）と WS079 p012 の guest 試験。

## guest（QEMU、Venus の pen の試験 image、worktree の build）

image: `plan/ws079/tests/build-pen-image.sh build/amd64`（`config-amd64-pen.mk`、注入の device 入りの kernel、touchinject）。rc=0、変えた file の warning 0、
toolchain の展開・patch 0 件（下の sysroot の扱いの後）。guest: `VENUS_RENDERER=/home/awe/zedBSD-rpi4/build/ws035-sq-venus/install GUEST_RUNTIME=<worktree>/build/ws081-run plan/ws079/tests/pen-guest.sh start build/amd64/hdd-image.img`。

| 試験 | 結果 |
| --- | --- |
| [p002-guest.sh](../tests/p002-guest.sh) | **PASS**。`touchinject -c` 14/14（従来どおり）。`touchinject -s` 15/15: pen の Scan Time・未知の bit・65535 超の拒否、`EVIOCGBIT(EV_MSC)` に `MSC_TIMESTAMP`、down・move・still・lift が `MSC_TIMESTAMP` 0・8300・16600・24900 で SYN_REPORT の直前、still の frame は 2 event だけ、各 frame の全 event が同じ evdev の時刻（1 報告 1 時刻の guest での確認）。jitter ±3 ms の 90 Hz の swipe（`size 1000 1000 2 scan`、`swipe 400 0 40 11.111 3`）を `touchinject -t` で読む: 42 frame、41 歩のうち 40 歩が 8.1〜14.2 ms（残り 1 歩は down と swipe の最初の frame の間の 0、台本の作り）、時刻の混ざった frame 0 |
| WS079 の [p012-guest.sh](../../ws079/tests/p012-guest.sh)（回帰） | **PASS**: touchinject -c、二本指の台本の evdev の 48 event が期待の file と完全一致（Scan Time の無い screen は変わらない）、pen（peninject -c 15/15、182 event）、compositor の seat に `kind=touch` で入り閉じる |
| `plan/tools/boot-test.sh`（同じ image、`OUTPUT=build/ws081-p002-boot`） | **PASS**、login prompt（`/home/awe/zedBSD-rpi4/build/ws081-shots/ws081-p002-20260929-boot-login.png`） |

経過: 初めの 2 回は guest の SSH の準備前と、`touchinject -s` の `EVIOCGBIT` の判定の誤り（kernel は成功で 0 を返す。Linux の長さではない）で FAIL。
判定を `< 0` に直した。WS079 p012 の compositor の段は 3 回 `ZWL VULKAN_ERROR operation=device result=-3` で FAIL したが、原因は guest の台本
（`plan/ws035/tests/zdesktop-guest.sh`）が Venus の renderer を worktree の `build/ws035-sq-venus/install` から取り、無いので host の標準の renderer
（物理 device を出さない）に落ちたこと。main の checkout の renderer を `VENUS_RENDERER` で渡して PASS（input の変更とは無関係）。
途中の guest への `put` による touchinject の入れ替えは guest の起動し直しで消えるので、最後に image を作り直し、その image で boot test を行った
（p002-guest.sh の最後の PASS は、作り直した image と同じ source の touchinject を `put` したもの）。

guest の出力: `/home/awe/zedBSD-rpi4/build/ws081-shots/ws081-p002-20260929-scancheck.txt`・`-scandump.txt`（worktree の `build/ws081-p002-guest2/` にも）。

## 残り

- arm64・pcat の kernel の build は未実施（規則は WS079 p012 で足された hid-touch.c と同じ）。
- 実機（USB の touch LCD）の Scan Time の単位と振る舞いは未確認（p007）。QEMU に USB の multitouch の device は無く、USB の経路（`usb_hid_completion` の時刻、
  parser の Scan Time）は host 試験だけが覆う。
- libc の互換の `dev/evdev/input.h` には `MSC_TIMESTAMP` が無い（使う側が出たら足す）。
- WS079 の fixture の comment「a Scan Time (which the driver ignores)」は、指示どおり WS079 の期待値の 1 行のほかは触れていないので古いまま（WS079 に伝える）。
