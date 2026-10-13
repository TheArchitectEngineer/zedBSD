<!-- awesome-plan project=zedbsd record=ws074-p209 -->

# ws074-p209: Document FontFaceSet readiness

Status: uncleared（q1065有限部分scope cleared、whole API未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p189 q1064](../phase189/evidence/20261014-q1064/README.md) の280件 no_completion と [p173](../phase173/phase.md) の固定manifest。

## 目的と受け入れ

CSS Font Loading の `Document.fonts` SameObject と `FontFaceSet.ready` の初期 font/style/layout 完了後の Promise 解決を一般的に実装する。`ready` が未準備時に早期解決せず、フォント・stylesheet・layout が揃って解決し、複数アクセスで同一集合とPromiseを得ることを確認する。対象WPTで現行のTypeError/no_completionからcompletion付き結果へ進み、Acid3画素差0を維持する。動的な FontFace add/load/check、WorkerGlobalScope など未実装面は別途評価し、全機能実装を偽らない。

## q1065-i01 有限部分範囲

2026-10-14 03:55 JST、このchatのユーザーによる全3,805件100%までの有限Queue事前承認を最大90分（05:25 JSTまで）に適用。固定 CSS Grid testharness の `document.fonts.ready` で止まる代表3 IDについて、`document.fonts` SameObject・`ready` Promise・stylesheet/font fetch/layout の初期settlementを既存 page/bind/font lifecycleで実装する。固定3 IDの no_completion→completion付き結果、host fixture（SameObject・readyの順序・初期無fontと読込font）、warning0 build、既存binding/Promise回帰、固定Acid3/Acid2を確認する。新規 broad re-census、WPT/runner/公開ABI/`plan/master.md`変更は含めない。90分内に正しいsettlement設計ができなければ、観測/試行/再開条件を記録しunclearedで止める。適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)。仕様根拠: [CSS Font Loading Level 3 §3.4](https://www.w3.org/TR/css-font-loading-3/#font-face-set-ready) 。

Event ws074-p209-addition-q1065-20261014: q1064のno_completion299件中280件はCSS Grid文書に`document.fonts.ready`があり、現在の`document.fonts`欠落により同じTypeErrorで停止する。p189のrunner全1,783件coverage基準、p173全3,805件100%分母は維持。共有Master/Queue/GitHub publication保留。

## q1065-i01 結果（2026-10-14 04:02 JST）

Event ws074-p209-q1065-outcome-20261014: 初期`document.fonts` SameObjectと`ready` Promiseを、stylesheet/font fetch完了後のlayoutへ接続。[証拠](evidence/20261014-q1065/README.md)。固定CSS Grid3 IDは旧no_completion→completion付きFAIL（subtest1/1・4/9・0/4 PASS）。固定font指定の一時起動wrapperでWPT runner sourceを変えず確認。host fixture各5/5・native properties65/65・realm30/30・parser8/8・stream9/9・images10/10、warning0 build、Acid3差0/Acid2差1865。q1065-i01 partial cleared、p209 whole uncleared。動的FontFaceSet/API・既定runner font設定と280件再測定、p173全件100%が残る。共有投影/GitHub publication保留。
