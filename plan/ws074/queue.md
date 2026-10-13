<!-- awesome-plan project=zedbsd record=queue -->

# q1016 / WS074 CSS.supports declaration API

Status: finished（2026-10-14 00:11〜00:14 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: 2026-10-14 00:07 JST、この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を、[p191の有限部分scope](phase191/phase.md) に適用。
Timebox: 2026-10-14 00:11 JST 開始、最大90分（01:41 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1016-i01 | [ws074-p191](phase191/phase.md) partial | CSS globalの `supports(property,value)`を既存宣言parserへ接続し、基本的な単一引数宣言も扱う。小さなhost試験、build、固定textdecoration全16件とAcid3回帰。新CSS property/serialization・公開ABI・WPT/Master変更なし。最大90分 | cleared（partial、p191 whole は uncleared） | p173固定manifest、p189 q1015 baseline |

Dependency graph: p173固定manifest → p189 q1015 baseline → q1016-i01 → p191 whole判断 → p173最終100%判断。p189 whole/p190は別の必要出力。

Upcoming Work Outlook: p191の残るtextdecoration CSSOM、p189 runnerの環境対応、[p190](phase190/phase.md) reftest/crashtest coverage、p173全件baselineを候補とする。次の Queue も有限scopeを記録する。

Coordination: [q1014](../history/queue-q1014.md) までの履歴を保持。共有 `plan/queue.md` / Master / GitHub publication は別の Q1 セッションとの競合を避け pending projection。WS074 lane の状態はこの file が所有。

Previous outcome: q1013-i01 partial cleared、p189 whole uncleared。選定testharness1件のWPT callbackから12 subtestとcompletionを取得し、4 PASS/8 FAIL、harness OK、runnerの結果はFAIL。status分類の unit5/5、py_compile、diff check PASS。[証拠](phase189/evidence/20261014-q1013/README.md)。残り1,782 testharnessと p173全件baseline、100%は未達。

Outcome: q1014-i01 partial cleared、p189 whole uncleared。19領域から条件を満たす15 IDを選定・実行、12 test FAIL・1 harness ERROR・1 completion無し・1 stdout無し、PASS0。4領域は未実行理由を保存。選定 unit2/2、py_compile、diff check PASS。[証拠](phase189/evidence/20261014-q1014/README.md)。q1013 と合わせユニーク16/1,783 testharnessを試行。ブラウザ/WPT/Master無変更、Acid3再計測省略。p173全件baseline・100%未達、共有Master/Queue/GitHub publication pending。

Outcome: q1015-i01 partial cleared、p189 whole uncleared。textdecorationの全16 IDを実測、4 PASS/12 FAIL、subtest47 PASS/132 FAIL。全16 completion取得。`CSS` global欠落とtext-decoration CSSOM不足を共通原因候補として記録。[証拠](phase189/evidence/20261014-q1015/README.md)。browser/WPT/Master無変更、p173全件baseline・100%未達。

Outcome: q1016-i01 partial cleared、p191 whole uncleared。`CSS.supports` を宣言parserへ接続。host fixture、build warning0、CSSOM12/12・rule model44/44、固定16件、Acid3差0を検証。textdecoration4 PASS/12 FAILと数不変だが `CSS is not defined` を解消。[証拠](phase191/evidence/20261014-q1016/README.md)。p173全件100%未達。
