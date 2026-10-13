<!-- awesome-plan project=zedbsd record=ws074-p198 -->

# ws074-p198: formatting context and child margin collapse

Status: uncleared（2026-10-14 q1048-i01 partial cleared、全体未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p197 q1047](../phase197/evidence/20261014-q1047/README.md)の固定縦方向7 FAILと参照ページ配置差。p190 whole clearanceは不要。

## 目的と受け入れ

独立したblock formatting contextを作る箱では、最初/最後の子の縦マージンを親マージンと相殺しないCSS2.2の一般規則を実装する。固定縦方向Flexbox XHTML参照の誤描画を解消し、関係host/Acid回帰を満たす。固定7件local PASSと関係fixtureをwhole clearance条件とする。p197のFlexbox配置、p190 runner、p173全件100%は別の受け入れ。

適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)、[CSS2.2 §8.3.1](https://www.w3.org/TR/CSS22/box.html#collapsing-margins)・[§9.4.1](https://www.w3.org/TR/CSS22/visuren.html#block-formatting)。libbrowser内部のみ変更し、公開ABI/Wayland境界・WPT・`plan/master.md`を変更しない。

## q1048-i01 の有限部分範囲

2026-10-14 02:12 JST、ユーザーの事前承認を適用、最大90分（03:42 JSTまで）。固定`flexbox-justify-content-vert-001a.xhtml`と参照のbox geometryを比較し、float BFCにおける親/先頭子のmargin相殺を局所fixtureで再現する。既存`block_owns_context`を用いて最初/最後の子との相殺を止める最小修正を試み、正常flowの相殺は維持する。固定10 IDの同条件比較、host warning0 build、関係host/Acid差画素非増加を確認する。縦方向の別原因や無関係なmargin/layout全再設計、WPT/公開ABI/Master変更は含めない。回帰する/根因が別なら戻してuncleared証拠を保存する。

Event ws074-p198-created-20261014: q1047後のvert001a差はtest側flex子でなく、reference側のfloat BFC最初の子のmargin-top190pxが0位置へ相殺されたこと。CSS2.2の一般規則として[p197](../phase197/phase.md)のFlexbox配置から分離。p190/p173受け入れは不変。GitHub publication保留。

## q1048-i01 結果（2026-10-14 02:14 JST）

**partial scope cleared、whole uncleared。** `block_owns_context`に従い独立formatting contextの最初/最後の子との親margin相殺を抑止。normal flow相殺は保持。[証拠](evidence/20261014-q1048/README.md)。float/normal top/bottom geometry4/4、WPT test/reference再layout14/14、host build warning0、Acid3差0・Acid2差1865。固定10 IDは3 PASS/7 FAIL/0 timeoutで数は不変だが、vert001a差22,000→5,600、vert006差27,820→5,560。vert001b差10,240→22,520は共通参照が修正された一方、test側min-height別欠陥が残るため。先行PASSの喪失はない。p198 wholeの7 PASS未達、p190/p173全件100%未達。

Event ws074-p198-q1048-outcome-20261014: q1048-i01はCSS2.2の一般規則と局所回帰を満たすpartial cleared。残りpixel差を別要因へ分離。GitHub publication保留。
