<!-- awesome-plan project=zedbsd record=ws074-p225 -->

# ws074-p225: Vertical Grid logical axes

## q1146-i01 有限部分範囲（2026-10-14 11:18 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（12:48 JSTまで）に適用。q1142のvertical-lr/LTR inline-grid・明示固定px track・gap0・単一span・in-flow非replaced/auto寸法限定経路をhorizontal-tb子にも適用し、親の論理track軸変換と子自身の横書きintrinsic/line geometryを両立させる。2×2最小ページ、固定vertical-lr stretch005–008全whole PASS、旧001–004と旧horizontal/vertical PASS、CSS/JS/Grid host、warning0 build、Acid3差0/Acid2非増加、未対応構成fallbackを確認。WPT/runner/公開ABI/`plan/master.md`変更なし。p225/p217/p189/p173 wholeは部分受け入れ外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1146-i01 結果（2026-10-14 11:19 JST）

Event ws074-p225-q1146-partial-clear-20261014: [証拠](evidence/20261014-q1146/README.md)。限定固定縦Grid物理軸経路にhorizontal-tb子を通し、2×2最小fixtureの空/文字の位置と寸法を確認。固定vertical-lr stretch005–008は4/4 whole FAIL→PASS、旧001–004とhorizontal/vertical旧PASS2件も維持。span2 fallbackは従来経路で非crash（準拠は主張しない）。CSS51/JS14/Grid48、warning0 build、Acid3 score100/inventory100/差0、Acid2差1865。q1146部分cleared、p225 wholeおよびp217/p189/p173全件100%は未達。次は固定Grid502全再測定と009–016明示Grid寸法/可変track。共有投影/GitHub publication保留。

Status: uncleared（q1142の文字/WPT基準未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p217 q1141診断](../phase217/evidence/20261014-q1141/README.md)。文字ありitemの縦inline geometryは[p220](../phase220/phase.md)の未実装出力を待つ。q1122はtext-orientation CSS stateのみでgeometry出力ではなかった。空itemの固定track軸変換はp220から独立。

## 目的と受け入れ

縦書きGridの論理列（inline）と行（block）を物理y/xへ正しく写し、track sizing、item placement、self alignment、absolute static area、DOM/paint geometryを一致させる。固定Interop 2025 vertical Grid代表の全whole PASSと旧horizontal/Acid3維持を確認した後にwhole clearanceを判定する。p217/p189/p173全件100%は別受け入れ。

## 設計・境界

Grid layoutでwriting-modeに応じて論理track軸と物理座標を写す。初回はvertical-lr/LTR、明示固定px track、gap0、in-flow非replaced item、auto物理寸法、span1、start/stretchのみの限定経路を設け、未対応構成は既存layoutへ戻す。初回範囲外のvertical-rl/RTL、flexible/implicit track、gap、absolute child、baseline/center/end、複雑な縦inlineは後続設計。WPT本体/runner採点・公開ABI・`plan/master.md`は変更しない。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。code後はWS074 near-final full-standard conformance Phaseで再確認する。

## q1142-i01 有限部分範囲（2026-10-14 10:51 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（12:21 JSTまで）に適用。上記限定vertical-lr固定track経路を実装し、空item2×2と単一非折返し文字itemの物理位置・寸法、固定vertical-lr stretch WPT代表2件、旧horizontal/vertical PASS、CSS/JS/Grid host、warning0 build、Acid3差0/Acid2非増加を確認。未対応構成のfallbackも確認する。p225 whole/p217/p189/p173全件100%は本部分基準外。

Event ws074-p225-addition-20261014: q1141でalignment192 FAIL中初回幅誤り110、固定縦Grid trackの軸変換不足が32 vertical stretchと他positioned familyの共通候補。p217の一般alignmentから物理軸配置を分離し、p220の縦inline出力を文字ありitemにのみ依存させる。p217/p189/p173分母・受け入れ不変。共有投影/GitHub publication保留。

## q1142-i01 結果（2026-10-14 10:58 JST）

Event ws074-p225-q1142-uncleared-20261014: [証拠](evidence/20261014-q1142/README.md)。限定固定track経路で空2×2の物理位置・寸法は期待どおり。単一行文字itemは軸位置のみ改善し、物理幅38px（WPT Ahemでは60px）で期待10pxに届かない。固定vertical-lr stretch WPT001/002は両FAILのため、q1142-i01の明示基準は未達、Phaseもuncleared。p220 q1122を縦文字geometry出力とみなした当初の依存記述を訂正した。次はp220で非Grid文字の縦line/fragment geometryを有限範囲で実装・検証し、その出力をGridで再検証する。空Grid軸変換コードは後退ゲートPASSで保持。Acid3差0/Acid2差1865維持。共有投影/GitHub publication保留。

Event ws074-p220-q1144-dependency-output-20261014: [p220 q1144](../phase220/evidence/20261014-q1144/README.md)の限定縦inline geometry出力で、p225 q1142時にFAILだった固定vertical-lr stretch001/002が両whole PASS。q1142試行の歴史的unclearedは維持し、p225 wholeは他vertical Grid/paint未検証のためuncleared。次は固定vertical-lr family全件と画素描画を別有限範囲で照合する。共有投影/GitHub publication保留。

Event ws074-p189-q1145-family-20261014: [固定Grid502全件再測定](../phase189/evidence/20261014-q1145/README.md)でvertical-lr stretch001–004がwhole PASS、005–016はFAIL。005–008は横書き子、009–016は明示250px Grid/可変trackで、q1142の限定経路外。次のp225有限scopeはこの構成別に定める。p225 wholeはuncleared、q1142歴史的結果は不変。共有投影/GitHub publication保留。
