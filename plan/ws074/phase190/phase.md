<!-- awesome-plan project=zedbsd record=ws074-p190 -->

# ws074-p190: Interop 2025 reftest・crashtest runner coverage

Status: in-progress（2026-10-14 q1033 partial）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p173 q1012 manifest](../phase173/evidence/20261013-q1012/manifest.json) の固定選定出力。p189とは独立。

## 目的と受け入れ

Interop 2025 の reftest 1,974件と crashtest 48件を WPT の期待と環境条件どおりに実行し、PASS/FAIL/error/crash/timeout/未実行を分けて保存する。q1012時点の static reftest候補1,561件、scripted reftest407件、variant URL6件、crashtest48件をすべて対象に保持する。必要な同一/異origin、HTTPS、script待機、reference・fuzzy 条件を確認する。全2,022件のrunner coverageと再現可能な結果が whole clearance 条件。ブラウザ固有の実際の描画FAILは runner の不備と分けて後続改善Phaseに渡す。

## 再開

最初の Queue は q1012 の sample失敗が runner の誤判定か描画差かを代表画像と reference 条件で検証し、static候補の有限 shard を正式 selection ID と結びつける範囲で定義する。動的/HTTPS/crashtestを一度に実装するとは約束しない。コード・実行は exact-scope Queue 承認後のみ。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。`master.md` は変更しない。

Event ws074-p173-q1012-split-20261013: [p173](../phase173/phase.md) から testharness 以外のrunner coverageを分離。現行runnerの script除外や単一HTTP origin、`--selection-manifest` で確認した限定を全件baselineに残さないためのPhase。p173の受け入れ・公式選定は変更しない。GitHub publication保留。

## q1033-i01 の有限部分範囲

2026-10-14 01:20 JST、ユーザーの事前承認を適用、最大90分。固定manifestで選ばれた`html/rendering/the-details-element`の静的reftest候補を最大10件、既存`run-wpt-reftests.py`で同一HTTP・800×600・固定font条件で実測し、PASS/FAIL/errorとmatch/mismatch参照・画素差を監査する。代表PASS/FAILの誤判定要因を画像とmetadataに照らして記録する。scripted/variant/HTTPS/crashtestとbrowser/WPT/公開ABI/`master.md`変更は範囲外。検証は固定commit/manifest、reportと参照の整合、diff check。適用は[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)。

## q1033-i01 結果（2026-10-14 01:20 JST）

**partial scope cleared、p190 whole uncleared。** 固定選定10件の既存runner結果は4 provisional PASS/6 FAIL/0 error。[監査と画像](evidence/20261014-q1033/README.md)。`details-display-type-001.html`のPASSは参照がさらに別の参照へmatchする連鎖をrunnerが無視したfalse passと判明。各ID/参照/commitと画素差を確認。scripted/variant/HTTPS/crashtestは未実行。browser/WPT/Master無変更、diff check PASS。コード無変更のためhost build/Acid再計測省略。次は参照グラフを扱うrunner修正が必要。p190 wholeとp173全件100%未達、GitHub publication保留。

Event ws074-p190-q1033-outcome-20261014: q1033-i01 partial cleared、p190 wholeはuncleared。PASSのreference-chain誤判定を特定し、既存runnerの全件拡大前に修正する。
