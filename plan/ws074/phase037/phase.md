<!-- awesome-plan project=zedbsd record=ws074p037 -->

# ws074-p037: table の layout（最小。CSS2 の auto layout、border-collapse は近似）

Phase ID: `ws074-p037`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29、amazon-goal.md §4 の 10 の「最小」の範囲。全体の残りは下の「未実施・残り」）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws.md の表では p036。デモの列（amazon-goal.md）では p072 の後（2026-09-29 main の判断で p073 → p072 → p074 → p075 → p037）。
p036（WPT の計測）は未実施のまま、デモの列の判断で先に最小を行った。

## 範囲

CSS2 の automatic table layout の最小: `display` の table 系（`table`・`inline-table`・`table-row-group`・`table-header-group`・
`table-footer-group`・`table-row`・`table-cell`・`table-caption`、`table-column`・`table-column-group` は box を作らない）、
欠けた部品の匿名の box（CSS2 §17.2.1 の最小）、`border-spacing`（shorthand、1 つか 2 つの長さ）、`border-collapse`（近似）、
HTML の `colspan`、cell の `vertical-align`（top・middle・bottom、他は top）、cell の幅（px と %）、caption（上だけ）。

## 設計と実装

- CSS（`css/css.h`・`internal.h`・`values.c`・`cascade.c`・`ua.c`）: display の値 `TABLE_ROW_GROUP`・`TABLE_CAPTION`・`INLINE_TABLE`・
  `TABLE_COLUMN`（以前は row group・caption を block、inline-table を table として扱っていた）。`border-spacing`（継承、`values_pair`）
  と `border-collapse`（継承）。UA: `col`・`colgroup`、`table { border-spacing: 2px; border-collapse: separate; box-sizing: border-box }`、
  `thead, tbody, tfoot { vertical-align: middle }`、`tr, td, th { vertical-align: inherit }`、`th { text-align: center }`。
  page.c の display の名前の表に 4 つを足した。
- box の木（`layout/box.c`）: `box_table_fix` を `box_fix_children` から呼ぶ。table・row group・row の子を仕分け
  （`box_table_children`）: 属する部品は残し、row の外の cell は匿名の row へ、他の内容は匿名の cell へ（部品の間の空白は捨てる）。
  他の box の中の row・row group・cell の連なりは匿名の table で包む（`box_wrap_tables`）。flex・grid の container の中の部品は item
  のまま。部品の判定は block の box だけ（text の box は親の style を持つので、cell の text を部品と誤らない）。`inline-table` は atomic。
- layout（`layout/table.c`、新）: `layout_table` を block の内容の分岐から呼ぶ（`block.c`）。
  1. row（row group の中も）と cell を集め、列の数を数える。
  2. 各列の最小（幅 0 で layout した内容の幅）と最大（`layout_max_content`）。px の幅の cell は両方をその幅にして列を固定、% の幅の
     cell は列の割合。span の cell は足りない分を列に等分。
  3. table の幅: auto なら列の最大と spacing（% の列が割合を保てるまで広げる: 最大/割合、残りの列の最大/(1−割合の和)）を containing
     block の幅まで、最小の和より狭くしない。幅が変われば auto の margin を同じ containing block で取り直す（`margin: 0 auto` の中央）。
  4. 列の幅: % の列が先に割合（最小以上）、残りを他の列で: 全て最大が入れば残りを固定でない列へ最大に比例、入らなければ最小と最大の
     間で差に比例、それも無理なら最小。
  5. caption を上に、row を上から（spacing を挟む）。cell は列の幅で block として layout（style の幅・高さ・margin を一時的に
     上書きして戻す、grid の item と同じ）。row の高さは cell の内容（と cell の px の高さ）の最大。cell は row の高さに伸ばし、
     `vertical-align` の middle・bottom で内容（行か子の box）を下げる。
  6. row group は自分の row を囲む box にし、row をその中の相対位置に直す。
  - `border-collapse: collapse` は近似: 端の spacing を 0、隣の cell を最初の cell の右と下の border だけ重ねる（同じ border なら
    線は 1 本、box は Chromium と 1 px 以内）。border の解決（太さ・style の優先）はしない。
  - table の max-content は `grid_content` の field（grid と共用）に置き、`layout_content_width` が返す（`position.c`）。
  - `block_owns_context` に table・inline-table・caption。
- inline（`layout/inline.c`）: 行の折り返しを、block の `white-space` でなく、折り返す所の前の piece（空白か text）の box の
  `white-space` で決める（`inline_wraps`）。Amazon の footer の `li { white-space: nowrap }` の中の `#navFooter a { white-space: normal }`
  が Chromium と同じく折り返す。この修正が無いと footer の table の列の最小が広すぎ、table が 1000 px の枠からはみ出した。
