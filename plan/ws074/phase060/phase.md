<!-- awesome-plan project=zedbsd record=ws074p060 -->

# ws074-p060: inline-block と vertical-align

Phase ID: `ws074-p060`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p032、p035

## 範囲（amazon-goal.md §4 の 6）

`inline-block` を atomic な inline に（shrink-to-fit、最後の行の baseline、`vertical-align` の top・middle・bottom・baseline）。
検索の結果の格子（Amazon の card）の最大の差。

## 設計と実装

- CSS（`css/css.h`・`internal.h`・`values.c`・`cascade.c`・`ua.c`）: `vertical-align`（`CSS_PROP_VERTICAL_ALIGN`、継承しない）。
  keyword（baseline・top・middle・bottom・text-top・text-bottom・sub・super）か長さ・%（`vertical_offset`）。UA の sheet に
  `sub { vertical-align: sub }`・`sup { vertical-align: super }`。
- box（`layout/box.c`）: `display: inline-block` の要素（置換でない）は block の box に `atomic` の印を付け、inline の内容として
  数える（`box_is_inline_level`）。absolute・fixed・float・root・flex item は atomic にしない。`::before`・`::after` の inline-block も。
  `<img>` の inline-block は inline の置換の box。行の中の atomic は float と同じく content box からの位置を持ち、
  `layout_absolute` で絶対に（`box_static_inline`）。
- 行（`layout/inline.c`）: atomic は `layout_shrink_to_fit` で自分の formatting context に layout し、margin box の幅の piece に。
  baseline は最後の行の baseline（子の block を下へ探す）、無いときと overflow が visible でないときは下の margin の端
  （`has_baseline`・`control_baseline` を control と共用）。行を作る時に各 fragment の到達（atomic は margin box、文字は
  line-height）を集め、`inline_align` で vertical-align を当てる: baseline に対する shift（middle は親の x-height の半分、
  text-top・text-bottom は親の font の ascent・descent、sub は font-size/5+1、super は font-size/3+1、長さ・% は上へ）、
  top・bottom は行の高さを決めた後に行の上端・下端へ（足りなければ行を伸ばす）。文字の vertical-align は周りの inline の
  box から取る（block の直下の文字は block の vertical-align を使わない）。行を積んだ後に atomic の box の位置を決める
  （`inline_place_atomics`）。`layout_fragment.shift` を新設。
- shrink-to-fit（`layout/position.c`）: 内容の max-content の幅を box ごとに一度だけ測って保つ（`layout_max_content`、flex の
  item の測定も共用）。幅は今回の layout だけに与え、style の `auto` に戻す（前は px を style に書き残し、測定の幅のまま次の
  layout に使っていた）。`box-sizing: border-box` の box（`<button>`）は枠の分を足す（前は padding の分だけ狭かった）。
  inline-block の auto の margin は 0（`block.c`）。
- 描画・hit・bounds・dump: `list_lines` が atomic の fragment で `list_box` を呼ぶ（行の文字の順）。文字と置換の box は
  shift を足した baseline に。hit は inline-block の中の文字と block を探す（`hit_inline_floats`・`hit_inline_blocks`）。
  bounds は shift を足す。dump は `inline-block X w W h H` の行と、shift のある fragment に ` shift S`、atomic の中身は
  `dump_out_of_flow` で行の後に。floats の走査（`inline_place_floats`・`list_inline_floats`・`position_inline_floats`）は
  atomic の中に入らない。
- 試験: `tests/pages/inline-block.html`（新、golden 4 つ）: text の間の inline-block、高い box、複数行の baseline、clip の下端、
  % の幅、Amazon 風の card の格子（`width: 23%`・`vertical-align: top`）、text-align center と auto margin、vertical-align の
  6 つの keyword、長さ・%・sub・sup・大きな文字の middle、入れ子、`::before` の inline-block、inline-block の link。
  `keys.html` の golden を更新（`<button>` が行に並ぶ。Chromium と 12/12 一致）。`browser-p060.sh`（新、guest）。

## 確認（host は Debian の cc。guest は QEMU（Venus）。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check（変更した source）: 新しい指摘 0（`cascade.c` の 1 件は既存）。
- 回帰（plain と ASan）: golden 52/52（inline-block の 4 つを追加、keys の 2 つを更新、他は不変）、host-view 59/59、
  host-form 28/28、host-link 22/22、host-position 19/19、host-text 20/20、host-base・heap・interp・number・object 全て 0 failed、
  run-js-tests 7/7、run-dom-tests 6/6、run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17。ASan の `--render` で Amazon の
  3 つの capture に報告なし。
- `chrome-boxes.py inline-block.html`（800x600）: 50/50 の box が 1px 以内。`render-compare.py`: 97.48%（違いは glyph の縁と
  角丸（p062））。`chrome-boxes.py keys.html`（1024x768）: 12/12、`render-compare.py keys.html`: 98.88%。
- Amazon（2026-09-28 23:52 の capture の `*-local-noscript.html`、1280x900）:

  | page | p035 の後 | p060 の後 |
  | --- | --- | --- |
  | トップ | 画素 34.45%、ink 23.79% | 画素 34.15%、ink 23.65% |
  | 検索 | 画素 77.89%、ink 27.53% | 画素 70.48%、ink 22.80% |

  検索は結果の格子が Chromium と同じ 4 列の card になった（前は 1 列で縦に長い）が、**画素の一致は下がった**: 本体の行
  （`.s-desktop-content.sg-row`、flex の row）が `direction: rtl`（子は `ltr`）で、Chromium では filter の列が左・結果が右に
  並ぶのに、私たちは `direction` を持たないので逆に並ぶ。前は結果が 1 列で白い部分が多く、偶然の一致が多かった。
  `direction` はこの Phase の範囲の外（下の「残り」）。トップは不変（card の 4 列は `display: grid`、p072）。
  `--render` は host で検索 1.53 s、トップ 0.48 s。
- guest（QEMU の Venus、zdesktop 1280x800）: `browser-p060.sh`: inline-block.html が host と同じに描かれ、inline-block の link の
  click で second.html へ（LINK・NAVIGATE、status 0）。`browser-p032.sh`（form）: status 0。`browser-page.sh keys.html`: status 0。
  live の Amazon はこの Phase では取得していない。

## 写真（`/home/awe/zedBSD-rpi4/build/ws074-shots/`）

- host: `p060-20260929-inline-block.png`（私たち | Chromium | 違い）、`p060-20260929-amazon-top-local-noscript.png`・
  `…-search-local-noscript.png`。
- guest（QEMU）: `p060-20260929-guest-inline-block.png`、`p060-20260929-guest-link-followed.png`。

## 未実施・残り

- 実機は未実施。
- **見つけたこと（main へ）**: Amazon の検索の本体の行は `direction: rtl` の flex で、filter の列を左に置いている。`direction`
  （rtl の flex の row・行の inline の順・`text-align: start`）が無いと検索の page の左右が逆のまま。amazon-goal.md の列に
  無い小さな Phase（`direction` の最小: flex の row の向きと text-align の start）として足すことを提案する。
- 範囲の外に残したもの: `inline-flex`・`inline-grid`・`inline-table` の inline-level（今は block）、inline の box の背景・枠・
  padding（p069 の残り）、文字の vertical-align の入れ子の正確な扱い（最も近い inline の box だけ）、`vertical-align` の
  table cell の意味（p037）、baseline の flex・grid の規則、`line-height` の inline の box の strut。
