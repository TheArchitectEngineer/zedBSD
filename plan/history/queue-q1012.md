<!-- awesome-plan project=zedbsd record=q1012 -->

# q1012 / WS074 Interop 2025 target inventory

Status: finished（2026-10-13 23:49〜23:58 JST）
Approval: この chat のユーザー回答「q1012 の範囲を承認する」（23:49 JST）。[承認時の範囲](../ws074/phase173/selection-snapshot.md)。最大90分、browser本体変更なし、`master.md` 変更なし。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1012-i01 | [p173](../ws074/phase173/phase.md) partial | 公式19 focus areaの固定manifest・WPT取得・runner inventory・実行可能な代表計測・失敗分離・後続Phase設計 | cleared（partial）。p173 whole uncleared |

Prerequisites: p172 whole cleared、p188 main統合済み Acid3 score100/100・RGB差0 verified。実行順 p172/p188 → q1012-i01 → p173のbaseline判断。p100 wholeは別の残余gate。

Outcome: Interop repository `2d28de9c1e86182c38a7311854ca59cefde7ac04`、metadata `018c06b2c0fae50400c71585c73b7f238a2d029a`、WPT `2d66b9b7998bb58c336138c178323ddee857b586` で19領域の3,805 unique IDを固定、未解決0。1,783 testharness、1,974 reftest、48 crashtest。現runnerの静的候補1,561、未対応2,244。選定sample35件は5 PASS/30 FAIL/0 error（全件率ではない）。runnerのsymlink pathとselection filter、manifest/coverage generatorを更新。Python構文check、選定ID照合、`git diff --check` PASS。[全証拠](../ws074/phase173/evidence/20261013-q1012/README.md)。

Remaining: p173全件baseline/ユーザーの全件PASS 100%は未達。testharness結果取得を[p189](../ws074/phase189/phase.md)、reftest/crashtest残部を[p190](../ws074/phase190/phase.md)に分割。別 Queue の exact-scope 承認が必要。browser本体無変更のため Acid3再計測は省略し、旧0差の回帰主張は更新しない。共有 Master/Queue/GitHub publication と push は pending。`master.md` は未変更。
