<!-- awesome-plan project=zedbsd record=ws074-p217 -->

# ws074-p217: Interop 2025 Grid alignment failures

## q1141-i01 有限部分範囲（2026-10-14 10:49 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（12:19 JSTまで）に適用。q1140固定Grid502中alignment残192 FAILのfirst failed callbackを全件抽出し、file family/width/height/offset症状、既対応と未対応の差、最大3の代表WPT sourceとlayout経路を分類する。一般的な次の有限実装範囲・回帰条件を記録する。読取のみ、browser/runner/WPT/公開ABI/`plan/master.md`変更なし、Acid/全Grid再測定なし。p217 whole/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1141-i01 結果（2026-10-14 10:49 JST）

Event ws074-p217-q1141-partial-clear-20261014: [証拠](evidence/20261014-q1141/README.md)。固定alignment残192 FAILのfirst callbackを全件分類。初回症状width110/offsetLeft37/offsetTop26/height18/other1。vertical-lr/rl stretch各16件は論理Grid軸を物理x/yへ写していないことが共通候補、positioned34件の縦subtestも関連候補。baseline43件は独立調査を要す。q1141診断部分cleared、p217/p189/p173全件100%未達。browser/WPT/runner/ABI/Master/Acid変更なし、共有投影/GitHub publication保留。

Status: uncleared（q1088–q1089部分cleared、RTL/縦書き/他alignment未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p189 q1087 Grid recensus](../phase189/evidence/20261014-q1087/README.md)。
Scoped output dependency: alignment style-changesの`grid` shorthandによる明示track sizingは[p218](../phase218/phase.md)の対応出力を要する。p217の他alignment調査/修正は独立に進められる。
Scoped output dependency: alignment style-changesの空子baselineは[p219 q1096](../phase219/evidence/20261014-q1096/README.md)の出力を用いる。内容line/auto行/縦書きは依然未達。
Scoped output dependency: alignment style-changes007/008のvertical-lr文字line geometryは[p220](../phase220/phase.md)の検証済み出力を待つ。空子vertical-rlと水平alignmentは独立。

## 目的と受け入れ

固定Grid testharnessのalignment配下FAIL206件について、一般的なCSS Grid alignment/layoutまたはDOM geometry欠陥を原因別に切り分け、仕様と再現に基づく修正と同一固定条件の再測定を行う。旧PASSとAcid3差0を維持し、残るFAILを正確に記録する。206件全PASS/全Grid502件全PASSは実測でのみ認める。p189/p173全件100%は別受け入れ。

## 設計・境界

関連する `userland/desktop/libbrowser/layout/`、CSS、DOM geometry、`plan/ws074/tests/`を各Queueの有限scopeで扱う。WPT本体/runnerの採点を改変しない。公開ABI/`plan/master.md`不変更。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。code変更後は既存WS074 near-final full-standard conformance Phaseで再確認する。

Event ws074-p217-addition-20261014: [p189 q1087](../phase189/evidence/20261014-q1087/README.md)の固定Grid399 FAIL中、alignment配下206件が最大群。原因を断定せず一般欠陥の調査Phaseを追加。p189/p173の分母・受け入れ不変。共有投影/GitHub publication保留。

## q1088-i01 有限部分範囲（2026-10-14 07:32 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（09:02 JSTまで）に適用。q1087のalignment FAIL206件をsubtest messageとWPT sourceで分類し、代表2–3件の期待/実測値と現行Grid layout・DOM geometry経路を追う。必要なら最小host HTMLで再現、一般欠陥候補・影響範囲・次の有限実装scopeを証拠化。製品browser/runner/WPT/公開ABI/`plan/master.md`変更なし。固定Acid3・全Grid再測定はこの調査では実行しない。p217 whole/p189/p173全件100%は本部分受け入れに含めない。

## q1088-i01 結果（2026-10-14 07:35 JST）

Event ws074-p217-q1088-partial-clear-20261014: [証拠](evidence/20261014-q1088/README.md)でalignment FAIL206件/failed subtest1109件を分類し、代表3系列を追跡。絶対配置grid子のstatic_x/y=0固定は最小HTMLでgrid area(108,88)に対し子(8,8)を再現。縦書きの無条件stretchと`grid` shorthand未認識は別欠陥。次の有限scopeは有効な明示grid線を持つ水平LTR絶対子のstatic area原点から選ぶ。q1088-i01 partial cleared、p217 whole/p189/p173全件100%未達。製品browser/runner/WPT/ABI/Master無変更、共有投影/GitHub publication保留。

