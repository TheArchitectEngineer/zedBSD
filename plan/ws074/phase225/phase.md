<!-- awesome-plan project=zedbsd record=ws074-p225 -->

# ws074-p225: Vertical Grid logical axes

Status: in-progress（2026-10-18 Q1: Status の行が無かったので写した。元: in-progress / q1142固定vertical-lr部分実装）

## q1159-i01 有限部分範囲（2026-10-14 12:17 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（13:47 JSTまで）に適用。q1150–q1153の2×2 implicit auto vertical-lr/LTR・definite content-box・gap0・単一ASCII文字子・start/stretch・非負px margin/border/padding経路をvertical-rlへ一般化。親物理行を右→左、子単純縦文字を解決済み列高/右端行で配置。固定vertical-rl stretch009–016 whole PASS、独立auto frameページChromium比較、旧rl001–008/lr001–016/縦横WPT、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。他Grid/WPT/runner/公開ABI/`plan/master.md`変更なし。未達ならuncleared。p225/p189/p173 wholeは基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1159-i01 結果（2026-10-14 12:20 JST）

Event ws074-p225-q1159-partial-clear-20261014: [証拠](evidence/20261014-q1159/README.md)。2×2縦Grid implicit auto trackをvertical-rl/LTRへ拡張し、物理行を右→左へ配置、子縦右文字の確定列content高とframe寄与を維持。独立border/padding各4矩形がChromium 153と一致。固定縦右stretch009–016は8/8 whole FAIL→PASS、旧縦右8+縦左16+縦横2 PASS、warning0 build、host CSS51/JS14/Grid48、Acid3差0/Acid2差1865。q1159部分cleared、p225 wholeは他Grid form等でuncleared、p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1158-i01 有限部分範囲（2026-10-14 12:14 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（13:44 JSTまで）に適用。q1142のvertical-lr/LTR inline-grid・明示固定px track・gap0・span1・in-flow非replaced/auto寸法・start/stretch経路をvertical-rlへ一般化。親論理行を物理右→左に配置し、子vertical-rl単純文字へ確定列高を渡す。固定vertical-rl stretch001–008 whole PASS、独立2×2固定Grid矩形Chromium比較、旧vertical-lr001–016/縦横WPT、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。暗黙auto tracks/他Grid、WPT/runner/公開ABI/`plan/master.md`変更なし。未達ならuncleared。p225/p189/p173 wholeは基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1158-i01 結果（2026-10-14 12:16 JST）

Event ws074-p225-q1158-partial-clear-20261014: [証拠](evidence/20261014-q1158/README.md)。明示固定px trackのvertical-rl/LTR Gridを右端から物理行配置し、子vertical-rl文字へ固定列高を渡す。独立空/文字2×2 Gridの全矩形がChromium 153と一致。固定縦右stretch001–008は8/8 whole FAIL→PASS、旧縦左16+縦横2 PASS、warning0 build、host CSS51/JS14/Grid48、Acid3差0/Acid2差1865。q1158部分cleared、p225 wholeは暗黙auto track等が残りuncleared、p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1153-i01 有限部分範囲（2026-10-14 11:58 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（13:28 JSTまで）に適用。q1152の2×2 implicit auto vertical-lr/LTR・definite content-box・gap0・単一ASCII文字子・start/stretch経路で、子content-boxの非負px border/padding frameを物理track寄与と確定列高/行幅へ反映。margin既存維持。固定border011/015・padding012/016 whole PASS、独立frame最小ページChromium比較、旧001–010/013–014・縦横WPT、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。WPT/runner/公開ABI/`plan/master.md`変更なし。未達ならattempt unclearedで原因を記録。p225/p189/p173 wholeは範囲外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1153-i01 結果（2026-10-14 12:01 JST）

Event ws074-p225-q1153-partial-clear-20261014: [証拠](evidence/20261014-q1153/README.md)。子の非負px border/paddingを縦Grid auto trackの物理外側寸法と縦文字の利用可能content高へ反映。独立border/paddingページは各4矩形がChromium 153と完全一致。固定011/012/015/016は4/4 whole FAIL→PASS、旧001–010/013–014と縦横2件もPASS。warning0 build、host CSS51/JS14/Grid48、Acid3 score100/inventory100/差0、Acid2差1865。q1153部分cleared、p225 wholeは他Grid form等でuncleared、p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1152-i01 有限部分範囲（2026-10-14 11:53 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（13:23 JSTまで）に適用。q1150の2×2 implicit auto vertical-lr/LTR・definite content-box・gap0・単一ASCII文字子・start/stretch限定経路で、非負px marginだけを内容寄与・子の解決済み物理サイズ・配置へ一般的に反映。border/paddingは対象外。独立margin最小ページをChromiumと比較し、固定010/014 whole PASS、旧001–009/013・縦横WPT、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。WPT/runner/公開ABI/`plan/master.md`変更なし。未達ならattempt unclearedで原因を記録。p225/p189/p173 wholeは基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1152-i01 結果（2026-10-14 11:57 JST）

