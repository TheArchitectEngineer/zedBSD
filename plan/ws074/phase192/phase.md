<!-- awesome-plan project=zedbsd record=ws074-p192 -->

# ws074-p192: Window named properties

Status: uncleared（2026-10-14 q1025 partial cleared、whole未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p173固定manifest](../phase173/evidence/20261013-q1012/manifest.json)、[p189 q1024 shard](../phase189/evidence/20261014-q1024/README.md)。p189 wholeは不要。

## 目的と受け入れ

HTML Standard の Window named properties として、文書内の対象要素の `id` / `name`、重複、iframe/navigable、既存 property との優先関係、DOM変更に伴う生存性を実装・検証する。Interop 2025 の関連 testharness を再測定し、Acid3のscore100/画素差0とAcid2差非増加、host build、C全文/browser component全文のsource規約確認を満たした時に whole clearanceを判定する。WPT本体・公開ABI・`master.md`は変更しない。

## q1025-i01 の有限部分範囲

2026-10-14 00:50 JST、ユーザーの事前承認を適用、最大90分。p189 shard の `beforematch-attribute-removal-001.html` 等で `a1 is not defined` となる原因に対し、HTML文書の一意な非空 `id` を持つ接続済み要素の Window named property を live lookup として実装する。既存 Window own property は保持し、削除・id変更後の参照と無関係な名前を局所fixtureで確認する。重複名、`name`、iframe/navigable、完全なproperty enumerationは後続部分範囲。host build・fixture、該当WPTの結果取得、固定textdecoration16件の代表とAcid3/Acid2差画素非増加を確認する。対象WPTの全PASSまではこの部分基準に含めない。適用は[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)。

Event ws074-p192-design-20261014: p189 q1024 の複数 `ReferenceError` は Window named property 未実装が共通原因候補。p189 runner出力とp173の分母は変えず、browser側の一般機能を本Phaseで担当する。GitHub publication保留。

## q1025-i01 結果（2026-10-14 00:53 JST）

**partial scope cleared、p192 whole uncleared。** 一意な接続済みHTML `id` の Window named lookupをliveに追加。[証拠](evidence/20261014-q1025/README.md)。局所fixtureはbare/window参照・既存own property優先・id変更・要素削除すべて期待どおり。host build warning0、native property65/65、realm30/30、代表textdecoration WPT14/14、diff check PASS。Acid3 score100/100・画素差0、Acid2差1865不変。`beforematch` WPTの `a1 is not defined` は消えたが `<details>.open` 未実装でno completion、`@scope`も `main is not defined` の次の失敗でharness error。重複/name/iframe等のp192 whole基準は未達、GitHub publication保留。

Event ws074-p192-q1025-outcome-20261014: q1025-i01 partial cleared、p192 whole uncleared。WPTの後続失敗とWindow named-property残件を分けて記録。p189/p173の全件数は維持。
