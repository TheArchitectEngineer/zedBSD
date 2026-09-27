<!-- awesome-plan project=zedbsd record=ws074p049 -->

# ws074-p049: layout 2c: overflow と clip

Phase ID: `ws074-p049`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p013（cleared）。p013 から分けた（2026-09-28）。

## 範囲（正常系のワンパス）

- CSS: `overflow`（一つの値で両方の軸）、`overflow-x`・`overflow-y`（visible・hidden・clip・scroll・auto・overlay）。
- layout: overflow が visible でない box は clip する（`layout_clips`）。root の overflow と、root が visible のときの body の overflow は
  viewport のもので clip しない。clip する box は自分の block formatting context を持ち、flow の中では横の float を避けて、その上端の
  帯で float が残す幅に置く（inline-block・table-cell・flex の box も同じ）。
- display list: `PAINT_CLIP`（padding box）と `PAINT_UNCLIP`。clip する box の内容（行、子、float）を挟む。positioned の層は、周りの
  clip する box のうち逃れないもので clip する（absolute は containing block から外側のもの、fixed は無し）。`--dump=paint` に
  `clip`・`unclip` の行。
- 描画: clip の stack（`paint_clips_*`、交わり）。CPU の描画は矩形を clip に切り、glyph は整数の pixel に丸めた clip の外の pixel を
  描かない。GPU の描画は staging で同じく矩形の instance を切り、glyph の instance は矩形を切って atlas の位置を同じだけずらす（shader は
  変えない）。

## 受け入れ

1. amd64 の build（warning 0）、style-check 0（layout・paint）。
2. host: `pages/overflow.html` の箱が Chromium 153 と 1px 以内、描画を Chromium と並べて確かめる、GPU（lavapipe）と CPU の描画が一致、
   前の試験が下がらない（plain・ASan）。
3. guest: overflow.html を窓で表示、guest の GPU（Venus）と CPU の描画が一致、前の窓の試験。boot test。

## 結果（2026-09-28）

cleared。

- 実装: `css/`（overflow）、`layout/box.c`（`layout_clips`）、`layout/block.c`（clip する box の文脈、float を避ける幅）、
  `paint/paint.h`・`list.c`（clip の item、層の clip、`paint_clips_*`）、`paint/software.c`・`vulkan.c`（clip）。
- 試験:
  - `chrome-boxes.py`: overflow.html 15/15（100%）。回帰: blocks 25/25、first 12/12、second 5/5、position 14/14、script 15/15、
    floats 20/20、同梱の start 29/29・about 21/21・text 22/22。Chromium との並び（clip、float の横の文脈、absolute が clip を逃れる）が
    一致: `/home/awe/zedBSD-rpi4/build/ws074-shots/p049-20260928-overflow-vs-chrome.png`（左が zdesktop-browser、右が Chromium）。
  - `gpu-compare.py`（host の lavapipe）: overflow・floats・position・blocks がどれも最大の差 1 で一致。
  - golden 28/28（overflow.html の 4 つを追加、paint に clip 14 行）、DOM の試験 5/5（plain・ASan）。
  - guest: `browser-page.sh overflow.html` status 0（`p049-20260928-overflow.png`）。guest の `--render-gpu`（Venus）と `--render` が
    overflow・floats・position で一致（最大の差 1）。回帰の browser-p013・p030・p045・p014 も status 0。
- boot test: PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p049-20260928-boot-login.png`）。
- 実機: 未実施。
- commit: `fd564157`（code と試験）、この記録の commit。

## 後回し（follow-up）

- scroll と auto の scroll（scroll bar、wheel で box の中を scroll、scroll の位置）。今は clip だけ。
- `overflow` の二つの値の書き方、`overflow: clip` と `overflow-clip-margin`、角丸の clip（p038）。
- hit test が clip を見ること（clip の外に出た内容に click が届く）。
- 文書の高さに clip された内容が入る（scrollable overflow の計算）。
- 文脈を持つ box が float を避けるとき、box の高さ全体の帯で float を見る（今は上端の帯だけ）。
