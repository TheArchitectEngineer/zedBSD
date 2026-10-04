<!-- awesome-plan project=zedbsd record=ws131-p015 -->

# ws131-p015: app の骨組みの API（kl_app）

Status: in-progress（q706、P2 generation14、2026-10-05。実装・3 OS のうち zedBSD と Linux の build・host 試験まで、QEMU は T1 待ち。判定は Q1）
Disposition: normal
Parent: [WS131](../ws.md)、計画の正本 [design.md](../design.md)
Queue: q706（P2）
依存: p014・p010 cleared
目安: 4〜5h（1 Queue）。実行者: Q1 が割り当てる（high）
所有 path: `userland/desktop/libkeiland/`（新しい `app/`、`ui/window.c` の app への分け、宣言的な層）、`keiland-ui.h`、exports.map、`userland/tests/kuidemo/`、`plan/ws131/tests/`、`plan/ws131/`

## 目的と結果

design.md §6 の `kl_app`（一つの registry と一度の roundtrip、pull 型の event loop、fd の監視、一つの event の queue、`kl_app_system()`）と、宣言的な menu・titlebar・glass と action の queue を足す。既存の `kl_window_open()` は暗黙の app を作る形で残す。

## 範囲

1. `kl_app_*`、`kl_app_window_create`、`kl_window_set_menu`・`set_controls`・`set_action_state`・`popup_menu`・`set_glass`、`kl_window_vulkan_surface`（`VK_VERSION_1_0` の条件、review 20）。
2. registry の一本化（titlebar・menu・glass・edit・inset・text-input・data device・primary）。
3. host の試験（表 → model、差分だけ送る、bind の回数）。kuidemo を新 API の見本に。

## 受け入れ

- 3 OS の build（zedBSD の amd64 は exit 0・自前の warning 0、`make keiland-linux` の gcc と clang、FreeBSD の native build と `native-build-audit.py`）と `plan/tools/keiland-os-boundary/check.sh` PASS。
- 新しい host の試験 PASS、kuidemo が新 API だけで動く（zedBSD と Linux の PNG）。Text Editor の試験一式（互換のまま）PASS、boot-test。BUG-111・BUG-112・IME の順・touch の時刻の補正の退行が無い。

## 検証の方法

[zedbsd-commands.md](../../tools/keiland-linux/zedbsd-commands.md)、[keiland-linux/README.md](../../tools/keiland-linux/README.md)、[keiland-freebsd/README.md](../../tools/keiland-freebsd/README.md)。QEMU の console・serial の log で判定しない（不具合は gdbstub・monitor・QMP）。build は自分の `BUILD=build/ws131-pNNN/`、共有の `build/` を消さない。FreeBSD の起動の確認は判断 D11 の範囲（passthrough なし）。QEMU と実機の証拠を分ける。

## 衝突・危険・rollback

- 衝突: — 順は Q1 が決める（design.md §7.2）。
- rollback: 前へ直すのを基本にし、後ろに同じ file の変更が無い時だけ統合の commit を revert する（design.md §7.4）。

## Resume

依存の Phase の cleared と main への統合、関係する判断の決定の後に、Q1 が Queue を作る。

## 実施（q706、P2 generation14、2026-10-05）

### API（`keiland-ui.h`、KL_VERSION 26）

- `struct kl_app`・`kl_app_open/close/dispatch/watch_fd/take/system/display/window_create`、`struct kl_app_options`・`struct kl_app_event`（KL_APP_WINDOW: window と input、KL_APP_FD: fd と ready）、`KL_APP_FDS_MAX 16`・`KL_APP_FD_READ/WRITE/HANGUP`。
- 宣言的な層: `kl_window_set_menu`・`set_controls`・`set_action_state`・`popup_menu`・`set_glass`、`struct kl_menu_entry`（各 app の `struct menu_item` と同じ形）・`struct kl_control_entry`、`KL_ACTION_DISABLED/CHECKED/HIDDEN`、選ばれた action は窓の input の `KL_WINDOW_ACTION`（code=action、id=item・control、begin=breadcrumb の part）。
- `kl_window_vulkan_surface`（`VK_VERSION_1_0` の条件、review 20）。
- design.md §6 との違い: `kl_window_set_glass` の count は `size_t`（`kl_glass_set_panels` と揃えた）。`kl_window_open()` は今の自前の接続のまま（「暗黙の app」にはしていない: Text Editor ほかの経路を変えないため。中身は同じく一つの接続と一つの queue）。

