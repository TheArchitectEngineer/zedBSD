<!-- awesome-plan project=zedbsd record=ws074-p227 -->

# ws074-p227: Flex absolute-child static position and alignment keywords

Status: in-progress（2026-10-18 Q1: Status の行が無かった。q1201-i01・q1202-i01 は partial clear、whole の criteria は未達）

B1 (2026-10-14). 範囲: flex container の absolute 子の static position を「唯一の flex item」として求める（justify-content は主軸、align-self / align-items は交差軸、row・column・reverse・wrap-reverse・direction・縦書き）、`first baseline`・`last baseline`・`safe`/`unsafe` の構文（last baseline は end に退避、safe/unsafe は無視）、flex-start/flex-end を start/end と区別する style の bit。

## q1201-i01 結果（2026-10-14）

Event ws074-p227-q1201-partial-clear-20261014: [証拠](evidence/20261014-q1201/transitions.txt)。同条件 1,455 ID: whole PASS 427→458（後退 0、callback 9,505→10,466）。`flex-abspos-staticpos-*` の justify-content 001–008・rtl・vertWM 001/002、align-self 001–008・rtl 001–004・vertWM 001–004、align-content-001 などが PASS。未達: `last baseline` / `baseline` の本来の整列（end・start に退避しているだけ）、`safe` の clamp。

## q1202-i01 結果（2026-10-14、B1）

Event ws074-p227-q1202-partial-clear-20261014: [証拠](evidence/20261014-q1202/transitions.txt)。範囲: (1) flex の複数 line の `align-content`（stretch・start/end/center・space-*）と `wrap-reverse`・RTL 列の交差軸の反転、wrap 列の幅 stretch と max-height での折り返し、row-gap の % は definite な高さ基準、(2) `display:inline-flex`（atomic inline、computed 名）、(3) float/absolute/flex・grid item の computed display の blockify、(4) flex の記述で factor 間に basis を挟む無効な値の拒否、(5) `overflow` の2値、`overflow-inline/block`、`writing-mode: sideways-lr/rl`（parse と報告のみ、配置は横書き）、(6) `align-*: safe X`（static position の safe 退避）と `justify-self`/`align-self` の flex-start/flex-end の区別。
同条件 1,455 ID: whole PASS 458→481（後退 0）。q1199 開始時の測定からの通算: 359→481（callback 8,818→10,980）、Grid502: 195→243 PASS（q1189 は 192）。静的 reftest 1,536 件（同 manifest の runner が通る分）: 開始 commit（8fb947cd8）682 PASS→815 PASS、PASS→FAIL 4 件（[一覧](evidence/20261014-q1202/reftest-transitions.txt)）、FAIL→PASS 137。
reftest の PASS→FAIL 4 件: `orthogonal-positioned-grid-items-007/013`（旧 engine は test も ref も横書きで描いて偶然一致していた。ref が縦書きに正しく描かれるようになり、block の `display:grid` 内の縦書き absolute 子は未対応のため不一致）、`grid-justify-baseline-001`（ref の描画が変わった）、`subgrid/line-names-008`（名前付き line 未対応の absolute 子の CB が padding edge になった。subgrid/名前付き line が未実装）。未解決。
Acid3 差 0、Acid2 差 1,865（不変）、host CSS 51/51・JS 14/14・Grid 48/48・flex-negative-justify 5/5・physical-justify 21/21、build warning 0。dom-tests 20/23 は main の build でも同じ（既存）。`run-grid-replaced-tests.py` の `align-items:stretch` の期待値 (0,100,150) は Chromium の実測 150,150 と WPT implies-size-change 011–018 に反するため失敗する（試験は変更していない）。