- `userland/desktop/libbrowser/Makefile` に `layout/table.c`。

## 試験

- `plan/ws074/tests/pages/tables.html`（新、golden 4 つ）: caption と thead・tbody、100% の table と colspan、px の幅の cell と
  `border-spacing: 8px 4px` と vertical-align の top・middle・bottom・継承、`border-collapse`、`margin: auto` の中央の table、
  div の `display: table`、span の `inline-table`、% の幅の空の cell（Amazon の footer の形）、25% の列、長い文の 2 列。
- `golden/selectors.layout` の 1 行の変更: clearfix の `::after { content: ""; display: table }` の幅が 1008 → 0（空の table、
  Chromium と同じ）。他の golden は不変。

## 確認（host は Debian の cc。guest は QEMU（Venus）。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。`plan/tools/style-check.py`（変えた 12 の file）: 新しい指摘 0（既存の cascade.c の
  1 件）。
- 回帰（plain と ASan）: golden 76/76（tables の 4 つを追加、selectors.layout の上の 1 行を更新）、host-view 59/59、host-form 28/28、
  host-link 22/22、host-position 19/19、host-text 20/20、host-base 2038・heap 31・interp 45・number 71・object 98（全て 0 failed）、
  run-js-tests 7/7、run-dom-tests 6/6、run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17、run-font-tests 8/8、
  host-relayout 161/161（test page と Amazon の 2 つ）。ASan の `--render` で Amazon の 3 つの capture に報告 0。
- `chrome-boxes.py tables.html`（800x700）: 60/73 の box が 1 px 以内。違う 13 は、最初の table の太字の `th` の列（合成の太字の
  文字の幅が Chromium より 1 文字 0.75 px 広い、table と無関係）と inline-table の x の 1.1 px（同じ所の inline-block でも同じ、
  前の文字の幅）。`render-compare.py`: 96.83%。
- Amazon（2026-09-28 23:52 の capture、1280x900）: トップ 画素 80.40% → **79.80%**、ink 76.55% → **75.82%**。検索 77.16%・
  ink 34.68%（不変）。トップが下がったのは footer: 以前は `navFooterVerticalColumn`（`display: table`、中は table-cell の div）が
  block で 1 列だけ出ていた（Chromium の 1 列目と偶然重なる）。今は Chromium と同じ 4 列と 10% の spacer の配置（列の x・幅・ul の
  高さが数 px 以内）になったが、footer 全体が Chromium より 25 px 上にある（footer の上の「rhf」の区画の高さの違い、p037 の前から）
  ので、増えた文字が重ならない。`--render` は host で検索 0.87 s、トップ 0.60 s。
- nowrap の中の normal の inline の試験（`build/ws074-p037/nowrap.html`、git に入れない一時の page）: Chromium と 4/4 の box が一致。
- guest（QEMU の Venus、worktree の image）: `browser-page.sh tables.html`（800x600）status 0、画面は host と同じ配置。800x700 の
  1 回目は READY の行の照合だけ MISSING（画面は正しい。窓の高さの照合の問題と見て、既定の大きさで通した）。live の
  `https://www.amazon.co.jp/`（取得 1 回）: 30 s でトップの card と header が出る（写真）。footer までの scroll は guest では未確認。
- `plan/tools/boot-test.sh`: 未実施（guest の image は起動し、窓の試験は通った）。

## 写真（`/home/awe/zedBSD-rpi4/build/ws074-shots/`）

- host: `p037-20260929-tables-side.png`（左が私たち、中が Chromium、右が差）、`p037-20260929-amazon-top-local-noscript-side.png`・
  `p037-20260929-amazon-search-local-noscript-side.png`、`p037-20260929-amazon-top-footer-before-after-chromium.png`（上から p075 の後、
  p037 の後、Chromium の footer）。
- guest（QEMU）: `p037-20260929-guest-tables.png`、`p037-20260929-guest-amazon-top-30s.png`。

## 未実施・残り

- 実機は未実施。boot-test は未実施。
- ws.md の p037 の全体のうち残り: rowspan、fixed の table layout（`table-layout: fixed`）、collapsed border の解決（太さと style の
  優先、table の外の border の半分）、column と column group の box（幅と背景）、caption の `caption-side: bottom`、cell の baseline の
  揃え（`vertical-align: baseline` は top として扱う）、row と row group の自分の border・padding・背景の layout（背景は描く）、
  table の `height` を row に配る、cell の % の高さ。
- Amazon のトップの footer の上の 25 px の違い（rhf の区画、table と無関係）。太字の合成の文字の幅（th）。
