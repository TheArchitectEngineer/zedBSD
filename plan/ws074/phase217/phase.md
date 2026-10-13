<!-- awesome-plan project=zedbsd record=ws074-p217 -->

# ws074-p217: Interop 2025 Grid alignment failures

Status: uncleared（q1088–q1089部分cleared、RTL/縦書き/他alignment未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p189 q1087 Grid recensus](../phase189/evidence/20261014-q1087/README.md)。

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
