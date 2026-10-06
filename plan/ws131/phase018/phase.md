<!-- awesome-plan project=zedbsd record=ws131-p018 -->

# ws131-p018: Terminal・Notes を新しい API へ（DnD・primary・tablet・fd の監視）

Status: test-wait（q807、P1、2026-10-06 実装済み・T1 の試験待ち。下の「q807（P1）: 実装」）
Disposition: normal
Parent: [WS131](../ws.md)、計画の正本 [design.md](../design.md)
Queue: q807（P1）
依存: p016 cleared。D8 の単独走行で番号の順
目安: 4〜5h（1 Queue）。実行者: Q1 が割り当てる（high）
所有 path: `userland/desktop/terminal/`・`notes/`、`libkeiland/app/`・`ui/`（DnD の受け・primary・tablet・fd の監視）、`plan/ws131/`

## 目的と結果

Terminal（pty の fd・自前の clipboard 808 行と primary 330 行）と Notes（tablet の自前の registry）の自前の registry を除き、libkeiland に DnD の受け・primary・tablet・`kl_app_watch_fd` を足して移す。Terminal の pty は Terminal に OS の依存を残す（D14、2026-10-03 user）: `TIOCSWINSZ` は 3 OS で同じで、`openpty` の header（`<pty.h>`／FreeBSD の `<libutil.h>`）だけを macro の block で切り替え、checker の許可の表に載せる。

## 範囲

1. libkeiland: `kl_window_accept_drops`、primary（自分の選択を自分で paste する時の詰まりの回避を保つ）、tablet の event、窓ごとの repeat の無効、全 motion。
2. Terminal・Notes の menu（413・316）・tabs を宣言的に。scroll の model の移行は WS090 の残りとして WS131 の完了の後に扱う（D9 の決定）。

## 受け入れ

- 3 OS の build（zedBSD の amd64 は exit 0・自前の warning 0、`make keiland-linux` の gcc と clang、FreeBSD の native build と `native-build-audit.py`）と `plan/tools/keiland-os-boundary/check.sh` PASS。
- 対象の app に旧名（`kui_`・`KUI_`・`keiland_`・`KEILAND_`）と自前の menu・titlebar・glass の結線・main loop が無い（grep）。結線の行数の減りを記録。
- zedBSD: `plan/ws081/tests/run-termtouch.sh`・`run-notestouch.sh`、`plan/ws128/tests/notes-p002.sh`、`demo-s8-s9.sh`、Terminal の clipboard・PRIMARY・drop の guest の手順、boot-test。Linux: 2 app の PNG と Terminal の shell。

## 検証の方法

[zedbsd-commands.md](../../tools/keiland-linux/zedbsd-commands.md)、[keiland-linux/README.md](../../tools/keiland-linux/README.md)、[keiland-freebsd/README.md](../../tools/keiland-freebsd/README.md)。QEMU の console・serial の log で判定しない（不具合は gdbstub・monitor・QMP）。build は自分の `BUILD=build/ws131-pNNN/`、共有の `build/` を消さない。FreeBSD の起動の確認は判断 D11 の範囲（passthrough なし）。QEMU と実機の証拠を分ける。

## 衝突・危険・rollback

- 衝突: WS128・WS079 と同じ file。 順は Q1 が決める（design.md §7.2）。
- rollback: 前へ直すのを基本にし、後ろに同じ file の変更が無い時だけ統合の commit を revert する（design.md §7.4）。

## Resume

依存の Phase の cleared と main への統合、関係する判断の決定の後に、Q1 が Queue を作る。

## q807（P1、2026-10-06）: 中断の時点

Terminal から始めた（Notes は P2 の WS175 p007 の統合の後、Q1 の指示）。調べただけで code は未変更のまま、q812（ws090-p020、Mahora の font）の優先の投入で止めた。再開の時の設計の案:

- libkeiland（KL_VERSION 44）: 選択の変化の input `KL_WINDOW_SELECTION`（code は clipboard か primary、pressed は text の有無。Terminal の log `ZTERM CLIPBOARD selection text=`・`ZTERM PRIMARY offer text=` はここから）と、自分の選択かの問い合わせ。
  drop の受け `kl_window_accept_drops(window, types)` と input `KL_WINDOW_DROP_ENTER`・`_LEAVE`・`KL_WINDOW_DROP`、読み出し `kl_window_take_drop`（自分の drag の text は pipe を通さずに渡す、ws035-p093）。
  text の drag の source `kl_window_drag_text` と input `KL_WINDOW_DRAG_DONE`（dropped か）。titlebar の tab `kl_window_set_tabs`（`struct kl_tab_entry`）と input `KL_WINDOW_TAB`（選択・閉じる・新規）。
- Terminal: `kl_app` の loop と `kl_app_watch_fd`（shell の pty）、自前の registry・clipboard.c・primary.c を除き、uri-list を quote した語にする所だけ Terminal に残す。menu.c・tabs.c を表に。pty の header は `__FreeBSD__` の macro の block。
- 試験の log で library の中に移って出せなくなる物: `ZTERM PRIMARY send bytes=`（zdesktop-p100.sh:83）。受け手の `paste received bytes=` で代える案。
- 窓ごとの repeat の無効・全 motion・tablet は Notes の側で要る物（Notes の時に）。

## q807（P1、2026-10-06）: 実装

q812（ws090-p020）の後に再開。Notes は P2 の WS175 p007・p008 の統合の後（Q1 の指示）。

### libkeiland（KL_VERSION 44）

