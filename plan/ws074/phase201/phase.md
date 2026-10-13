<!-- awesome-plan project=zedbsd record=ws074-p201 -->

# ws074-p201: column flex auto height and min-height

Status: in-progress（2026-10-14 q1051-i01）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p200 q1050](../phase200/evidence/20261014-q1050/README.md)の固定10件8 PASS/2 FAIL baseline。p190 whole clearanceは不要。

## 目的と受け入れ

`height:auto; min-height` の縦Flexで、min-heightにより確定する主軸余白を子配置へ反映する。内容の必要高さがmin-heightを超える場合は内容を不当に縮めない。固定`flexbox-justify-content-vert-001b.xhtml`のlocal pixel exact PASSと最小host/Acid回帰をwhole clearance条件とする。writing-modeの残差、p190 runner、p173全件100%は別。

適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)、[CSS Flexbox §9.8](https://www.w3.org/TR/css-flexbox-1/#definite-sizes)。libbrowser内部のみ変更し、WPT/公開ABI/`plan/master.md`を変更しない。

## q1051-i01 の有限部分範囲

2026-10-14 02:26 JST、このchatのユーザーによるInterop 2025全件100%までの有限Queue事前承認を適用。最大90分（03:56 JSTまで）。`vert-001b`でheight:auto/min-height:200pxの親が200pxになるのに子配置は固有高さを使う差を診断する。折り返し無し・縦Flex・pixel min-height・max-height無しに絞り、内容必要高さとmin-heightの最大値を主軸availableへ渡す最小修正を試す。min-height超えの内容と固定10件、host/CSS/build/Acid非増加を検証する。wrap、percentage min/max、writing-mode、WPT/公開ABI/Master変更を含めない。回帰する/原因が別なら戻し、uncleared証拠を保存する。

Event ws074-p201-created-20261014: q1050後のvert001bでは`layout_block()`がflex子配置後に親heightを200pxへ引き上げるが、`layout_flex()`のcolumn availableはheight:autoで-1のまま。p200の端数丸めとp197/p199の物理配置は不変。GitHub publication保留。

## q1051-i01 結果（2026-10-14）

**cleared、p201 whole cleared。** nowrap縦Flexでheight:auto・pixel min-height・max-height:noneの場合、子の必要外寸合計とmin-heightの大きい方を主軸availableに使用。固定vert001bはpixel exact PASS、固定10件は9 PASS/1 FAIL。[証拠](evidence/20261014-q1051/README.md)。局所fixtureでmin-height未満は配置余白を得、内容超過は縮まない。host物理配置12/12、CSS rule44/44・CSSOM12/12、warning0 build、Acid3 score100/100・差0/480000、Acid2差1865/120000不変。残るwmvertは別writing-mode問題、p190/p173全件受け入れは未達。Event ws074-p201-q1051-clear-20261014: clearanceとclose予定を記録、GitHub publication保留。
