<!-- awesome-plan project=zedbsd record=ws131-p025 -->

# ws131-p025: browser の shell の窓を新しい API へ（D7）

Status: cleared（2026-10-07 Q1 の判定: T1-311 で browser-p045・p056 PASS、T1-272 で browser-p014 PASS）（旧: uncleared（2026-10-07 Q1 の判定: T1-272 で browser-p014 は PASS、browser-p045・p056 は `set -- $link` が空で走らない（試験の側の疑い）。P1 が直す）（旧: in-progress（2026-10-03 user「D7,browserのshellもlibkeilandで書きましょう。」で新設。2026-10-06 Q1 承認、q820 で P1 が実行。B1 は停止中、libbrowser は触らない）））
Disposition: normal
Parent: [WS131](../ws.md)、計画の正本 [design.md](../design.md)
Queue: q820（P1）
依存: p020 cleared（app の移行で `kl_app` の API が揃っている）。実行の順は p020 の後・p023 の前。**WS074（browser）は Codex が作業中なので、この Phase の開始の前に Q1 がユーザーに衝突を確かめる**
目安: 4〜5h（1 Queue）。実行者: Q1 が割り当てる（high）
所有 path: `userland/desktop/browser/`（`shell/`・`main.c` の窓の結線、`Makefile`）、API の不足の補い（`userland/desktop/libkeiland/app/`・`ui/`）、`plan/ws131/`。**libbrowser（`userland/desktop/libbrowser/`）は触らない**。Q1 の委任が要る: WS074 の browser の試験（`plan/ws074/tests/browser-*.sh`）の手順の調整

## 目的と結果

browser の shell（`userland/desktop/browser/shell/`、4,232 行。そのうち自前の窓 `window.c` 1,176 行・Vulkan の present `present.c` 648 行・titlebar の結線 `titlebar.c` 349 行・touch の結線 `touch.c` 601 行・key `keys.c` 227 行）の自前の窓を `kl_app`・`kl_window` と宣言的な titlebar（URL の field）へ移す。libbrowser の描画は今のとおり shell の Vulkan の present で画面に出し、窓は `kl_window_vulkan_surface`（見せ方 NONE）で作る。Guardrail の「browser / libbrowser」（libbrowser は Wayland を使わない、shell が Wayland の event を public input interface に変換する、[browser component](../../standards/browser-component.md)）は変えない。

## 範囲

1. 最初の 30 分で WS074 の作業の状態（Q1 の確認の結果）と、shell の `window.c`・`present.c` が Wayland のどの object を持つか（xdg toplevel・seat・touch・keyboard・titlebar・gesture・scroller）を表にして phase.md に書く。
2. 窓・入力（pointer・key・touch・repeat）・main loop を `kl_app` へ。shell の input の変換（Wayland の event → libbrowser の public input interface）は `kl_app_take` の event からの変換に置き換える。
3. titlebar（URL の field・戻る・進む・再読み込み）を宣言的な control に。System Menu は今は無い（study §1.3）ので足さない。
4. touch の scroll・gesture は今の呼び方（scroller・gesture）を保ち、新名に。
5. 新名と `<keiland.h>`。browser は zedBSD だけの package（Linux・FreeBSD の Makefile は無い）。

## 受け入れ

- zedBSD の amd64 の build（exit 0・自前の warning 0）と `plan/tools/keiland-os-boundary/check.sh` PASS。Linux・FreeBSD の `make keiland-linux`・FreeBSD の native build が壊れていない（browser は入らないが libkeiland の変更の確認）。
- browser の shell に自前の `xdg_wm_base`・`wl_registry`・swapchain の code が無い（grep）。libbrowser の diff が 0。libbrowser が Wayland の header を include しない（browser component の規則の check）。
- 回帰: `plan/ws074/tests/browser-guest.sh` と WS074 の代表の試験（Q1 と決める）、`plan/ws081/tests/run-browsertouch.sh`・`run-browser-scroll.sh`、boot-test。QEMU の console・serial の log で判定しない。

## 検証の方法

