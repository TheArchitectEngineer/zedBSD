<!-- awesome-plan project=zedbsd record=ws074-p222 -->

# ws074-p222: Interop 2025 Grid automatic placement

Status: in-progress（q1131部分実装）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p221 q1130のgrid-auto-flow CSS state](../phase221/evidence/20261014-q1130/README.md)、[p189 q1105固定Grid502](../phase189/evidence/20261014-q1105/README.md)。

## 目的と受け入れ

固定Interop 2025 Grid placement WPTで `grid-auto-flow:row|column` と `dense` の自動配置を実際に反映し、対象のwhole PASSと旧PASS維持を検証する。対象IDとPhase whole基準は後続有限診断で固定する。p189全Grid502/p173全3,805件100%は別受け入れ。

## 設計・境界

Grid配置cursor・taken cells・必要な暗黙trackと既存CSS stateを各有限Queue scopeで扱う。CSS parser/JS bindingはp221の出力を用い、責務を重複しない。WPT本体/runner採点・公開ABI・`plan/master.md`は変更しない。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。code後はWS074 near-final full-standard conformance Phaseで再確認する。

Event ws074-p222-addition-20261014: q1130でcolumn/dense値がparse/style/CSSOMへ入ったがGrid layoutはrow sparse cursorのままで、値の配置意味が未達。p221 CSS stateとp222 placementを分離。p189/p173分母・受け入れ不変。共有投影/GitHub publication保留。

## q1131-i01 有限部分範囲（2026-10-14 10:05 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（11:35 JSTまで）に適用。Grid `grid-auto-flow:row dense`で各auto itemの探索を先頭から始め、既存row sparseの前進cursorを保持。固定明示2列・span穴埋めfixture、選定WPT placement代表の前後比較、旧PASS、CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加。column/dense列、暗黙trackサイズ、WPT/runner/公開ABI/`plan/master.md`変更なし。p222 whole/p189/p173全件100%は本部分基準外。[CSS Grid §7.7](https://www.w3.org/TR/css-grid-2/#propdef-grid-auto-flow)、[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1131-i01 結果（2026-10-14 10:07 JST）

Event ws074-p222-q1131-partial-clear-20261014: [証拠](evidence/20261014-q1131/README.md)。row denseで各auto itemのcursorを原点から再開し、明示2列/穴埋めfixtureの第3子を sparse(0,40)からdense(40,0)へ。固定implicit-tracks WPTは前後0/12 whole FAILのまま、最初の失敗は未対応`grid-auto-columns`由来の幅494/期待100。旧whole PASS4件、CSS51/JS14/Grid48、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865維持。q1131部分cleared、p222/p189/p173全件100%未達。column/dense列と暗黙trackサイズは後続。共有投影/GitHub publication保留。
