<!-- awesome-plan project=zedbsd record=ws074-p193 -->

# ws074-p193: HTML details / hidden until-found

Status: uncleared（2026-10-14 q1026 partial cleared、whole未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p189 q1024 shard](../phase189/evidence/20261014-q1024/README.md)、[p192 q1025 unique id output](../phase192/evidence/20261014-q1025/README.md)。p192 wholeは不要。

## 目的と受け入れ

Interop 2025 details領域の固定 testharnessについて、HTMLDetailsElement の属性と toggle、hidden/until-found の探索・beforematch・ancestor revealing を仕様どおり実装し、固定対象全件のPASS、host build/試験、Acid3 score100/画素差0とAcid2非増加を確認した時に whole clearanceを判定する。p189 runnerとp173の固定分母は維持。WPT本体/公開ABI/`master.md`は変更しない。

## q1026-i01 の有限部分範囲

2026-10-14 00:54 JST、ユーザーの事前承認を適用、最大90分。p192後の `beforematch-attribute-removal-001.html` は `<details>.open` が未定義となる。HTMLDetailsElement専用prototypeと boolean `open` 属性の getter/setterを実装し、他のHTMLElementに `open` を追加しない。DOM属性の変更と後続WPTの失敗位置を局所fixtureで確認する。toggle event、hidden until-found探索は後続部分範囲。host build/関連試験、固定WPT、Acid3/Acid2差画素非増加を確認。適用は[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)。

Event ws074-p193-design-20261014: p192で先頭ReferenceError解消後、detailsの一般的な属性・探索機能不足が次の原因候補。p192 named-property scopeと分離し、p189 runner/p173分母は変えない。GitHub publication保留。

## q1026-i01 結果（2026-10-14 00:56 JST）

**partial scope cleared、p193 whole uncleared。** HTMLDetailsElement専用prototypeとboolean `open` getter/setterを追加。[証拠](evidence/20261014-q1026/README.md)。局所fixtureはinterface・他HTMLElementとの区別、属性反映の全項目が期待どおり。host build warning0、native property65/65、realm30/30、diff check PASS。Acid3 score100/100・画素差0、Acid2差1865不変。固定WPTの `a1.open` 未定義は解消したが、`hidden="until-found"` がboolean trueで返る後続失敗のためno completion。toggle/hidden探索は未実装、GitHub publication保留。

Event ws074-p193-q1026-outcome-20261014: q1026-i01 partial cleared、p193 wholeはuncleared。固定WPTの次の差分と残りのdetails/hidden条件を記録。p189/p173は不変。
