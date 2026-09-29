<!-- awesome-plan project=zedbsd record=ws035p136 -->

# ws035-p136: Terminal の窓は本体だけを直角に、浮いた title bar は丸めたまま

Phase ID: `ws035-p136`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-30、サブエージェント、worktree `wt/ws035`。QEMU の Venus、実機は未実施）
Phase disposition: normal
Queue: なし（2026-09-29 main の割り当て。ユーザーの指示（2026-09-29 夜）「ターミナルのフローティングタイトルバーが、ボディと一緒にラウンドが
取れてしまっています。タイトルバーはラウンドさせたまま、ウィンドウボディだけをラウンドなしにしたいです。それはターミナルだけ特別で、ほかの
アプリはボディをラウンドさせたままにしたいです。」）

## 範囲と受け入れ

[p134](../phase134/phase.md) の直し。Terminal（app_id `terminal`）では、浮いた title bar（`draw_title_bar` の影とガラス、背後のぼかしの下地の
title bar のガラス）の角を丸めたままにし、本体（`draw_body` の像・影・すりガラス）の四隅だけを直角にする。他の app は title bar も本体も丸める。
Wiseview の縮図は本体の像が直角、title bar（開く途中で消えていくもの）は丸いまま。

## 実装

`userland/desktop/wayland/shell.c`: `draw_title_bar` と `draw_window_blurred`（title bar のガラス）から `window_square()` の判定を外し、
`GLASS_RADIUS` に戻した。`draw_body` と `draw_tile`（Wiseview の縮図の影・像・青い縁）は p134 のまま、表の窓で半径 0。表の説明を
「本体が直角、title bar は丸いまま」に直した。規約: `style-check.py shell.c` 0、`git diff --check` 0。

## 検証

**QEMU（amd64、Venus の guest 1280x800、graphical の login の image `build/p136-after.img`、main（WS095 p004）を取り込んだ後）**:

- 判定の道具 `plan/ws035/tests/p134-corners.py` を直した: 角の画素（角から 1 px 内側）を、同じ行の内側の画素（辺から 20 px、丸めの外）と
  外側の画素（辺の 2 px 外）と比べ、内側に近ければ直角、外側に近ければ丸い（外側が画面の外のときは内側との差 24 以下で直角）。
  半透明の title bar のガラスも見分けられる。既存の画面で確かめた: p133 の画面（全部丸い）で本体 0/4・title 0/4 直角、p134 の画面
  （全部直角）で本体 4/4・title 4/4 直角。
- `plan/ws035/tests/zdesktop-p134.sh`（title bar の判定を足した）→ `p134/p136: PASS`:

| 判定 | 結果 |
| --- | --- |
| Terminal の本体（916x632） | 4 of 4 直角 |
| Terminal の title bar | 0 of 4 直角（丸い） |
| dock（最大化）した Terminal の本体 | 4 of 4 直角 |
| dock を戻した Terminal の本体・title bar | 4 of 4 直角・0 of 4 直角（丸い） |
| popup-probe（app_id 無し、単独）の本体・title bar | 0 of 4・0 of 4 直角（両方丸い） |

- 画面（`build/ws035-shots/p136/`）: `terminal.png`（丸い title bar と直角の本体）、`both.png`、`wiseview.png`（Terminal の縮図は直角、影も直角に沿う。
  開ききった Wiseview の縮図に title bar は無い）、`maximized.png`、`restored.png`、`probe.png`。
- 回帰: `zdesktop-p052.sh` PASS、`zdesktop-p053.sh` PASS（`build/ws035-shots/p136-p052/`・`p136-p053/`）、boot test PASS
  （`build/ws035-shots/p136-20260929-boot-test.png`）。
- build: desktop の source の warning 0、`Permission denied` なし。log の warning 476 は main の取り込みで再 build された外部の package
  （openssh・openssl・perl）と NoctLang のもの。

未実施: 実機。Wiseview の開く途中の title bar の丸さは画面では撮っていない（`draw_title_bar` を通ることを code で確かめた）。

## Resume point

2026-09-30: cleared。
