<!-- awesome-plan project=zedbsd record=ws074p011 -->

# ws074-p011: layout の最小（ワンパス）

Phase ID: `ws074-p011`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 範囲

正常系のワンパス: box tree（display: none・contents、anonymous block、inline の中の block は block 扱い、list の marker）、block
（幅、auto の margin の中央寄せ、min/max、兄弟・空の block・親と最初と最後の子の margin の相殺）、inline（空白の畳み込み、
white-space、改行の機会での貪欲な行の分割、`<br>`、baseline と half-leading、text-align）、`--dump=layout`、Chrome の box との比較。

## 受け入れ

1. amd64 の build が通る（browser と libtruetype に warning 0）。style-check（layout/・text/・libtruetype/design.c）0 件。
2. host の plain・ASan で golden（dom・style・layout）が一致。tree construction・tokenizer の率が下がらない。
3. guest の `--dump=layout` が golden と一致。
4. Chrome との box の比較（自前の page）で ±1 px の一致の率を記録。
5. boot test。

## 結果（2026-09-27）

cleared。

- 書いたもの: `layout/{layout.h,box.c,block.c,inline.c,dump.c}`、`page` の `page_open_fonts`・`page_layout`、`main.c` の
  `--dump=layout` と `--font=`・`--mono-font=`・`--fallback-font=`。libtruetype に関数を 2 つ足した（既存は変えない、main の了解済み）:
  `truetype_design_metrics`・`truetype_glyph_design_advance`（`userland/base/libtruetype/design.c`）。
- 21 時の中断の後に直したこと:
  - style-check の違反（閉じ括弧の後の空行、段落の comment、条件の中の `memcmp`、条件演算子）と、同じ所の規約の逸脱（式で作る
    Boolean）。
  - **monospace の既定の大きさ 13px**（Chromium と同じ）: `css_style.font_size_keyword`（大きさが keyword の尺度から来たか。
    初期値 medium、keyword、その em・% は 1、絶対の長さは 0）を持ち、font-family が `monospace` だけになった（またはそれをやめた）
    element で 13/16（16/13）を掛ける。font-size と font-family を他の宣言より先に適用する。UA の sheet の `html { font-size: 16px }`
    を `medium` に直した（16px は絶対の長さで、keyword の尺度を切っていた）。
  - **fallback の font と line-height: normal**: piece が fallback の face の glyph を含むなら、その face の ascent・descent・line gap
    （half-leading 込み）も行の高さに入れる（Chromium の used fonts と同じ）。
  - **空の block（margin が通り抜ける block）の位置**: 前の margin と自分の上の margin の相殺の後に置く（前は cursor に置いていた）。
  - p007 からの UB: 宣言の無い element で `qsort(NULL, 0, …)`（UBSan が止めた）。1 件以下なら並べない。
- Chrome との比較（`plan/ws074/tests/chrome-boxes.py`、Chromium 153、Inter・JetBrains Mono・Droid の fontconfig）:
  | page | 1024 | 800 | 520 |
  | --- | --- | --- | --- |
  | `pages/first.html`（12 box） | 12/12 | 12/12（前は 5/12） | 12/12 |
  | `pages/blocks.html`（25 box、新規） | 25/25 | 25/25 | 25/25 |
  `blocks.html` は margin の相殺（兄弟・空の block・親子）、auto の margin の中央寄せ、min/max-width・min-height、%、text-align、
  `<br>`、line-height（数と px）、`pre`・`nowrap`・`pre-line`、入れ子の list、display: contents・none、anonymous block を含む。
  比べる script は `display: contents` の element を数えない（box を持たない）ように直した。headless の Chromium の窓は幅 500 未満に
  ならないので、幅は 520 以上で比べる。
- golden: `plan/ws074/tests/golden/{first,blocks}.{dom,style,layout}`。`golden-dumps.sh` は `build/ws035-fonts` の font を渡す
  （guest の既定の font と同じ file）。`first.style` は `<code>` の 16px → 13px だけが変わった（Chrome と同じ）。
- 試験:
  - host plain・ASan（UBSan 込み）: golden 6/6。host-base 2038/2038、host-heap 31/31、host-text 20/20（ASan）。
  - html5lib: tokenizer 7032/7032（100%）、tree construction 1648/1753（94.0%、p005 と同じ）。
  - amd64 の build: browser と libtruetype に warning 0（image の log の他の 476 件は別の package）。
  - guest（plain、`build/amd64/hdd-image.img`）: `--dump=dom|style|layout` の 6 file が golden と一致（QEMU の証拠）。
  - boot test: PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p011-20260927-boot-login.png`）。
  - 実機: 未実施。
- commit: `84214b7c`（code・試験）と、この記録の commit。

## 後回し（follow-up）

- float・position・table・flex・grid（block として扱う）、inline-block（block として扱う）、inline の box の border・padding・
  背景、vertical-align、text-align: justify、block の中の inline の分割（block-in-inline）、list の marker の block の子への付け替え。
- keyword の font-size の monospace の表: medium 以外は 13/16 を掛けるだけ（Chromium は keyword ごとの表を持つ）。
- 空の block の位置: 親の最初の子で親と margin が相殺する場合と、その後に続く clearance の細部。
