<!-- awesome-plan project=zedbsd record=ws074-p200 -->

# ws074-p200: Flexbox distributed spacing rounding

Status: in-progress（2026-10-14 q1050-i01）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p199 q1049](../phase199/evidence/20261014-q1049/README.md) の固定10件・差画素baseline。p190 whole clearanceは不要。

## 目的と受け入れ

Flexbox `space-around` / `space-evenly` / `space-between` の端数余白をlayout-unit最近接へ丸め、reference CSS calc と同じ位置を得る。固定3 reftest `vert-001a`/`002`/`006`のpixel一致と関連host/Acid回帰をwhole clearance条件とする。p197/p198/p199/p190/p173全体の受け入れは別。

適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)、[CSS Flexbox §8.2](https://www.w3.org/TR/css-flexbox-1/#justify-content-property)。libbrowser内部のみ変更し、WPT/公開ABI/`plan/master.md`を変更しない。

## q1050-i01 の有限部分範囲

2026-10-14 02:22 JST、このchatのユーザーによるInterop 2025全件100%までの有限Queue事前承認を適用。最大90分（03:52 JSTまで）。固定`vert-001a`/`002`/`006`の差画素とtest/reference layoutを突き合わせ、分配余白のlayout-unit整数除算が切り捨てる一般的な誤差を局所fixtureで再現する。`layout/flex.c` の正の分配余白だけを最近接へ丸める最小修正を試し、固定10件、局所host/CSS回帰、warning0 build、Acid3/Acid2非増加を検証する。負の余白、min-height、writing-mode等の別欠陥には踏み込まない。改善しない/回帰する場合は戻し、証拠を記録してunclearedとする。

Event ws074-p200-created-20261014: q1049で残ったvert001a差200はx382/426の端数配置だけ。test layout y14.66/37.98/101.31に対しreference y14.67/38.00/101.33。layout-unit整数除算の切捨てが原因候補。p197/p199の受け入れとp190/p173分母は不変。GitHub publication保留。

## q1050-i01 結果（2026-10-14）

**cleared、p200 whole cleared。** 正の分配余白を最近接layout unitで丸め、reverse行では丸め残差を反対端に配分。固定対象vert001a/002/006は全てpixel exact PASS、固定10件全体は5→8 PASS/2 FAIL、timeout0。[証拠](evidence/20261014-q1050/README.md)。host物理配置12/12、CSS rule44/44・CSSOM12/12・mutation12/12、warning0 build、Acid3 score100/100・差0/480000、Acid2差1865/120000不変。残るvert001bのmin-heightとwmvertのwriting-modeは別。p197/p198/p199/p190/p173全体受け入れは未達。Event ws074-p200-q1050-clear-20261014: p200 clearanceとclose予定を記録、GitHub publication保留。
