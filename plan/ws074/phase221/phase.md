<!-- awesome-plan project=zedbsd record=ws074-p221 -->

# ws074-p221: Interop 2025 Grid parsing and CSSOM

## q1139-i01 有限部分範囲（2026-10-14 10:38 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（12:08 JSTまで）に適用。Grid template/auto trackのcalc後に負となる絶対長をcomputed時0pxへclampし、正値・割合・既存trackを保つ。固定auto列/行computedの負calc各1とtemplate代表、invalid/旧PASS、CSS/JS/Grid host、warning0 build、Acid3差0/Acid2非増加。新track文法/area/animation、WPT/runner/公開ABI/`plan/master.md`変更なし。p221 whole/p189/p173全件100%は本部分基準外。[CSS Grid §7.2.1](https://www.w3.org/TR/css-grid-2/#track-sizes)、[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。q1137 calc length track出力が前提。

## q1139-i01 結果（2026-10-14 10:40 JST）

Event ws074-p221-q1139-partial-clear-20261014: [証拠](evidence/20261014-q1139/README.md)。Grid trackの負の計算済み絶対長を0pxへclamp、auto/template最小computed0px。固定auto列/行computed各9→10/25、invalid全PASS、template代表と旧placement/empty-grid PASS維持。CSS51/JS14/Grid48、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865維持。q1139部分cleared、p221/p189/p173全件100%未達。minmax/fit-content等残る。共有投影/GitHub publication保留。

## q1138-i01 有限部分範囲（2026-10-14 10:36 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（12:06 JSTまで）に適用。非負`fr`を`grid-auto-columns/rows`の単一・複数cycleで読み、definite列幅/行高に明示/暗黙frをともに含めて配分する。複数frと固定値混合の最小geometry、固定valid/computed/invalid各6件の前後比較、旧implicit placement/empty-grid PASS、CSS/JS/Grid host、warning0 build、Acid3差0/Acid2非増加。minmax/fit-content/area/animation、WPT/runner/公開ABI/`plan/master.md`変更なし。p221 whole/p189/p173全件100%は本部分基準外。[CSS Grid §7.2.4と§7.6](https://www.w3.org/TR/css-grid-2/#fr-unit)、[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。q1137 fixed length cycle出力が前提。

## q1138-i01 結果（2026-10-14 10:37 JST）

Event ws074-p221-q1138-partial-clear-20261014: [証拠](evidence/20261014-q1138/README.md)。暗黙frを列・行のdefinite残余配分に参加させ、1:2:1で100/200/100px、50px+1:2で50/100/200pxを確認。固定valid各10→12/30、computed各7→9/25、invalid全PASS、旧placement/empty-grid whole PASS維持。CSS51/JS14/Grid48、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865維持。q1138部分cleared、p221/p189/p173全件100%未達。minmax/fit-content等残る。共有投影/GitHub publication保留。

## q1137-i01 有限部分範囲（2026-10-14 10:33 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（12:03 JSTまで）に適用。`grid-auto-columns/rows`の非負固定length/percentage/calc trackを単一・複数cycleで読み、既存cascade後のcomputed CSSOMとdefinite/intrinsic寸法のlayoutを検査する。固定valid/computed/invalid各6件の前後比較、旧implicit placement/empty-grid PASS、CSS/JS/Grid host、warning0 build、Acid3差0/Acid2非増加。fr/minmax/fit-content/area、WPT/runner/公開ABI/`plan/master.md`変更なし。p221 whole/p189/p173全件100%は本部分基準外。[CSS Grid §7.6](https://www.w3.org/TR/css-grid-2/#propdef-grid-auto-columns)、[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。q1136 list出力が前提。

## q1137-i01 結果（2026-10-14 10:35 JST）

Event ws074-p221-q1137-partial-clear-20261014: [証拠](evidence/20261014-q1137/README.md)。auto track cycleの非px固定長/割合/calcを受理し、2em/10%/calcの列・行40/20/20px、computed40px 10% 20pxを確認。固定valid各6→10/30、computed各4→7/25、invalid全PASS、旧placement/empty-grid whole PASS維持。CSS51/JS14/Grid48、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865維持。q1137部分cleared、p221/p189/p173全件100%未達。fr/minmax/fit-content等残る。共有投影/GitHub publication保留。

