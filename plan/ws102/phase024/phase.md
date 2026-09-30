# ws102-p024 — 右の列の道具の面の「履歴」の tab

- Parent: [ws102](../ws.md)
- Status: cleared（2026-09-30、QEMU）
- Disposition: normal
- Level: L2
- Prerequisites: p016（道具の面）、p018（`zwl_clipboard_history_*`）

## 目的と受け入れ条件

flick の panel の道具の面の「履歴」の tab に、p018 のクリップボードの履歴（新しい順、最大 10 件、長い文は 1 行に切る）を並べ、
tap した項目を貼る。受け入れ: 3 つの app で複写した後、履歴の 2 番目を tap すると受け手に入る。

## 実装

`userland/desktop/wayland/keyboard.c`:

- 道具の面に `tools_face`（編集 / 履歴）を持つ。「編集」の tab で編集の面、「履歴」の tab で履歴の面（log `ZWL OSK tool face=history items=N`）。
  履歴の面では編集の道具（2 行目以降）を描かず、tap も受けない。「候補」「絵文字」の tab は無効のまま。
- `keyboard_history_rect`・`keyboard_history_at`・`keyboard_history_release`・`keyboard_draw_history`: 1 行 26 px・間隔 32 px で最大 10 行。
  改行・tab は空白に替え、160 byte で切る。空なら「履歴はまだありません」。tap の release で `zwl_clipboard_history_paste`
  （log `ZWL OSK history paste index=%u error=%d`）。

`plan/ws102/tests/osk-guest.sh`:

- 手順 `history`: textedit を 3 つ（alpha・bravo・charlie）開いて Ctrl+A・Ctrl+C、空の receive.txt を開き、flick → 「履歴」の tab →
  2 行目を tap → 保存して "bravo" を確かめる。
- `install` は build の `dynamic/*.so` を全部（libc・ld を除く）入れ、色の絵文字の font（p019）を `/usr/share/fonts/keiland-emoji.ttf` に置く
  （main の merge で compositor が libpng-compat・libz-compat に依るようになり、pen の image には無かった）。

## 確認

| 確認 | 結果 |
| --- | --- |
| build（`build/ws102-amd64` の wayland・textedit・ime-probe・wltest・libkeiui・libpng-compat・libz-compat） | rc=0、warning 0 |
| `plan/tools/style-check.py`（keyboard.c） | 違反 0 |
| QEMU（pen の image）`osk-guest.sh install history` | PASS。`tool face=history items=3`、`history paste index=1 error=0`、receive.txt が "bravo" |
| 画面 | `build/ws102-shots/p024/history.png`（charlie・bravo・alpha）、`history-pasted.png`（bravo が入り、履歴の先頭が bravo） |

## 未実施

- 全手順の回帰（osk-guest の全手順・WS079 p010・1920x1080・C9・boot test）: build の後、ユーザーの指示のラップアップで止めた（未実施）。
- 実機: 未実施。

## 残り

- ユーザーの指示で優先を下げ、P3 の担当を終えた（2026-09-30）。全手順の回帰は再開の時に `build/ws102-p024-final.sh` で流す。
  再開の条件: ユーザーが再開を言うとき。
