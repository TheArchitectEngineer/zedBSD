<!-- awesome-plan project=zedbsd record=ws131-p019 -->

# ws131-p019: Settings の窓を新しい API へ

Status: cleared（2026-10-07 Q1 の判定: T1-255 PASS、後の回帰（T1-281: boot-test・textinput-p013・viewers-p008・demo-s8-s9・titlebar-p010・files-regress、T1-295 menu-p003、Linux の 8 app の PNG、FreeBSD の backend-test）も PASS）（旧: test-wait（q807、P1、2026-10-06 実装済み・T1 の試験待ち。下の「q807（P1）」））
Disposition: normal
Parent: [WS131](../ws.md)、計画の正本 [design.md](../design.md)
Queue: q807（P1）
依存: p011・p016 cleared。旧 ws090-p007 の窓の部分はこの Phase（D9 の決定）。WS090 の他の残りは WS131 の完了の後
目安: 4h（1 Queue）。実行者: Q1 が割り当てる（high）
所有 path: `userland/desktop/settings/`（`window.c`・`present.c`・`menu.c`・`titlebar.c`・`glass.c`・`main.c`、Makefile 3 本）、`plan/ws131/`

## 目的と結果

Settings の自前の窓（`window.c` 1,350 行）と present（`present.c` 1,218 行）を除き、`kl_app` と宣言的な部品へ。Files の canvas・text・icons の source の共有の置き換えは描画が byte で一致する時だけ。

## 範囲

1. 窓・present・入力を libkeiland へ、menu（371）・titlebar（358）・glass（162）を宣言的に、`kl_app_system()` を使う。

## 受け入れ

- 3 OS の build（zedBSD の amd64 は exit 0・自前の warning 0、`make keiland-linux` の gcc と clang、FreeBSD の native build と `native-build-audit.py`）と `plan/tools/keiland-os-boundary/check.sh` PASS。
- 対象の app に旧名（`kui_`・`KUI_`・`keiland_`・`KEILAND_`）と自前の menu・titlebar・glass の結線・main loop が無い（grep）。結線の行数の減りを記録。
- zedBSD: `plan/ws089/tests/host-build.sh`・`settings-regress.sh`・`settings-p007.sh`、`volume-p005.sh`、boot-test。Linux: Settings の全頁の PNG。

## 検証の方法

[zedbsd-commands.md](../../tools/keiland-linux/zedbsd-commands.md)、[keiland-linux/README.md](../../tools/keiland-linux/README.md)、[keiland-freebsd/README.md](../../tools/keiland-freebsd/README.md)。QEMU の console・serial の log で判定しない（不具合は gdbstub・monitor・QMP）。build は自分の `BUILD=build/ws131-pNNN/`、共有の `build/` を消さない。FreeBSD の起動の確認は判断 D11 の範囲（passthrough なし）。QEMU と実機の証拠を分ける。

## 衝突・危険・rollback

- 衝突: WS089・WS113 p006 と同じ file。 順は Q1 が決める（design.md §7.2）。
- rollback: 前へ直すのを基本にし、後ろに同じ file の変更が無い時だけ統合の commit を revert する（design.md §7.4）。

## Resume

依存の Phase の cleared と main への統合、関係する判断の決定の後に、Q1 が Queue を作る。

## q807（P1、2026-10-06）

### libkeiland（KL_VERSION 45）

Settings の自前の窓が持っていた物を libkeiland の窓へ: `kl_window_maximized`・`kl_window_set_maximized`・`kl_window_minimize`（configure の MAXIMIZED を保ち、変わったら KL_WINDOW_RESIZE）、`kl_window_output_mode`（最初の wl_output の current の mode、未知は ENOENT）、`kl_window_device_name`（Vulkan の presenter の device 名）、`kl_window_present_times`（`struct kl_present_times`、SLOW-FRAME の行）、`kl_window_set_control_parts`（breadcrumb の部分、選ばれた部分は ACTION の begin）、`kl_window_set_glass_blur`（glass を作り blur を選ぶ、glass の無い compositor は ENODEV）。`ui/present.c` に `<stdio.h>`（Linux の build の修正）。

### Settings

- `window.c` 1,396→521 行: `kl_app`・`kl_app_window_create`（KL_PRESENT_VULKAN）、`kl_app_dispatch`・`kl_app_take` の input を `se_event` に変える（wheel は libkeiland の 4 倍の単位を Settings の 3 倍に換算し端数を持ち越す、touch pad の source と停止、指 1 本を pointer に、key の repeat は kl_app の中）。titlebar の control の action（`SE_TITLEBAR_ACTION`＋ID）と field の text は titlebar の queue へ、menu の action は `SE_EVENT_ACTION` へ（log `MENU item= action= serial=` を保つ）。一回だけの起動の socket は `kl_app_watch_fd`。
- `present.c` 1,218→138 行: libkeiland の presenter（Settings が写していた Files の present.c と同じ物）の上の薄い層（size・device 名・see-through・時間）。`shaders.h` は使われなくなった（削除は Q1 の rm の手順）。
- `menu.c` 371→240、`titlebar.c` 358→272、`glass.c` 162→164: 表と action の状態、`kl_window_set_controls`・`_control_parts`・`_control_text`・`_focus_control`、`kl_window_set_glass`・`_glass_blur`。試験の log（`MENU ready|state`、`TITLEBAR ready|activated|state`、`GLASS on|off|blur|panels`、`WINDOW zoom|minimize`）は保つ。
- 旧名 0（`KEILAND_TEXT_*`→`KL_TEXT_*`、header guard `SETTINGS_SETTINGS_H`。artwork の `KEILAND_MARK_LAYERS` は API でない）。
- 残り: `kl_app_system()` への移行は host の試験（`host-kl-system.c` の stub が `kl_system_open` を差し替える）を壊さないように見送り（`se_system_open` は app の接続で `kl_system_open`）、外観は既存の `kl_appearance_open` のまま。

### 確認

- build: zedBSD amd64 の libkeiland・settings（exit 0、warning 0）、`make keiland-linux` の gcc と clang（exit 0、warning・error 0）。
- host: `plan/ws089/tests/host-build.sh` built settings-render、`host-about.sh` 10 checks passed。`run-host-dark.sh`・`run-host-instance.sh`・`run-host-wired.sh` は host の rm を含むので流していない（Q1 へ）。`plan/ws131/tests/host-tabs.sh` PASS（KL 45 の stub を足した）。
- 未実施: FreeBSD、keiland-os-boundary（Q1）、QEMU（T1）: `settings-regress.sh`・`settings-p007.sh`・`volume-p005.sh`、boot-test、Linux の Settings の全頁の PNG。