[zedbsd-commands.md](../../tools/keiland-linux/zedbsd-commands.md)。build は自分の `BUILD=build/ws131-p025/`、共有の `build/` を消さない。QEMU と実機の証拠を分ける。

## 衝突・危険・rollback

- 衝突: WS074（Codex が作業中、`userland/desktop/browser/` と libbrowser）。開始の前に Q1 がユーザーに確かめる。
- 危険: shell の input の変換の順（key の repeat、IME の無い field の入力、touch の時刻）の退行。browser の試験を前後で流す。
- rollback: 前へ直すのを基本にし、後ろに同じ file の変更が無い時だけ統合の commit を revert する（design.md §7.4）。

## 実行の記録（2026-10-06、P1）

### 開始前の Wayland の object の表（範囲 1）

WS074 の状態: Q1 の確認で B1 は停止中、libbrowser は触らない（2026-10-06 Q1）。

| object | 持ち主（移行前） | 使い道 | 移行後 |
| --- | --- | --- | --- |
| `wl_display`・`wl_registry` | window.c（`shell_window_open` が connect・roundtrip） | 接続と global の bind | `kl_app_open`（libkeiland が持つ） |
| `wl_compositor`・`wl_surface` | window.c | 窓の surface。present.c が `vkCreateWaylandSurfaceKHR` に渡す | `kl_app_window_create`（`KL_PRESENT_NONE`）、surface は `kl_window_vulkan_surface` |
| `xdg_wm_base`・`xdg_surface`・`xdg_toplevel` | window.c（ping、configure、bounds、close、title、app_id） | 窓の役割・大きさ・閉じる要求 | libkeiland。大きさは `KL_WINDOW_RESIZE` と `kl_window_size`、閉じるは `KL_WINDOW_CLOSE`、題は `kl_window_set_title` |
| `wl_seat`・`wl_pointer` | window.c（enter・motion・button・axis・axis_source・axis_stop・frame） | pointer・wheel・touch pad の指 | `KL_WINDOW_MOTION`・`BUTTON`・`LEAVE`・`AXIS`（`axis_source`）・`AXIS_STOP` |
| `wl_keyboard` | window.c（key・modifiers・repeat_info・enter/leave）と自前の repeat | key・修飾・focus・key の repeat | `KL_WINDOW_KEY`（`repeated`）・`KL_WINDOW_FOCUS`。repeat は `kl_app_dispatch` の中（BUG-111 の順） |
| `wl_touch` | window.c（down・motion・up・cancel） | 指の scroll・gesture（touch.c） | `KL_WINDOW_TOUCH_*`（`time_us`・`arrival_us`）。touch.c の scroller・gesture はそのまま |
| titlebar（`kl_titlebar_v1`） | titlebar.c（旧 `keiland_titlebar_*` の transaction） | 戻る・進む・再読み込み・location の breadcrumb と URL の field | 宣言的な `kl_window_set_controls`・`set_control_parts`・`set_control_text`・`set_action_state`・`focus_control_mode`、event は `KL_WINDOW_ACTION`・`CONTROL_DONE` |
| gesture・scroller | touch.c（`keiland_gesture_*`・`keiland_scroller_*`） | 指の drag・flick・tap・long press | 呼び方を保ち新名 `kl_gesture_*`・`kl_scroller_*` |
| 網の descriptor（最大 64） | window.c の `poll` に一緒に渡す | libbrowser の network | `kl_app_watch_fd`（`KL_APP_FDS_MAX` を 16 から 64 へ、KL_VERSION 50） |
| swapchain | present.c | libbrowser の描画を出す | present.c に残す（`KL_PRESENT_NONE` の窓では Vulkan は app の物。terminal・imageview・notes と同じ）。受け入れの「swapchain の code が無い」は §目的の「shell の Vulkan の present で画面に出し」と合わないので、Wayland の code（`vkCreateWaylandSurfaceKHR` を含む）が無いことで確かめる |

### 実装（範囲 2〜5）