## q1089-i01 有限部分範囲（2026-10-14 07:35 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（09:05 JSTまで）に適用。位置指定のある水平LTR grid containerの直接absolute子について、両axis inset auto、行・列が既存明示track内の有効lineで確定する場合、track size/gap/justify-contentからstatic area原点を計算。out-of-flow子をtrack sizing/auto placementへ入れず、非対象static位置・明示insetを維持する。最小HTML、固定WPT水平代表、host JS14/警告0 build/Acid3差0・Acid2非増加を検証。RTL/縦書き、auto/無効line、full self-alignment、Grid502全再測定、WPT/runner/公開ABI/`plan/master.md`変更なし。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。p217 whole/p189/p173全件100%は本部分基準外。

## q1089-i01 結果（2026-10-14 07:39 JST）

Event ws074-p217-q1089-partial-clear-20261014: 水平LTR/有効明示areaの絶対配置子だけ、track size/gap/justify-contentを使いstatic原点を設定。[証拠](evidence/20261014-q1089/README.md)。最小hostはarea子(108,88)、明示inset(15,17)、無効line(8,8)を確認。固定WPT代表2件の水平subtest各1件がFAIL→PASS、wholeはRTL/縦書きの3件FAILで未達。旧PASS module代表はPASS維持。classic JS14/14、warning0 build、Acid3差0・Acid2差1865。q1089-i01 partial cleared、p217 whole/p189/p173全件100%未達、共有投影/GitHub publication保留。

## q1090-i01 有限部分範囲（2026-10-14 07:40 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（09:10 JSTまで）に適用。q1089固定代表WPTのRTL subtest期待340/230対実0、Grid明示track・direction/justify-selfと既存static_x/position_placeの評価順を追跡。固定幅とauto幅の小HTMLをhostで再現し、RTL grid areaと幅確定後start alignmentを一般的に表す内部設計、次の有限実装scope・リスクを証拠化する。読み取り調査と証拠fixtureのみ。製品browser/runner/WPT/公開ABI/`plan/master.md`変更なし、Acid/全Grid再測定なし。p217 whole/p189/p173全件100%は本部分基準外。

## q1090-i01 結果（2026-10-14 07:41 JST）

Event ws074-p217-q1090-partial-clear-20261014: [証拠](evidence/20261014-q1090/README.md)。最小RTL 400px Gridの固定20px/auto24px absolute子は両方物理x=8、inline-start期待x=388/384。Gridが子の幅確定前に左端static_xを置き、position_placeが幅確定後も左端として読むことが原因。次はprivate right-edge static metadataを介し、幅確定後に右端から配置する限定実装。q1090-i01部分cleared。p217 whole/p189/p173全件100%未達。製品browser/runner/WPT/ABI/Master無変更、共有投影/GitHub publication保留。

## q1091-i01 有限部分範囲（2026-10-14 07:42 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（09:12 JSTまで）に適用。水平RTL Gridの固定trackと余白で`justify-content:normal`をinline-start（右）へ配置し、位置指定grid直接absolute子で両axis inset auto、有効明示row/column、inline start alignmentに限りprivate right-edge static metadataを幅確定後の配置へ使う。固定/auto幅最小HTML、q1089 LTR/明示inset/無効line、固定WPT水平RTL2件、旧PASS代表、classic JS14、warning0 host build、Acid3差0・Acid2非増加を検証。縦書き/他alignment/Grid502全再測定、runner/WPT/公開ABI/`plan/master.md`変更なし。p217 whole/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1091-i01 結果（2026-10-14 07:48 JST）

Event ws074-p217-q1091-partial-clear-20261014: [証拠](evidence/20261014-q1091/README.md)。RTL normalの余白を右へ配し、有効明示areaのabsolute子で幅確定後に右端からstatic配置。最小RTL5ケースとq1089 LTR、Grid48/48/physical21/21、classic JS14/14、warning0 host build、Acid3差0・Acid2差1865。固定WPT代表2件の水平RTL subtestが各FAIL→PASS、旧PASS代表100/100維持。wholeは縦書き2 subtestがFAIL。q1091-i01部分cleared、p217 whole/p189/p173全件100%未達、共有投影/GitHub publication保留。

