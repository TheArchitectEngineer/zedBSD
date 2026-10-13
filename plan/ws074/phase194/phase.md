<!-- awesome-plan project=zedbsd record=ws074-p194 -->

# ws074-p194: Element.attributes / Attr read surface

Status: uncleared（2026-10-14 q1029 partial cleared、whole未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p193 q1028 fragment output](../phase193/evidence/20261014-q1028/README.md)。p193 wholeは不要。

## 目的と受け入れ

Interop 2025 のDOM/testharnessが要素属性を扱えるよう、`Element.attributes` のSameObject/live NamedNodeMap、Attr identity・owner/value反映、indexed/named/item APIと変更を実装し、固定関連WPT PASS、host試験、Acid3画素差0とAcid2非増加、C全文/browser component全文レビューでwhole clearanceを判定する。WPT本体・公開ABI・`master.md`は変更しない。

## q1029-i01 の有限部分範囲

2026-10-14 01:10 JST、ユーザーの事前承認を適用、最大90分。q1028固定WPTの `assert_equals(e.target,a1)` がWPT harness内の要素整形で `val.attributes.length` に失敗するため、`Element.attributes` のSameObject collection、現在の属性数・indexed read、各項目の `name`/`value` readを実装する。保存したcollectionは属性追加・除去の後も現在の内容を読む。Attr identity/setNamedItem等のwhole条件は後続。局所fixture、固定beforematch WPT、host build/関連試験、Acid3/Acid2差画素非増加を確認する。WPT本体/公開ABI/`master.md`は変更しない。適用は[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)。

Event ws074-p194-design-20261014: q1028でfragment eventは発火しWPT completionへ進んだが、testharnessのNode整形に`Element.attributes`が必要と判明。p193のnavigation責務を保ち、属性collectionは本Phaseに分離。p189 runner/p173分母は不変。GitHub publication保留。

## q1029-i01 結果（2026-10-14 01:13 JST）

**partial scope cleared、p194 whole uncleared。** Element.attributesのSameObject collection、現在の属性数・indexed read、項目名/値のreadを追加。[証拠](evidence/20261014-q1029/README.md)。局所fixtureは追加・値変更・除去後の保存済みcollectionを確認。固定beforematch WPT1/1 PASS、別のhidden/details WPTはp193のreveal順序差まで進んでFAIL。host build warning0、native property65/65、realm30/30、diff check PASS。Acid3 score100/100・画素差0、Acid2差1865不変。Attr identity/書込・NamedNodeMap API等とp173全件は未達。GitHub publication保留。

Event ws074-p194-q1029-outcome-20261014: q1029-i01 partial cleared、p194 wholeはuncleared。p193のreveal順序差を別件として保持。
