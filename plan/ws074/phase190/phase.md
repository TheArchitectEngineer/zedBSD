<!-- awesome-plan project=zedbsd record=ws074-p190 -->

# ws074-p190: Interop 2025 reftest・crashtest runner coverage

Status: planned（2026-10-13 追加、Queueなし）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p173 q1012 manifest](../phase173/evidence/20261013-q1012/manifest.json) の固定選定出力。p189とは独立。

## 目的と受け入れ

Interop 2025 の reftest 1,974件と crashtest 48件を WPT の期待と環境条件どおりに実行し、PASS/FAIL/error/crash/timeout/未実行を分けて保存する。q1012時点の static reftest候補1,561件、scripted reftest407件、variant URL6件、crashtest48件をすべて対象に保持する。必要な同一/異origin、HTTPS、script待機、reference・fuzzy 条件を確認する。全2,022件のrunner coverageと再現可能な結果が whole clearance 条件。ブラウザ固有の実際の描画FAILは runner の不備と分けて後続改善Phaseに渡す。

## 再開

最初の Queue は q1012 の sample失敗が runner の誤判定か描画差かを代表画像と reference 条件で検証し、static候補の有限 shard を正式 selection ID と結びつける範囲で定義する。動的/HTTPS/crashtestを一度に実装するとは約束しない。コード・実行は exact-scope Queue 承認後のみ。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。`master.md` は変更しない。

Event ws074-p173-q1012-split-20261013: [p173](../phase173/phase.md) から testharness 以外のrunner coverageを分離。現行runnerの script除外や単一HTTP origin、`--selection-manifest` で確認した限定を全件baselineに残さないためのPhase。p173の受け入れ・公式選定は変更しない。GitHub publication保留。