- **tab**: `kl_window_set_tabs(window, struct kl_tab_entry *, count, options)`（ID・題・KL_TAB_* の表、変化だけを一つの transaction で、無い時は titlebar を外すか controls の mode に戻す）と input `KL_WINDOW_TAB`（`KL_WINDOW_TAB_CHOSEN`・`_CLOSE`・`_NEW`）。`window-declare.c`。
- **選択の変化**: input `KL_WINDOW_SELECTION`（code は `KL_SELECTION_CLIPBOARD`・`_PRIMARY`、pressed は text の有無）と `kl_window_selection_own`。`clipboard.c`・`primary.c`。
- **drop**: `kl_window_accept_drops(window, KL_DROP_TEXT | KL_DROP_URIS)`、input `KL_WINDOW_DROP_ENTER`・`_LEAVE`・`KL_WINDOW_DROP`（drag の位置）、`kl_window_take_drop`（uri-list はそのまま、自分の drag の text は pipe を通さずに、ws035-p093）。Terminal の clipboard.c から移した。
- **drag**: `kl_window_drag_text(window, text, length, serial)` と input `KL_WINDOW_DRAG_DONE`（dropped か）。
- **tablet**: `ui/tablet.c`（Notes の tablet.c を移した）。`kl_window_accept_tablet`、input `KL_WINDOW_TABLET_DOWN`・`_MOTION`・`_UP`・`_HOVER`・`_LEAVE`、`kl_window_event` に `tool`（`KL_TABLET_PEN`・`_ERASER`）・`buttons`（`KL_TABLET_BUTTON_*`）・`pressure`（無い tool は -1）・`tilt_x`・`tilt_y`。tablet manager は窓の global として bind、tablet seat は accept の時だけ（取らない app には pen は pointer のまま）。
- **窓ごとの repeat の無効**: `kl_window_set_repeat`。**全 motion**: libkeiland は pointer の motion を元から全て queue に積む（合わせない）ので変更なし。
- **fd の監視**: 既存の `kl_app_watch_fd` を使用。

### Terminal

- `kl_app` と `kl_app_window_create`（KL_PRESENT_NONE）、`kl_app_dispatch` の前に shell の pty を `kl_app_watch_fd` で同期（無くなった fd は外す）、`KL_APP_FD` を ready に。key の repeat は kl_app の中（`terminal_window_repeat`・`_wait` を除いた）。自前の registry と clipboard・primary・drag・drop の protocol の code を除き、`clipboard.c`（uri-list を quote した語にする所だけ残す）・`primary.c` は libkeiland の上の薄い層。menu は `kl_menu_entry` の表と action の状態、tab は `kl_window_set_tabs`（tab 1 つでは titlebar を作らない。zdesktop は既定で窓を飾る、ws114-p008）。Vulkan の surface は `kl_window_vulkan_surface`、題は `kl_window_set_title`。
- pty の header は `__FreeBSD__` で `<libutil.h>`／他は `<pty.h>`（D14、checker の許可の表への登録は Q1）。旧名は 0（header guard も `TERMINAL_*_H` に、`ambiguous-gen.py`・`shaders/regenerate.py` も）。
- 試験の log は保つ（`ZTERM MENU item= action= serial=`・`ZTERM TABS count= active= mode=`・`ZTERM DROP enter|bytes`・`ZTERM DRAG start|done`・`ZTERM CLIPBOARD set|received|selection`・`ZTERM PRIMARY set|offer|paste own|paste received`）。`ZTERM PRIMARY send bytes=` は libkeiland の送信になって出ない → `plan/ws035/tests/zdesktop-p100.sh` の 1 行を除いた（受け手の `paste received bytes=10` で確かめる）。
- 行数: clipboard.c 808→259、primary.c 330→62、menu.c 483→357、tabs.c 257→183、window.c 817→847（kl_app の fd の同期と新しい input の分）。

### Notes（P2 の p007・p008 の後、変更は小さく）

- `kl_app`・`kl_app_window_create`、`kl_window_set_repeat(0)`・`kl_window_accept_tablet`。menu は表と action の状態（`notes_menu_chosen`）。自前の registry・titlebar（飾りの要求、今は zdesktop の既定）・tablet の protocol の code を除き、`tablet.c` は `KL_WINDOW_TABLET_*` を Notes の入力（eraser の端か第 1 barrel button で消しゴム、pressure が無い tool は pointer の値）に変える 80 行に。Vulkan の surface は `kl_window_vulkan_surface`。旧名 0。
- 外観は main.c の既存の `kl_appearance_open` のまま（P2 の main.c との衝突を避けた。`KL_APP_THEME` への移行は残り）。
- `NOTES TABLET tool type=` の log は libkeiland の中に移って出ない → `plan/ws079/tests/notes-pen.sh` の 1 行を除いた（STROKE の pressure と tilt で確かめる）。
- 行数: menu.c 316→238、tablet.c 660→80、window.c 454→407。

### 確認

- build: zedBSD amd64 の libkeiland・terminal・notes（exit 0、warning 0）、`make keiland-linux` の gcc と clang（exit 0、warning・error 0）。`exports.py --check` OK。
- host: `sh plan/ws131/tests/host-tabs.sh`（新規、titlebar の stub で tab の表の送信と input）PASS 19 項目。`plan/ws081/tests/run-termtouch.sh` ok (20)、`plan/ws090/tests/host-pad.sh` ok (7)、`plan/ws081/tests/run-notestouch.sh` ok (52)。
- 未実施: FreeBSD の build、keiland-os-boundary（Q1）、QEMU（T1）: Terminal の clipboard・PRIMARY・drop・drag・tab（zdesktop-p079・p086・p088・p091・p093・p100・p103・p111、ws081 p014-guest）、Notes の pen（ws079 notes-pen・notes-p011、ws128 notes-p002）、run-termtouch の guest、demo-s8-s9、boot-test、Linux の 2 app の PNG と Terminal の shell。
