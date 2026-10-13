<!-- awesome-plan project=zedbsd record=ws074-p205 -->

# ws074-p205: vertical writing-mode logical margin mapping for Flex gap references

Status: cleared（2026-10-14 q1057-i01）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p190 q1056](../phase190/evidence/20261014-q1056/README.md)の旧PASS→FAIL8 IDと[p202](../phase202/phase.md)のvertical-lr Flex axis。

## 目的と受け入れ

`margin-inline-start/end`・`margin-block-start/end`とそれぞれの2値 shorthandが、要素のcomputed `writing-mode`と`direction`に従って物理余白に割り当てられ、物理宣言と同じcascade順位・source orderで競合することを検証する。固定vertical-lr gap 5 reftestをpixel exact PASSにし、host・固定Flex10・Acid3/2回帰を保つことをwhole clearance条件とする。他の論理サイズ/配置・全8回帰・p190/p173全件100%は別。

適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)、[CSS Logical Properties 1 §4.2](https://www.w3.org/TR/css-logical-1/#margin-properties)、[CSS Writing Modes 4](https://www.w3.org/TR/css-writing-modes-4/)。libbrowser内部に限り、WPT/公開ABI/`plan/master.md`は変更しない。

## q1057-i01 有限部分範囲

2026-10-14 03:01 JST、このchatのユーザーによるInterop 2025全件100%までの有限Queue事前承認を適用。最大90分（04:31 JSTまで）。固定5 gap IDと代表宣言順・direction・writing-mode組を最小fixtureで切り分ける。CSS logical margin 4 longhandと2 shorthandのparse identity、computed writing-mode/directionの先行確定、cascade時の物理側解決を最小実装する。fixed5と固定Flex10、host CSS rule/CSSOM、warning0 build、Acid3/Acid2非増加を測る。5件が残れば原因と証拠を残しunclearedで停止する。p190全1,561件再集計は含めない。

Event ws074-p205-created-20261014: q1056で旧PASS→FAIL8件のうち5件がvertical-lr `gap` reftest。各referenceは論理margin、現parserはhorizontal-ltrの物理余白へ即時aliasしている。computed writing-modeと宣言順の両方を保持して直す。p190/p173の受け入れは不変、共有Master/Queue/GitHub publication保留。

## q1057-i01 結果（2026-10-14）

Event ws074-p205-q1057-outcome-20261014: 論理margin宣言をcomputed writing-mode/direction後に物理側へ写像、物理宣言とのsource orderを保持。固定vertical-lr gap5件は5/5 local pixel exact PASS。[証拠](evidence/20261014-q1057/README.md)。host CSS rule51/51・CSSOM12/12・mutation12/12、warning0 build、固定Flex10/10 PASS、Acid3差0/Acid2差1865。p205 whole cleared、q1057-i01 cleared。残る旧8回帰中3件、p190/p173全件100%は未達、共有投影/GitHub publication保留。
