<!-- awesome-plan project=zedbsd record=ws074-p218 -->

# ws074-p218: Interop 2025 CSS Grid shorthand

Status: uncleared（q1094部分cleared、shorthand全体未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p189 q1093 fixed Grid census](../phase189/evidence/20261014-q1093/README.md)。

## 目的と受け入れ

Interop 2025固定Grid testharnessの`grid` shorthand利用FAIL群を公式構文と現行CSS parser/cascadeに照らして分類し、一般的な shorthand の解析・適用・CSSOM挙動を段階的に実装・再測定する。既存longhand、旧PASS、Acid3差0を維持。p218 wholeは対象固定IDの要件を明記して検証後にのみcleared。p189/p173全件100%は別受け入れ。

## 境界

`userland/desktop/libbrowser/css/`とGrid layoutの必要箇所、`plan/ws074/tests/`。WPT本体/runner採点、公開ABI、`plan/master.md`は変更しない。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。コード生成後はWS074 near-final full-standard conformance Phaseで再確認する。

Event ws074-p218-addition-20261014: [p189 q1093](../phase189/evidence/20261014-q1093/README.md)固定Grid399 FAIL中、`grid` shorthand利用はp217 alignmentだけでなくgrid-definition/parsingにもまたがる。p217のalignment受け入れを変えず、shorthandを独立Phaseに分ける。共有投影/GitHub publication pending。

## q1094-i01 有限部分範囲（2026-10-14 07:57 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（09:27 JSTまで）に適用。q1093固定Grid399 FAILのsourceから`grid` shorthandを使用する対象を分類し、代表WPT2–3件とCSS values/cascade/decl/CSSOM経路を追う。最小HTML hostと可能なCSSOM probeで再現し、初回実装に含める公式構文・リセット/順序規則・残る形を有限scopeとして証拠化。調査/証拠のみ、製品browser/runner/WPT/公開ABI/`plan/master.md`変更なし、Acid/全Grid再測定なし。p218 whole/p189/p173全件100%は本部分基準外。

## q1094-i01 結果（2026-10-14 07:59 JST）

Event ws074-p218-q1094-partial-clear-20261014: [証拠](evidence/20261014-q1094/README.md)。固定Grid502中`grid:`字面は90件（FAIL83/PASS7）の上限で、字面だけでは因果を断定できない。最小静的HTMLでshorthandは1008pxの2行、等価longhandは50px×2列/80px行。values property tableに`grid`/`grid-template`無し。固定WPTは静的layout、CSSOM computed initial、serialization/auto-flow等が別不足。次は既存track-listに限る明示行/列構文をatomicに展開し、他形は拒否する有限scope。q1094-i01部分cleared、p218 whole/p189/p173全件100%未達、共有投影/GitHub publication保留。
