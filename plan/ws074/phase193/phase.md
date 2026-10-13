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

## q1027-i01 の有限部分範囲

2026-10-14 00:57 JST、ユーザーの事前承認を適用、最大90分。`HTMLElement.hidden` getter は `hidden="until-found"` を文字列、その他の属性ありを true、属性なしを falseとして返す。setter は大文字小文字を問わない `until-found`、false/空文字/null/0/NaN の除去、それ以外の空値属性設定を HTML Standard に合わせる。局所fixture、固定beforematch WPT、host build/関連試験、Acid3/Acid2差画素非増加を確認。hiddenの描画/ancestor revealingは後続、対象WPT全PASSはこの部分基準に含めない。WPT本体/公開ABI/`master.md`は変更しない。

Scope reconciliation 2026-10-14 00:59 JST: 固定2025 WPT `hidden-idl.html` は `hidden = undefined` を属性除去と判定する一方、2026-10-13更新の living HTML Standard はその他の値として空値属性設定と記す。ユーザーの固定Interop 2025全件PASS目標を実装選択の出典として、q1027の setter 検証に `undefined` 除去を含める。仕様改訂との差は受け入れ証拠で明示し、WPTは改変しない。

## q1027-i01 結果（2026-10-14 01:00 JST）

**partial scope cleared、p193 whole uncleared。** `HTMLElement.hidden` の3状態getterとsetterを実装。[証拠](evidence/20261014-q1027/README.md)。固定`hidden-idl.html`17/17、`hidden-until-found-idl.html`1/1 subtest PASS。局所fixture、host build warning0、native property65/65、diff check PASS。Acid3 score100/100・画素差0、Acid2差1865不変。固定beforematch WPTはID/反映失敗を越えたが fragment navigation/ancestor revealing未完でno output。固定2025 WPTと2026 living Standardの`undefined`挙動差を記録、WPTは無変更。p193 wholeは未達、GitHub publication保留。

Event ws074-p193-q1027-outcome-20261014: q1027-i01 partial cleared、p193 wholeはuncleared。hidden IDL固定2 WPT PASSとfragment navigation未完を分離。p189/p173分母は不変。

## q1028-i01 の有限部分範囲

2026-10-14 01:02 JST、ユーザーの事前承認を適用、最大90分。同一文書の `Location.hash` setterを実装し、URLのfragmentを更新する。対象IDを探し、`hidden=until-found` と閉じた `<details>` の祖先を仕様の順に reveal して `beforematch` を同期通知し、状態変化・DOM除去で中断する。対象へのscrollを行う。固定 `beforematch-attribute-removal-001.html` と関連fragment WPT、局所fixture、host build/関連試験、Acid3/Acid2の差画素非増加を確認する。別文書navigation、History、全details testのPASSはこの部分範囲外。WPT本体/公開ABI/`master.md`は変更しない。適用は[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)。

## q1028-i01 結果（2026-10-14 01:09 JST）

**partial scope cleared、p193 whole uncleared。** 同一文書URL更新、fragment ID探索、同期beforematch・祖先revealと状態変化時の中断を実装。[証拠](evidence/20261014-q1028/README.md)。局所fixtureは順序・伝播・中断・hashを確認。固定beforematch WPTはno outputからcompletion付きFAILへ進行したが、次は一般的な `Element.attributes` 欠落でWPT harnessの要素整形に失敗する。host build warning0、native property65/65、realm30/30、diff check PASS。Acid3 score100/100・画素差0、Acid2差1865不変。p193 whole、p173全件は未達、GitHub publication保留。

Event ws074-p193-q1028-outcome-20261014: q1028-i01 partial cleared、p193 wholeはuncleared。固定WPTの新たな `Element.attributes` 欠落を次の独立scopeへ送る。
