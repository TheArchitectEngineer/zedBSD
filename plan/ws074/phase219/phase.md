<!-- awesome-plan project=zedbsd record=ws074-p219 -->

# ws074-p219: Interop 2025 Grid baseline self-alignment

Status: uncleared（q1096部分cleared、他baseline未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p189 q1093 Grid census](../phase189/evidence/20261014-q1093/README.md)、[p218 q1095明示track出力](../phase218/evidence/20261014-q1095/README.md)はstyle-changes代表に限る。

## 目的と受け入れ

固定Grid alignmentのbaseline FAIL群を、仕様と最小例から一般的なbaseline set/groupの実装と再測定で減らす。p219 wholeは対象固定IDとbaseline条件を明記し、旧PASS/Acid3差0を維持して検証後にのみcleared。p217 alignment206全件、p189/p173全件100%は別受け入れ。

## 境界

`userland/desktop/libbrowser/layout/`と必要なCSS/DOM geometry、`plan/ws074/tests/`。WPT本体/runner採点、公開ABI、`plan/master.md`は変更しない。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。code変更後はWS074 near-final full-standard conformance Phaseで再確認する。

Event ws074-p219-addition-20261014: [p218 q1095](../phase218/evidence/20261014-q1095/README.md)でstyle-changes-001の幅失敗を解消し3/6 subtest PASSにした。残りはempty in-flow子のbaseline offsetが全て0という別欠陥。43件のbaseline名FAILには他条件があるため同一原因と断定せず、baselineを独立Phaseで段階実装する。p217/p189/p173受け入れ不変。

## q1096-i01 有限部分範囲（2026-10-14 08:03 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（09:33 JSTまで）に適用。水平writing Gridの同一固定行、row span1、明示高さを持つ空in-flow子の`align-items`/`align-self:baseline`だけを対象に、border-box下端合成baselineの最大値へ同group子を配置する。baseline外のend等は従来の配置、auto行/内容line/縦書きは対象外。p218 q1095の固定style-changes-001、最小複数行/子のhost、旧PASS代表、Grid/CSS/JS、warning0 build、Acid3差0・Acid2非増加で検証。WPT/runner/公開ABI/`plan/master.md`変更なし。p219 whole/p217/p189/p173全件100%は本部分基準外。

## q1096-i01 結果（2026-10-14 08:06 JST）

Event ws074-p219-q1096-partial-clear-20261014: [証拠](evidence/20261014-q1096/README.md)。水平Gridの固定行で空in-flow子のborder下端合成baseline groupを配置。2行6子の最小geometry PASS、固定style-changes-001は3/6→6/6 subtest PASS・whole FAIL→PASS、旧PASS代表100/100維持。warning0 build、Grid48/CSS51/JS14、Acid3差0・Acid2差1865。q1096-i01部分cleared、p219 whole/p217/p189/p173全件100%未達。内容line/auto行/縦書きのbaseline残、共有投影/GitHub publication保留。

## q1097-i01 有限部分範囲（2026-10-14 08:06 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（09:36 JSTまで）に適用。固定Grid alignment style-changes-001〜008の8 IDをq1093と同じWPT/localhost/固定font3種/5秒settle/20秒processで測り、q1093 rawとwhole/subtest遷移、旧PASS後退、残るbaseline条件を分類。browser/runner/WPT/公開ABI/`plan/master.md`変更なし、Acid/全Grid再測定なし。p219 whole/p217/p189/p173全件100%は本部分基準外。

## q1097-i01 結果（2026-10-14 08:08 JST）

Event ws074-p219-q1097-partial-clear-20261014: [証拠](evidence/20261014-q1097/README.md)。固定style-changes8 IDは旧0 PASS/8 FAIL・0/48 subtest→新2 PASS/6 FAIL・20/48 subtest。20 FAIL→PASS、PASS→FAIL0。003/004はvertical-rl子の水平offset、005/006は文字lineの縦baseline、007/008はvertical-lr文字幅/offsetが主な残差。q1097-i01部分cleared、p219 whole/p217/p189/p173全件100%未達。browser/runner/WPT/ABI/Master無変更、共有投影/GitHub publication保留。

## q1098-i01 有限部分範囲（2026-10-14 08:09 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（09:39 JSTまで）に適用。q1096固定行baseline groupへ水平Grid子の直接inline text先頭line baselineを追加。空子合成baselineと同group、auto height line子、row span1・固定pixel行のみ。block descendant/auto行/縦書きは対象外。最小異font-size2行fixture、固定style-changes-005/006、q1096旧PASS2件、Grid/CSS/JS、warning0 build、Acid3差0・Acid2非増加。WPT/runner/公開ABI/`plan/master.md`変更なし。p219 whole/p217/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1098-i01 結果（2026-10-14 08:11 JST）

Event ws074-p219-q1098-partial-clear-20261014: [証拠](evidence/20261014-q1098/README.md)。固定pixel行の水平Grid直接inline textで先頭line baselineをempty合成baseline groupへ追加。最小2行で文字line3件がy42.5、次行empty/文字がy148を共有し、end子は独立。固定style-changes-005/006は各3/6→6/6 subtest、whole FAIL→PASS。旧001/002と100 subtest PASS維持、Grid48/CSS51/JS14、warning0 build、Acid3差0・Acid2差1865。q1098-i01部分cleared、p219 whole/p217/p189/p173全件100%未達。縦書き4件/auto行等残、共有投影/GitHub publication保留。
