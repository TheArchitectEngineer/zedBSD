<!-- awesome-plan project=zedbsd record=ws074p013 -->

# ws074-p013: layout 2a: position（relative・absolute・fixed）、offset、z-index の描画の順

Phase ID: `ws074-p013`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p012（cleared）

## 分割（2026-09-28、着手の時）

表の内容（float・clear、position、overflow と clip、list と marker、replaced の大きさ、単位）は 1 Phase には大きいので分けた:

- **p013（この Phase）**: position（relative・absolute・fixed）、top・right・bottom・left、z-index と描画の順、重なりの hit test。
- **ws074-p048**（新）: float と clear（行の箱を float の横で短くする、block formatting context）。
- **ws074-p049**（新）: overflow と clip（display list の clip、CPU と GPU の描画）、overflow が作る block formatting context。
- list と marker は p011 で入っている（`box_marker`）。replaced の大きさは画像の Phase（p021）で行う。単位（em・rem・ex・vw・vh・pt 等）は
  p007 の cascade で px に変わっている。

## 範囲（正常系のワンパス）

- CSS: `top`・`right`・`bottom`・`left`（長さ・%・auto）、`z-index`（auto・整数）、position の値の名前（`enum css_position`）。
- layout: absolute・fixed の箱は block にして flow の外へ（親の layout は通り過ぎ、static position を記録）。flow の後に木の順に
  containing block（最も近い positioned の祖先の padding box、無ければ viewport の矩形）へ置く（`layout/position.c`、新）。width が
  auto で left か right が auto なら shrink-to-fit（とても広く一度 layout して内容の幅を測り、狭い方で layout し直す）。left と right の
  両方なら間を埋める（width があれば auto の margin で中央）。top と bottom の両方で height が auto なら間を埋める。relative は flow の
  位置から offset だけずらす（子孫も）。文書の高さは absolute の箱を含むまで伸びる。
- 描画: positioned の箱は別の層として、z-index の負 → flow → auto と 0（木の順）→ 正の順に、各々の positioned でない子孫と一緒に描く
  （stacking context は平らにした近似）。
- hit test: 描画の上の層から探す（z-index 2 が 1 に重なる所は 2）。`--dump=layout` は inline の中の flow の外の箱も出す。

## 受け入れ

1. amd64 の build（warning 0）、style-check 0（layout・paint の変更した file）。
2. host: `pages/position.html` の箱が Chromium 153 と 1px 以内で一致（`chrome-boxes.py`）、`host-position`（配置・順・hit test）、
   golden、前の試験が下がらない（plain・ASan）。
3. guest（Venus、`--glass`）: position.html の表示と、重なりの click が上の箱に届く。画面を撮る。
4. boot test。

## 結果（2026-09-28）

cleared。

- 実装: `layout/position.c`（新: `layout_position`・`layout_stacking_order`）、`layout/box.c`（flow の外の箱、`layout_absolute` の
  relative の offset と static position、`layout_is_positioned`）、`layout/block.c`・`inline.c`（flow の外を通り過ぎる）、
  `layout/hit.c`（層の順の hit test）、`layout/dump.c`、`paint/list.c`（層の順の描画）、`css/`（offset・z-index）。
- 試験:
  - `chrome-boxes.py`: position.html 14/14（100%）、blocks.html 25/25、first.html 12/12（Chromium 153、`chrome-fonts.sh` の font）。
  - `plan/ws074/tests/host-position.c`（新）: 19 checks、0 failed（plain・ASan）。描画の順
    `below;|flow|stage;corner…;label;stretch;shifted;middle;over;`。
  - golden 20/20（position.html の 4 つを追加、前の 16 は変わらず）、DOM の試験 5/5、host-link 22/22（plain・ASan）。
  - guest（`plan/ws074/tests/browser-p013.sh`）: status 0。click が over・middle・corner top-left に届く。回帰の browser-p045・p030・
    p014 も status 0。画面: `/home/awe/zedBSD-rpi4/build/ws074-shots/p013-20260928-position.png`、Chromium との並び
    `p013-20260928-position-vs-chrome.png`（左が browser の CPU の描画、右が Chromium）。
- boot test: PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p013-20260928-boot-login.png`）。
- 実機: 未実施。
- commit: `9a34a12d`（code と試験）、この記録の commit。

## 後回し（follow-up）

- stacking context の入れ子（z-index の親子、opacity・transform が作る文脈）。今は全ての positioned を一つの順に並べる。
- fixed は viewport に置くが、scroll に付いて動く（画面に固定しない）。sticky は static として扱う。
- inline の positioned（`span` の relative の offset、inline を containing block にする absolute）。inline の中の absolute の static
  position は block の内容の左上（行の位置ではない）。
- height の % の offset と relative の top の %（containing block の高さが決まる場合）、shrink-to-fit の min-content。
- absolute の箱の hit test は層の順だが、層の中の子孫の重なりは近似。