Event ws074-p225-q1152-partial-clear-20261014: [証拠](evidence/20261014-q1152/README.md)。非負px marginを縦Gridのauto track物理内容寄与・縦文字の列内利用可能高・配置に反映。余白最小ページとtop/leftを加えた別ページはChromium 153と全box一致。固定010/014は2/2 whole FAIL→PASS、旧001–009/013・縦横WPTもPASS。border011/015・padding012/016は従来経路で初回差不変。warning0 build、host CSS51/JS14/Grid48、Acid3 score100/inventory100/差0、Acid2差1865。q1152部分cleared、p225 wholeはborder/padding等が残りuncleared、p189/p173全件100%未達。共有投影/GitHub publication保留。

Event ws074-p225-q1151-source-correction-20261014: [固定WPT原本の再確認](evidence/20261014-q1149/README.md)で009/013以外の6件を「全てmargin」と記した分類を訂正。010/014はmargin、011/015はborder、012/016はpadding。q1150が実装した外側寸法0の基準と固定009/013 PASSは不変。後続は外側寸法種別を明示して有限範囲を選ぶ。

## q1150-i01 有限部分範囲（2026-10-14 11:40 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（13:10 JSTまで）に適用。[q1149診断](evidence/20261014-q1149/README.md)のmargin無し009/013だけを対象に、vertical-lr/LTR inline-gridのdefinite content-box 250×250・gap0・2×2 implicit auto tracks・margin/border/padding0・span1・in-flow非replaced auto-size単一ASCII文字子・start/stretchに限定。論理列→物理y/行→x、内容寄与と残り空間によるauto track sizing、縦文字の解決済み列高でlayoutし、最小ページと固定009/013 whole PASS、旧001–008/縦横WPT、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。未対応構成fallback。WPT/runner/公開ABI/`plan/master.md`変更なし。未達ならattempt uncleared、部分コード維持は後退検証時だけ。p225/p189/p173 wholeは範囲外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1150-i01 結果（2026-10-14 11:48 JST）

Event ws074-p225-q1150-partial-clear-20261014: [証拠](evidence/20261014-q1150/README.md)。2×2 implicit auto trackを子の物理content寄与と残り空間で測り、論理列→物理y/行→xへ配置。250×250基準と300×240不等内容の最小ページがChromium 153と全box座標・寸法一致。固定009/013は2/2 whole FAIL→PASS、旧001–008・縦横2件もPASS。margin付き010/014は従来経路で初回差不変。warning0 build、host CSS51/JS14/Grid48、Acid3 score100/inventory100/差0、Acid2差1865。q1150部分cleared、p225 wholeはmargin等が残りuncleared、p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1149-i01 有限部分範囲（2026-10-14 11:35 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（13:05 JSTまで）に適用。固定WPT vertical-lr stretch009–016最大8件の同条件baselineを取得し、WPT sourceと最小2×2ページで論理track→物理寸法、auto track content contribution・free space配分、alignment/子writing-modeを切り分け、次の有限実装境界を証拠化。browser/WPT/runner/公開ABI/`plan/master.md`/Acid変更なし。p225/p189/p173 wholeは部分受け入れ外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1149-i01 結果（2026-10-14 11:38 JST）

Event ws074-p225-q1149-partial-clear-20261014: [証拠](evidence/20261014-q1149/README.md)。固定WPT009–016は8/8 whole FAIL、初回差は縦子width期待20/実125・115、横子期待80/実125・115。独立2×2ページで論理列が物理xへ進む誤りをChromium 153と比較。`grid_vertical_fixed`は明示固定px track/auto heightだけで対象外、旧Grid経路は横軸サイズ/位置と縦書き子の強制stretchを使用し、`inline_vertical_room`もauto trackで縦文字寸法を得られない。次の有限scopeはmargin無し009/013の一般的auto-track軸・content contribution・縦文字の確定roomに限定し、margin有り6件は後続。q1149診断部分cleared、p225/p189/p173 whole未達。製品コード・WPT/runner/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

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
