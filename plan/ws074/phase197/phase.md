<!-- awesome-plan project=zedbsd record=ws074-p197 -->

# ws074-p197: Flexbox justify-content overflow positioning

Status: uncleared（2026-10-14 q1047-i01 partial cleared、全体未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p196 q1046](../phase196/evidence/20261014-q1046/README.md)のXHTML描画と固定10件の画素baseline。p190 whole clearanceは不要。

## 目的と受け入れ

固定Interop 2025のFlexbox justify-content XHTMLで、main-axis overflow時の配置をCSS Flexbox/Box Alignmentの一般規則に合わせ、固定9 FAILの描画差を解消する。各修正でWPT同一条件の参照比較とAcid3/Acid2の非増加を確認する。全9件local PASS・関連host回帰がwhole clearance条件。p190の公式環境/runner coverageとp173全件100%は別。

適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)、[CSS Flexbox §8.2](https://www.w3.org/TR/css-flexbox-1/#justify-content-property)、[CSS Align §4.3](https://www.w3.org/TR/css-align-3/#distribution-values)。libbrowser内部のみ変更し、公開ABI/Wayland境界・WPT・`plan/master.md`を変更しない。

## q1047-i01 の有限部分範囲

2026-10-14 02:09 JST、ユーザーの事前承認を適用、最大90分（03:39 JSTまで）。固定`flexbox-justify-content-horiz-003.xhtml`/`004.xhtml`の差分を負の余白と配置値別に切り分ける。`layout/flex.c` のmain-axis残余が負のとき、end/centerのoverflowとdistributed valuesのfallbackという単一一般規則だけを最小修正する。最小host fixture、2 IDと固定10 IDの同条件再測定、host warning0 build、Acid3/Acid2差画素非増加を確認する。縦方向の別原因、全Flexbox再設計、WPT/公開ABI/Master変更は含めない。改善しない/回帰する場合は戻してunclearedで証拠を保存する。

Event ws074-p197-created-20261014: q1046でtimeoutを解消した固定10件は1 local PASS/9 FAIL。horiz003参照と現状の`flex_place_line`で、負の余白を0へ切り上げる処理がend/centerのはみ出しを消すことを確認。p196のXHTML lifecycle、p190 runner、p173全件目標は不変。GitHub publication保留。

## q1047-i01 結果（2026-10-14 02:10 JST）

**partial scope cleared、whole uncleared。** 負のmain-axis余白をend/centerで保持し、space-between/around/evenlyは分配不能時にstartへfallback。固定横方向2 IDは双方local PASSへ改善し、固定10件は1→3 PASS/7 FAIL/0 timeout。[証拠](evidence/20261014-q1047/README.md)。geometry fixture5/5、host build warning0、Acid3差0/Acid2差1865、Python compile/diff check PASS。残り縦方向7 IDは次の調査へ。p190/p173全件100%未達。Event ws074-p197-q1047-outcome-20261014: q1047-i01 partial cleared、wholeは7 FAILでuncleared。GitHub publication保留。