## q1136-i01 有限部分範囲（2026-10-14 10:30 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（12:00 JSTまで）に適用。`grid-auto-columns/rows`の空白区切り単一/複数`auto`・非負px trackを`CSS_TRACKS`上限でparseし、暗黙track循環・computed/inline CSSOMを確認する。固定valid/computed/invalid各6件の前後比較、明示/暗黙trackと空Gridの旧PASS代表、CSS/JS/Grid host、warning0 build、Acid3差0/Acid2非増加。他のtrack値、WPT/runner/公開ABI/`plan/master.md`変更なし。p221 whole/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。q1132単一trackとq1135空Grid出力が前提。

## q1136-i01 結果（2026-10-14 10:32 JST）

Event ws074-p221-q1136-partial-clear-20261014: [証拠](evidence/20261014-q1136/README.md)。単一auto/pxの上限付き循環listをparseし、最小fixtureの列20/30/20・行30/40/30pxとinline/computed CSSOMを確認。固定valid各3→6/30、computed各3→4/25、invalid全PASS、旧placement/empty-grid whole PASS維持。CSS51/JS14/Grid48、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865維持。q1136部分cleared、p221/p189/p173全件100%未達。他のtrack種・calc・area等残る。共有投影/GitHub publication保留。

Status: in-progress（q1123部分診断）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p189 q1105固定Grid502](../phase189/evidence/20261014-q1105/README.md)。Grid shorthandの既存出力は[p218](../phase218/phase.md)から利用する。独立した解析群は別々に進められる。

## 目的と受け入れ

固定Interop 2025 Grid testharnessの`/css/css-grid/parsing/`41件を現行browserで再測定し、宣言構文・shorthand・computed CSSOMの一般欠陥を原因別に修正する。41件全whole PASSと旧PASS維持を実測した後にwhole clearanceを判定する。p189全1,783件/p173全3,805件100%は別受け入れ。

## 設計・境界

CSS values/parser/cascade、Grid style、必要なCSSOM、および`plan/ws074/tests/`を各有限Queueのscopeで扱う。WPT本体/runner採点・公開ABI・`plan/master.md`は変更しない。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。code後はWS074 near-final full-standard conformance Phaseで再確認する。

Event ws074-p221-addition-20261014: [p189 q1105](../phase189/evidence/20261014-q1105/README.md)の固定Grid502残392 whole FAILに`parsing/`35件を含む。p217のalignment/p220の縦文字と独立する解析・CSSOM群として追加。p189/p173分母・受け入れ不変。共有投影/GitHub publication保留。

## q1123-i01 有限部分範囲（2026-10-14 09:36 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（11:06 JSTまで）に適用。固定manifestの`/css/css-grid/parsing/` testharness41件をp189と同じWPT/フォント/HTTP/settle/process条件で現行browser再測定。whole/subtestのPASS/FAIL・初回失敗・sourceの構文を分類し、最大3の共通原因と最初の有限実装候補を証拠化する。読取/runner実行のみ。browser製品source/WPT/runner/公開ABI/`plan/master.md`/Acid変更なし。p221 whole/p189/p173全件100%は本部分基準外。

## q1123-i01 結果（2026-10-14 09:38 JST）

Event ws074-p221-q1123-partial-clear-20261014: [証拠](evidence/20261014-q1123/README.md)。固定Grid parsing41件を同条件再測定し6 whole PASS/35 FAIL、subtest336 PASS/930 FAIL、no_output/process_error0。失敗はGrid auto/areas属性欠落9、track解析/直列化14、area/shorthand10、その他2。最初の有限修正は既にparsed `grid-template-rows`のCSSOM返却。q1123部分cleared、p221/p189/p173全件100%未達。browser/WPT/runner/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

## q1124-i01 有限部分範囲（2026-10-14 09:39 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（11:09 JSTまで）に適用。既存`css_style.rows/row_count`を使って`grid-template-rows` computed CSSOMを列と共通の宣言track直列化で返す。none/px/fr/auto最小host、固定rows parsing6件と旧PASS代表、CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加。line names/implicit used tracks/auto-*/areas/shorthand、WPT/runner/公開ABI/`plan/master.md`変更なし。p221 whole/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1124-i01 結果（2026-10-14 09:40 JST）