## q1092-i01 有限部分範囲（2026-10-14 07:50 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（09:20 JSTまで）に適用。q1091固定WPT代表2件に残るvertical-lr/rl subtestのoffset/width/height期待と実測を抽出。対応する最小縦書きGridでin-flowとabsolute子をhost実測し、writing-modeの軸/track対応と現行layout/grid/position経路を追い、次の有限修正scope・リスクを証拠化する。読み取り調査/証拠fixtureのみ。製品browser/runner/WPT/公開ABI/`plan/master.md`変更なし、Acid/全Grid再測定なし。p217 whole/p189/p173全件100%は本部分基準外。

## q1092-i01 結果（2026-10-14 07:50 JST）

Event ws074-p217-q1092-partial-clear-20261014: [証拠](evidence/20261014-q1092/README.md)。固定WPT縦書きは両代表で最初にwidth期待10/実60。最小vertical-lr Gridでin-flow100×13・absolute24×13、テキストは横方向の幅24を維持。Gridは列/行を物理x/yに対応させ非水平を強制stretch、inline文字組みはwriting-modeを使わない。幅だけの局所修正では全条件を満たさないため、文字組み・Grid軸・absolute areaを分けて有限実装する設計を次候補にし、独立のGrid shorthand等へも進める。q1092-i01部分cleared、p217 whole/p189/p173全件100%未達。製品browser/runner/WPT/ABI/Master無変更、共有投影/GitHub publication保留。

Event ws074-p217-p218-shorthand-output-20261014: [p218](../phase218/phase.md)を独立追加。p217 alignment style-changes 8件の`grid` shorthand指定によるtrack sizingはp218の該当出力に依存する。p217全206件の受け入れは不変で、他alignment修正は独立。q1094は字面83 FAILを上限として記録し、原因同一性は断定しない。[p218調査証拠](../phase218/evidence/20261014-q1094/README.md)。

Follow-up ws074-p217-p218-q1095-output-20261014: [p218 q1095](../phase218/evidence/20261014-q1095/README.md)で明示`grid: rows / columns`のtrack sizing出力を検証。固定alignment style-changes-001は幅失敗が解消し3/6 subtest PASSになったが、baseline縦offset差でwholeはFAIL。p217の他style-changesと全206件は未測定/未達で、p217受け入れは不変。

Follow-up ws074-p217-p219-q1096-output-20261014: [p219 q1096](../phase219/evidence/20261014-q1096/README.md)で空子の固定行合成baselineを検証。style-changes-001は3/6→6/6 subtest、whole FAIL→PASS。p217 alignment残205件の同条件全再測定は未実施で、p217 whole受け入れは未達。

Follow-up ws074-p217-p219-q1097-eight-20261014: [p219 q1097](../phase219/evidence/20261014-q1097/README.md)でstyle-changes固定8件を同条件測定し、q1093の0/8 whole PASS・0/48 subtestから2/8 whole PASS・20/48 subtestへ改善。旧PASS後退0。残6件は文字line baseline2と縦書き4で、p217全206件の再測定/受け入れは未達。

Follow-up ws074-p217-p219-q1099-eight-20261014: [p219 q1099](../phase219/evidence/20261014-q1099/README.md)で固定8件を同条件再測定し4/8 whole PASS・26/48 subtest PASS。横書き文字line2件が新規whole PASS、残4件は縦書き。p217全206件の再測定/受け入れは未達。

## q1100-i01 有限部分範囲（2026-10-14 08:15 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（09:45 JSTまで）に適用。q1099残4 vertical-rl/lr style-changes IDの48中22失敗から期待/実際の初回不一致と後続assertを分類。固定WPT sourceとGrid layout/text/geometry経路を追い、必要ならhost最小HTMLで縦書き空子/文字子を実測。次の独立有限実装境界を記録。browser/runner/WPT/公開ABI/`plan/master.md`変更なし、Acid/全Grid再測定なし。p217 whole/p189/p173全件100%は本部分基準外。

## q1100-i01 結果（2026-10-14 08:16 JST）

Event ws074-p217-q1100-partial-clear-20261014: [証拠](evidence/20261014-q1100/README.md)。003/004は空vertical-rl子のwidth/heightが合い、失敗はoffsetLeftのみ。baseline群ではx40、50px子除外後は残群x70・除外子x40。007/008はvertical-lr文字子が初回width期待20/40/30対実100で全12 subtest失敗。前者の限定された縦書き空子baseline配置を次の有限修正とし、後者は縦文字layout/論理幅の別設計が必要。q1100部分cleared、p217/p219/p189/p173全件100%未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

