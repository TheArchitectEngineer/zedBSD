<!-- awesome-plan project=zedbsd record=ws035p086 -->

# ws035-p086: terminal のタブ（titlebar の TABS mode）と configure_bounds

Phase ID: `ws035-p086`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-27、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-27 main の割り当て「terminal tabs through the titlebar TABS mode ... bind xdg_wm_base v4 and honour configure_bounds」）

## 範囲

1. 1 つの窓に複数の shell（タブ）。Ctrl+Shift+T（Shell > New Tab）で新しいタブ、Ctrl+Shift+W（Shell > Close Tab）・タブの ×
   で閉じる、titlebar のタブの click・zdesktop のタブの key（Ctrl+Tab・Ctrl+PageUp/Down）で切り替え、「+」。
2. xdg_wm_base version 4 で bind し、configure_bounds を守る（files と同じ形: 望む大きさを bounds に収める）。

## 設計の判断（戻せる既定）

- タブが 1 つのときは MENU mode（menu bar がそのまま見える）、2 つ以上で TABS mode（menu は右端の「…」、titlebar-design §13-1）。
- **zdesktop のタブの key から Ctrl+W と Ctrl+T を外した**（ws070-p013 で入れたもの）: terminal の shell が Ctrl+W（単語の削除）と
  Ctrl+T を使うため。タブを閉じる・作る key は application の menu の shortcut で持つ（files は File > New Tab Ctrl+T・
  Close Tab Ctrl+W、terminal は Ctrl+Shift+T・Ctrl+Shift+W）。Ctrl+Tab・Ctrl+Shift+Tab・Ctrl+PageUp/Down は zdesktop のまま。
  `zdesktop.h` の説明と `titlebar-p013.sh` を合わせた。
- タブの題名は「Shell N」（OSC の題名は使わない）。背景のタブの shell も毎回読む（pty が詰まらない）。

## 実装（2026-09-27）

- `userland/desktop/terminal/tabs.c`（新規）: libkeiland の titlebar（tab の追加・削除・状態、「+」、mode）、変わったときだけ
  1 つの transaction で送る、tab の click・×・「+」を main loop へ。
- `main.c`: タブの表（grid は malloc、shell と pty、ID、題名）、`main_tab_new`・`_switch`・`_close`・`_requests`・`main_tabs_show`、
  全タブの pty を poll して読む、resize・zoom は全タブの grid と shell へ、shell の終了はそのタブを閉じ、最後のタブで終わる
  （`ZTERM TAB new/active/closed`、`ZTERM TABS count= active= mode=`）。
- `window.c`: `terminal_window_dispatch` が複数の fd を poll、xdg_wm_base を min(広告, 4) で bind、`configure_bounds`。
- `menu.c`: Shell > New Tab（Ctrl+Shift+T）、Close Tab（Ctrl+Shift+W）。menu の項目は 28 → 30（`menu-p003.sh` を合わせた）。
- zdesktop `titlebar-shell.c`、`include/libc/zdesktop.h`: 上の key の判断。

## 検証（amd64、Venus の guest、2026-09-27）

- `plan/ws035/tests/zdesktop-p086.sh` PASS: bounds（`ZWL BOUNDS client=1 ... 1256x690`）、1 タブは menu（mode=0）、Ctrl+Shift+T で
  2 タブ（mode=2、strip）、タブ 1 の click で自分の画面、Ctrl+W は shell のもの（zdesktop に tab close の event なし）、Ctrl+Tab でタブ 2、
  「+」で 3 つ目とその × で閉じる、Ctrl+Shift+W で 1 タブと menu に戻る、`exit` で `ZTERM DONE reason=shell-exited`。
- 回帰 PASS: menu-p003（項目 30）、titlebar-p013（Ctrl+W・T を外した版）、titlebar-p011。
- 規約: `tabs.c` の style-check 0、変えた file は前と同じ 0。build warning 0。
- 画面（`/home/awe/zedBSD-rpi4/build/ws035-shots/`）: `p086-20260927-venus-tabs.png`（2 タブ、Shell 2）、`-tab1.png`（Shell 1 の画面）、
  `-one.png`（1 タブ、menu bar に戻る）。
- 実機（i915）: 未実施。boot test: 2026-09-27 のユーザーの指示で無し。

## 残り

- タブの題名を shell の OSC 0/2 の題名や作業 directory から付けること。
- タブごとの Edit > Select All の状態は grid が持つが、clipboard は窓で 1 つ（今のまま）。
- タブの drag での並べ替え（F-043）。