Event ws074-p221-q1124-partial-clear-20261014: [証拠](evidence/20261014-q1124/README.md)。既存列track直列化を行CSSOMにも適用し、none/px/fr/autoとgetPropertyValue/camel-caseを返す。最小host PASS。固定行側6件でsubtest6 FAIL→PASS・逆0、whole6件は行名/暗黙track/無効値等でFAIL継続。旧whole PASS3件、CSS51/JS14/Grid48、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865。q1124部分cleared、p221/p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1125-i01 有限部分範囲（2026-10-14 09:42 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（11:12 JSTまで）に適用。Grid trackの直値/minmax両引数/fit-content引数で負の長さ・割合を構文時に拒否し、0と正値を維持。最小CSSOM host、固定`grid-template-columns-invalid`/`rows-invalid`の負数7×2 subtest、valid代表/旧PASS、CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加。行名・その他track grammar、WPT/runner/公開ABI/`plan/master.md`変更なし。p221 whole/p189/p173全件100%は本部分基準外。[CSS Grid §7.2.1](https://www.w3.org/TR/css-grid-2/#track-sizes)の非負track長制約、[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1125-i01 結果（2026-10-14 09:45 JST）

Event ws074-p221-q1125-partial-clear-20261014: [証拠](evidence/20261014-q1125/README.md)。Grid track直値/minmax引数/fit-content直値の負長・割合を拒否し、正値/零と既存calc式を保持。最小host両軸7/7拒否・9/9有効値受理。固定columns/rows-invalidは各33→40/42 subtest PASS、validは各19/34維持。残る無効値2件/軸は行名構文、whole4件FAIL。旧whole PASS3件、CSS51/JS14/Grid48、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865。q1125部分cleared、p221/p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1126-i01 有限部分範囲（2026-10-14 09:47 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（11:17 JSTまで）に適用。Grid track listの`[line names]`構文を検証し、隣接括弧群、予約名`auto`/`span`、不正token/未閉鎖を拒否する。既存の有効な行名・repeat()の受理を保持。最小host、固定columns/rows-invalid・validと旧PASS、CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加。行名保存/CSSOMや他のGrid機能、WPT/runner/公開ABI/`plan/master.md`変更なし。p221 whole/p189/p173全件100%は本部分基準外。[CSS Grid §7.2.2](https://www.w3.org/TR/css-grid-2/#line-names)、[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1126-i01 結果（2026-10-14 09:49 JST）

Event ws074-p221-q1126-partial-clear-20261014: [証拠](evidence/20261014-q1126/README.md)。Grid track行名群の隣接・予約名・不正token/未閉鎖を拒否し、有効名/repeatを保持。固定columns/rows-invalidは各40→42/42 subtest、whole FAIL→PASS。valid各19/34・旧whole PASS3件維持、後退0。最小host両軸6/6拒否・5/5受理、CSS51/JS14/Grid48、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865。q1126部分cleared、p221/p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1127-i01 有限部分範囲（2026-10-14 09:51 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（11:21 JSTまで）に適用。Grid track直値と`minmax()`両引数で既存`calc()`長・割合を構文/計算値として受理し、computed負trackを零に丸める。最小CSSOM/幅・行高、固定columns/rows-valid・invalidと旧PASS、CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加。その他math関数/auto-repeat/行名CSSOM、WPT/runner/公開ABI/`plan/master.md`変更なし。p221 whole/p189/p173全件100%は本部分基準外。[CSS Grid §7.2.1](https://www.w3.org/TR/css-grid-2/#track-sizes)、[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1127-i01 結果（2026-10-14 09:54 JST）

Event ws074-p221-q1127-partial-clear-20261014: [証拠](evidence/20261014-q1127/README.md)。Grid直値/minmax両引数の`calc()`長・割合を既存parser/cascadeで扱い、負のcomputed trackを零に丸める。minmax最小側frを拒否。固定columns/rows-valid各19→27/34 subtest、無効値各42/42 whole PASSと旧whole PASS3件維持、後退0。最小host両軸8/8受理・30px/0px geometry、CSS51/JS14/Grid48、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865。q1127部分cleared、p221/p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1128-i01 有限部分範囲（2026-10-14 09:55 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（11:25 JSTまで）に適用。`grid-template-columns/rows`の有効なJS style設定値から空行名群`[]`をCSSOM直列化時に除去する。非空行名・repeat/track値・無効値は保持。最小CSSOM、固定columns/rows-valid・invalidと旧PASS、CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加。auto-repeat/他のGrid CSSOM、WPT/runner/公開ABI/`plan/master.md`変更なし。p221 whole/p189/p173全件100%は本部分基準外。[CSS Grid §7.2.2](https://www.w3.org/TR/css-grid-2/#line-names)、[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1128-i01 結果（2026-10-14 09:57 JST）

Event ws074-p221-q1128-partial-clear-20261014: [証拠](evidence/20261014-q1128/README.md)。JS設定された有効なGrid列/行templateで空行名`[]`のみCSSOM直列化から除去し、非空名・repeat・track・無効値を保持。固定columns/rows-valid各27→29/34 subtest、invalid各42/42 whole PASSと旧whole PASS3件維持、後退0。最小host・以前の3 fixture・CSS51/JS14/Grid48・warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865。q1128部分cleared、p221/p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1129-i01 有限部分範囲（2026-10-14 09:58 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（11:28 JSTまで）に適用。q1123と同じ固定Grid parsing41 ID/manifest/WPT/HTTP/fonts/timeout/settleで現行browserを全件再測定。whole/subtest変化・後退・初回差を比較し、最大3の次実装候補を選ぶ。読取/runner実行のみ。browser製品source/WPT/runner/公開ABI/`plan/master.md`/Acid変更なし。p221 whole/p189/p173全件100%は本部分基準外。

## q1129-i01 結果（2026-10-14 09:59 JST）

Event ws074-p221-q1129-partial-clear-20261014: [証拠](evidence/20261014-q1129/README.md)。固定41件を全件同条件再測定し、q1123の6→8 whole PASS/35→33 FAIL、336→399 PASS subtest/930→867 FAIL、後退0・無出力0。改善はcolumns/rows-invalidのwhole2件。残33件の大群はauto-*/areas未対応、template computed行名/暗黙track、auto-repeat、area/Grid shorthand。次の有限候補は`grid-auto-flow`構文とcomputed CSSOM。q1129部分cleared、p221/p189/p173全件100%未達。browser/WPT/runner/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

## q1130-i01 有限部分範囲（2026-10-14 10:00 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（11:30 JSTまで）に適用。`grid-auto-flow`の`row`/`column`/`dense`組合せをCSS parser・style state・CSSOM指定/計算値で受理/正規化する。CSS-wide初期/明示継承/解除も保持。最小host、固定valid/computed/invalidと旧PASS、CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加。列/dense実配置、他のauto-*・shorthand、WPT/runner/公開ABI/`plan/master.md`変更なし。p221 whole/p189/p173全件100%は本部分基準外。[CSS Grid §7.7](https://www.w3.org/TR/css-grid-2/#propdef-grid-auto-flow)、[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。列/denseのlayout未対応は後続の明示的残差とする。

## q1130-i01 結果（2026-10-14 10:04 JST）

Event ws074-p221-q1130-partial-clear-20261014: [証拠](evidence/20261014-q1130/README.md)。`grid-auto-flow`のrow/column/dense組合せをparse/style/CSSOMで保持・正規化し、initial/unset/inheritを確認。固定valid/computed各0→7/7 subtest・whole FAIL→PASS、invalid3/3 whole PASSと旧whole PASS4件維持、後退0。最小host・以前の3 fixture・CSS51/JS14/Grid48・warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865。列/dense実配置は未実装・後続別scope。q1130部分cleared、p221/p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1132-i01 有限部分範囲（2026-10-14 10:09 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分のq1132（11:39 JSTまで）に適用。`grid-auto-columns/rows`の単一`auto`または非負pxをCSS parser/style/指定・computed CSSOMへ通し、initial/unset/inheritを保持。最小host、固定valid/computed/invalidの対象subtest、CSS/JS、warning0 build。他のtrack値/複数・layout/WPT/runner/公開ABI/`plan/master.md`変更なし。p221 whole/p189/p173全件100%は本部分基準外。[CSS Grid §7.6](https://www.w3.org/TR/css-grid-2/#propdef-grid-auto-columns)、[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。i01出力をp222 q1132-i02が利用する。

## q1132-i01 結果（2026-10-14 10:13 JST）

Event ws074-p221-q1132-i01-partial-clear-20261014: [証拠](evidence/20261014-q1132/README.md)。単一auto/非負pxのauto-columns/rowsをparse/style/指定・computed CSSOMへ通し、initial/unset/inheritを確認。固定valid各0→3/30、computed各0→3/25 subtest、invalid列16/16・行15/15 whole PASS維持、後退0。最小host・CSS51/JS14・warning0 build。q1132-i01部分cleared、p222-i02へ検証済みCSS stateを渡す。p221/p189/p173全件100%未達、共有投影/GitHub publication保留。
