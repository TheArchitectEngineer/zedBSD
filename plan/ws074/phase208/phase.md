<!-- awesome-plan project=zedbsd record=ws074-p208 -->

# ws074-p208: Flex base and hypothetical size separation for wrap/grow

Status: cleared（2026-10-14 q1061-i01）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p190 q1060](../phase190/evidence/20261014-q1060/README.md)の新規PASS→FAIL `flexbox-flex-wrap-flexing.html` と[p206](../phase206/phase.md)のmin/max再配分。

## 目的と受け入れ

Flex itemのflex baseとmin/maxで制限されたhypothetical main sizeを分け、wrap判定・成長時凍結をCSS Flexbox §9.7に沿って行う。固定回帰1件をpixel exact PASSにし、q1059のtextarea3件・固定Flex10件とhost/build/Acid非増加を保つことをwhole clearance条件とする。p190/p173全件100%は別。

適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)、[CSS Flexbox 1 §9.7](https://www.w3.org/TR/css-flexbox-1/#resolve-flexible-lengths)。libbrowser内部だけ変更可能、WPT/公開ABI/`plan/master.md`は変更しない。

## q1061-i01 有限部分範囲

2026-10-14 03:24 JST、このchatのユーザーによるInterop 2025全件100%までの有限Queue事前承認を適用。最大90分（04:54 JSTまで）。固定1件のwrap/min-widthを最小ページで確認。flex baseをclamp前値として保持し、hypotheticalを別に使ってline wrap・initial grow/shrink判定、CSS §9.7のmin/max violation後のbounded freeze/distributionを必要な範囲で修正する。固定1+textarea3+Flex10、意味のあるhost、warning0 build、Acid3/2を測る。新規回帰が出れば原因を記録してunclearedで止める。全1,561件censusは含めない。

Event ws074-p208-created-20261014: q1060新規PASS→FAIL1件はmin-width100/flex-basis0の2 itemを150px幅でwrapするケース。q1058の早期min凍結が各行の成長を止めて赤い50px帯を出している。baseとhypotheticalを分離し、p190/p173受け入れは不変、共有Master/Queue/GitHub publication保留。

## q1061-i01 結果（2026-10-14）

Event ws074-p208-q1061-outcome-20261014: Flex base/hypotheticalを分離、wrapはhypothetical、成長/縮小分配はbaseを使用。min/max違反をbounded freezeで再分配。[証拠](evidence/20261014-q1061/README.md)。固定新規回帰1件は5000差画素→0、textarea等guard3件と合わせ4/4 local pixel exact PASS。固定Flex10/10・host physical21/21・CSS rule51/51・CSSOM12/12、warning0 build、Acid3差0/Acid2差1865。q1061-i01/p208 whole cleared。広域censusは次Queue、p190/p173全件100%は未達、共有投影/GitHub publication保留。
