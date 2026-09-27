<!-- awesome-plan project=zedbsd record=ws074p048 -->

# ws074-p048: layout 2b: float と clear

Phase ID: `ws074-p048`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p013（cleared）。p013 から分けた（2026-09-28）。

## 範囲（正常系のワンパス）

- CSS: `clear`（none・left・right・both、inline-start・inline-end）、float の値の名前（`enum css_float`）。
- layout（`layout/float.c`、新）: block formatting context（root、float、flow の外の箱、inline-block、table-cell、flex）ごとの float の
  margin box の一覧。layout 中の block の content box が文脈の中のどこにあるかを tree が持つ（`origin_x`・`origin_y`）。float は
  shrink-to-fit で layout し、次の block が始まる所（inline の中の float は内容の上端）より下で、その側に入るまで下げて置く。
  行の箱は float の横で短くなり（行の頭の語が入らなければ次の float の下端まで下げる）、text-align はその幅の中で。`clear` の block は
  その側の float の下から始まる。自分の文脈を持つ block は float を含む高さに伸びる。
- 描画: block の子は float でないもの、次に float（inline の中の float は行の前）。hit test と `--dump=layout` も inline の中の float を
  辿る。shrink-to-fit を `layout_shrink_to_fit`（position.c）にまとめた（absolute と float が使う）。

## 受け入れ

1. amd64 の build（warning 0）、style-check 0（layout・paint）。
2. host: `pages/floats.html` の箱が Chromium 153 と 1px 以内で一致、前の page も下がらない、golden、DOM の試験、host-position
   （plain・ASan）。
3. guest: floats.html を窓で表示。前の窓の試験が下がらない。boot test。

## 結果（2026-09-28）

cleared。

- 実装: `layout/float.c`（新）、`layout/block.c`（文脈、float、clear、float の高さ）、`layout/inline.c`（inline の float、行の幅）、
  `layout/box.c`（float の箱）、`layout/position.c`（`layout_shrink_to_fit`、inline の float の幅）、`layout/hit.c`・`dump.c`、
  `paint/list.c`、`css/`（clear）。
- 試験:
  - `chrome-boxes.py`: floats.html 20/20（100%）。回帰: blocks 25/25、first 12/12、second 5/5、position 14/14、script 15/15、
    同梱の start 29/29・about 21/21・text 22/22。
  - golden 24/24（floats.html の 4 つを追加、前の 20 は変わらず）、DOM の試験 5/5、host-position 19/19（plain・ASan）。
  - guest（`plan/ws074/tests/browser-page.sh`、新: 1 つの page を窓で表示して撮る）: status 0。回帰の browser-p013・p045・p030 も
    status 0。画面: `/home/awe/zedBSD-rpi4/build/ws074-shots/p048-20260928-floats.png`、Chromium との並び
    `p048-20260928-floats-vs-chrome.png`（左が browser、右が Chromium）。
- boot test: PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p048-20260928-boot-login.png`）。
- 実機: 未実施。
- commit: `7e67b114`（code と試験）、この記録の commit。

## 後回し（follow-up）

- inline の中の float を、それがある行の位置に置く（今は内容の上端）。float の後の行の高さを実際の行の高さで測る（今は block の
  font の strut の高さの帯で float を見る）。
- block の位置は最初の子との margin の相殺の前の見積もりで float を問う（相殺があると float が margin 分ずれることがある）。
- 自分の文脈を持つ block（overflow・flow-root）が float を避けて狭くなること（p049 の overflow と一緒に）。
- clear の clearance と margin の相殺の細部、`<br clear>`。
