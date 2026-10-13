<!-- awesome-plan project=zedbsd record=ws074-p206 -->

# ws074-p206: residual Flex vertical-row and textarea reftest regressions

Status: cleared（2026-10-14 q1058-i01 partial、q1059後続証拠で縦textarea画素差0）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p190 q1056](../phase190/evidence/20261014-q1056/README.md)の旧PASS→FAIL8件と[p205](../phase205/phase.md)で回復したgap5件。

## 目的と受け入れ

旧PASS→FAIL8件の残り3件（`flex-direction-row-vertical.html`、`flexbox-basic-textarea-horiz-001.xhtml`、`flexbox-basic-textarea-vert-001.xhtml`）を、実画像/DOM/幾何とreferenceで比較し、一般的な描画欠陥を特定して直す。固定3件のpixel exact PASS、固定Flex10件・CSS host・warning0 build・Acid3/2非増加をwhole clearance条件とする。個別原因が異なれば調査結果を分離し、同一Queue内で無理に拡張しない。p190/p173全件100%は別。

適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)、[CSS Flexbox 1](https://www.w3.org/TR/css-flexbox-1/)、[CSS Writing Modes 4](https://www.w3.org/TR/css-writing-modes-4/)。libbrowser内部だけを変更可能とし、WPT/公開ABI/`plan/master.md`は変更しない。

## q1058-i01 有限部分範囲

2026-10-14 03:06 JST、このchatのユーザーによるInterop 2025全件100%までの有限Queue事前承認を適用。最大90分（04:36 JSTまで）。固定3件を同条件で画像・DOM・幾何再測定し、referenceとの差を局所fixtureへ落とす。縦書きrow方向またはtextarea flex itemの一般的欠陥を確認できた場合だけ、libbrowser内部を最小修正する。固定3件・固定Flex10件、意味のあるhost、warning0 build、Acid3/2を測る。原因不明/異なる大規模scopeなら部分証拠と残余を記録してunclearedで止める。全1,561件censusやWPT/runner変更は含めない。

Event ws074-p206-created-20261014: q1057で旧8件中gap5件を回復。残る3件はvertical row1件とtextarea2件で原因未確認。p190/p173受け入れは不変、共有Master/Queue/GitHub publication保留。

## q1058-i01 結果（2026-10-14）

Event ws074-p206-q1058-outcome-20261014: auto主軸のRTL rowをused extent内でreverseし、min/maxで凍結したflex itemの残余空間を再配分。固定旧回帰3件は0→2 local pixel exact PASS、縦textarea1件は198差画素。[証拠](evidence/20261014-q1058/README.md)。host physical21/21・CSS rule51/51・CSSOM12/12、warning0 build、固定Flex10/10 PASS、Acid3差0/Acid2差1865。残1件は小数寸法のdotted border/文字差を別範囲で調査。q1058-i01 partial cleared、p206 whole uncleared、p190/p173全件100%未達。共有投影/GitHub publication保留。

## q1059後続判定（2026-10-14）

Event ws074-p206-q1059-followup-20261014: [p207](../phase207/phase.md)のfont shorthand resetとtextarea描画端pixel丸めで旧残1件の差198→0。[後続証拠](../phase207/evidence/20261014-q1059/README.md)。固定旧3件が3/3 local pixel exact PASS、固定Flex10とAcid回帰を確認。q1058当時のpartial outcomeは保持し、p206 wholeを後続証拠でcleared。p190/p173全件100%は未達、共有投影/GitHub publication保留。