## q1101-i01 有限部分範囲（2026-10-14 08:16 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（09:46 JSTまで）に適用。水平Gridの固定pixel単一column内の直接空子、vertical-rl、明示pixel幅、column span1、`justify-items/self:baseline`群でborder-leftを共有し、最大右側extentをarea終端へ合わせる。`justify-self:end`子は除外。auto幅/文字/縦container/他alignmentは対象外。最小2行・margin変化fixture、固定style-changes003/004、旧001/002/005/006と100-subtest代表、Grid/CSS/JS、warning0 build、Acid3差0・Acid2非増加。WPT/runner/公開ABI/`plan/master.md`変更なし。p217 whole/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1101-i01 結果（2026-10-14 08:19 JST）

Event ws074-p217-q1101-partial-clear-20261014: [証拠](evidence/20261014-q1101/README.md)。水平Grid固定1列の空vertical-rl子でborder-left baselineを共有。最小2gridのmargin/endケースPASS、固定003/004各1/6→6/6 subtest・whole FAIL→PASS。旧001/002/005/006と100-subtest PASS維持。Grid48/CSS51/JS14、warning0 build、Acid3差0・Acid2差1865。q1101部分cleared、p217/p219/p189/p173全件100%未達。文字入りvertical-lr 007/008と一般縦書きは残る。共有投影/GitHub publication保留。

Follow-up ws074-p217-q1102-eight-20261014: [p189 q1102](../phase189/evidence/20261014-q1102/README.md)の固定8件再測定は6/8 whole PASS・36/48 subtest PASS。残007/008はvertical-lr文字子width期待20/40/30対実100。p217全206件の受け入れは未達。

## q1103-i01 有限部分範囲（2026-10-14 08:22 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（09:52 JSTまで）に適用。固定007/008のvertical-lr Ahem文字幅期待/実際、q1092最小host、CSS writing-mode/line/paint/DOM geometry経路を追い、1文字sideways inlineの最小実装境界と次の検証fixtureを設計する。読取と証拠のみ。browser/runner/WPT/公開ABI/`plan/master.md`変更なし、Acid/全Grid再測定なし。p217 whole/p189/p173全件100%は本部分基準外。

## q1103-i01 結果（2026-10-14 08:23 JST）

Event ws074-p217-q1103-partial-clear-and-p220-add-20261014: [証拠](evidence/20261014-q1103/README.md)。固定007/008はvertical-lr文字子width期待20/40/30対実100。Gridが非水平子をstretchし、inlineは横x方向で行組み、paint/bounds/hitも横書き座標、CSS `text-orientation`無し。Grid単独の修正では不十分なため縦inline geometryを[p220](../phase220/phase.md)へ分離し、p217の007/008文字alignmentだけその出力へ依存させる。q1103部分cleared、p217/p220/p189/p173全件100%未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

Follow-up ws074-p217-q1105-full-grid-20261014: [p189 q1105](../phase189/evidence/20261014-q1105/README.md)の固定Grid502全再測定ではalignmentは206→200 whole FAIL。旧whole PASS後退0、別のfr/gutter1 subtest後退。p217全206受け入れは未達。

## q1106-i01 有限部分範囲（2026-10-14 08:37 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（10:07 JSTまで）に適用。q1105固定alignment200 whole FAILをファイル系列/初回subtest症状とWPT sourceで分類。縦書き・baseline・positioned/size等を分け、上位1〜2代表を現行layout経路へ結び、次の有限実装候補を決める。読取/証拠のみ。browser/runner/WPT/公開ABI/`plan/master.md`/Acid変更なし。p217 whole/p189/p173全件100%は本部分基準外。

## q1106-i01 結果（2026-10-14 08:38 JST）

Event ws074-p217-q1106-partial-clear-20261014: [証拠](evidence/20261014-q1106/README.md)。固定alignment200 whole FAILの初回失敗はwidth79、offsetLeft65、offsetTop37、height18、scrollWidth1。positioned/縦stretchは別系統。auto margin系列7件は各2 subtest offsetTop差で、40%/60%行とauto上下marginの再計算が共通。Gridが行開始へ置いており、CSS Grid §11.2の正の余白吸収が欠落。次はこの独立した有限修正を選ぶ。q1106部分cleared、p217/p189/p173全件100%未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

