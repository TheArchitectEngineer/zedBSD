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

## 統合の試験（2026-09-30 夕、Q1）

ユーザーの指示「P7の試験については、いまからすべての成果を統合して、1本の試験で動いたら、合格にしましょう。それはメインエージェントQ1でやります」により、
P4（ws090-p014）・P7（ws090-p008・p011）・P3（ws102-p024）・P6（ws094-p010）を main に統合した tree（722de611）で `plan/ws079/tests/demo-s8-s9.sh` を 1 本流し、
**PASS**（QEMU の Venus、build/amd64 で image を作り直した）。S8: Notes の全画面・pen の線・窓に戻る、S9: PDF Viewer の scroll の頁送り最長 140 ms、
page の frame 最長 148 ms、double tap と pinch、`ZWL ERROR` 0。画面と log は main の checkout の `build/integ-0930/`。この試験は Terminal を開かない
（Terminal の画面と p088 の切り分けは未実施）。実機は未実施。

## 手順（2026-10-01 追記）: 残りの回帰（再開の時）

p024 の「未実施」（全手順の回帰・1920x1080・WS079-p010・C9・boot test）を最新の main で埋める。source は変えない。`build/ws102-p024-final.sh` は main の checkout に無い（2026-10-01 確認）ので、下の command を使う。
試みは新しい記録として下に足し、上の結果は書き換えない。command の説明は [手引き](../guide.md) の「コマンド」。

```
mkdir -p build/ws102-p024r
sh plan/ws102/tests/build-inset-image.sh build/ws102-p024r-inset > build/ws102-p024r/inset-build.log 2>&1; echo "exit=$?"
make -j64 ZEDBSD_CONFIG=plan/ws102/tests/config-amd64-inset.mk BUILD=build/ws102-p024r-inset build/ws102-p024r-inset/bin/ime-probe >> build/ws102-p024r/inset-build.log 2>&1; echo "exit=$?"
grep -E ':[0-9]+:[0-9]+: warning:' build/ws102-p024r/inset-build.log | grep -vE '/packages/|^\.\./src/|userland/base/noct/noct/' | wc -l
sh plan/ws102/tests/host-keyboard.sh
sh plan/ws102/tests/host-inset.sh build/ws102-p024r/host-inset
sh plan/ws102/tests/host-emoji.sh
export GUEST_RUNTIME=$PWD/build/ws102-run
sh plan/ws079/tests/pen-guest.sh start build/ws102-p024r-inset/hdd-image.img
sh plan/ws079/tests/pen-guest.sh wait --timeout 240
BIN=build/ws102-p024r-inset sh plan/ws102/tests/osk-guest.sh build/ws102-p024r/osk-all install start pointer flick edges touch send close qwerty hand extra workarea roll tools history
sh plan/ws079/tests/pen-guest.sh stop
VENUS_SIZE=1920x1080 sh plan/ws079/tests/pen-guest.sh start build/ws102-p024r-inset/hdd-image.img
sh plan/ws079/tests/pen-guest.sh wait --timeout 240
OSK_WIDTH=1920 OSK_HEIGHT=1080 BIN=build/ws102-p024r-inset sh plan/ws102/tests/osk-guest.sh build/ws102-p024r/osk-large install large
sh plan/ws079/tests/pen-guest.sh stop
unset GUEST_RUNTIME
sh plan/ws099/tests/build-criteria-image.sh build/ws102-p024r-criteria > build/ws102-p024r/criteria-build.log 2>&1; echo "exit=$?"
sh plan/ws099/tests/criteria.sh build/ws102-p024r-criteria/hdd-image.img build/ws102-p024r/criteria C9
grep -c ' FAIL ' build/ws102-p024r/criteria/results.txt
sh plan/ws035/tests/zdesktop-guest.sh start build/ws102-p024r-criteria/hdd-image.img
sh plan/ws035/tests/zdesktop-guest.sh wait --timeout 240
sh plan/ws079/tests/zdesktop-p010.sh build/ws102-p024r-criteria build/ws102-p024r/p010
sh plan/ws035/tests/zdesktop-guest.sh stop
OUTPUT=build/ws102-p024r/boot plan/tools/boot-test.sh build/ws102-p024r-criteria/hdd-image.img; echo "exit=$?"
```

- image の build（inset と criteria）は続けて 1 つずつ。他の agent の image の build と重ねない。
- 時間の目安: build 2 回（各〜15 分）、osk-guest の全手順 20〜25 分、large 5 分、C9 約 15 分、p010 5 分、boot 1 分。

## 完了の条件（残りの回帰）

- build の `exit=0` が 2 回、warning の数 `0`。
- `host-keyboard: PASS`、`host-inset` と `host-emoji` が exit 0。
- `osk-guest: PASS` が 2 回（全手順と large）。
- C9 の `results.txt` の FAIL が 0。p076 だけが FAIL なら [BUG-125](../../bugs/BUG-125.md) として、p076 を単独で 3 回流し直し、結果を両方書く（BUG-125 は WS099 の提案 p017 で直す）。
- `p010: PASS`、`boot-test: PASS build/ws102-p024r/boot/login.png`（PNG をユーザーに見せる）。
- 上の全てを満たせば、ws.md の p024 の行の「全手順の回帰・C9・boot は未実施」を結果に替える（main に依頼）。
