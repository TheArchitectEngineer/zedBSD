<!-- awesome-plan project=zedbsd record=ws074p072 -->

# ws074-p072: grid の最小（`repeat(N,1fr)`、`grid-column`）

Phase ID: `ws074-p072`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p035

## 範囲（amazon-goal.md §4 の 11。2026-09-29 main の判断で p073 の後、p037 の前）

`display: grid`（`inline-grid` は block として）、`grid-template-columns`・`-rows`（長さ・%・fr・auto・min-content・max-content（auto
として）・`minmax()`・`fit-content()`（auto として）・`repeat(N, …)`、名前の括弧は捨てる）、`grid-column`・`grid-row`・`grid-area`
（行番号、負の番号、`span N`）と longhand、行ごとの自動の配置、`gap`、`align-items`・`align-self`、`place-items`（align-items だけ）、
`justify-content` の center・end（track が box より狭い時）。

## 実装

- CSS（`css/css.h`・`internal.h`・`values.c`・`cascade.c`）: `CSS_DISPLAY_GRID`、`css_track`（種類・大きさ・fr・minmax の最小）の
  template を style に 24 まで、`css_grid_place`（start・start_span・end・end_span）。宣言は `CSS_VALUE_TRACKS`（arena の
  `css_track_list`、`repeat` は展開）と `CSS_VALUE_GRID_LINE`。`values_split_at`（関数の外の token で分ける）。
- layout（`layout/grid.c` 新、`box.c`・`block.c`・`position.c`・`layout.h`）: grid の子は flex と同じく item に（block 化）。
  配置: 行と列の決まった item、行だけの item、残りを cursor から行ごとに空いた cell へ。列の幅: 固定の track、auto の track は
  1 列の item の max-content（複数列の item は auto の track を等しく広げる）、fr の track が残りを比で分ける（最小は minmax の値）、
  fr が無ければ auto の track が残りを分ける。測定中（shrink-to-fit の中）は fr を auto として測り、% は不定。item は area の幅で
  block として layout（幅が auto なら area を埋める、style は後で戻す）。行の高さ: 固定の track、他は 1 行の item の margin box、
  複数行の item は最後の行を伸ばす。align で area の中に置き、stretch は高さを行に合わせる。container の内容の高さは行と gap の和、
  `layout_content_width` は track と gap の和（`grid_content`）。rtl の block の margin の規則から grid item を除く。
- 試験: `tests/pages/grid.html`（新、golden 4 つ）: `repeat(4, minmax(0,1fr))` と `1 / -1`、`120px 1fr 2fr`、`auto 1fr`、12 列の
  span 4・span 6、行番号・`grid-area`・固定の行、align-items center と align-self end・start、justify-content center、template 無し、%。

## 確認（host は Debian の cc。guest は QEMU（Venus）。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check（`grid.c`・`values.c` ほか）: 新しい指摘 0（既存の 1 件）。
- 回帰（plain と ASan）: golden 64/64（grid の 4 つを追加、他は不変）、host-view 59/59、host-form 28/28、host-link 22/22、
  host-position 19/19、host-text 20/20、host-base・heap・interp・number・object 全て 0 failed、run-js-tests 7/7、run-dom-tests 6/6、
  run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17、run-font-tests 8/8、host-relayout 140/140。ASan の `--render` で
  Amazon の 3 つの capture に報告なし。
- `chrome-boxes.py grid.html`（800x600）: 44/44 の box が 1px 以内。`render-compare.py`: 98.41%。
- Amazon（2026-09-28 23:52 の capture、1280x900）: トップ 画素 34.16% → 34.72%、ink 24.28% → 24.43%。検索 77.16%（不変、結果の
  list の grid は `@supports (grid-auto-flow: dense)` の中で、`grid-auto-flow` を読まないので適用されない）。トップの最初の card の
  列は grid ではなく `width: max-content` の横の carousel と container query の単位で、p074・p075（2026-09-29 main の判断で追加）。
  host の時間は、この計測の時の host の負荷（load average 約 65）で比べられない（perf の分布は前と同じで grid.c は出ない）。
- guest（QEMU の Venus、zdesktop 1280x800）: `browser-page.sh grid.html` status 0（host と同じ描画）。live の Amazon は取得していない。

## 写真（`/home/awe/zedBSD-rpi4/build/ws074-shots/`）

- host: `p072-20260929-grid.png`（私たち | Chromium | 違い）。guest（QEMU）: `p072-20260929-guest-grid.png`。

## 未実施・残り

- 実機は未実施。
- 名前の付いた line と area（`grid-template-areas`）、`grid-auto-flow`（dense・column）、`grid-auto-columns`・`-rows`、`order`、
  `justify-items`・`justify-self`、baseline、`auto-fill`・`auto-fit`、fr の track の min-content の最小、`inline-grid` の inline-level、
  subgrid。