## q1107-i01 有限部分範囲（2026-10-14 08:39 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（10:09 JSTまで）に適用。水平Gridのrow span1・definite pixel/percent行でin-flow子の上下auto marginを最終item/area height後の正の余白へ配分（両auto等分、片側auto全量）。その軸のstretch/self alignmentを抑え、溢れはauto0。最小双方/片側/overflow fixture、固定auto-margins001〜007、旧Grid/PASS、CSS/JS、warning0 build、Acid3差0・Acid2非増加。縦書き/row span>1/公開ABI/WPT/runner/`plan/master.md`変更なし。p217 whole/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1107-i01 結果（2026-10-14 08:40 JST）

Event ws074-p217-q1107-partial-clear-20261014: [証拠](evidence/20261014-q1107/README.md)。水平Grid definite行で上下auto marginを最終item高さ後の正の余白へ配分。最小6ケースPASS、固定auto-margins001〜007は全7 whole FAIL→PASS・計14 subtest FAIL→PASS、逆0。旧style-changes001と100-subtest代表PASS維持、Grid48/CSS51/JS14、warning0 build、Acid3差0・Acid2差1865。q1107部分cleared、p217/p189/p173全件100%未達。auto行/row span/縦書き残、共有投影/GitHub publication保留。

## q1108-i01 有限部分範囲（2026-10-14 08:41 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（10:11 JSTまで）に適用。q1105固定column-axis/row-axis positioned-items各17件の水平LTR/RTLと縦書きsubtestを分け、水平初回失敗のalign/justify値・期待/実座標をWPT sourceと`grid_static_position`/`position_place`で追う。幅確定後area内整列に必要なprivate metadata境界と次の有限修正を記録。読取/証拠のみ、browser/runner/WPT/公開ABI/`plan/master.md`/Acid変更なし。p217 whole/p189/p173全件100%は本部分基準外。

## q1108-i01 結果（2026-10-14 08:42 JST）

Event ws074-p217-q1108-partial-clear-20261014: [証拠](evidence/20261014-q1108/README.md)。固定positioned列軸17/行軸17の水平subtestは20/34・26/34失敗、縦書き68/68はwidth期待10/実60。列軸002/005/007/009/011の水平10失敗はend/center系`align-self`のoffsetTop期待140/70・実0が共通。Gridはarea topのみstatic_yへ保存し、position_placeは子高さ確定後もそのまま使用。次はprivate area height/alignmentを渡す限定実装。q1108部分cleared、p217/p189/p173全件100%未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

## q1109-i01 有限部分範囲（2026-10-14 08:43 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（10:13 JSTまで）に適用。水平LTR/RTL Gridの直接absolute子、有効明示row/column area、四inset auto、水平子について`align-self`または親`align-items`のend/center/flex-end/self-endをblock軸へ反映。最終子高さとprivate area情報を使う。最小fixture、固定WPT column-axis positioned-items 002/005/007/009/011の水平subtest、旧PASS代表、Grid/CSS/JS、warning0 build、Acid3差0・Acid2非増加で検証。縦書き、inline軸整列、他CSS配置、runner/WPT/公開ABI/`plan/master.md`変更なし。p217 whole/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1109-i01 結果（2026-10-14 08:47 JST）

Event ws074-p217-q1109-partial-clear-20261014: [証拠](evidence/20261014-q1109/README.md)。水平Gridの確定areaで絶対子のblock-axis end/center系位置を最終子高さ後に解決。最小end/center/explicit inset/無効line/RTL親整列fixture PASS。固定列軸5件の横書き10 subtest FAIL→PASS、縦書き10 subtestはwidth期待10/実60でFAIL継続。旧PASS2件、Grid48/CSS51/JS14、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865。q1109部分cleared、p217/p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1110-i01 有限部分範囲（2026-10-14 08:48 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（10:18 JSTまで）に適用。水平LTR/RTL Gridの直接absolute子、有効明示area、四inset auto、水平子について`justify-self`または親`justify-items`のend/center/flex-end/self-endをinline軸へ反映。最終子幅とprivate area情報を使う。最小LTR/RTL/明示inset/無効line、固定row-axis positioned-items002/005/007/009の水平subtest、旧PASS代表、Grid/CSS/JS、warning0 build、Acid3差0・Acid2非増加で検証。縦書き、left/right/self-start、他CSS配置、runner/WPT/公開ABI/`plan/master.md`変更なし。p217 whole/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1110-i01 結果（2026-10-14 08:50 JST）