- `shell/window.c`: 自前の registry・xdg toplevel・seat・key の repeat を捨て、`kl_app_open` と `kl_app_window_create`（`KL_PRESENT_NONE`、app_id `browser`）にした。`shell_window_dispatch` は網の descriptor を `kl_app_watch_fd` に揃え（増減を追う）、`kl_app_dispatch` の後に `kl_app_take` の event を shell_event（pointer の move の合併・button・wheel・leave・key と repeat・focus）、touch.c の列（指・touch pad の指と lift）、titlebar の列（`KL_WINDOW_ACTION`・`CONTROL_DONE`）へ分け、`KL_APP_FD` を `revents` に戻す。wheel の距離は libkeiland の 4 px/単位から browser の 3 px/単位に直す（今までと同じ 45 px/notch）。`shell_window_repeat` は消した（repeat は `kl_app_dispatch` の中、BUG-111 の順）。
- `shell/titlebar.c`: 旧 `keiland_titlebar_*` の transaction を宣言的な `kl_window_set_controls`（Back・Forward・Reload・Location の breadcrumb、action は 0x100 + ID）・`kl_window_set_action_state`（戻る・進むの可否）・`set_control_parts`・`set_control_text`・`focus_control_mode`（Ctrl+L と breadcrumb の click で `KL_FOCUS_EDIT`）に。`ZBROWSER TITLEBAR` の行は同じ。
- `shell/present.c`: surface を `kl_window_vulkan_surface` で作る（`vkCreateWaylandSurfaceKHR` を消した）。swapchain は上の表のとおり残す。
- `shell/touch.c`・`touch.h`: 呼び方を保ち `kl_gesture_*`・`kl_scroller_*`・`KL_GESTURE_*` に。`shell/shell.c`: repeat の待ちを消し（timeout は -1 から view と指の待ちで縮める）、`KL_TEXT_SUBMITTED`。`shell/internal.h`: `wayland-client.h`・`xdg-shell-client-protocol.h` の include を消し、`struct shell_window` を kl_app の物に。
- libkeiland: `KL_APP_FDS_MAX` を 16 から 64（browser の `SHELL_NET_FDS`）へ、`KL_VERSION` 50（Q1 が番号を調整してよい）。
- 準正常系・異常系の積み残しは [WS177 backlog-p1](../../ws177/backlog-p1.md) に 2 行（64 超の descriptor、`POLLERR` の区別）。

### 確認（host、2026-10-06）

| 確認 | 結果 |
| --- | --- |
| `make -j16 build/amd64/dynamic/libkeiland.so build/amd64/bin/browser`（`-Werror`） | exit 0、warning 0 |
| `make -j16 keiland-linux` | exit 0、warning 0 |
| `plan/tools/keiland-os-boundary/check.sh` | PASS |
| grep: browser の shell と main.c に `wl_registry`・`xdg_wm_base`・`wayland-client`・`xdg-shell`・`vkCreateWaylandSurface`・`wl_display` が無い | 無い（window.c の注記の「its own xdg-shell toplevel … before」だけ） |
| `git diff -- userland/desktop/libbrowser` | 0 行 |
| `plan/ws081/tests/run-browsertouch.sh build/ws131-p025/touch` | `host-browsertouch: ok (21 checks)` |
| `plan/ws081/tests/run-browser-scroll.sh` | `25 checks, 0 failed`。試験の側の古さを直した: WS074 の host-build の `main.o` が `browser_main.o` の名になり `main` が二重になっていたので、除く名に足した |

未実施: FreeBSD の native build（browser は入らない。libkeiland の変更は定数 1 つ）、QEMU の試験（T1 に依頼: `plan/ws074/tests/browser-guest.sh`、boot-test）。

## Resume

p020 の cleared と main への統合、WS074 との衝突の確認の後に、Q1 が Queue を作る。

## T1-272 の browser-p045・p056 の FAIL（2026-10-07、P1）

`95: 1: parameter not set`・`120: …`（exit 2）。原因は試験: link の位置を host の build（`build/ws074-host/plain/browser --dump=layout`）から取るが、T1 の worktree にその build が無く（`not found`）、`$link` が空だった。browser の不具合ではない（p014 は PASS、titlebar の control も ok）。
修正: 2 つの試験は host の build が無ければ `plan/ws074/tests/host-build.sh plain` で作り、`$link` が空なら理由を出して止まる。
