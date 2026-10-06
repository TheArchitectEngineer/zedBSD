<!-- awesome-plan project=zedbsd record=ws131-p016 -->

# ws131-p016: Text Editor を新しい API へ（最初の移行）

Status: test-wait（q807、P1、2026-10-06 実装済み・T1 の試験待ち。下の「q807（P1）」）
Disposition: normal
Parent: [WS131](../ws.md)、計画の正本 [design.md](../design.md)
Queue: q807（P1）
依存: p015 cleared。D8 の単独走行で番号の順
目安: 3〜4h（1 Queue）。実行者: Q1 が割り当てる（high）
所有 path: `userland/desktop/textedit/`、API の不足の補い（`libkeiland/app/`・`ui/`）、`plan/ws131/`

## 目的と結果

Text Editor（部品・IME・chooser・inset・編集の操作を全て使う）を最初に `kl_app` と宣言的な部品へ移して API を確かめる。

## 範囲

1. `<keiland.h>` と新名に。`menu.c`（540 行の結線）・`titlebar.c`（313）・`glass.c`（112）・main loop を宣言的な表と `kl_app_dispatch`・`take` へ。
2. 見つかった API の不足は libkeiland に足して記録。

## 受け入れ

- 3 OS の build（zedBSD の amd64 は exit 0・自前の warning 0、`make keiland-linux` の gcc と clang、FreeBSD の native build と `native-build-audit.py`）と `plan/tools/keiland-os-boundary/check.sh` PASS。
- 対象の app に旧名（`kui_`・`KUI_`・`keiland_`・`KEILAND_`）と自前の menu・titlebar・glass の結線・main loop が無い（grep）。結線の行数の減りを記録。
- zedBSD: `plan/tools/textedit/host-core.sh`、`textinput-p013.sh`、`plan/ws090/tests/sheet-guest.sh`、`plan/ws102/tests/edit-guest.sh`・`inset-guest.sh`、`plan/ws128/tests/textedit-p003.sh`、boot-test。Linux: Text Editor の起動と入力の PNG。

## 検証の方法

[zedbsd-commands.md](../../tools/keiland-linux/zedbsd-commands.md)、[keiland-linux/README.md](../../tools/keiland-linux/README.md)、[keiland-freebsd/README.md](../../tools/keiland-freebsd/README.md)。QEMU の console・serial の log で判定しない（不具合は gdbstub・monitor・QMP）。build は自分の `BUILD=build/ws131-pNNN/`、共有の `build/` を消さない。FreeBSD の起動の確認は判断 D11 の範囲（passthrough なし）。QEMU と実機の証拠を分ける。

## 衝突・危険・rollback

- 衝突: WS128 と同じ file。 順は Q1 が決める（design.md §7.2）。
- rollback: 前へ直すのを基本にし、後ろに同じ file の変更が無い時だけ統合の commit を revert する（design.md §7.4）。

## Resume

依存の Phase の cleared と main への統合、関係する判断の決定の後に、Q1 が Queue を作る。

2026-10-06 ユーザー（クリック）「承認、p016 から順に」: p016〜p020（app を kl_app へ）を番号の順に進めてよい。

## q807（P1、2026-10-06）

### 変更

- **名前**: textedit の source から旧名（`kui_`・`KUI_`・`keiland_`・`KEILAND_`）を全て新名に（`keiui.h`・`keiland.h` の互換の define を機械的に写した）。`<keiui.h>` の include を除いた。
  残る `KEILAND_DATADIR`（path の macro、他の app も同じ）と `struct keiland_color_image`（picture/ の型）は libkeiland の API ではない。
- **app の骨組み**: `kl_app_open` と `kl_app_window_create`、main loop は `kl_app_dispatch`・`kl_app_take`（key の repeat は dispatch の中）。外観は `KL_APP_THEME`
  （`kl_appearance_open` を除いた）。menu・titlebar の選択は `KL_WINDOW_ACTION`（`main_action`、log `ACTION id= action=`、Replace の Select All はそのまま）。
- **宣言的な部品**: menu.c は `kl_menu_entry` の表と `kl_window_set_menu`（Open Recent の項目は表の後ろに足して送り直す）、文脈の menu は `kl_window_popup_menu`、
  editor の状態は action の状態（`kl_window_set_action_state`、menu の項目と titlebar の control に同時に効く）。titlebar.c は `kl_control_entry` の表と
  `kl_window_set_controls`、find field は `kl_window_set_control_text`・`kl_window_focus_control` と入力の `KL_WINDOW_CONTROL_TEXT`・`_DONE`。glass.c は
  `kl_window_set_glass`（最初の card で glass の有無を知る）。`te_window_action`（`kl_window_post`）を除いた。
- **行数**: menu.c 540→352、titlebar.c 313→153、queue.c 104→89、glass.c 112→132（card の送りを 1 つの関数に）。menu・titlebar の Wayland の object と listener は無くなった。
- **振る舞いの差**: menu の Save も変更が無い時は灰色（titlebar の Save と同じ action の状態を共有するため）。

### libkeiland に足した物（KL_VERSION 43）

- `KL_WINDOW_CONTROL_TEXT`（19）・`KL_WINDOW_CONTROL_DONE`（20）: 宣言的な titlebar の field の text の入力（id・text、done は code に how）。
- `kl_window_set_control_text`・`kl_window_focus_control`。
- menu・context menu・control の選択の serial を window の最後の入力に（clipboard の copy の serial、旧 menu.c の `kl_window_set_serial` の代わり）。
- 同じ版で別に: `KL_WINDOW_AXIS_STOP` を 18 に（17 は `KL_WINDOW_ACTION` と衝突していた、9586770fa）、`kl_ui_axis` の `KL_UI_AXIS_FLUNG`（abb83ef0f）。

### 確認

zedBSD amd64 の libkeiland.so・textedit の build（warning 0）、`plan/tools/textedit/host-core.sh` 53/53。guest の試験が読む Text Editor の log の行（READY・OPEN・SAVE・REPLACE・
MENU recent・RECENT・CHOSEN・TEXT）は変えていない（`MENU item=`・`TITLEBAR control=` を読む試験は無い）。
未実施: `make keiland-linux`（make の規則の中の rm の扱いを Q1 に確認中）、FreeBSD の build（この環境に無い）、`plan/tools/keiland-os-boundary/check.sh`（中に `rm -rf`、Q1 に依頼）。
QEMU の試験は T1 に依頼する。

## q807-i02（P1、2026-10-06）: 起動の直後の最初の key の欠け

T1-248 で ime-p007 が FAIL ×2（保存が `漢字\n日本語Hello`）。T1-250（`plan/ws131/tests/textedit-first-key.sh`）で、READY の直後に送った最初の Ctrl+End だけが効かず（Ln 1, Col 1）、2 秒後・3 秒待った後の Ctrl+End は効く（Ln 2, Col 1）ことを確かめた。
zdesktop の log では MAP と IME の activate が READY の後に来ていて、key が届いた時に窓がまだ keyboard を持っていない（または text input が後から有効になる）と見られる（Venus の compositor の frame は 100 ms 以上）。
直し（ca572c7d8、`textedit/main.c`）: READY を「最初の frame を出し、窓が keyboard を得た（KL_WINDOW_FOCUS）時」、得られなければ 2 秒後に出す（log に `focus=1|0`）。text input は最初の frame の前に求める。
確認: zedBSD amd64 の textedit の build（warning 0）。QEMU は T1 に ime-p007 の流し直しを依頼。
