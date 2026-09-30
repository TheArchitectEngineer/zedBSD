<!-- awesome-plan project=zedbsd record=ws102-p001 -->

# ws102-p001: 設計

Status: cleared（2026-09-30）
Disposition: normal
Parent: [WS102](../ws.md)
Queue: main の依頼（2026-09-30、worktree `.claude/worktrees/ws102-keyboard`、branch `wt/ws102`）

## 範囲と受け入れ

ws.md の「設計で決めること」を決めた。受け入れは、[design.md](../design.md) に次のものが書かれていること。

- 形: compositor の `keyboard.c`。IME の file と seat.c の IME の hook を変えない。
- 段（L1〜L4）と数値目標と測り方。
- 小さな Phase の一覧。
- 自分での見直し。

source は変えない。

## 行ったこと

- compositor（`userland/desktop/wayland/`）の調査。調べたのは次のもの。
  - touch の経路と振り分け（`input.c`・`touch.c`）。
  - 角と端の gesture（`corner.c`・`home.c`・`shell.c`）。
  - key と文字列の送出（`seat.c` の `zwl_seat_key_deliver`、`ime.h` の公開の API）。
  - 描画（`glass.c`・`volume.c` の popup・frame の順・direct scanout）。
  - 作業の領域（4 か所）。
- text-input-v3 を持つ app の調査。持っているのは `ime-probe` だけで、Text Editor・Terminal は持たない。
- Windows の QEMU（WS085 の `usb-multitouch`）と、Linux の QEMU の注入（`touchinject`）の経路の確認。どちらも同じ evdev → `touch.c` を通る。
- 設計（design.md §1〜§6）と自分での見直し（§7）。

## 結果

- 設計: [design.md](../design.md)。
- 段と Phase: [ws.md](../ws.md)。L1 は p002〜p005 の 4 つ。
- 判断待ち（ユーザー）:
  - D1: Text Editor・Terminal へのかな。
  - D2: 日本語の変換と IME の組み方。
- D1 と D2 は L1 の開始を止めない。L1 では、かなは text-input の app で確かめる。

build・試験は無し（設計の Phase）。

## Resume point

2026-09-30: cleared。次は p002。
