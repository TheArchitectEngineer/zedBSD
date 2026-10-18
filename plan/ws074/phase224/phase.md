<!-- awesome-plan project=zedbsd record=ws074-p224 -->

# ws074-p224: Grid track animation

Status: uncleared（q1135診断部分cleared、実装未着手・active item無し）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p189 q1134](../phase189/evidence/20261014-q1134/README.md)の行track animation後退。

## 目的と受け入れ

CSS/Web AnimationsがGrid列・行track listのcomputed値へ補間・合成・置換を正しく反映する。固定`grid-template-rows-composition`/`interpolation`の旧PASS回復と列側の旧PASS維持を確認し、Grid animation対象の残差を評価する。WPT本体/runnerの期待は変更しない。p189/p173全件100%は別受け入れ。

## q1135-i02 有限部分範囲（2026-10-14 10:26 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（11:56 JSTまで）に適用。固定行track animation2件のraw callback・WPT sourceとCSS/JS animation/`getComputedStyle`経路を読取・最小probeで比較し、q1105の旧PASSが実際のanimation結果を検証していたか、現在の一般欠陥と有限実装境界を記録する。読取/runner実行のみ。browser製品source/WPT/runner/公開ABI/`plan/master.md`変更なし。p224 whole/PASS回復は本診断部分の基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。q1135-i01とは独立。

Event ws074-p224-addition-20261014: q1134で旧PASSの行track compositionがFAIL、interpolationも238 subtestがPASS→FAIL。先行q1124の行computed CSSOM追加で潜在欠陥が可視化された疑いを調べる。p189/p173分母・受け入れ不変。共有投影/GitHub publication保留。

## q1135-i02 結果（2026-10-14 10:29 JST）

Event ws074-p224-q1135-partial-clear-20261014: [証拠](evidence/20261014-q1135/README.md)。q1124前は行track computed CSSOMが空でactual/expected両方空となり、旧PASSはanimation動作の証明でなかった。現在は`@keyframes`指定でも50pxの元値のまま、`Element.prototype.animate`は不在。CSS animation/transitionとWeb Animationsの一般実装が必要。読取・最小probeのみでq1135-i02診断部分cleared、p224 whole/p189/p173全件100%未達。共有投影/GitHub publication保留。

## Native primitiveとの実装境界（2026-10-18）

[p248](../phase248/phase.md)がnative clock/Animation DOM stateとGC/seek/pause/cancel/frame invalidationを提供し、そのverified scoped outputを受けて本PhaseがGrid track list matching・intrinsic/fit/minmaxのinterpolation/compositionとCSS/WA統合を所有する。必要API/timingが実コードで検証されるまで補間の実行Queueは選定しない。p224 q1135診断部分clearedとwhole未達は維持する。固定Grid animation12 ID/全Grid/影響領域のwhole/callback、独立補間・合成/host/target/規約/Acidを検証する。

Event ws074-animation-phase-ownership-reconciled-20261018: q14の待機中に既存[p224](../phase224/phase.md) q1135を再確認。新p248とGrid track goalが重複していたため、p224の既存goal/history/未達を保持し、p248をnative timing/DOM lifecycleという前提outputへ限定。p248 verified output → p224 Grid interpolation/compositionと依存を明示。p246/q11未達とq14のcomputed-only scope/criteria/approvalは不変。両Phaseは未選定であり、この設計改訂だけでは実装を開始しない。

Event ws074-p224-state-reconciled-20261018: q1135診断attemptは終了済みで現在active Queue itemが無いため、古いin-progress表記をwhole未達のunclearedへreconcile。過去の診断部分clearedは保持し、p248 verified prerequisite後の新Queueまで実装を再開しない。実装済み/後退修復の主張は追加しない。

Event ws074-q14-animation-fit-collapse-diagnosis-20261018: q14 final全件の途中で同じGrid columns/rows composition/interpolation4 IDの各4 callback、計16が旧PASS→FAIL。独立[probe](../phase250/evidence/20261018-q14/animation-collapse-probe.html)でq12/final両pairのanimate/Animationがundefined、異なるfit(5px)/fit(20px)とfit(5px)/fit(40px)が旧serializerでは両auto autoとなり等しい一方、新serializerで本来の異なる値を保持することを確認。p250既定criteria通り既存native animation未実装の可視化として診断し、q11/p246の24未達やp224/p248をclearしない。全件terminal outcomeは継続待ち、runner/WPT結果判定は変更しない。
