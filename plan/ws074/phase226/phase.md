<!-- awesome-plan project=zedbsd record=ws074-p226 -->

# ws074-p226: Grid content distribution and absolute-child containing block

B1 (2026-10-14, handed over from the Codex session). 範囲: horizontal-tb の grid container の (1) align-content / justify-content の分配（stretch・start・end・center・space-*）と auto track の stretch、(2) position 付き grid の absolute 子の containing block を grid line から決める（auto line は padding edge）と、absolute 子の百分率 height/width の基準、(3) row-gap/column-gap の calc(%+px)。vertical grid と orthogonal 子は範囲外。

## q1199-i01 結果（2026-10-14）

Event ws074-p226-q1199-partial-clear-20261014: [証拠](evidence/20261014-q1199/run.md)。1,455 ID の同条件測定で whole PASS 359→379（後退 0）、callback PASS 8,818→9,171。Acid3 差 0、Acid2 差 1,865（不変）、host CSS 51/51・JS 14/14・Grid 48/48、build warning 0。p226 の有限範囲は cleared。p217/p189/p173 whole は未達。

## q1200-i01 結果（2026-10-14、B1）

Event ws074-p226-q1200-partial-clear-20261014: [証拠](evidence/20261014-q1200/transitions.txt)。範囲を広げて実施（承認は q1199 と同じ延長）: (1) `repeat(auto-fill|auto-fit, …)`（layout で count を決め、auto-fit の空 track は 0 幅に畳む。`minmax` の max が min 未満なら min）、(2) 水平 grid の in-flow 縦書き子（行 track が固定 px なら inline room とし、auto 行は無制限。self-start/end、justify/align）、(3) stretch された置換要素が aspect ratio で他軸を決める（Chromium 150,150 と一致）、(4) fr の合計が 1 未満のとき auto track が残りを分け合う、(5) min-width/min-height の %（repeat の個数用）、(6) `getComputedStyle` の align/justify の名前表の誤り（`CSS_ALIGN_STRETCH` が enum に入った時に 1 ずれていた）を直し、flex-start と start を style の bit で区別。
同条件 1,455 ID: whole PASS 379→427（後退 0、callback 9,171→9,505）。うち 21 件は enum のずれで落ちていた flexbox の computed 試験 19 件・inheritance 等の回復（q1068 では PASS だった）。純増は auto-repeat 10・stretch 005–008/013–016 の 8・implies-size-change 6・orthogonal-flows 1 など。Acid3 差 0・Acid2 差 1,865（不変）、host CSS 51/51・JS 14/14・Grid 48/48、build warning 0。
注意: `plan/ws074/tests/run-grid-replaced-tests.py` の `align-items:stretch` の期待値 (0,100,150) は Chromium（150,150）と WPT alignment-implies-size-change-011〜018 に反する。試験側は変更していない（Q1 の判断待ち）。
