<!-- awesome-plan project=zedbsd record=ws074-p223 -->

# ws074-p223: Empty Grid intrinsic size

Status: in-progress（q1135部分実装）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p189 q1134](../phase189/evidence/20261014-q1134/README.md)の旧PASS後退。

## 目的と受け入れ

明示列もin-flow itemもないGridで暗黙列trackを実在列としてintrinsic幅へ算入しない。固定`empty-grid-001`旧PASS回復、別の明示/暗黙列・absolute child配置・旧PASS、Acid3 pixel差0とAcid2差非増加を確認し、一般的なGrid intrinsic sizingの残差を評価する。p189/p173全件100%は別受け入れ。

## q1135-i01 有限部分範囲（2026-10-14 10:26 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（11:56 JSTまで）に適用。`grid-auto-columns:200px`を含む空Gridに実在しない列を加えない最小layout修正。最小empty/explicit/absolute/子ありgeometry、固定empty-grid WPTと旧PASS代表、CSS/JS/Grid host、warning0 build、Acid3差0/Acid2非増加。Grid animation/他のtrack grammar、WPT/runner/公開ABI/`plan/master.md`変更なし。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。q1135-i02とは独立。

Event ws074-p223-addition-20261014: q1134で旧PASSのempty-gridが幅0期待/200実測へ後退。p222のauto track指定後のGrid intrinsic sizingとして分離。p189/p173分母・受け入れ不変。共有投影/GitHub publication保留。

## q1135-i01 結果（2026-10-14 10:29 JST）

Event ws074-p223-q1135-partial-clear-20261014: [証拠](evidence/20261014-q1135/README.md)。無項目・明示列なしのGridで内部placeholder暗黙列を0幅としてintrinsic幅に加えない。固定empty-gridは3→9/9 whole PASS、旧PASSのimplicit placement12/12維持。CSS51/JS14/Grid48、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865維持。q1135-i01部分cleared。p223 wholeの一般的intrinsic sizing網羅、p189/p173全件100%未達。共有投影/GitHub publication保留。