Event ws074-p217-q1110-partial-clear-20261014: [証拠](evidence/20261014-q1110/README.md)。水平Gridの確定areaで絶対子のinline-axis end/center系位置を最終子幅後に解決。最小LTR/RTL/明示inset/無効line/子の異方向self-end fixture PASS。固定行軸4件の横書き8 subtest FAIL→PASS、縦書き8 subtestはwidth期待10/実60でFAIL継続。旧PASS2件、Grid48/CSS51/JS14、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865。q1110部分cleared、p217/p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1111-i01 有限部分範囲（2026-10-14 08:51 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（10:21 JSTまで）に適用。q1110後の固定row-axis positioned-items003/004/008/010/011を同条件再測定し、水平LTR/RTL各subtestの初回差とWPTの方向指定を抽出。left/right/self-start/self-endの物理辺と現行private static-area経路を照合して次の有限修正を設計。読取/証拠のみ、browser/runner/WPT/公開ABI/`plan/master.md`/Acid変更なし。p217 whole/p189/p173全件100%は本部分基準外。

## q1111-i01 結果（2026-10-14 08:51 JST）

Event ws074-p217-q1111-partial-clear-20261014: [証拠](evidence/20261014-q1111/README.md)。固定行軸003/004/008/010/011の水平10 subtest中6 FAIL、011のself-endはq1110で両PASSへ改善。物理left/rightと子自身のdirectionを使うself-startの未配置が共通。現在のGridはRTLでそれらを静的area経路から除外、LTRではstartへ置く。次はprivate area widthと最終子幅で左/右辺を計算。縦書き10 subtestはwidth差、全5 whole FAIL。q1111部分cleared、p217/p189/p173全件100%未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

## q1112-i01 有限部分範囲（2026-10-14 08:52 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（10:22 JSTまで）に適用。水平LTR/RTL Gridの直接absolute子、有効明示area、四inset auto、水平子について`justify-self`または親`justify-items`のleft/right/self-startをprivate area幅と最終子幅で物理辺に配置。固定row-axis positioned-items003/004/008/010/011の水平subtest、最小LTR/RTL/子の異方向/明示inset/無効line、旧PASS代表、Grid/CSS/JS、warning0 build、Acid3差0・Acid2非増加で検証。縦書き、他CSS配置、runner/WPT/公開ABI/`plan/master.md`変更なし。p217 whole/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1112-i01 結果（2026-10-14 08:53 JST）

Event ws074-p217-q1112-partial-clear-20261014: [証拠](evidence/20261014-q1112/README.md)。水平Gridの絶対子で物理left/rightと子方向self-startをprivate area端へ配置。最小fixture PASS。固定行軸5件で横書き6 subtest FAIL→PASS、横書き10/10 PASS。縦書き10 subtestはwidth期待10/実60でFAIL継続、whole5件FAIL。旧PASS2件、Grid48/CSS51/JS14、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865。q1112部分cleared、p217/p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1113-i01 有限部分範囲（2026-10-14 08:54 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（10:24 JSTまで）に適用。q1112後の固定column-axis/row-axis positioned-items012–016計10件を同条件再測定。水平LTR/RTL各subtestの初回差、親/子direction・明示area・align/justify値をWPT sourceと現行static area/position_placeで照合し、次の有限修正候補を確定。読取/証拠のみ、browser/runner/WPT/公開ABI/`plan/master.md`/Acid変更なし。p217 whole/p189/p173全件100%は本部分基準外。

## q1113-i01 結果（2026-10-14 08:56 JST）

Event ws074-p217-q1113-partial-clear-20261014: [証拠](evidence/20261014-q1113/README.md)。固定列軸/行軸012–016計10件の横書き20/20・縦書き20/20 subtestがFAIL。行軸はauto線をpadding edgeでなく最初のtrackへ潰し100pxのcontent shift差、列軸はblock軸content center shift未伝搬とRTL offsetLeft大差が重なる。次は行軸auto境界の有限修正、列軸は別調査。q1113部分cleared、p217/p189/p173全件100%未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

