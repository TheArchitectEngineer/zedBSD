<!-- awesome-plan project=zedbsd record=ws074-p213 -->

# ws074-p213: CSS Grid auto-width self alignment

Status: cleared（q1073-i01、代表WPT PASS・静的旧PASS全件維持）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p212 q1071](../phase212/evidence/20261014-q1071/README.md) の横書き Grid 配置と [p189 q1072](../phase189/evidence/20261014-q1072/README.md) の固定 Grid baseline。

## 目的と受け入れ

横書き Grid の in-flow 非置換項目について、`width:auto` と `justify-self` / `justify-items` 非 stretch の組合せを、grid area 内の fit-content 幅で配置する。auto margin、縦書き、replaced、min/max の一般化は別範囲。固定代表 WPT の幅/位置を改善し、固定 Grid 静的旧 PASS、host build、Acid3 画素差0と Acid2 非増加を維持する。p189/p190/p173 全件受け入れは独立。

## q1073-i01 有限範囲

2026-10-14 05:24 JST、この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を最大90分（06:54 JSTまで）の本 Queue に適用。`layout/grid.c` の横書き、in-flow、非置換、auto width、非 stretch、auto margin 無しの幅と位置を一般規則で最小修正する。固定 `/css/css-grid/alignment/grid-self-alignment-stretch-004.html` を代表にし、局所 fixture、固定 Grid 静的529 ID旧 PASS、warning0 host build、Acid3/Acid2を確認。WPT/runner/公開ABI/`plan/master.md`を変更しない。期限内に検証できなければ uncleared として証拠・残件を記録する。適用規則: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)。

Event ws074-p213-addition-q1073-20261014: q1072で代表 WPT の `justify-self:start` 項目が期待60pxに対し列幅150pxになることを確認し、p211/p212で保留したauto幅を分離。p189/p190/p173分母と受け入れは維持。共有 Master/Queue/GitHub publication はQ1競合回避で保留。

Event ws074-p213-q1073-scope-revision-20261014: 最終sourceによる固定Grid静的529件で旧PASS→FAIL 3件を確認。auto margin の test と非stretchのreferenceとの整合2件、縦書き子項目のbaseline参考1件。旧PASS維持の受け入れを満たすため、q1073の範囲に横書きGrid内のauto inline margin の内容幅・残余配分を追加し、縦書き子項目は今回のauto幅変更から除外する。auto margin全軸・縦書き項目の一般実装は対象外。ユーザーの後続有限Queue事前承認をこの最大90分の限定改訂にも適用。旧3 PASS全件の復帰、全529件、代表WPT、host/Acidを再確認する。最初の529件は旧123 PASS/406 FAIL→121 PASS/408 FAIL、3回帰/1改善として履歴を保存。

Event ws074-p213-q1073-vertical-rl-diagnosis-20261014: auto margin修正後、旧3回帰のうち2件はPASSへ復帰。最後の`grid-justify-baseline-001.html`は子の`writing-mode:vertical-rl`がCSS値表に無く、横書きとして計算され、referenceの`justify-self:end`だけが新しいauto幅配置を受ける。今回の「縦書き子項目をauto幅変更から除外」を実現するためCSS値としてvertical-rlを保持する最小追加を行い、layout全般の縦書き実装は対象外とする。旧PASS回復と固定全Gridを再計測する。

## q1073-i01 結果（2026-10-14 05:37 JST）

Event ws074-p213-q1073-clear-20261014: [固定WPT/静的529件/Acid証拠](evidence/20261014-q1073/README.md)。代表testharness 1 ID whole FAIL→PASS（幅150→期待60px）、局所44/44。固定静的Grid529件は旧123 PASS/406 FAIL→124 PASS/405 FAIL、旧PASS後退0/FAIL→PASS1。最初の3回帰はauto inline margin残余配分とvertical-rl値認識で全回復。host build warning0、CSS rule51/51・CSSOM12/12、Acid3得点100/画素差0、Acid2差1865。q1073-i01/p213 whole cleared。縦書きlayout、min/max、replaced、全Grid失敗とp189/p190/p173全件100%は未達。共有Master/Queue/GitHub publication保留。
