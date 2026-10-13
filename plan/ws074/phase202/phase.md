<!-- awesome-plan project=zedbsd record=ws074-p202 -->

# ws074-p202: vertical-lr writing mode Flexbox axis

Status: in-progress（2026-10-14 q1052-i01）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p201 q1051](../phase201/evidence/20261014-q1051/README.md)の固定10件9 PASS/1 FAILと[p200](../phase200/phase.md)分配丸め。p190 whole clearanceは不要。

## 目的と受け入れ

`writing-mode:vertical-lr`でFlexbox rowの主軸が物理的に縦、cross軸が横になる一般規則を実装する。既存horizontal-tbの回帰を避け、固定`flexbox-justify-content-wmvert-001.xhtml`をlocal pixel exact PASSにする。writing-mode全体のtext/table/inline対応とp190/p173全件100%は別。

適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)、[CSS Writing Modes §3.1](https://www.w3.org/TR/css-writing-modes-4/#block-flow)、[CSS Flexbox §5.1](https://www.w3.org/TR/css-flexbox-1/#flex-direction-property)。libbrowser内部のみ変更し、WPT/公開ABI/`plan/master.md`を変更しない。

## q1052-i01 の有限部分範囲

2026-10-14 02:29 JST、このchatのユーザーによるInterop 2025全件100%までの有限Queue事前承認を適用。最大90分（03:59 JSTまで）。固定wmvert IDと最小非文字Flex fixtureで、未対応のCSS writing-mode parsing・継承、rowの縦方向main axis、floatのintrinsic幅、reverse/RTL/left/rightを切り分ける。内部CSS property/styleとFlex/layoutの小さい一般修正のみ試し、局所host・固定10 ID・warning0 build・Acid3/Acid2非増加を検証する。text回転、table/inline等のwriting-mode全般、WPT/公開ABI/Master変更は含めない。最小改修で規範的な実装にならない場合は診断証拠を記録し、コードを戻してunclearedで停止する。

Event ws074-p202-created-20261014: q1051後のwmvert固定1件は393,560差画素。CSSにwriting-mode propertyの登録が無く、現行flex rowは物理横軸と固定されている。W3C Flexbox §5.1ではrowが当該writing modeのinline軸に従う。p197/p198/p199/p200/p201の受け入れ、p190/p173分母は不変。GitHub publication保留。

## q1052-i01 結果（2026-10-14）

**cleared、p202 scoped whole cleared。** vertical-lrのCSS登録/継承、Flex物理軸、float intrinsic幅、縦rowのline-left/rightを実装。固定wmvertは393,560差画素→pixel exact PASS、固定10件は10/10 PASS。[証拠](evidence/20261014-q1052/README.md)。host局所21/21・CSS rule44/44・CSSOM12/12・mutation12/12、warning0 build、Acid3 score100/100・差0/480000、Acid2差1865/120000不変。文字回転・vertical-rl/sideways・table/inline全般は対象外でp173全件100%には未達。Event ws074-p202-q1052-clear-20261014: scoped clearanceとclose予定を記録、GitHub publication保留。