## q1114-i01 有限部分範囲（2026-10-14 08:57 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（10:27 JSTまで）に適用。水平LTR/RTL Gridの直接absolute子、四inset auto、`grid-column:auto / 1`のauto inline-start境界をコンテナpadding辺へ配置。justify-content:center等によるtrack shiftはauto辺へ適用せず、明示終端線/area幅とq1110–q1112整列を維持。最小LTR/RTL/各整列fixture、固定row-axis012–016の最初の横書きassertと次失敗、旧PASS代表、Grid/CSS/JS、warning0 build、Acid3差0・Acid2非増加で検証。`2 / auto`・`auto / auto`・縦書き、runner/WPT/公開ABI/`plan/master.md`変更なし。p217 whole/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1114-i01 結果（2026-10-14 08:59 JST）

Event ws074-p217-q1114-partial-clear-20261014: [証拠](evidence/20261014-q1114/README.md)。`grid-column:auto / 1`のauto始端をpadding辺へ設定し、track中央寄せの100px shiftを除外。最小LTR/RTL start/end/centerと明示線control PASS。固定行軸012–016の水平最初の100px差が消え、次の子/軸不一致へ進んだがwhole5件FAIL。旧PASS2件、Grid48/CSS51/JS14、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865。q1114部分cleared、p217/p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1115-i01 有限部分範囲（2026-10-14 09:00 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（10:30 JSTまで）に適用。水平LTR/RTL Gridの直接absolute子、四inset auto、`grid-column:2 / auto`のauto inline-end境界をコンテナpadding辺へ配置。中央寄せ明示line始端とpadding終端間のarea幅をprivate metadataに反映。最小LTR/RTL start/end/centerと明示線control、固定row-axis012–016の次assert、旧PASS代表、Grid/CSS/JS、warning0 build、Acid3差0・Acid2非増加で検証。`auto / auto`・縦書き、runner/WPT/公開ABI/`plan/master.md`変更なし。p217 whole/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1115-i01 結果（2026-10-14 09:02 JST）

Event ws074-p217-q1115-partial-clear-20261014: [証拠](evidence/20261014-q1115/README.md)。`grid-column:2 / auto`のauto終端をpadding辺へ設定し、明示始端からのarea幅を再計算。最小LTR/RTL start/end/centerと明示線control PASS。固定行軸013/014/015の当該不一致は次の子へ進み、012/016は別残差でwhole5件FAIL。旧PASS2件、Grid48/CSS51/JS14、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865。q1115部分cleared、p217/p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1116-i01 有限部分範囲（2026-10-14 09:03 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（10:33 JSTまで）に適用。水平LTR/RTL Gridの直接absolute子、四inset auto、`grid-column:auto / auto`と有効明示row、padding/borderなしのinline areaをコンテナ幅へ設定し、row静的位置も計算。最小LTR/RTL start/end/center・明示行fixture、固定row-axis012–016の横書きsubtest、旧PASS代表、Grid/CSS/JS、warning0 build、Acid3差0・Acid2非増加で検証。padding/border、auto row、縦書き、runner/WPT/公開ABI/`plan/master.md`変更なし。p217 whole/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1116-i01 結果（2026-10-14 09:06 JST）

Event ws074-p217-q1116-partial-clear-20261014: [証拠](evidence/20261014-q1116/README.md)。水平Grid絶対子の`grid-column:auto / auto`を全幅inline areaにし、明示row原点を維持。最小LTR/RTL start/end/center・明示行fixture PASS。固定行軸012–016の横書き10 subtestがFAIL→PASS、縦書き10 subtestはwidth期待10/実60でFAIL継続。旧PASS2件、Grid48/CSS51/JS14、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865。q1116部分cleared、p217/p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1117-i01 有限部分範囲（2026-10-14 09:07 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（10:37 JSTまで）に適用。水平Grid、固定pixel height、`align-content:center`、正の未使用縦余白について、明示start rowの直接absolute子とin-flow Grid itemのrow原点へcontent shiftを加算。auto start rowは対象外のまま保つ。最小explicit row/auto row/in-flow fixture、固定column-axis012–016の最初の横書きassert・次失敗、旧PASS代表、Grid/CSS/JS、warning0 build、Acid3差0・Acid2非増加で検証。percent height/他content値/縦書き、runner/WPT/公開ABI/`plan/master.md`変更なし。p217 whole/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1117-i01 結果（2026-10-14 09:12 JST）