### 実装

- `ui/app.c`: 一つの接続・一つの registry・一度の roundtrip で globals の表（96 まで）、窓の list、一つの event の ring（512）、fd の監視、`kl_app_dispatch` は各窓の edit の状態を送り、repeat の期限まで待ち、読んだ後に fd の event と期限の来た repeat を積む（BUG-111 の順）。`kl_app_system` は初回に `kl_system_open`。`keiui_app_global`（display の app の表から global を返す）。
- `ui/window.c`: `keiui_window_open_app`・`window_setup_app`（app の表の globals を `window_global` に流して app の registry から bind、search も roundtrip も無し。最初の configure の roundtrip の間の input は積まない）、`window_surface`（標準の窓と共通にした）。`window_push` は app の窓なら app の queue へ。`kl_window_close` は app から外し（queue からその窓の event も除く）、宣言的な object を toplevel より先に壊す。
- `ui/declare.c`・`declare.h`: 表と表示中の model の差分を sink への操作の列にする純粋な model（変わらなければ transaction も送らない、木の形が変われば作り直し、action の状態は該当の項目だけ、拒否されても commit を送り次は作り直し）。
- `ui/window-declare.c`: model を libkeiland の menu（window menu・context menu）・titlebar（controls mode）・glass に送る sink、listener から `KL_WINDOW_ACTION` を積む。app の窓は app の menu service を共有。
- `ui/globals.c`（registry の一本化）: titlebar・menu・glass・edit・keyboard inset の bind を `keiui_global_find/bind/end` に統一。app の display なら app の表から bind（search と roundtrip なし）、そうでなければ今までの自前の queue の search。各 module の重複した search の code（約 450 行）を除いた。system.c は最初の状態の roundtrip が要るので今のまま。
- `userland/tests/kuidemo`: 新 API だけで動く見本に（`kl_app_open`・`kl_app_window_create`・`kl_app_dispatch/take`、menu（File > Quit、Page > 3 頁の radio、Ctrl+1〜3）、controls（前の頁・次の頁、端で disabled）、右 press の context menu、glass は `kl_window_set_glass`、`KUIDEMO ACTION action= id=` の log）。
- exports.map 再生成（14 の追加）、Makefile 3 本に 4 file。

### 確認（host）

- zedBSD: `dynamic/libkeiland.so` warning 0（exports 14）、kuidemo・textedit・imageview・files・settings・notes・pdfviewer・terminal・compositor の rootfs の build exit 0・warning 0。
- Linux: `make keiland-linux` gcc・clang warning 0（kuidemo を含む）。FreeBSD は未実施（ユーザー: ベータ1 まで不要）。
- `plan/ws131/tests/host-declare.sh`: 22/22（初回の build、同じ表で何も送らない、label だけ、action の状態、shortcut と action、項目の追加で作り直し（入れ子は親だけ remove）、拒否の後の作り直し、controls の label・action・role・状態、空の表、状態の表の上限）。
- `keiland-os-boundary/check.sh` PASS、`rename-map.py check-keiland` PASS、規約の checker: 新しい file と変えた file で 0（edit.c・keyboard-inset.c の既存の 2 件は今回の変更の外）。

### 未実施（T1 に依頼）

- QEMU: `plan/ws131/tests/kuidemo-p015.sh`（image `plan/ws131/tests/config-amd64-p015.mk`）と、bind の変更（titlebar・menu・glass・edit・inset）の退行の確認に既存の回帰（files-regress・textinput-p013・viewers-p008・menu-p003・titlebar-p010、TQ-1 の A と同じ組）。
- Linux の kuidemo の PNG（Debian guest）。FreeBSD の native build。
