<!-- awesome-plan project=zedbsd record=ws035p134 -->

# ws035-p134: Terminal の窓の角を丸めない

Phase ID: `ws035-p134`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-29、サブエージェント、worktree `wt/ws035`。QEMU の Venus、実機は未実施）
Phase disposition: normal
Queue: なし（2026-09-29 main の割り当て。ユーザーの指示（2026-09-29 夜）「ターミナルアプリですが、四隅のラウンドがあって文字が欠けてしまい、
ちょっと読みづらいかなと思います。ターミナルだけはラウンドをなしにしてもいいと思います。」）

## 範囲と受け入れ

- 対象は Terminal の窓（app_id `terminal`、`userland/desktop/terminal/window.c`）だけ。本体の角の丸め（`GLASS_RADIUS`: 本体の像の clip、
  影、半透明のときのすりガラス）、浮いた title bar（影とガラス）、背後の窓のぼかしの下地（`draw_window_blurred`）、Wiseview の縮図
  （`WISEVIEW_RADIUS`: 影、像、青い縁）を直角にする。他の app の窓は今のまま丸める。
- 後で他の app（X terminal など）にも当てられる形にする。
- 確認: QEMU の Venus で Terminal の四隅の文字が欠けないこと、他の app の窓が丸いこと、最大化（dock）・Wiseview・影が崩れないこと（画面）。
  回帰 p052・p053・boot test。

## 設計と実装

`userland/desktop/wayland/shell.c`:

- 「角を丸めない app_id」の表 `shell_square_apps[]`（今は `"terminal"` だけ。X terminal などは 1 行足せばよい）と、`window_square()`
  （窓の `app_id` が表にあるか）。
- `draw_body`・`draw_title_bar`・`draw_window_blurred`・`draw_tile`（Wiseview）の半径を、表にある窓では 0 にする（青い縁は 0 + 3）。
  dock した窓の下の角を出力の外へ押し出す `box[3] += 2 * radius` も同じ半径を使う（直角なら押し出さない）。
- popup（menu）、dock の位置の示し（`draw_dock_hint`）、system bar などの窓でない形は変えない。

規約: `plan/tools/style-check.py userland/desktop/wayland/shell.c` 0、`git diff --check` 0。

## 検証

**QEMU（amd64、Venus の guest 1280x800、graphical の login の image。前 `build/p133-after.img`、後 `build/p134-after.img`）**:
`plan/ws035/tests/zdesktop-p134.sh`（新）と `plan/ws035/tests/p134-corners.py`（新: 窓の四隅の画素を同じ辺の中央の画素と比べ、
差が 24 以下なら直角、超えれば丸い（後ろの壁紙か影が見える））。Terminal は画面の端まで `#` の行で埋める。

| 確認 | 前（p133） | 後（p134） |
| --- | --- | --- |
| Terminal の四隅（916x632） | 0 of 4 直角（丸い） | **4 of 4 直角** |
| dock（最大化）した Terminal の四隅 | 0 of 4 直角 | **4 of 4 直角** |
| dock を戻した Terminal の四隅 | — | **4 of 4 直角** |
| popup-probe（app_id 無し、400x300、単独）の四隅 | — | **0 of 4 直角（丸いまま）** |

`zdesktop-p134.sh` → `p134: PASS`（後の image）。

画面（`build/ws035-shots/p134-p134-after/`）:
- `terminal.png`: Terminal の本体と title bar が直角、四隅の `#` が欠けない。前の image の `build/ws035-shots/p134-p133-after/terminal.png` は四隅が丸い（この 1280x800・既定の字の大きさでは、内側の余白 約 8 px のため `#` は目に見えて欠けてはいない。ユーザーが見た欠けの条件（字の大きさ・窓の大きさ）は再現していない。直角にしたので、余白によらず角で欠けることは無い）。
- `both.png`: 直角の Terminal の上に丸い popup-probe。
- `wiseview.png`: Wiseview で Terminal の縮図は直角、影も直角に沿う。probe の縮図は丸く、青い縁も丸い。
- `maximized.png`: dock した Terminal、上の角も直角で bar との間に隙間や欠けは無い。`restored.png`: 戻した窓も直角。
- `probe.png`: probe だけのとき四隅が丸い。

途中の失敗（script の修正で解消）: 1 回目は probe が Terminal の上に重なって開き、丸い角の外にも Terminal の暗い色が見えて「直角」と判定した。
また最大化の画面で pointer の矢印が比べる点（下の辺の中央）に重なっていた。probe は Terminal を閉じた後に単独で判定し、pointer を外した。

回帰（`build/p134-regress.sh`、`build/p134-after.img`）: `zdesktop-p052.sh` PASS、`zdesktop-p053.sh` PASS（`build/ws035-shots/p134-p052/`・`p134-p053/`）、
boot test（GPU の無い q35、console の login）PASS、`build/ws035-shots/p134-20260929-boot-test.png`。

build（worktree の `build/amd64`、graphical の login の image）: warning 0（log の全体）、`Permission denied` なし。

未実施: 実機。X terminal（xterm などの X11 の窓）は表に入れていない（app_id が決まったら 1 行足す）。

## Resume point

2026-09-29: cleared。