Event ws074-p217-q1117-partial-clear-20261014: [証拠](evidence/20261014-q1117/README.md)。水平Grid固定高さの`align-content:center`で明示行とin-flow itemの原点をshift。最小auto/明示2行とin-flow PASS。固定列軸012/015の最初のoffsetTop差が次のoffsetLeft差へ進む。auto row 013/014/016とRTL x差が残りwhole5件FAIL。旧PASS2件、Grid48/CSS51/JS14、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865。q1117部分cleared、p217/p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1118-i01 有限部分範囲（2026-10-14 09:13 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（10:43 JSTまで）に適用。水平Gridの固定pixel height、`align-content:center`、zero vertical padding/borderで、直接absolute子・四inset autoの`grid-row:auto / 1`,`2 / auto`,`auto / auto` block areaをpadding端と明示lineから算定。end/centerは最終子高さで配置。最小LTR/RTL auto境界/明示行fixture、固定column-axis012–016の横書きsubtest、旧PASS代表、Grid/CSS/JS、warning0 build、Acid3差0・Acid2非増加で検証。percent height/縦書き、runner/WPT/公開ABI/`plan/master.md`変更なし。p217 whole/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1118-i01 結果（2026-10-14 09:17 JST）

Event ws074-p217-q1118-partial-clear-20261014: [証拠](evidence/20261014-q1118/README.md)。水平Gridのauto row始端/終端/両端をpadding端とshift済み明示lineから計算し、end/centerを最終子高さで配置。最小LTR/RTLと明示行fixture PASS。固定列軸012–016の横書きLTR5 subtest FAIL→PASS、RTL5件はoffsetLeft期待190/実948、縦書き10もFAIL継続。旧PASS2件、Grid48/CSS51/JS14、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865。q1118部分cleared、p217/p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1119-i01 有限部分範囲（2026-10-14 09:19 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（10:49 JSTまで）に適用。固定column-axis012–016 RTLのoffsetLeft期待190/実948をlayout dumpとWPT sourceに照合し、`inline-grid` auto幅のCSS値変換、box kind/atomic、width/shrink-to-fit、Grid track/DOM geometry経路を監査。最小の次の有限実装scopeと回帰範囲を定義。読取/証拠のみ、browser/runner/WPT/公開ABI/`plan/master.md`/Acid変更なし。p217 whole/p189/p173全件100%は本部分基準外。

## q1119-i01 結果（2026-10-14 09:20 JST）

Event ws074-p217-q1119-partial-clear-20261014: [証拠](evidence/20261014-q1119/README.md)。固定RTL共通offsetLeft期待190/実948は親`inline-grid`の自動幅が250pxでなく1008pxに広がることに起因。CSS表示値のgridへの畳み込みによりatomic/intrinsic-width経路を通らない。次はdistinct enumでinline-gridを保持しGrid item/layout/width経路へ分配する有限実装。q1119部分cleared、p217/p189/p173全件100%未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

## q1120-i01 有限部分範囲（2026-10-14 09:21 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（10:51 JSTまで）に適用。`display:inline-grid`をdistinct内部display値として保持し、inline-level atomicのshrink-to-fit幅へ通しつつGrid item/layout/intrinsic width経路を維持。最小auto幅250px/explicit幅/block-grid/RTL absolute fixture、固定column-axis012–016水平subtest、旧PASS代表、CSS/JS/Grid、warning0 build、Acid3差0・Acid2非増加で検証。WPT/runner/公開ABI/`plan/master.md`変更なし。p217 whole/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1120-i01 結果（2026-10-14 09:28 JST）

Event ws074-p217-q1120-partial-clear-20261014: [証拠](evidence/20261014-q1120/README.md)。`inline-grid`を独立display値・inline atomic幅とGrid item/layout経路へ接続し、CSSOM表示値も保持。最小LTR/RTL auto250px、明示400px、block Grid1008px、RTL absolute位置190px、computed display PASS。固定列軸012–016の横書きRTL5 subtest FAIL→PASS、横書き計10/10 PASS。縦書き10 subtestはFAIL継続でwhole5件FAIL。旧whole PASS2件、Grid48/CSS51/JS14、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865。q1120部分cleared、p217/p189/p173全件100%未達。共有投影/GitHub publication保留。
